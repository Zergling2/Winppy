#include <winppy/Network/TCPClient.h>
#include <cassert>
#include <winppy/Common/Math.h>
#include <winppy/Common/GlobalConstant.h>
#include <winppy/Core/SerializeBuffer.h>

using namespace winppy;

TCPClientInitDesc::TCPClientInitDesc()
	: m_eventHandler(nullptr)
	, m_recvBufSize(RECV_BUFFER_SIZE_MIN)
	, m_sendQueueSize(SEND_QUEUE_SIZE_MIN)
	, m_tcpNoDelay(true)
	, m_zeroByteSendBuf(false)
{
}

TCPClient::TCPClient()
	: m_init(false)
	, m_connected(0)
	, m_tcpNoDelay(false)
	, m_zeroByteSendBuf(false)
	, m_eventHandler(nullptr)
	, m_recvBufSize(0)
	, m_sendQueueSize(0)
	, m_sock(INVALID_SOCKET)
	, m_recvOverlapped()
	, m_recvBuf()
	, m_sendOverlapped()
	, m_sendQueueLock()
	, m_sendQueue()
{
}

int TCPClient::Init(const TCPClientInitDesc& desc)
{
	int result = -1;
	do
	{
		if (m_init)
		{
			result = 0;
			break;
		}

		if (!desc.m_eventHandler)	// 이벤트 핸들러가 없을 시 생성 불가.
			break;

		const uint32_t recvBufSize = Math::NextPowerOf2(Math::Clamp(desc.m_recvBufSize, RECV_BUFFER_SIZE_MIN, RECV_BUFFER_SIZE_MAX));
		const uint32_t sendQueueSize = Math::NextPowerOf2(Math::Clamp(desc.m_sendQueueSize, SEND_QUEUE_SIZE_MIN, SEND_QUEUE_SIZE_MAX));

		uint8_t* pRecvBuf = new uint8_t[recvBufSize];
		SerializeBuffer** pSendQueueBuf = new SerializeBuffer * [sendQueueSize];

		m_recvBuf.BindMem(pRecvBuf, recvBufSize);
		m_sendQueue.BindMem(pSendQueueBuf, sendQueueSize);

		ZeroMemory(&m_recvOverlapped, sizeof(m_recvOverlapped));
		ZeroMemory(&m_sendOverlapped, sizeof(m_sendOverlapped));
		InitializeSRWLock(&m_sendQueueLock);	// 메모리 재사용으로 동일 위치에 SRWLOCK 재초기화가 일어날 수 있지만 더 이상 참조하는 스레드가 없을 시 문제 없음.

		m_eventHandler = desc.m_eventHandler;
		m_recvBufSize = recvBufSize;
		m_sendQueueSize = m_sendQueueSize;

		m_init = true;
		result = 0;
	} while (false);

	return result;
}

void TCPClient::Release()
{
	this->Disconnect();	// Safely disconnect.

	if (m_recvBuf.IsValid())
	{
		// m_recvBuf.Clear();

		uint8_t* pRecvBuf = m_recvBuf.UnbindMem();
		assert(pRecvBuf);
		delete[] pRecvBuf;
	}

	if (m_sendQueue.IsValid())
	{
		while (!m_sendQueue.Empty())	// 잔여 직렬화 버퍼 해제
		{
			SerializeBuffer* pSerBuf = m_sendQueue.Pop();
			pSerBuf->Release();
		}

		SerializeBuffer** pSendBuf = m_sendQueue.UnbindMem();
		assert(pSendBuf);
		delete[] pSendBuf;
	}
}

TCPClient::~TCPClient()
{
	this->Release();
}
