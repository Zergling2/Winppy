#include <winppy/Network/TCPClient.h>
#include <winppy/Common/Math.h>
#include <winppy/Common/GlobalConstant.h>
#include <winppy/Core/SerializeBuffer.h>
#include <winppy/Core/Debug.h>
#include <winppy/Core/LogPrefix.h>
#include <winppy/Core/WinHelper.h>
#include <winppy/Network/TCPClientEngine.h>
#include <winppy/Platform/PlatformNetwork.h>
#include <MSWSock.h>
#include <cassert>
#include <mutex>

using namespace winppy;

TCPClientInitDesc::TCPClientInitDesc()
	: m_pEngine(nullptr)
	, m_recvBufSize(RECV_BUFFER_SIZE_MIN)
	, m_sendQueueSize(SEND_QUEUE_SIZE_MIN)
	, m_tcpNoDelay(true)
	, m_zeroByteSendBuf(false)
{
}

TCPClient::TCPClient()
	: m_cancelIo(0)
	, m_isSending(0)
	, m_numOfPacketsPending(0)
	, m_init(false)
	, m_tcpNoDelay(false)
	, m_zeroByteSendBuf(false)
	, m_connecting(0)
	, m_pEngine(nullptr)
	, m_recvBufSize(0)
	, m_sendQueueSize(0)
	, m_sock(INVALID_SOCKET)
	, m_connOverlapped()
	, m_recvOverlapped()
	, m_recvBuf()
	, m_sendOverlapped()
	, m_sendQueueLock()
	, m_sendQueue()
{
	m_flag.m_refCount = 0;
	m_flag.m_released = 1;	// Released 상태로 시작.
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

		if (!desc.m_pEngine)
			break;

		m_pEngine = desc.m_pEngine;

		// OVERLAPPED 구조체 초기화
		ZeroMemory(&m_connOverlapped, sizeof(m_recvOverlapped));
		ZeroMemory(&m_recvOverlapped, sizeof(m_recvOverlapped));
		ZeroMemory(&m_sendOverlapped, sizeof(m_sendOverlapped));

		// 버퍼 메모리 할당
		const uint32_t recvBufSize = Math::NextPowerOf2(Math::Clamp(desc.m_recvBufSize, RECV_BUFFER_SIZE_MIN, RECV_BUFFER_SIZE_MAX));
		const uint32_t sendQueueSize = Math::NextPowerOf2(Math::Clamp(desc.m_sendQueueSize, SEND_QUEUE_SIZE_MIN, SEND_QUEUE_SIZE_MAX));
		uint8_t* pRecvBuf = new uint8_t[recvBufSize];
		SerializeBuffer** pSendQueueBuf = new SerializeBuffer*[sendQueueSize];
		m_recvBuf.BindMem(pRecvBuf, recvBufSize);
		m_sendQueue.BindMem(pSendQueueBuf, sendQueueSize);

		// 송신 큐 락 초기화
		InitializeSRWLock(&m_sendQueueLock);

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

bool TCPClient::Connect(const wchar_t* ip, uint16_t port)
{
	wchar_t logMsgBuf[128];
	bool result = false;
	SOCKET sock = INVALID_SOCKET;

	do
	{
		// 스레드 안전
		if (InterlockedExchange8(&m_connecting, 1) != 0)
			break;

		if (!ip)
			break;

		// 소켓 생성
		sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (sock == INVALID_SOCKET)
		{
			int ec = WSAGetLastError();
			Debug::GetWinErrString(ec, logMsgBuf, _countof(logMsgBuf));
			wprintf(L"%ls WSASocket failed with error: %d. %ls\n", LogPrefixString::Error(), ec, logMsgBuf);
			break;
		}

		// IOCP와 연결
		if (!AssociateDeviceWithCompletionPort(m_pEngine->m_hIoCompletionPort, reinterpret_cast<HANDLE>(sock), reinterpret_cast<ULONG_PTR>(this)))
			break;

		// 바인딩 (비동기 Connect를 위해서 필요한 작업)
		SOCKADDR_IN localAddr;
		ZeroMemory(&localAddr, sizeof(localAddr));
		localAddr.sin_family = AF_INET;
		localAddr.sin_port = 0;
		localAddr.sin_addr.s_addr = INADDR_ANY;
		if (bind(sock, reinterpret_cast<sockaddr*>(&localAddr), static_cast<int>(sizeof(localAddr))) == SOCKET_ERROR)
		{
			int ec = WSAGetLastError();
			Debug::GetWinErrString(ec, logMsgBuf, _countof(logMsgBuf));
			wprintf(L"%ls bind failed with error: %d. %ls\n", LogPrefixString::Error(), ec, logMsgBuf);
			break;
		}

		// 원격 주소
		SOCKADDR_IN remoteAddr;
		ZeroMemory(&remoteAddr, sizeof(remoteAddr));
		remoteAddr.sin_family = AF_INET;
		remoteAddr.sin_port = htons(port);	// 포트 바인딩
		INT ret = InetPtonW(AF_INET, ip, &remoteAddr.sin_addr);
		if (ret == -1)	// error code can be retrieved by calling the WSAGetLastError for extended error information.
		{
			int ec = WSAGetLastError();
			Debug::GetWinErrString(ec, logMsgBuf, _countof(logMsgBuf));
			wprintf(L"%ls InetPtonW failed with error: %d. %ls\n", LogPrefixString::Error(), ec, logMsgBuf);
			break;
		}
		else if (ret == 0)	// if the pszAddrString parameter points to a string that is not a valid IPv4 dotted-decimal string or a valid IPv6 address string.
		{
			wprintf(L"%ls %ls is not a valid IPv4 dotted-decimal string.\n", LogPrefixString::Fail(), ip);
			break;
		}

		// 비동기 Connect
		ZeroMemory(&m_connOverlapped, sizeof(m_connOverlapped));
		BOOL connExResult = ConnectEx(sock, reinterpret_cast<sockaddr*>(&remoteAddr), static_cast<int>(sizeof(remoteAddr)), nullptr, 0, nullptr, &m_connOverlapped);
		if (connExResult == FALSE)
		{
			int ec = WSAGetLastError();
			switch (ec)
			{
			case WSA_IO_PENDING:
				result = true;
				break;
			default:
				Debug::GetWinErrString(ec, logMsgBuf, _countof(logMsgBuf));
				wprintf(L"%ls ConnectEx failed with error: %d. %ls\n", LogPrefixString::Error(), ec, logMsgBuf);
				break;
			}
		}
		else
		{
			result = true;
			// 동기적 성공도 IOCP 완료통지 항목 전달됨.
		}
	} while (false);

	// 생성한 자원 해제
	if (!result)
	{
		if (sock != INVALID_SOCKET)
			closesocket(sock);
	}
	else
	{
		m_sock = sock;
	}

	return result;
}

void TCPClient::Disconnect()
{
	bool result = false;
	InterlockedIncrement16(&m_flag.m_refCount);		// 세션 유효성 확인 참조

	do
	{
		if (m_flag.m_released)
			break;

		InterlockedExchange8(&m_cancelIo, 1);
		if (CancelIoEx(reinterpret_cast<HANDLE>(m_sock), nullptr) == FALSE)
		{
			// 파일 핸들이 완료 포트와 연결되어 있는 경우, 동기 작업이 성공적으로 취소되면 I/O 완료 패킷이 해당 포트에 대기열에 추가되지 않습니다.
			// 하지만 아직 보류 중인 비동기 작업의 경우, 취소 작업으로 인해 I/O 완료 패킷이 대기열에 추가됩니다.
			DWORD ec = GetLastError();
			switch (ec)
			{
			case ERROR_NOT_FOUND:
				break;
			default:
				// m_fileLogger.Write(L"%ls CancelIoEx failed with error: %lu.\n", LogPrefixString::Warning(), ec);
				break;
			}
		}

		result = true;
	} while (false);

	if (InterlockedDecrement16(&m_flag.m_refCount) == 0)	// 세션 유효성 확인 참조에 대응
		m_pEngine->ReleaseClient(*this);
}

void TCPClient::Send(Packet packet)
{
	if (!packet)
		return;

	InterlockedIncrement16(&m_flag.m_refCount);		// 송신중 재연결 방지

	do
	{
		if (m_flag.m_released)
			break;

		SerializeBuffer* pSerBuf = packet.Detach();				// 참조 카운트 유지하면서 Packet 객체로부터 분리.
		AcquireSRWLockExclusive(&m_sendQueueLock);
		bool success = m_sendQueue.Push(pSerBuf);		// A.
		ReleaseSRWLockExclusive(&m_sendQueueLock);

		if (!success)
		{
			pSerBuf->Release();
			this->Disconnect();
			break;
		}

		// B.

		if (InterlockedExchange8(&m_isSending, 1) != 0)
			break;

		if (m_cancelIo)
			break;

		// A ~ B 지점 사이에 스레드가 대기 상태로 전환되었다가 깨어난 경우 다른 스레드가 송신을 했을 수 있다. PostSend 함수 내에서 큐가 비어있을 수 있음.
		m_pEngine->PostSend(*this);
	} while (false);

	if (InterlockedDecrement16(&m_flag.m_refCount) == 0)		// 송신중 재연결 방지에 대응
		m_pEngine->ReleaseClient(*this);
}

TCPClient::~TCPClient()
{
	this->Release();
}
