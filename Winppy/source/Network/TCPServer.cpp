#include <winppy/Network/TCPServer.h>
#include <winppy/Network/TCPSession.h>
#include <winppy/Common/Math.h>
#include <winppy/Common/GlobalConstant.h>
#include <winppy/Core/Debug.h>
#include <winppy/Core/WinHelper.h>
#include <winppy/Core/LogPrefix.h>
#include <winppy/Core/SRWLock.h>
#include <winppy/Core/SerializeBuffer.h>
#include <winppy/Core/SerializeBufferBatchPool.h>
#include <process.h>
#include <conio.h>
#include <cassert>
#include <cstdlib>

using namespace winppy;

static inline size_t ComputeSessionIndex(uint64_t id)
{
	return static_cast<uint32_t>(id);
}

static inline BOOL PostSessionReleaseJob(HANDLE hCompletionPort, TCPSession& session)
{
	// 문서에 따르면 GetQueuedCompletionStatus 함수가 TRUE를 반환하면서 lpOverlapped가 NULL인 경우는 없으므로
	// 이 경우를 사용자 정의 이벤트 식별 용도로 사용할 수 있음.
	return PostQueuedCompletionStatus(hCompletionPort, 0, reinterpret_cast<ULONG_PTR>(&session), nullptr);
}

TCPServerConfig::TCPServerConfig()
	: m_logFileName(nullptr)
	, m_bindAddr(nullptr)
	, m_bindPort(0)
	, m_headerCode(DEFAULT_HEADER_CODE)
	, m_maxSessionCount(0)
	, m_sessionRecvBufSize(RECV_BUFFER_SIZE_MIN)
	, m_sessionSendQueueSize(SEND_QUEUE_SIZE_MIN)
	, m_backlogSize(1u << 10)
	, m_numOfWorkerThreads(0)
	, m_numOfConcurrentThreads(0)
	, m_tcpNoDelay(true)
	, m_zeroByteSendBuf(false)
	, m_endKey(EndKey::Backspace)
{
}

TCPServer::TCPServer()
	: m_init(false)
	, m_isInstallableFileSystemLSP(false)
	, m_si()
	, m_fileLogger()
	, m_bindAddr()
	, m_bindPort(0)
	, m_headerCode(0)
	, m_maxSessionCount(0)
	, m_sessionRecvBufSize(0)
	, m_sessionSendQueueSize(0)
	, m_tcpNoDelay(true)
	, m_zeroByteSendBuf(false)
	, m_pLargeMemRecvBuf(nullptr)
	, m_pLargeMemSendQueue(nullptr)
	, m_pSessions(nullptr)
	, m_hIoCompletionPort(NULL)
	, m_listenSock(INVALID_SOCKET)
	, m_hAcceptThread(NULL)
	, m_workerThreads()
	, m_numOfWorkerThreads(0)
	, m_numOfConcurrentThreads(0)
	, m_readySessionIndicesLock()
	, m_readySessionIndices()
	, m_sessionCount(0)
	, m_acceptThreadContext()
	, m_workerThreadContext()
{
}

TCPServer::~TCPServer()
{
	this->Shutdown();
}

