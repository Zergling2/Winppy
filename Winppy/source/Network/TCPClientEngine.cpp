#include <winppy/Network/TCPClientEngine.h>
#include <winppy/Network/TCPClient.h>
#include <winppy/Common/GlobalConstant.h>
#include <winppy/Core/Debug.h>
#include <winppy/Core/WinHelper.h>
#include <winppy/Core/LogPrefix.h>
#include <winppy/Core/SRWLock.h>
#include <winppy/Core/SerializeBuffer.h>
#include <winppy/Core/SerializeBufferBatchPool.h>

using namespace winppy;

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
		// 헤더 식별자 코드 설정
		SerializeBufferBatchPool::GetInstance().SetHeaderCode(m_headerCode);
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
