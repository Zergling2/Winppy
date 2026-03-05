#include <winppy/Network/TCPClientEngine.h>
#include <winppy/Network/TCPClient.h>
#include <winppy/Network/Packet.h>
#include <winppy/Core/Debug.h>
#include <winppy/Core/WinHelper.h>
#include <winppy/Core/LogPrefix.h>
#include <winppy/Core/SRWLock.h>
#include <winppy/Core/SerializeBuffer.h>
#include <winppy/Core/SerializeBufferBatchPool.h>
#include <winppy/Common/GlobalConstant.h>
#include <cassert>
#include <MSWSock.h>

using namespace winppy;

static inline BOOL PostClientReleaseJob(HANDLE hCompletionPort, TCPClient& client)
{
	// 문서에 따르면 GetQueuedCompletionStatus 함수가 TRUE를 반환하면서 lpOverlapped가 NULL인 경우는 없으므로
	// 이 경우를 사용자 정의 이벤트 식별 용도로 사용할 수 있음.
	return PostQueuedCompletionStatus(hCompletionPort, 0, reinterpret_cast<ULONG_PTR>(&client), nullptr);
}

TCPClientEngineConfig::TCPClientEngineConfig()
	: m_logFileName(nullptr)
	, m_numOfWorkerThreads(0)
	, m_numOfConcurrentThreads(0)
	, m_headerCode(DEFAULT_HEADER_CODE)
{
}

TCPClientEngine::TCPClientEngine()
	: m_init(false)
	, m_si()
	, m_fileLogger()
	, m_headerCode(0)
	, m_hIoCompletionPort(NULL)
	, m_workerThreads()
	, m_numOfWorkerThreads(0)
	, m_numOfConcurrentThreads(0)
{
}

TCPClientEngine::~TCPClientEngine()
{
	this->Release();
}