int TCPServer::Run(const TCPServerConfig& desc)
{
	wchar_t logMsgBuf[128];

	assert(m_sessionCount == 0);

	do
	{
		if (m_init)
			break;	// escape do while(false)

		GetSystemInfo(&m_si);
		// WSAEnumProtocols();
		// SetFileCompletionNotificationMode -> WSARecv 반복 호출 구조에서 적용하기 쉽지 않음...
		// SetFileIoOverlappedRange -> 

		// ########################################################################################################
		// 파일 로거 생성
		const wchar_t* logFileName = desc.m_logFileName;
		wchar_t logFileNameBuf[64];
		if (!logFileName)
		{
			SYSTEMTIME st;
			GetLocalTime(&st);
			swprintf_s(logFileNameBuf, L"%04d%02d%02d_%02d%02d%02d_TCPServer.log", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
			logFileName = logFileNameBuf;
		}
		
		if (!m_fileLogger.Open(logFileName))
			break;	// escape do while(false)
		// ########################################################################################################

		// ########################################################################################################
		// 초기화 변수 검사 및 조정
		m_headerCode = desc.m_headerCode;
		m_maxSessionCount = Math::Clamp(desc.m_maxSessionCount, 1u, SESSION_COUNT_MAX);
		m_sessionRecvBufSize = Math::NextPowerOf2(Math::Clamp(desc.m_sessionRecvBufSize, RECV_BUFFER_SIZE_MIN, RECV_BUFFER_SIZE_MAX));
		m_sessionSendQueueSize = Math::NextPowerOf2(Math::Clamp(desc.m_sessionSendQueueSize, SEND_QUEUE_SIZE_MIN, SEND_QUEUE_SIZE_MAX));
		m_numOfWorkerThreads = desc.m_numOfWorkerThreads == 0 ? static_cast<uint32_t>(m_si.dwNumberOfProcessors * 1.5) : desc.m_numOfWorkerThreads;
		m_numOfConcurrentThreads = desc.m_numOfConcurrentThreads == 0 ? m_si.dwNumberOfProcessors : desc.m_numOfConcurrentThreads;
		m_tcpNoDelay = desc.m_tcpNoDelay;
		m_zeroByteSendBuf = desc.m_zeroByteSendBuf;

		// 조정된 설정값 로그 출력
		m_fileLogger.Write(L"%ls Maximum session count: %u\n", LogPrefixString::Info(), m_maxSessionCount);
		m_fileLogger.Write(L"%ls Session receive buffer size: %u\n", LogPrefixString::Info(), m_sessionRecvBufSize);
		m_fileLogger.Write(L"%ls Session send queue size: %u\n", LogPrefixString::Info(), m_sessionSendQueueSize);
		m_fileLogger.Write(L"%ls Number of worker threads: %u\n", LogPrefixString::Info(), m_numOfWorkerThreads);
		m_fileLogger.Write(L"%ls Number of concurrent threads: %u\n", LogPrefixString::Info(), m_numOfConcurrentThreads);
		// ########################################################################################################

		// ########################################################################################################
		// 직렬화 버퍼 풀 초기화
		SerializeBufferBatchPool::GetInstance().Init(m_headerCode);
		// ########################################################################################################

		// ########################################################################################################
		// 세션 메모리 할당 및 초기화
		// 1. 세션 수신 버퍼 메모리 할당
		const size_t recvBufMemSize = static_cast<size_t>(m_maxSessionCount) * m_sessionRecvBufSize;
		m_pLargeMemRecvBuf = VirtualAlloc(nullptr, recvBufMemSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!m_pLargeMemRecvBuf)
		{
			DWORD ec = GetLastError();
			Debug::GetWinErrString(ec, logMsgBuf, _countof(logMsgBuf));
			m_fileLogger.Write(L"%ls VirtualAlloc failed with error: %lu. %ls\n", LogPrefixString::Fatal(), ec, logMsgBuf);
			break;	// escape do while(false)
		}
		m_fileLogger.Write(L"%ls VirtualAlloc memory allocation: %zuKiB. (Session RecvBufs)\n", LogPrefixString::Info(), recvBufMemSize / Math::OneKiB());
		// 2. 세션 송신 큐 메모리 할당
		const size_t sendQueueMemSize = static_cast<size_t>(m_maxSessionCount) * m_sessionSendQueueSize * sizeof(SerializeBuffer*);
		m_pLargeMemSendQueue = VirtualAlloc(nullptr, sendQueueMemSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!m_pLargeMemSendQueue)
		{
			DWORD ec = GetLastError();
			Debug::GetWinErrString(ec, logMsgBuf, _countof(logMsgBuf));
			m_fileLogger.Write(L"%ls VirtualAlloc failed with error: %lu. %ls\n", LogPrefixString::Fatal(), ec, logMsgBuf);
			break;	// escape do while(false)
		}
		m_fileLogger.Write(L"%ls VirtualAlloc memory allocation: %zuBytes. (Session SendQueues)\n", LogPrefixString::Info(), sendQueueMemSize);


		// 2. 세션 인스턴스 생성 및 초기화
		m_pSessions = static_cast<TCPSession*>(_aligned_malloc_dbg(sizeof(TCPSession) * m_maxSessionCount, alignof(TCPSession), __FILE__, __LINE__));
		if (!m_pSessions)
		{
			m_fileLogger.Write(L"%ls _aligned_malloc failed.\n", LogPrefixString::Fatal());
			break;	// escape do while(false)
		}

		// - Placement new -
		for (uint32_t i = 0; i < m_maxSessionCount; ++i)
			new(m_pSessions + i) TCPSession();

		ptrdiff_t recvBufOffset = 0;	// 수신 버퍼 주소 오프셋
		ptrdiff_t sendQueueOffset = 0;	// 송신 큐 주소 오프셋
		TCPSessionInitDesc sessionInitDesc;
		sessionInitDesc.m_recvBufSize = m_sessionRecvBufSize;	// 루프 도중 불변
		sessionInitDesc.m_sendQueueSize = m_sessionSendQueueSize;
		for (uint32_t i = 0; i < m_maxSessionCount; ++i)
		{
			sessionInitDesc.m_pRecvBufAddr = static_cast<uint8_t*>(m_pLargeMemRecvBuf) + recvBufOffset;
			sessionInitDesc.m_pSendQueueAddr = static_cast<SerializeBuffer**>(m_pLargeMemSendQueue) + sendQueueOffset;
			m_pSessions[i].Init(sessionInitDesc);

			recvBufOffset += m_sessionRecvBufSize;
			sendQueueOffset += m_sessionSendQueueSize;
		}

		// 3. 가용 세션 인덱스 채우기
		InitializeSRWLock(&m_readySessionIndicesLock);	// 자료구조 동기화용
		m_readySessionIndices.reserve(m_maxSessionCount);
		for (uint32_t i = m_maxSessionCount; i-- > 0;)
			m_readySessionIndices.push_back(i);
		// ########################################################################################################
		
		// ########################################################################################################
		// I/O Completion Port 생성
		m_hIoCompletionPort = winppy::CreateNewCompletionPort(m_numOfConcurrentThreads);
		if (m_hIoCompletionPort == NULL)
		{
			int ec = WSAGetLastError();
			Debug::GetWinErrString(ec, logMsgBuf, _countof(logMsgBuf));
			m_fileLogger.Write(L"%ls CreateIoCompletionPort failed with error: %d. %ls\n", LogPrefixString::Error(), ec, logMsgBuf);
			break;	// escape do while(false)
		}
		// ########################################################################################################

		// ########################################################################################################
		// 리슨 소켓 생성 및 주소 바인딩.
		m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_listenSock == INVALID_SOCKET)
		{
			int ec = WSAGetLastError();
			Debug::GetWinErrString(ec, logMsgBuf, _countof(logMsgBuf));
			m_fileLogger.Write(L"%ls socket failed with error: %d. %ls\n", LogPrefixString::Error(), ec, logMsgBuf);
			break;	// escape do while(false)
		}

		SOCKADDR_IN bindAddr;
		ZeroMemory(&bindAddr, sizeof(bindAddr));
		bindAddr.sin_family = AF_INET;
		bindAddr.sin_port = htons(desc.m_bindPort);	// 포트 바인딩
		if (!desc.m_bindAddr)
		{
			bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		}
		else
		{
			INT ret = InetPtonW(AF_INET, desc.m_bindAddr, &bindAddr.sin_addr);
			if (ret == -1)	// error code can be retrieved by calling the WSAGetLastError for extended error information.
			{
				int ec = WSAGetLastError();
				Debug::GetWinErrString(ec, logMsgBuf, _countof(logMsgBuf));
				m_fileLogger.Write(L"%ls InetPtonW failed with error: %d. %ls\n", LogPrefixString::Error(), ec, logMsgBuf);
				break;	// escape do while(false)
			}
			else if (ret == 0)	// if the pszAddrString parameter points to a string that is not a valid IPv4 dotted-decimal string or a valid IPv6 address string.
			{
				m_fileLogger.Write(L"%ls %ls is not a valid IPv4 dotted-decimal string.\n", LogPrefixString::Fail(), desc.m_bindAddr);
				break;	// escape do while(false)
			}
			else
			{
				// no error occurs.
			}
		}

		if (bind(m_listenSock, reinterpret_cast<sockaddr*>(&bindAddr), static_cast<int>(sizeof(bindAddr))) == SOCKET_ERROR)
		{
			int ec = WSAGetLastError();
			Debug::GetWinErrString(ec, logMsgBuf, _countof(logMsgBuf));
			m_fileLogger.Write(L"%ls bind failed with error: %d. %ls\n", LogPrefixString::Fail(), ec, logMsgBuf);
			break;	// escape do while(false)
		}
		m_bindAddr = desc.m_bindAddr == nullptr ? L"INADDR_ANY" : desc.m_bindAddr;
		m_bindPort = desc.m_bindPort;
		// ########################################################################################################

		// ########################################################################################################
		// Accept Thread 생성 & Worker Thread 생성
		// Accept Thread
		// Context 설정
		m_acceptThreadContext.m_pServer = this;
		m_acceptThreadContext.m_pSessionCount = &m_sessionCount;
		m_acceptThreadContext.m_pFileLogger = &m_fileLogger;
		m_acceptThreadContext.m_hIoCompletionPort = m_hIoCompletionPort;
		m_acceptThreadContext.m_listenSock = m_listenSock;
		m_acceptThreadContext.m_backlogSize = Math::Clamp(desc.m_backlogSize, static_cast<uint16_t>(200), static_cast<uint16_t>(65535));
		m_acceptThreadContext.m_tcpNoDelay = m_tcpNoDelay;
		m_acceptThreadContext.m_zeroByteSendBuf = m_zeroByteSendBuf;
		m_acceptThreadContext.m_pSessions = m_pSessions;
		m_acceptThreadContext.m_pReadySessionIndicesLock = &m_readySessionIndicesLock;
		m_acceptThreadContext.m_pReadySessionIndices = &m_readySessionIndices;
		// 스레드 생성
		HANDLE hAcceptThread = winppy::LogBeginThreadEx(
			m_fileLogger,
			nullptr,
			0,
			TCPServer::AcceptThreadEntry,
			&m_acceptThreadContext,
			CREATE_SUSPENDED,
			nullptr
		);
		if (hAcceptThread == NULL)
			break;	// escape do while(false)

		m_hAcceptThread = hAcceptThread;


		// Worker Thread
		// Context 설정
		m_workerThreadContext.m_pServer = this;
		m_workerThreadContext.m_pFileLogger = &m_fileLogger;
		m_workerThreadContext.m_hIoCompletionPort = m_hIoCompletionPort;
		// 스레드 생성
		bool workerThreadCreationSuccess = true;
		m_workerThreads.reserve(m_numOfWorkerThreads);
		for (uint32_t i = 0; i < m_numOfWorkerThreads; ++i)
		{
			HANDLE hWorkerThread = winppy::LogBeginThreadEx(
				m_fileLogger,
				nullptr,
				0,
				TCPServer::WorkerThreadEntry,
				&m_workerThreadContext,
				CREATE_SUSPENDED,
				nullptr
			);
			if (hWorkerThread == NULL)
			{
				workerThreadCreationSuccess = false;
				break;
			}
			m_workerThreads.push_back(hWorkerThread);
		}

		if (!workerThreadCreationSuccess)
			break;	// escape do while(false)
		// ########################################################################################################



		// ########################################################################################################
		// 지연된 Worker 스레드 및 Accept 스레드 실행 재개
		for (HANDLE hWorkerThread : m_workerThreads)
			ResumeThread(hWorkerThread);
		Sleep(50);
		ResumeThread(m_hAcceptThread);
		Sleep(50);
		// ########################################################################################################

		m_init = true;
	} while (false);

	if (!m_init)
	{
		this->Shutdown();
		return -1;
	}
	else
	{
		m_fileLogger.Write(L"%ls Bind Address: %ls, Port: %u\n", LogPrefixString::Info(), m_bindAddr.c_str(), static_cast<uint32_t>(m_bindPort));
		m_fileLogger.Write(L"%ls Server is running...\n", LogPrefixString::Info());

		const char* endKeyStr;
		switch (desc.m_endKey)
		{
		case EndKey::Backspace:
			endKeyStr = "Backspace";
			break;
		case EndKey::Tab:
			endKeyStr = "Tab";
			break;
		case EndKey::Enter:
			endKeyStr = "Enter";
			break;
		case EndKey::Esc:
			endKeyStr = "Esc";
			break;
		case EndKey::Spacebar:
			endKeyStr = "Spacebar";
			break;
		default:
			endKeyStr = nullptr;
			break;
		}

		if (endKeyStr)
			printf_s("Press '%s' key to exit.\n", endKeyStr);
		else
			printf_s("Press '%c' key to exit.\n", static_cast<char>(desc.m_endKey));

		while (_getch() != static_cast<char>(desc.m_endKey));
		this->Shutdown();

		return 0;
	}
}

