#include <winppy/Network/TCPSession.h>
#include <winppy/Core/SerializeBuffer.h>
#include <winppy/Core/Debug.h>
#include <cassert>

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
	, m_numOfPacketsPending(0)
	, m_id(0)
	, m_sock(INVALID_SOCKET)
	, m_addr{}
	, m_recvOverlapped()
	, m_recvBuf()
	, m_sendOverlapped()
	, m_sendQueueLock()
	, m_sendQueue()
{
	m_flag.m_refCount = 0;
	m_flag.m_released = 1;	// Released 상태로 시작.

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
	// OVERLAPPED 구조체 초기화
	ZeroMemory(&m_recvOverlapped, sizeof(m_recvOverlapped));
	ZeroMemory(&m_sendOverlapped, sizeof(m_sendOverlapped));

	// 버퍼 메모리 바인딩
	m_recvBuf.BindMem(desc.m_pRecvBufAddr, desc.m_recvBufSize);
	m_sendQueue.BindMem(desc.m_pSendQueueAddr, desc.m_sendQueueSize);

	// 송신 큐 락 초기화
	InitializeSRWLock(&m_sendQueueLock);
}

void TCPSession::Start(const TCPSessionStartDesc& desc)
{
	// assert(m_flag.m_refCount == 0);
	// assert(m_flag.m_released == 1);
	assert(m_flag.m_releasedAndRefCount == 0x00010000);
	assert(m_recvBuf.Empty());
	assert(m_sendQueue.Empty());
	assert(m_numOfPacketsPending == 0);

	m_cancelIo = 0;
	m_isSending = 0;
	m_id = desc.m_id;	// released 플래그보다 먼저 설정 및 commit 되어야 함.
	m_sock = desc.m_sock;
	m_addr = desc.m_addr;
	ZeroMemory(&m_recvOverlapped, sizeof(m_recvOverlapped));
	ZeroMemory(&m_sendOverlapped, sizeof(m_sendOverlapped));


	// 세션은 해제 돌입된 뒤로 재사용되기까지 절대로 m_released가 0이면서 m_refCount도 0인 상태로 되어서는 안된다!
	// TryReleaseSession에서 대기타고 있는 또다른 스레드가 존재할 수 있기 때문이다.
	// TryReleaseSession 함수에서는 InterlockedCompareExchange로 m_releasedAndRefCount가 0x00000000인 경우 해제를 수행하는데,
	// 세션이 재활용되어 유효한 세션임에도 불구하고 밑에서 released 플래그를 끄고 나간 순간 m_releasedAndRefCount가 0x00000000이 되어
	// 이제 막 시작하려고 하는 새 세션을 해제시켜버릴 수 있다.
	// 
	// 이 문제를 해결하기 위해, m_released 플래그를 끄기 전에 먼저 보호 참조 카운트 1을 증가시킨 뒤 세션을 시작시킨다.
	// (보충내용)
	// 처음에는 이렇게 문제를 해결하려고 했었다.
	// m_flag.m_refCount = 0xffff;	// TryReleaseSession에서 착각할 수 없도록 쓰이지 않을만한 센티넬 값을 넣어둔다. (쓸 수 없는 방법)
	// 이렇게 하면 다른 스레드들이 +1과 -1을 반복중인 참조 카운트 흐름의 연속성을 깨버리는 아주 위험한 코드이다.

	// (최종 문제 해결 코드)
	InterlockedIncrement16(&m_flag.m_refCount);		// 보호 카운트 1을 준 뒤 released 플래그를 꺼야 한다.

	// 다른 스레드들에게 반드시 새로운 m_id 값이 먼저 보이고, 그 뒤에 released 플래그가 0으로 꺼지는 것으로 보여져야 한다.
	InterlockedExchange16(&m_flag.m_released, 0);	// x86에서는 위의 요구조건을 인터락 계열들이 보장함. (컴파일러 재배치 방지까지 보장된다.)
	// x86은 일단 의존성 없는 변수들간에 한해서 load가 store를 앞지르는 경우 외에는 하드웨어 재배치는 없으므로 괜찮으나
	// 혹시 모를 컴파일러의 명령어 재배치를 막으려면 인터락으로 해야 재배치를 안한다. (인터락이 암시적으로 컴파일러 재배치까지 막음.)
	
	// 이렇게 해야 released 플래그가 바뀐 것을 다른 스레드가 본 순간 m_id는 이미 새 값이 보여짐을 보장할 수 있음.
	// 버그 시나리오: 어떤 다른 스레드가 세션에 대해 Disconnect를 호출한 경우 released 플래그가 먼저 0이 되어버리고 재활용된 세션의
	// 새로운 id값이 그 스레드의 메모리 가시성에는 보이지 않는 경우 그 스레드는 대상 세션이 자신이 믿는 세션과 동일한 것으로 간주하고
	// Disconnect 루틴을 완전히 실행해버리게 된다. -> 새로운 유효 세션이 갑자기 다른 스레드에 의해 해제되어버리는 버그 시나리오이다.
	// 
	// 세션 재활용 확인 유무는 반대로 released 플래그 먼저 확인 후 id를 확인하면 된다. (확인 전후로 refCount 증감은 당연)
}

bool TCPSession::GetIPStr(wchar_t* pBuf, size_t len) const
{
	return InetNtopW(AF_INET, &m_addr.sin_addr, pBuf, len) != nullptr;
}

uint32_t TCPSession::GetIP() const
{
	return static_cast<uint32_t>(ntohl(m_addr.sin_addr.s_addr));
}

uint16_t TCPSession::GetPort() const
{
	return ntohs(m_addr.sin_port);
}