int TCPClientEngine::Init(const TCPClientEngineConfig& desc)
{
	wchar_t logMsgBuf[128];

	do
	{
		if (m_init)
			break;

		GetSystemInfo(&m_si);

		// ########################################################################################################
		// 파일 로거 생성
		const wchar_t* logFileName = desc.m_logFileName;
		wchar_t logFileNameBuf[64];
		if (!logFileName)
		{
			SYSTEMTIME st;
			GetLocalTime(&st);
			swprintf_s(logFileNameBuf, L"%04d%02d%02d_%02d%02d%02d_TCPClientEngine.log", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
			logFileName = logFileNameBuf;
		}

		if (!m_fileLogger.Open(logFileName))
			break;	// escape do while(false)
		// ########################################################################################################

		// ########################################################################################################
		// 초기화 변수 검사 및 조정
		m_headerCode = desc.m_headerCode;
		m_numOfWorkerThreads = desc.m_numOfWorkerThreads == 0 ? static_cast<uint32_t>(m_si.dwNumberOfProcessors * 1.5) : desc.m_numOfWorkerThreads;
		m_numOfConcurrentThreads = desc.m_numOfConcurrentThreads == 0 ? m_si.dwNumberOfProcessors : desc.m_numOfConcurrentThreads;

		// 조정된 설정값 로그 출력
		m_fileLogger.Write(L"%ls Number of worker threads: %u\n", LogPrefixString::Info(), m_numOfWorkerThreads);
		m_fileLogger.Write(L"%ls Number of concurrent threads: %u\n", LogPrefixString::Info(), m_numOfConcurrentThreads);
		// ########################################################################################################

		// ########################################################################################################
		// 직렬화 버퍼 풀 초기화
		SerializeBufferBatchPool::GetInstance().Init(m_headerCode);
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
		// Worker Thread 생성
		// Context 설정
		m_workerThreadContext.m_pEngine = this;
		m_workerThreadContext.m_pFileLogger = &m_fileLogger;
		m_workerThreadContext.m_hIoCompletionPort = m_hIoCompletionPort;
		// 스레드 생성
		bool workerThreadCreationSuccess = true;
		m_workerThreads.reserve(m_numOfWorkerThreads);
		for (uint32_t i = 0; i < m_numOfWorkerThreads; ++i)
		{
			HANDLE hWorkerThread = LogBeginThreadEx(
				m_fileLogger,
				nullptr,
				0,
				TCPClientEngine::WorkerThreadEntry,
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
		// ########################################################################################################

		m_init = true;
	} while (false);

	if (!m_init)
	{
		this->Release();
		return -1;
	}
	else
	{
		return 0;
	}
}

void TCPClientEngine::Release()
{
	// wchar_t logMsgBuf[128];

	// I/O Completion Port 해제 +> 모든 Worker Thread 종료
	if (m_hIoCompletionPort)
	{
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
	m_workerThreadContext.m_hIoCompletionPort = NULL;


	m_fileLogger.Write(L"%ls TCPClientEngine has been shutdown.\n", LogPrefixString::Info());

	// 파일 로거 리소스 해제
	m_fileLogger.Close();

	m_init = false;
}

void TCPClientEngine::DirectDisconnect(TCPClient& client)
{
	assert(client.m_flag.m_refCount > 0);
	// Disconnect(uint64_t) 함수의 Interlock 오버헤드를 생략시킨 함수

	assert(client.m_flag.m_released == 0);

	InterlockedExchange8(&client.m_cancelIo, 1);
	if (CancelIoEx(reinterpret_cast<HANDLE>(client.m_sock), nullptr) == FALSE)
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

void TCPClientEngine::ReleaseClient(TCPClient& client)
{
	// 실패 시 다른 스레드의 세션 참조로 인한 RefCount 증가
	// refCount, released 플래그 모두 0이었던 경우에만 통과
	if (InterlockedCompareExchange(&client.m_flag.m_releasedAndRefCount, 0x00010000, 0x00000000) != 0x00000000)
		return;
	// 다른 스레드에 의해서 refCount가 증가된 경우 절대 이 아래로 수행되지 않는다.
	// 따라서 다른 스레드는 안전하게 세션에 대한 동작을 수행할 수 있다.


	// 아래 코드 수행 시점부터는 다른 스레드에서 세션의 released 플래그가 켜진것을 확인할 수 있으므로 세션에 대한 추가 작업 없이 리턴하는 식으로 동기화 가능.



	// OnDisconnect 호출 및 추가 정리 과정을 다른 스레드로 우회시켜 컨텐츠 코드에서의 재귀 락으로 인한 데드락 가능성을 방지.
	if (PostClientReleaseJob(m_hIoCompletionPort, client) == FALSE)
	{
		DWORD ec = GetLastError();
		m_fileLogger.Write(L"%ls PostQueuedCompletionStatus failed with error: %lu.\n", LogPrefixString::Error(), ec);
	}
}

void TCPClientEngine::DoClientReleaseJob(TCPClient& client)
{
	// 이 함수는 반드시 재진입 가능 함수여야 한다.

	// 수신 버퍼 클리어
	ReceiveBuffer& rb = client.m_recvBuf;
	rb.Clear();

	// 송신 큐의 잔여 패킷 제거
	SendQueue& sq = client.m_sendQueue;
	AcquireSRWLockExclusive(&client.m_sendQueueLock);
	while (!sq.Empty())
	{
		SerializeBuffer* pSerBuf = sq.Pop();
		pSerBuf->Release();
	}
	ReleaseSRWLockExclusive(&client.m_sendQueueLock);

	// 소켓 닫기
	closesocket(client.m_sock);

	// release 플래그 꺼서 재연결 허용
	InterlockedExchange16(&client.m_flag.m_released, 0);
}

void TCPClientEngine::OnReceiveData(TCPClient& client, size_t numOfBytesTransferred)
{
	// 이 함수는 반드시 재진입 가능 함수여야 한다.

	ReceiveBuffer& rb = client.m_recvBuf;
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
			m_fileLogger.Write(L"%ls Packet marshaling failed. Invalid header code: 0x%08x.\n", LogPrefixString::Info(), header.m_code);
			DirectDisconnect(client);
			break;
		}

		if (header.m_size > SerializeBuffer::Capacity())		// 비정상 패킷
		{
			m_fileLogger.Write(L"%ls Packet marshaling failed. Invalid payload size: %uBytes.\n", LogPrefixString::Info(), header.m_size);
			DirectDisconnect(client);
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

		client.OnReceive(std::move(packet));
	}
}

void TCPClientEngine::PostRecv(TCPClient& client)
{
	// 이 함수는 반드시 재진입 가능 함수여야 한다.

	DWORD flags = 0;
	WSABUF wsaBufs[2];
	DWORD bufferCount = 0;

	ReceiveBufferArea wa;
	client.m_recvBuf.GetWritableArea(wa);

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
	ZeroMemory(&client.m_recvOverlapped, sizeof(WSAOVERLAPPED));		// Recv overlapped 구조체 초기화
	InterlockedIncrement16(&client.m_flag.m_refCount);		// 입출력 참조 카운트 증가
	int ioResult = WSARecv(client.m_sock, wsaBufs, bufferCount, nullptr, &flags, &client.m_recvOverlapped, nullptr);
	if (ioResult == SOCKET_ERROR)
	{
		int ec = WSAGetLastError();
		switch (ec)
		{
		case WSA_IO_PENDING:	// WSARecv가 비동기로 수행됨.
			if (client.m_cancelIo)		// Disconnect 요청된 세션인 경우 전송 취소
			{
				if (CancelIo(reinterpret_cast<HANDLE>(client.m_sock)) == FALSE)
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
			m_fileLogger.Write(L"%ls WSARecv failed with error: %d. Terminate the connection.\n", LogPrefixString::Fail(), ec);
			this->DirectDisconnect(client);
			if (InterlockedDecrement16(&client.m_flag.m_refCount) == 0)	// (완료통지 오지 않으므로 참조 카운트 여기서 차감.)
				this->ReleaseClient(client);
			break;
		}
	}
}

void TCPClientEngine::PostSend(TCPClient& client)
{
	// 이 함수는 반드시 재진입 가능 함수여야 한다.

	WSABUF wsaBufs[WSABUF_LEN_MAX];
	SerializeBuffer* pSerBufs[WSABUF_LEN_MAX];

	SendQueue& sq = client.m_sendQueue;
	AcquireSRWLockExclusive(&client.m_sendQueueLock);
	const size_t numOfPacketsToSend = (std::min)(sq.Size(), WSABUF_LEN_MAX);
	sq.Peek(pSerBufs, numOfPacketsToSend);
	ReleaseSRWLockExclusive(&client.m_sendQueueLock);

	assert(numOfPacketsToSend < (std::numeric_limits<uint16_t>::max)());

	// SendQueue에 패킷을 Push하고 SendFlag를 켜는 과정이 원자적이지 않기 때문에 그 사이에 다른 스레드가 PostSend를 하여 SendQueue가 비어있을 수 있다.
	if (numOfPacketsToSend == 0)
	{
		CHAR ret = InterlockedExchange8(&client.m_isSending, 0);
		if (ret != 1)	// 심각한 결함
			Debug::ForceCrash();

		return;
	}

	for (size_t i = 0; i < numOfPacketsToSend; ++i)
	{
		wsaBufs[i].buf = static_cast<CHAR*>(const_cast<void*>(pSerBufs[i]->Message()));
		wsaBufs[i].len = static_cast<ULONG>(pSerBufs[i]->SizeIncludingHeader());
	}
	client.m_numOfPacketsPending = static_cast<uint16_t>(numOfPacketsToSend);		// 이 변수에 대한 접근은 isSending 플래그 인터락 매커니즘으로 단일 스레드만 변경을 보장해야 함.

	// 동기, 비동기 모두 완료 포트로 완료통지가 온다.
	ZeroMemory(&client.m_sendOverlapped, sizeof(WSAOVERLAPPED));
	InterlockedIncrement16(&client.m_flag.m_refCount);
	int ioResult = WSASend(client.m_sock, wsaBufs, static_cast<DWORD>(numOfPacketsToSend), nullptr, 0, &client.m_sendOverlapped, nullptr);
	if (ioResult == SOCKET_ERROR)
	{
		int ec = WSAGetLastError();
		switch (ec)
		{
		case WSA_IO_PENDING:	// WSASend가 비동기로 수행됨.
			if (client.m_cancelIo)		// Disconnect 요청된 세션인 경우 전송 취소
			{
				if (CancelIo(reinterpret_cast<HANDLE>(client.m_sock)) == FALSE)
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
			m_fileLogger.Write(L"%ls WSASend failed with error: %d. Terminate the connection.\n", LogPrefixString::Fail(), ec);
			this->DirectDisconnect(client);
			if (InterlockedDecrement16(&client.m_flag.m_refCount) == 0)	// (완료통지 오지 않으므로 참조 카운트 여기서 차감.)
				this->ReleaseClient(client);
			break;
		}
	}
}

unsigned int __stdcall TCPClientEngine::WorkerThreadEntry(void* pArg)
{
	const WorkerThreadContext& context = *reinterpret_cast<WorkerThreadContext*>(pArg);
	TCPClientEngine* pEngine = context.m_pEngine;
	const HANDLE hIoCompletionPort = context.m_hIoCompletionPort;
	FileLogger& fileLogger = *context.m_pFileLogger;

	bool exit = false;
	while (!exit)
	{
		OVERLAPPED_ENTRY overlappedEntry;
		BOOL ret = GetQueuedCompletionStatus(
			hIoCompletionPort,
			&overlappedEntry.dwNumberOfBytesTransferred,
			&overlappedEntry.lpCompletionKey,
			&overlappedEntry.lpOverlapped,
			INFINITE
		);

		TCPClient& client = *reinterpret_cast<TCPClient*>(overlappedEntry.lpCompletionKey);
		if (ret)
		{
			if (overlappedEntry.lpOverlapped == &client.m_recvOverlapped)		// Recv 완료 처리
			{
				pEngine->OnReceiveData(client, overlappedEntry.dwNumberOfBytesTransferred);

				if (client.m_cancelIo == 0 && overlappedEntry.dwNumberOfBytesTransferred != 0)
					pEngine->PostRecv(client);
			}
			else if (overlappedEntry.lpOverlapped == &client.m_sendOverlapped)	// Send 완료 처리
			{
				const size_t transmitted = client.m_numOfPacketsPending;
				SendQueue& sq = client.m_sendQueue;
				AcquireSRWLockExclusive(&client.m_sendQueueLock);
				assert(sq.Size() >= transmitted);
				for (size_t i = 0; i < transmitted; ++i)
				{
					SerializeBuffer* pSerBuf = sq.Pop();
					pSerBuf->Release();
				}
				ReleaseSRWLockExclusive(&client.m_sendQueueLock);


				// 이 멤버 변수에 대한 접근은 isSending 플래그 인터락 매커니즘으로 단일 스레드만 변경을 보장해야 함.
				// isSending 플래그를 먼저 끄고 이 변수를 변경하면 isSending 플래그가 꺼진 순간 다른 스레드의 PostSend 및 Send 완료통지 루틴이 실행되어
				// 데이터 경쟁이 발생하게 된다.
				client.m_numOfPacketsPending = 0;

				InterlockedExchange8(&client.m_isSending, 0);

				// AcquireSRWLockExclusive(client.GetSendQueueLock());
				const size_t sqSize = sq.Size();
				// ReleaseSRWLockExclusive(client.GetSendQueueLock());
				if (client.m_cancelIo == 0 && sqSize > 0)
					if (InterlockedExchange8(&client.m_isSending, 1) == 0)
						pEngine->PostSend(client);
			}
			else if (overlappedEntry.lpOverlapped == &client.m_connOverlapped)
			{
				setsockopt(client.m_sock, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
				InterlockedExchange8(&client.m_connecting, 0);
				InterlockedExchange16(&client.m_flag.m_released, 0);

				InterlockedIncrement16(&client.m_flag.m_refCount);

				client.OnConnect();

				if (client.m_cancelIo == 0)
					pEngine->PostRecv(client);

				if (InterlockedDecrement16(&client.m_flag.m_refCount) == 0)
					pEngine->ReleaseClient(client);

				continue;
			}
			else
			{
				// 문서에 따르면 GetQueuedCompletionStatus 함수가 TRUE를 반환하면서 lpOverlapped가 NULL인 경우는 없으므로
				// 이 경우를 사용자 정의 이벤트 식별 용도로 사용할 수 있음.	-> PostClientReleaseJob
				// assert(client.m_flag.m_refCount == 0 && client.m_flag.m_released == 1);
				assert(client.m_flag.m_releasedAndRefCount == 0x00010000);

				client.OnDisconnect();	// 재연결했는데 OnDisconnect 함수가 뒤늦게 호출되는 경우를 예방하려면 DoClientReleaseJob 이전에 이벤트 함수를 호출해주어야 한다.

				pEngine->DoClientReleaseJob(client);
				continue;	// InterlockedDecrement16(&client.m_flag.m_refCount) 실행 루트로 가면 절대 안됨! (이미 0이 되어서 작업이 예약되었으므로.)
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
				// PostSend, PostRecv 생략 -> Client 해제.
				wprintf(L"%ls Failed I/O completion status: %u.\n", LogPrefixString::Error(), ec);

				if (overlappedEntry.lpOverlapped == &client.m_connOverlapped)
				{
					InterlockedExchange8(&client.m_connecting, 0);
					continue;
				}
			}
		}

		if (InterlockedDecrement16(&client.m_flag.m_refCount) == 0)
			pEngine->ReleaseClient(client);
	}

	return 0;
}