void TCPServer::Shutdown()
{
	// wchar_t logMsgBuf[128];

	// 리슨 소켓 닫기 -> Accept 스레드 종료
	if (m_listenSock != INVALID_SOCKET)
	{
		closesocket(m_listenSock);
		m_listenSock = INVALID_SOCKET;
	}

	if (m_init)
	{
		// 모든 세션과의 연결 종료
		this->DisconnectAllSessions();
		// 현재 문제점: DisconnectAllSessions 호출 후 worker thread들이 세션들에 대한 disconnection routine을 수행하기 전에
		// Line 401에 의해 Completion port가 닫혀버린다. 따라서 worker thread들이 완료 포트의 작업들을 처리하지 못한 채 종료되고
		// 이는 Line 427의 assert(m_sessionCount == 0); assertion을 통과하지 못하게 된다.
		// Sleep(1000);	// 임시 해결책. (1초동안 작업 처리 대기 시간 부여). 하지만 OnDisconnect에서 오래 걸리는 작업을 하게 되면 여전히 문제가 발생하므로 매우 좋지 않음.

		// 생각해낸 다른 해결책:
		// 서버의 listen & accept를 닫는 플래그를 추가하고, 해당 플래그를 켠 뒤, DisconnectAllSessions를 호출한다.
		// 그 후 session count가 0이 될 때까지 Sleep 루프를 돌며 대기한다. 이 경우에는 문제 하나만 해결하면 된다.
		// Accept ~ TCPSession::Start 사이에 해당하는 세션은 DisconnectAllSessions 함수에서 검출할 수 없다는 문제점.
	}
	
	// Accept Thread 종료
	if (m_hAcceptThread)
	{
		if (!m_init)
			ResumeThread(m_hAcceptThread);

		WaitForSingleObject(m_hAcceptThread, INFINITE);
		CloseHandle(m_hAcceptThread);
		m_hAcceptThread = NULL;
	}

	// I/O Completion Port 해제 +> 모든 Worker Thread 종료
	if (m_hIoCompletionPort)
	{
		while (m_sessionCount > 0)
			Sleep(100);

		// GetQueuedCompletionStatus 호출이 진행중인 동안 연결된 완료 포트 핸들이 닫혀서 실패하는 경우
		// 함수는 FALSE를 반환하고 lpOverlapped는 NULL이 되며 GetLastError는 ERROR_ABANDONED_WAIT_0을 반환합니다.
		CloseHandle(m_hIoCompletionPort);
		m_hIoCompletionPort = NULL;
	}
	for (HANDLE hWorkerThread : m_workerThreads)
	{
		if (!m_init)
			ResumeThread(hWorkerThread);

		WaitForSingleObject(hWorkerThread, INFINITE);
		CloseHandle(hWorkerThread);
	}
	m_workerThreads.clear();

	// 모든 worker thread가 종료된 시점에 남은 세션은 없어야 한다. worker thread가 모두 disconnection routine을 수행했어야만 한다.
	assert(m_sessionCount == 0);


	// 스레드 컨텍스트 댕글링 리소스 관리
	m_acceptThreadContext.m_listenSock = INVALID_SOCKET;
	m_workerThreadContext.m_hIoCompletionPort = NULL;


	// 가용 세션 인덱스 배열 데이터 제거
	m_readySessionIndices.clear();


	// 세션 리소스 해제
	// 수신 버퍼 해제
	if (m_pLargeMemRecvBuf)
	{
		VirtualFree(m_pLargeMemRecvBuf, 0, MEM_RELEASE);
		m_pLargeMemRecvBuf = nullptr;
	}
	// 송신 큐 해제
	if (m_pLargeMemSendQueue)
	{
		VirtualFree(m_pLargeMemSendQueue, 0, MEM_RELEASE);
		m_pLargeMemSendQueue = nullptr;
	}
	// 세션 메모리 해제
	if (m_pSessions)
	{
		// - Placement new -
		for (uint32_t i = 0; i < m_maxSessionCount; ++i)
			m_pSessions[i].~TCPSession();

		_aligned_free_dbg(m_pSessions);
		m_pSessions = nullptr;
	}

	m_maxSessionCount = 0;
	m_sessionRecvBufSize = 0;
	m_sessionSendQueueSize = 0;
	m_bindPort = 0;
	m_bindAddr.clear();

	m_fileLogger.Write(L"%ls Server has been shutdown.\n", LogPrefixString::Info());

	// 파일 로거 리소스 해제
	m_fileLogger.Close();

	m_init = false;
}

