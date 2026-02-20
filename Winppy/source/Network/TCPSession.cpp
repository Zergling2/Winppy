#include <winppy/Network/TCPSession.h>
#include <cassert>
#include <winppy/Core/SerializeBuffer.h>
#include <winppy/Core/Debug.h>

using namespace winppy;

TCPSessionInitDesc::TCPSessionInitDesc()
	: m_pRecvBufAddr(nullptr)
	, m_recvBufSize(0)
	, m_pSendQueueAddr(nullptr)
	, m_sendQueueSize(0)
{
}

TCPSession::TCPSession()
	: m_cancelIo(0)
	, m_isSending(0)
	, m_pending(0)
	, m_id(0)
	, m_sock(INVALID_SOCKET)
	, m_recvOverlapped()
	, m_recvBuf()
	, m_sendOverlapped()
	, m_sendQueueLock()
	, m_sendQueue()
{
	m_flag.m_refCount = 0;
	m_flag.m_released = 1;	// Released 상태로 시작.
	InitializeSRWLock(&m_sendQueueLock);

	// 64바이트 경계 검사
	if (reinterpret_cast<uintptr_t>(this) & (Cache::L1LineSize() - 1 != 0))
		Debug::ForceCrash();	// 안전하게 수행될 수 없는 플랫폼.

	// 런타임 메모리 레이아웃 검사 (m_flag.m_refCountAndReleased가 m_flag.m_refCount와 m_flag.m_released를 완전히 포함하는지 검사)
	if (reinterpret_cast<uintptr_t>(&m_flag.m_releasedAndRefCount) != reinterpret_cast<uintptr_t>(&m_flag.m_refCount))
		Debug::ForceCrash();	// 안전하게 수행될 수 없는 플랫폼.
	if (reinterpret_cast<uintptr_t>(&m_flag.m_releasedAndRefCount) + sizeof(m_flag.m_refCount) != reinterpret_cast<uintptr_t>(&m_flag.m_released))
		Debug::ForceCrash();	// 안전하게 수행될 수 없는 플랫폼.
}

void TCPSession::Init(const TCPSessionInitDesc& desc)
{
	ZeroMemory(&m_recvOverlapped, sizeof(m_recvOverlapped));
	ZeroMemory(&m_sendOverlapped, sizeof(m_sendOverlapped));
	m_recvBuf.BindMem(desc.m_pRecvBufAddr, desc.m_recvBufSize);
	m_sendQueue.BindMem(desc.m_pSendQueueAddr, desc.m_sendQueueSize);
}

void TCPSession::Start(const TCPSessionStartDesc& desc)
{
	// assert(m_flag.m_refCount == 0);
	// assert(m_flag.m_released == 1);
	assert(m_flag.m_releasedAndRefCount == 0x00010000);
	assert(m_recvBuf.Empty());
	assert(m_sendQueue.Empty());
	assert(m_pending == 0);

	m_cancelIo = 0;
	m_isSending = 0;
	m_id = desc.m_id;
	m_sock = desc.m_sock;
	ZeroMemory(&m_recvOverlapped, sizeof(m_recvOverlapped));
	ZeroMemory(&m_sendOverlapped, sizeof(m_sendOverlapped));

	InterlockedExchange16(&m_flag.m_released, 0);	// 메모리 장벽, id 설정 메모리에 반영 후 released 플래그 변경 (반드시 인터락으로 설정해야 함)
	// 반드시 m_flag.m_relased가 m_id 보다 먼저 store되는 식의 컴파일러 재배치를 막아야만 함.
	// x86은 일단 의존성 없는 변수들간에 한해서 load가 store를 앞지르는 경우 외에는 하드웨어 재배치는 없으므로 괜찮으나 컴파일러의 명령어 재배치를 막으려면
	// 인터락으로 해야 재배치를 안한다.
	
	// 이렇게 해야 released 플래그가 바뀐 것을 다른 스레드가 본 순간 m_id는 이미 새 값이 보여짐을 보장할 수 있음.
	// 버그 시나리오: 특히 다른 스레드가 Disconnect를 호출한 경우 released 플래그가 먼저 0이 되어버리고 재활용된 세션의
	// 새로운 id값이 다른 스레드에게는 반영되지 않는 경우 그 스레드는 Disconnect 대상 세션이 유효하다고 간주하고 Disconnect 루틴을 완전히 실행해버리게 된다.
	// 세션 재활용 확인 유무는 반대로 released 플래그 먼저 확인 후 id를 확인하면 된다. (확인 전후로 refCount 증감은 당연)
}
