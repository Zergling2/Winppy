#pragma once

#include <winppy/Platform/Platform.h>
#include <winppy/Core/FileLogger.h>
#include <vector>
#include <unordered_set>
#include <memory>

namespace winppy
{
	class TCPClient;

	struct TCPClientEngineConfig
	{
	public:
		TCPClientEngineConfig();
	public:
		/// 로그 파일의 이름. nullptr로 지정하면 자동으로 로컬 시각 기준의 'YYYYMMDD_HHMMSS_TCPClientEngine.log' 파일명이 사용됩니다.
		const wchar_t* m_logFileName;

		/// 프로토콜 헤더 식별 상수를 지정합니다. 설정하지 않을 경우 기본값이 사용됩니다.
		uint32_t m_headerCode;

		/// I/O 완료 포트 작업을 처리하는 작업자 스레드 개수.
		uint32_t m_numOfWorkerThreads;

		/// I/O 완료 포트 작업을 동시에 처리할 최대 작업자 스레드 수.
		uint32_t m_numOfConcurrentThreads;
	};

	class TCPClientEngine
	{
		friend class TCPClient;
	private:
		struct WorkerThreadContext
		{
		public:
			WorkerThreadContext()
				: m_pEngine(nullptr)
				, m_pFileLogger(nullptr)
				, m_hIoCompletionPort(NULL)
			{
			}
		public:
			TCPClientEngine* m_pEngine;
			FileLogger* m_pFileLogger;
			HANDLE m_hIoCompletionPort;
		};
	public:
		TCPClientEngine();
		~TCPClientEngine();

		int Init(const TCPClientEngineConfig& desc);
		void Release();
	private:
		void DirectDisconnect(TCPClient& client);
		void ReleaseClient(TCPClient& client);
		void DoClientReleaseJob(TCPClient& client);
		void OnReceiveData(TCPClient& client, size_t numOfBytesTransferred);
		void PostRecv(TCPClient& client);
		void PostSend(TCPClient& client);
		static unsigned int __stdcall WorkerThreadEntry(void* pArg);
	private:
		bool m_init;
		SYSTEM_INFO m_si;
		FileLogger m_fileLogger;

		uint32_t m_headerCode;
		HANDLE m_hIoCompletionPort;
		std::vector<HANDLE> m_workerThreads;
		uint32_t m_numOfWorkerThreads;
		uint32_t m_numOfConcurrentThreads;

		WorkerThreadContext m_workerThreadContext;
	};
}