void TCPServer::Send(uint64_t id, Packet packet)
{
	if (!packet)
		return;

	TCPSession& session = m_pSessions[ComputeSessionIndex(id)];
	InterlockedIncrement16(&session.m_flag.m_refCount);		// 세션 유효성 확인 참조

	do
	{
		// 세션 Start 코드에서 released 플래그가 Interlocked초기화되기 때문에 released가 0으로 읽힌 시점에 id는 반드시 새 세션의 id임이 보장됨.
		if (session.m_flag.m_released)
			break;

		_ReadWriteBarrier();	// 세션 초기화 과정에서 id가 먼저 초기화, released 플래그가 나중에 초기화되기 때문에 엇갈리게 읽어야 한다.

		if (session.GetId() != id)
			break;

		SerializeBuffer* pSerBuf = packet.Detach();				// 참조 카운트 유지하면서 Packet 객체로부터 분리.
		AcquireSRWLockExclusive(session.GetSendQueueLock());
		bool success = session.GetSendQueue().Push(pSerBuf);	// A.
		ReleaseSRWLockExclusive(session.GetSendQueueLock());

		if (!success)
		{
			pSerBuf->Release();
			this->Disconnect(id);
			break;
		}

		// B.

		if (InterlockedExchange8(&session.m_isSending, 1) != 0)
			break;

		if (session.m_cancelIo)
			break;

		// A ~ B 지점 사이에 스레드가 대기 상태로 전환되었다가 깨어난 경우 다른 스레드가 송신을 했을 수 있다. PostSend 함수 내에서 큐가 비어있을 수 있음.
		this->PostSend(session);
	} while (false);

	if (InterlockedDecrement16(&session.m_flag.m_refCount) == 0)	// 세션 유효성 확인 참조에 대응
		this->ReleaseSession(session);
}

bool TCPServer::Disconnect(uint64_t id)
{
	bool result = false;
	TCPSession& session = m_pSessions[ComputeSessionIndex(id)];
	InterlockedIncrement16(&session.m_flag.m_refCount);		// 세션 유효성 확인 참조

	do
	{
		if (session.m_flag.m_released)	// 세션 Start 코드에서 released 플래그가 Interlocked초기화되기 때문에 0으로 읽힌 시점에 id는 반드시 새 세션의 id임이 보장됨.
			break;

		_ReadWriteBarrier();	// 세션 초기화 과정에서 id가 먼저 초기화, released 플래그가 나중에 초기화되기 때문에

		if (session.GetId() != id)
			break;

		InterlockedExchange8(&session.m_cancelIo, 1);
		if (CancelIoEx(reinterpret_cast<HANDLE>(session.GetSocket()), nullptr) == FALSE)
		{
			// 파일 핸들이 완료 포트와 연결되어 있는 경우, 동기 작업이 성공적으로 취소되면 I/O 완료 패킷이 해당 포트에 대기열에 추가되지 않습니다.
			// 하지만 아직 보류 중인 비동기 작업의 경우, 취소 작업으로 인해 I/O 완료 패킷이 대기열에 추가됩니다.
			DWORD ec = GetLastError();
			switch (ec)
			{
			case ERROR_NOT_FOUND:
				break;
			default:
				m_fileLogger.Write(L"%ls CancelIoEx failed with error: %lu.\n", LogPrefixString::Warning(), ec);
				break;
			}
		}

		result = true;
	} while (false);

	if (InterlockedDecrement16(&session.m_flag.m_refCount) == 0)	// 세션 유효성 확인 참조에 대응
		this->ReleaseSession(session);

	return result;
}

void TCPServer::DirectDisconnect(TCPSession& session)
{
	// Disconnect(uint64_t) 함수의 Interlock 오버헤드를 생략시킨 함수

	// 이 함수는 유효 세션을 참조중인 경우에만 호출되었어야 한다. (참조 카운트 1 이상)
	assert(session.m_flag.m_refCount > 0);
	assert(session.m_flag.m_released == 0);

	InterlockedExchange8(&session.m_cancelIo, 1);
	if (CancelIoEx(reinterpret_cast<HANDLE>(session.GetSocket()), nullptr) == FALSE)
	{
		// 파일 핸들이 완료 포트와 연결되어 있는 경우, 동기 작업이 성공적으로 취소되면 I/O 완료 패킷이 해당 포트에 대기열에 추가되지 않습니다.
		// 하지만 아직 보류 중인 비동기 작업의 경우, 취소 작업으로 인해 I/O 완료 패킷이 대기열에 추가됩니다.
		DWORD ec = GetLastError();
		switch (ec)
		{
		case ERROR_NOT_FOUND:
			break;
		default:
			m_fileLogger.Write(L"%ls CancelIoEx failed with error: %lu.\n", LogPrefixString::Warning(), ec);
			break;
		}
	}
}

void TCPServer::DisconnectAllSessions()
{
	m_fileLogger.Write(L"%ls Disconnect with all sessions...\n", LogPrefixString::Info());

	// accept 후 세션 초기화중인 경우는 아래 반복문에서 잡아내지 못할 수 있음..
	for (uint32_t i = 0; i < m_maxSessionCount; ++i)
	{
		TCPSession& session = m_pSessions[i];
		if (session.m_flag.m_released)
			continue;

		Disconnect(session.GetId());
	}
}

void TCPServer::ReleaseSession(TCPSession& session)
{
	// 실패 시 다른 스레드의 세션 참조로 인한 RefCount 증가
	// refCount, released 플래그 모두 0이었던 경우에만 통과
	if (InterlockedCompareExchange(&session.m_flag.m_releasedAndRefCount, 0x00010000, 0x00000000) != 0x00000000)
		return;
	// 다른 스레드에 의해서 refCount가 증가된 경우 절대 이 아래로 수행되지 않는다.
	// 따라서 다른 스레드는 안전하게 세션에 대한 동작을 수행할 수 있다.


	// 아래 코드 수행 시점부터는 다른 스레드에서 세션의 released 플래그가 켜진것을 확인할 수 있으므로 세션에 대한 추가 작업 없이 리턴하는 식으로 동기화 가능.



	// OnDisconnect 호출 및 추가 정리 과정을 다른 스레드로 우회시켜 컨텐츠 코드에서의 재귀 락으로 인한 데드락 가능성을 방지.
	if (PostSessionReleaseJob(m_hIoCompletionPort, session) == FALSE)
	{
		DWORD ec = GetLastError();
		m_fileLogger.Write(L"%ls PostQueuedCompletionStatus failed with error: %lu.\n", LogPrefixString::Error(), ec);
	}
}

void TCPServer::DoSessionReleaseJob(TCPSession& session)
{
	// 이 함수는 반드시 재진입 가능 함수여야 한다.

	const size_t sessionIndex = ComputeSessionIndex(session.GetId());

	// 수신 버퍼 클리어
	ReceiveBuffer& rb = session.GetReceiveBuffer();
	rb.Clear();

	// 송신 큐의 잔여 패킷 제거
	SendQueue& sq = session.GetSendQueue();
	AcquireSRWLockExclusive(session.GetSendQueueLock());	/* 생략 가능할듯? */
	while (!sq.Empty())
	{
		SerializeBuffer* pSerBuf = sq.Pop();
		pSerBuf->Release();
	}
	ReleaseSRWLockExclusive(session.GetSendQueueLock());	/* 생략 가능할듯? */

	// 소켓 닫기
	closesocket(session.GetSocket());

	// 세션 카운트 감소
	InterlockedDecrement(&m_sessionCount);

	// 가용 세션 인덱스에 추가
	AcquireSRWLockExclusive(&m_readySessionIndicesLock);
	m_readySessionIndices.push_back(static_cast<uint32_t>(sessionIndex));
	ReleaseSRWLockExclusive(&m_readySessionIndicesLock);

	// released 플래그는 아직 절대 끄면 안됨.
}

void TCPServer::OnReceiveData(TCPSession& session, size_t numOfBytesTransferred)
{
	// 이 함수는 반드시 재진입 가능 함수여야 한다.

	ReceiveBuffer& rb = session.GetReceiveBuffer();
	rb.AdvanceWriteCursor(numOfBytesTransferred);	// 쓰기 커서 전진.

	// 직렬화 버퍼의 m_size가 수신 버퍼 크기보다 크면 절대 안됨.
	SerializeBuffer::Header header;

	for (;;)
	{
		const size_t readableSize = rb.Size();
		if (readableSize < sizeof(header))
			break;

		rb.Peek(&header, sizeof(header));	// Size 함수로 읽을 수 있는 크기인지 확인 뒤 호출해야 안전.

		if (header.m_code != m_headerCode)						// 비정상 패킷
		{
			m_fileLogger.Write(L"%ls Packet marshaling failed. Invalid header code: 0x%08x.\n", LogPrefixString::Warning(), header.m_code);
			DirectDisconnect(session);
			break;
		}

		if (header.m_size > SerializeBuffer::Capacity())		// 비정상 패킷
		{
			m_fileLogger.Write(L"%ls Packet marshaling failed. Invalid payload size: %uBytes.\n", LogPrefixString::Warning(), header.m_size);
			DirectDisconnect(session);
			break;
		}

		const size_t messageSize = sizeof(header) + header.m_size;
		if (messageSize > readableSize)		// 데이터 스트림이 아직 다 도착하지 않은 경우.
			break;

		// 수신 버퍼의 내용을 직렬화 버퍼에 복사.
		Packet packet;
		SerializeBuffer* pSerBuf = packet.Get();
		rb.Peek(pSerBuf->Message(), messageSize);
		pSerBuf->SetReadCursorOffset(0);
		pSerBuf->SetWriteCursorOffset(header.m_size);

		rb.AdvanceReadCursor(messageSize);		// 읽기 커서 전진.

		this->OnReceive(session.GetId(), std::move(packet));
	}
}

void TCPServer::PostRecv(TCPSession& session)
{
	// 이 함수는 반드시 재진입 가능 함수여야 한다.

	DWORD flags = 0;
	WSABUF wsaBufs[2];
	DWORD bufferCount = 0;

	ReceiveBufferArea wa;
	session.GetReceiveBuffer().GetWritableArea(wa);

	if (wa.m_contiguousSize > 0)
	{
		wsaBufs[0].buf = static_cast<CHAR*>(wa.m_pContiguous);
		wsaBufs[0].len = static_cast<ULONG>(wa.m_contiguousSize);
		++bufferCount;
	}
	if (wa.m_wrapSize > 0)
	{
		wsaBufs[1].buf = static_cast<CHAR*>(wa.m_pWrap);
		wsaBufs[1].len = static_cast<ULONG>(wa.m_wrapSize);
		++bufferCount;
	}

	if (bufferCount == 0)
		return;

	// 동기, 비동기 모두 완료 포트로 완료통지가 온다.
	ZeroMemory(session.GetRecvOverlapped(), sizeof(WSAOVERLAPPED));		// Recv overlapped 구조체 초기화
	InterlockedIncrement16(&session.m_flag.m_refCount);		// 입출력 참조 카운트 증가
	int ioResult = WSARecv(session.GetSocket(), wsaBufs, bufferCount, nullptr, &flags, session.GetRecvOverlapped(), nullptr);
	if (ioResult == SOCKET_ERROR)
	{
		int ec = WSAGetLastError();
		switch (ec)
		{
		case WSA_IO_PENDING:	// WSARecv가 비동기로 수행됨.
			if (session.m_cancelIo)		// Disconnect 요청된 세션인 경우 전송 취소
			{
				if (CancelIo(reinterpret_cast<HANDLE>(session.GetSocket())) == FALSE)
				{
					// 굳이 CancelIoEx를 사용할 필요 없음. (현재 스레드가 건 IO를 취소하면 되므로.)
					// 파일 핸들이 완료 포트와 연결되어 있는 경우, 동기 작업이 성공적으로 취소되면 I/O 완료 패킷이 해당 포트에 대기열에 추가되지 않습니다.
					// 하지만 아직 보류 중인 비동기 작업의 경우, 취소 작업으로 인해 I/O 완료 패킷이 대기열에 추가됩니다.
					DWORD ec = GetLastError();
					switch (ec)
					{
					case ERROR_NOT_FOUND:
						break;
					default:
						m_fileLogger.Write(L"%ls CancelIoEx failed with error: %lu.\n", LogPrefixString::Warning(), ec);
						break;
					}
				}
			}
			break;
		default:	// Any other error code indicates that the overlapped operation was not successfully initiated and 'no completion indication will occur'.
			// 예시) WSARecv를 걸기 전 RST가 도착해있는 경우, ...
			m_fileLogger.Write(L"%ls WSARecv failed with error: %d. Terminate the connection to the session.\n", LogPrefixString::Fail(), ec);
			this->DirectDisconnect(session);
			if (InterlockedDecrement16(&session.m_flag.m_refCount) == 0)	// (완료통지 오지 않으므로 참조 카운트 여기서 차감.)
				this->ReleaseSession(session);
			break;
		}
	}
}

void TCPServer::PostSend(TCPSession& session)
{
	// 이 함수는 반드시 재진입 가능 함수여야 한다.

	WSABUF wsaBufs[WSABUF_LEN_MAX];
	SerializeBuffer* pSerBufs[WSABUF_LEN_MAX];

	SendQueue& sq = session.GetSendQueue();
	AcquireSRWLockExclusive(session.GetSendQueueLock());
	const size_t numOfPacketsToSend = (std::min)(sq.Size(), WSABUF_LEN_MAX);
	sq.Peek(pSerBufs, numOfPacketsToSend);
	ReleaseSRWLockExclusive(session.GetSendQueueLock());

	assert(numOfPacketsToSend < (std::numeric_limits<uint16_t>::max)());

	// SendQueue에 패킷을 Push하고 SendFlag를 켜는 과정이 원자적이지 않기 때문에 그 사이에 다른 스레드가 PostSend를 하여 SendQueue가 비어있을 수 있다.
	if (numOfPacketsToSend == 0)
	{
		CHAR ret = InterlockedExchange8(&session.m_isSending, 0);
		if (ret != 1)	// 심각한 결함
			Debug::ForceCrash();

		return;
	}

	for (size_t i = 0; i < numOfPacketsToSend; ++i)
	{
		wsaBufs[i].buf = static_cast<CHAR*>(const_cast<void*>(pSerBufs[i]->Message()));
		wsaBufs[i].len = static_cast<ULONG>(pSerBufs[i]->SizeIncludingHeader());
	}
	session.m_numOfPacketsPending = static_cast<uint16_t>(numOfPacketsToSend);		// 이 변수에 대한 접근은 isSending 플래그 인터락 매커니즘으로 단일 스레드만 변경을 보장해야 함.

	// 동기, 비동기 모두 완료 포트로 완료통지가 온다.
	ZeroMemory(session.GetSendOverlapped(), sizeof(WSAOVERLAPPED));
	InterlockedIncrement16(&session.m_flag.m_refCount);
	int ioResult = WSASend(session.GetSocket(), wsaBufs, static_cast<DWORD>(numOfPacketsToSend), nullptr, 0, session.GetSendOverlapped(), nullptr);
	if (ioResult == SOCKET_ERROR)
	{
		int ec = WSAGetLastError();
		switch (ec)
		{
		case WSA_IO_PENDING:	// WSASend가 비동기로 수행됨.
			if (session.m_cancelIo)		// Disconnect 요청된 세션인 경우 전송 취소
			{
				if (CancelIo(reinterpret_cast<HANDLE>(session.GetSocket())) == FALSE)
				{
					// 굳이 CancelIoEx를 사용할 필요 없음. (현재 스레드가 건 IO를 취소하면 되므로.)
					// 파일 핸들이 완료 포트와 연결되어 있는 경우, 동기 작업이 성공적으로 취소되면 I/O 완료 패킷이 해당 포트에 대기열에 추가되지 않습니다.
					// 하지만 아직 보류 중인 비동기 작업의 경우, 취소 작업으로 인해 I/O 완료 패킷이 대기열에 추가됩니다.
					DWORD ec = GetLastError();
					switch (ec)
					{
					case ERROR_NOT_FOUND:
						break;
					default:
						m_fileLogger.Write(L"%ls CancelIoEx failed with error: %lu.\n", LogPrefixString::Warning(), ec);
						break;
					}
				}
			}
			break;
		default:	// Any other error code indicates that the overlapped operation was not successfully initiated and 'no completion indication will occur'.
			m_fileLogger.Write(L"%ls WSASend failed with error: %d. Terminate the connection to the session.\n", LogPrefixString::Fail(), ec);
			this->DirectDisconnect(session);
			if (InterlockedDecrement16(&session.m_flag.m_refCount) == 0)	// (완료통지 오지 않으므로 참조 카운트 여기서 차감.)
				this->ReleaseSession(session);
			break;
		}
	}
}

unsigned int __stdcall TCPServer::AcceptThreadEntry(void* pArg)
{
	wprintf(L"Accept thread begins.\n");

	wchar_t logMsgBuf[128];
	const AcceptThreadContext& context = *reinterpret_cast<const AcceptThreadContext*>(pArg);

	TCPServer* pServer = context.m_pServer;
	uint32_t* pSessionCount = context.m_pSessionCount;
	FileLogger& fileLogger = *context.m_pFileLogger;
	const HANDLE hIoCompletionPort = context.m_hIoCompletionPort;
	const SOCKET listenSock = context.m_listenSock;
	const uint16_t backlogSize = context.m_backlogSize;
	const bool zeroByteSendBuf = context. m_zeroByteSendBuf;
	const bool tcpNoDelay = context.m_tcpNoDelay;
	TCPSession* pSessions = context.m_pSessions;
	SRWLOCK* pReadySessionIndicesLock = context.m_pReadySessionIndicesLock;
	std::vector<uint32_t>& readySessionIndices = *context.m_pReadySessionIndices;
	uint32_t idCounter = 0;	// 상위 32비트 id 발급용

	const linger lg{ 1, 0 };
	
	if (listen(listenSock, SOMAXCONN_HINT(backlogSize)) == SOCKET_ERROR)
	{
		int ec = WSAGetLastError();
		Debug::GetWinErrString(ec, logMsgBuf, _countof(logMsgBuf));
		fileLogger.Write(L"%ls listen failed with error: %d. %ls\n", LogPrefixString::Fail(), ec, logMsgBuf);
		return -1;
	}
	wprintf(L"Server is listening...\n");

	bool exit = false;
	while (!exit)
	{
		SOCKADDR_STORAGE clientAddr;
		int addrLen = static_cast<int>(sizeof(clientAddr));
		const SOCKET sock = accept(listenSock, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
		if (sock == INVALID_SOCKET)
		{
			const int ec = WSAGetLastError();
			if (ec == WSAEINTR)		// accept중인 리슨 소켓을 다른 스레드에서 closesocket 하는 경우 이 오류 코드가 나온다.
				exit = true;

			Debug::GetWinErrString(ec, logMsgBuf, _countof(logMsgBuf));
			fileLogger.Write(L"%ls accept failed with error: %d. %ls\n", LogPrefixString::Error(), ec, logMsgBuf);
			continue;
		}

		wchar_t ipAddrBuf[INET_ADDRSTRLEN];
		uint16_t port = 0;
		ipAddrBuf[0] = '\0';
		winppy::SockAddrToString(&clientAddr, ipAddrBuf, _countof(ipAddrBuf), &port);

		bool success = false;
		uint32_t readySessionIndex = (std::numeric_limits<uint32_t>::max)();	// 감시값으로 초기화
		do
		{
			// 세션 시작
			// 가용 세션 인덱스 조회
			AcquireSRWLockExclusive(pReadySessionIndicesLock);
			if (!readySessionIndices.empty())
			{
				readySessionIndex = readySessionIndices.back();
				readySessionIndices.pop_back();
			}
			ReleaseSRWLockExclusive(pReadySessionIndicesLock);
			if (readySessionIndex == (std::numeric_limits<uint32_t>::max)())	// 가용 세션 인덱스를 획득하지 못한 경우
			{

				wprintf_s(L"%ls Connection denied due to exceeding the maximum number of available sessions. Remote address: %ls:%u.\n",
					LogPrefixString::Info(), ipAddrBuf, static_cast<uint32_t>(port));
				break;	// escape do while(false)
			}

			// TCP_NODELAY 옵션 적용.
			if (tcpNoDelay)
			{
				BOOL noDelay = TRUE;
				if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), static_cast<int>(sizeof(noDelay))) == SOCKET_ERROR)
				{
					int ec = WSAGetLastError();
					// Debug::GetWinErrString(ec, logMsgBuf, _countof(logMsgBuf));
					fileLogger.Write(L"%ls setsockopt failed with error(trying to apply TCP_NODELAY option): %d.\n", LogPrefixString::Fail(), ec);
					break;	// escape do while(false)
				}
			}

			// SO_SNDBUF 옵션 적용.
			if (zeroByteSendBuf)
			{
				int bufSize = 0;
				if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize)) == SOCKET_ERROR)
				{
					int ec = WSAGetLastError();
					// Debug::GetWinErrString(ec, logMsgBuf, _countof(logMsgBuf));
					fileLogger.Write(L"%ls setsockopt failed with error(trying to apply SO_SNDBUF option): %d.\n", LogPrefixString::Fail(), ec);
					break;	// escape do while(false)
				}
			}

			// linger 옵션 적용.
			if (setsockopt(sock, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&lg), static_cast<int>(sizeof(lg))) == SOCKET_ERROR)
			{
				int ec = WSAGetLastError();
				// Debug::GetWinErrString(ec, logMsgBuf, _countof(logMsgBuf));
				fileLogger.Write(L"%ls setsockopt failed with error(trying to apply linger option): %d.\n", LogPrefixString::Fail(), ec);
				break;	// escape do while(false)
			}

			// Completion Port와 소켓 바인딩.
			if (!AssociateDeviceWithCompletionPort(hIoCompletionPort, reinterpret_cast<HANDLE>(sock), reinterpret_cast<ULONG_PTR>(pSessions + readySessionIndex)))
			{
				DWORD ec = GetLastError();
				fileLogger.Write(L"%ls AssociateDeviceWithCompletionPort failed with error: %lu.\n", LogPrefixString::Fail(), ec);
				break;	// escape do while(false)
			}

			/*
			if (isInstallableFileSystemLSP)
			{
				static_assert(_WIN32_WINNT >= 0x0600, "To compile an application that uses this function, define the _WIN32_WINNT macro as 0x0600 or later.");
				if (SetFileCompletionNotificationModes(reinterpret_cast<HANDLE>(sock), FILE_SKIP_COMPLETION_PORT_ON_SUCCESS) == FALSE)
				{
					DWORD ec = GetLastError();
					fileLogger.Write(L"%ls SetFileCompletionNotificationModes failed with error. %lu.\n", LogPrefixString::Error(), ec);
					break;
				}
			}
			*/

			success = true;
		} while (false);

		if (!success)
		{
			if (readySessionIndex != (std::numeric_limits<uint32_t>::max)())
			{
				AcquireSRWLockExclusive(pReadySessionIndicesLock);
				readySessionIndices.push_back(readySessionIndex);
				ReleaseSRWLockExclusive(pReadySessionIndicesLock);
			}

			closesocket(sock);
			continue;
		}

		TCPSession& session = pSessions[readySessionIndex];
		TCPSessionStartDesc desc;
		desc.m_id = (static_cast<uint64_t>(++idCounter) << 32) | static_cast<uint64_t>(readySessionIndex);
		desc.m_sock = sock;
		session.Start(desc);
		InterlockedIncrement(pSessionCount);

		// 초기 상태에서는 RecvPost로 인한 최소 참조 카운트 1조차 없으므로 Inc/Dec로 로직을 보호한다.
		// 특히 OnConnet에서 Disconnect함수를 호출 할 수 있으므로 참조 카운트 보호가 적용되어 있어야	한다.
		InterlockedIncrement16(&session.m_flag.m_refCount);
		do
		{
			bool conn = pServer->OnConnect(ipAddrBuf, port, desc.m_id);
			if (!conn)
				break;

			// if (session.m_flag.m_released)	// 참조 카운트 증가시켜놓은 상태이므로 절대 released 플래그가 켜져있을 수 없음.
			// 	break;
			// _ReadWriteBarrier();
			if (session.m_cancelIo)
				break;

			pServer->PostRecv(session);
		} while (false);

		if (InterlockedDecrement16(&session.m_flag.m_refCount) == 0)
			pServer->ReleaseSession(session);
	}

	return 0;
}

unsigned int __stdcall TCPServer::WorkerThreadEntry(void* pArg)
{
	wprintf(L"Worker thread begins.\n");

	const WorkerThreadContext& context = *reinterpret_cast<const WorkerThreadContext*>(pArg);
	TCPServer* pServer = context.m_pServer;
	FileLogger& fileLogger = *context.m_pFileLogger;
	const HANDLE hIoCompletionPort = context.m_hIoCompletionPort;

	bool exit = false;
	while (!exit)
	{
		OVERLAPPED_ENTRY overlappedEntry;
		// GetQueuedCompletionStatusEx - Single Worker Thread 구현 시 사용 가능
		BOOL result = GetQueuedCompletionStatus(
			hIoCompletionPort,
			&overlappedEntry.dwNumberOfBytesTransferred,
			&overlappedEntry.lpCompletionKey,
			&overlappedEntry.lpOverlapped,
			INFINITE
		);

		// Windows Server 2003 및 Windows XP 이후부터는... (MSDN 참조)
		// GetQueuedCompletionStatus 호출이 진행중인 동안 연결된 완료 포트 핸들이 닫혀서 실패하는 경우
		// 함수는 FALSE를 반환하고 lpOverlapped는 NULL이 되며 GetLastError는 ERROR_ABANDONED_WAIT_0을 반환합니다.
		TCPSession& session = *reinterpret_cast<TCPSession*>(overlappedEntry.lpCompletionKey);
		if (result)
		{
			if (overlappedEntry.lpOverlapped == session.GetRecvOverlapped())		// Recv 완료 처리
			{
				pServer->OnReceiveData(session, overlappedEntry.dwNumberOfBytesTransferred);

				if (session.m_cancelIo == 0 && overlappedEntry.dwNumberOfBytesTransferred != 0)
					pServer->PostRecv(session);
			}
			else if (overlappedEntry.lpOverlapped == session.GetSendOverlapped())	// Send 완료 처리
			{
				const size_t transmitted = session.m_numOfPacketsPending;
				SendQueue& sq = session.GetSendQueue();
				AcquireSRWLockExclusive(session.GetSendQueueLock());
				assert(sq.Size() >= transmitted);
				for (size_t i = 0; i < transmitted; ++i)
				{
					SerializeBuffer* pSerBuf = sq.Pop();
					pSerBuf->Release();
				}
				ReleaseSRWLockExclusive(session.GetSendQueueLock());


				// 이 멤버 변수에 대한 접근은 isSending 플래그 인터락 매커니즘으로 단일 스레드만 변경을 보장해야 함.
				// isSending 플래그를 먼저 끄고 이 변수를 변경하면 isSending 플래그가 꺼진 순간 다른 스레드의 PostSend 및 Send 완료통지 루틴이 실행되어
				// 데이터 경쟁이 발생하게 된다.
				session.m_numOfPacketsPending = 0;

				InterlockedExchange8(&session.m_isSending, 0);

				// AcquireSRWLockExclusive(session.GetSendQueueLock());
				const size_t sqSize = sq.Size();
				// ReleaseSRWLockExclusive(session.GetSendQueueLock());
				if (session.m_cancelIo == 0 && sqSize > 0)
					if (InterlockedExchange8(&session.m_isSending, 1) == 0)
						pServer->PostSend(session);
			}
			else
			{
				// 문서에 따르면 GetQueuedCompletionStatus 함수가 TRUE를 반환하면서 lpOverlapped가 NULL인 경우는 없으므로
				// 이 경우를 사용자 정의 이벤트 식별 용도로 사용할 수 있음.	-> PostSessionReleaseJob
				// assert(session.m_flag.m_refCount == 0 && session.m_flag.m_released == 1);
				assert(session.m_flag.m_releasedAndRefCount == 0x00010000);

				pServer->OnDisconnect(session.GetId());

				pServer->DoSessionReleaseJob(session);
				continue;	// InterlockedDecrement16(&session.m_flag.m_refCount) 실행 루트로 가면 절대 안됨! (이미 0이 되어서 작업이 예약되었으므로.)
			}
		}
		else
		{
			DWORD ec = GetLastError();
			if (overlappedEntry.lpOverlapped == nullptr)
			{
				/*
				* lpOverlapped가 NULL인 경우, 함수가 완료 포트에서 완료 패킷을 가져오지 못한 것입니다.
				* 이 경우 함수는 lpNumberOfBytes 및 lpCompletionKey 매개변수가 가리키는 변수에 정보를 저장하지 않으며, 해당 값은 알 수 없습니다.
				*/
				// 여기서는 completion key가 유효하지 않으므로 역참조하지 않도록 주의.
				switch (ec)
				{
				case ERROR_ABANDONED_WAIT_0:
					fileLogger.Write(L"%ls The completion port is closed. Exit the worker thread.\n", LogPrefixString::Info());
					break;
				default:
					fileLogger.Write(L"%ls Exit the worker thread due to an unexpected problem: %lu.\n", LogPrefixString::Error(), ec);
					break;
				}

				exit = true;
				continue;	// exit loop
			}
			else
			{
				/*
				* lpOverlapped가 NULL이 아니고 함수가 완료 포트에서 실패한 I/O 작업에 대한 완료 패킷을 큐에서 제거하면
				* 함수는 실패한 작업에 대한 정보를 lpNumberOfBytes, lpCompletionKey 및 lpOverlapped가 가리키는 변수에 저장합니다.
				* 확장된 오류 정보를 얻으려면 GetLastError를 호출하십시오.
				*/
				// PostSend, PostRecv 생략 -> Session 해제.
				wprintf(L"%ls Failed I/O completion status: %u.\n", LogPrefixString::Error(), ec);
			}
		}

		if (InterlockedDecrement16(&session.m_flag.m_refCount) == 0)
			pServer->ReleaseSession(session);
	}

	return 0;
}
