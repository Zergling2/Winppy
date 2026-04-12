#pragma once

#include <winppy/Platform/Platform.h>
#include <winppy/Platform/CPU.h>
#include <winppy/Common/EndKey.h>
#include <winppy/Core/FileLogger.h>
#include <winppy/Network/Packet.h>
#include <vector>
#include <string>

namespace winppy
{
	class TCPSession;

	/**
	* @brief TCPServer 설정 디스크립터입니다.
	*/
	struct TCPServerConfig
	{
	public:
		TCPServerConfig();
	public:
		/// 로그 파일의 이름. nullptr로 지정하면 자동으로 로컬 시각 기준의 'YYYYMMDD_HHMMSS_TCPServer.log' 파일명이 사용됩니다.
		const wchar_t* m_logFileName;

		/// 바인딩할 IPv4 주소.
		/// nullptr로 지정하면 모든 네트워크 인터페이스에서 오는 연결을 수신(INADDR_ANY)합니다.
		/// 그렇지 않다면 이 변수는 표준 점선-소수 표기법으로 IPv4 주소의 텍스트 표현을 가리켜야 합니다. (예시: "127.0.0.1")
		const wchar_t* m_bindAddr;

		/// 바인딩할 포트 번호. 필수로 지정해야 합니다.
		uint16_t m_bindPort;

		/// 프로토콜 헤더 식별 상수를 지정합니다. 설정하지 않을 경우 기본값이 사용됩니다.
		uint32_t m_headerCode;

		/// 연결을 허용할 최대 세션 수. 1 이상의 값이어야 합니다.
		uint32_t m_maxSessionCount;

		/// 세션당 수신 버퍼의 크기. 4096(4KB)보다 크거나 같은 2의 승수만 지원됩니다.
		/// 2의 승수가 아닐 시 자동으로 가까운 2의 승수로 올림됩니다.
		uint32_t m_sessionRecvBufSize;

		/// 세션당 송신 큐 크기. 단위는 패킷 개수이며 128보다 크거나 같은 2의 승수만 지원됩니다.
		/// 2의 승수가 아닐 시 자동으로 가까운 2의 승수로 올림됩니다.
		uint32_t m_sessionSendQueueSize;

		/// 연결 백로그 사이즈를 지정합니다. 지정하지 않을 시 기본값은 1024입니다.
		uint16_t m_backlogSize;

		/// I/O 완료 포트 작업을 처리하는 작업자 스레드 개수. 0으로 설정할 시 시스템 사양에 맞게 자동으로 결정됩니다.
		uint32_t m_numOfWorkerThreads;

		/// I/O 완료 포트 작업을 동시에 처리할 최대 작업자 스레드 수. 0으로 지정할 시 시스템 사양에 맞게 자동으로 결정됩니다.
		uint32_t m_numOfConcurrentThreads;

		/// TCP_NODELAY 옵션 적용 유무.
		bool m_tcpNoDelay;

		/// 운영체제 소켓 송신 버퍼 크기를 0으로 설정할지 선택.
		bool m_zeroByteSendBuf;

		/// 종료 키. 설정하지 않을 시 기본값은 Backspace입니다. 가능한 키는 EndKey enum을 참고하십시오. (대소문자 구별됨.)
		EndKey m_endKey;
	};

	/**
	* @brief TCP/IP기반 서버입니다.
	* 
	* 서버 인스턴스는 반드시 프로세스 내에서 단 한 개만 생성되고 실행되어야 합니다. 서버 인스턴스를 Shutdown 후 다시 재가동해서는 안됩니다.
	* 
	* 서버를 재가동하는 현재 유일한 방법은 프로세스의 재시작 뿐입니다.
	* 
	* (이유는 여러 스레드들이 각자 할당받은 직렬화버퍼를 모두 다시 재수집하는 것이 현실적으로 쉽지 않으므로)
	* (Packet 인스턴스를 하나라도 생성한 스레드는 직렬화 버퍼 배치를 할당받고 내부 직렬화 버퍼는 여러 스레드들간에 이동됨. 워커 스레드들 & 컨텐츠 스레드들 등이 해당될 수 있다.)
	*/
	class TCPServer
	{
	private:
		struct AcceptThreadContext
		{
		public:
			AcceptThreadContext()
				: m_pServer(nullptr)
				, m_pSessionCount(nullptr)
				, m_pFileLogger(nullptr)
				, m_hIoCompletionPort(NULL)
				, m_listenSock(INVALID_SOCKET)
				, m_backlogSize(0)
				, m_tcpNoDelay(true)
				, m_zeroByteSendBuf(false)
				, m_pSessions(nullptr)
				, m_pReadySessionIndicesLock(nullptr)
				, m_pReadySessionIndices(nullptr)
			{
			}
		public:
			TCPServer* m_pServer;
			uint32_t* m_pSessionCount;
			FileLogger* m_pFileLogger;
			HANDLE m_hIoCompletionPort;
			SOCKET m_listenSock;
			uint16_t m_backlogSize;
			bool m_tcpNoDelay;
			bool m_zeroByteSendBuf;
			TCPSession* m_pSessions;
			SRWLOCK* m_pReadySessionIndicesLock;
			std::vector<uint32_t>* m_pReadySessionIndices;
		};
		struct WorkerThreadContext
		{
		public:
			WorkerThreadContext()
				: m_pServer(nullptr)
				, m_pFileLogger(nullptr)
				, m_hIoCompletionPort(NULL)
			{
			}
		public:
			TCPServer* m_pServer;
			FileLogger* m_pFileLogger;
			HANDLE m_hIoCompletionPort;
		};
	public:
		TCPServer();
		~TCPServer();

		/**
		* @brief 서버 인스턴스를 초기화하고 실행합니다.
		* @param desc 설정 디스크립터.
		* @return 성공한 경우 0, 실패한 경우 0이 아닌 값을 반환합니다.
		*/
		int Run(const TCPServerConfig& desc);

		/**
		* @brief 서버를 종료합니다.
		*/
		void Shutdown();

		/**
		* @brief 바인딩된 IP 주소를 반환합니다.
		* @return 현재 바인딩된 IP 주소.
		*/
		const wchar_t* GetBindedAddress() const { return m_bindAddr.c_str(); }

		/**
		* @brief 바인딩된 포트 번호를 반환합니다.
		* @return 현재 바인딩된 포트 번호.
		*/
		uint16_t GetBindedPort() const { return m_bindPort; }

		/**
		* @brief 프로토콜 헤더 식별 상수를 반환합니다.
		* @return 프로토콜 헤더 식별 상수.
		*/
		uint32_t GetHeaderCode() const { return m_headerCode; }

		/**
		* @brief 연결이 허용되는 최대 세션 수를 반환합니다.
		* @return 연결이 허용되는 최대 세션 수.
		*/
		uint32_t GetMaxSessionCount() const { return m_maxSessionCount; }

		/**
		* @brief 세션당 사용중인 수신 버퍼의 크기를 반환합니다.
		* @return 세션당 사용중인 수신 버퍼의 크기.
		*/
		uint32_t GetSessionRecvBufSize() const { return m_sessionRecvBufSize; }

		/**
		* @brief 세션당 사용중인 패킷 송신 큐의 크기를 반환합니다.
		* 세션의 패킷 송신 큐에 패킷이 이 함수로 반환되는 값 이상으로 차는 경우 세션과의 연결이 끊긴 것으로 간주되어 연결이 종료됩니다.
		* @return 세션당 사용중인 송신 큐의 크기.
		*/
		uint32_t GetSessionSendQueueSize() const { return m_sessionSendQueueSize; }

		/**
		* @brief 현재 연결된 세션의 수를 반환합니다.
		* @return 현재 연결된 세션의 수.
		*/
		uint32_t GetSessionCount() const { return m_sessionCount; }

		/**
		* @brief 작업자 스레드 수를 반환합니다.
		* @return 작업자 스레드 수.
		*/
		uint32_t GetNumberOfWorkerThreads() const { return static_cast<uint32_t>(m_workerThreads.size()); }

		/**
		* @brief 완료 포트 최대 동시 작업 스레드 수를 반환합니다.
		* @return 완료 포트 최대 동시 작업 스레드 수.
		*/
		uint32_t GetNumberOfConcurrentThreads() const { return m_numOfConcurrentThreads; }

		/**
		* @brief 새로운 세션과의 연결이 시작될 때 호출됩니다. 세션과의 연결을 거부하려는 경우 오버라이딩 함수에서 false, 그렇지 않을 경우 true를 반환하세요.
		* @param id 세션 식별자.
		* @return 세션 허용 여부.
		*/
		virtual bool OnConnect(const wchar_t* ip, uint16_t port, uint64_t id) = 0;

		/**
		* @brief 세션으로부터 패킷을 수신한 경우 호출됩니다.
		* @param id 세션 식별자.
		* @param packet 수신된 패킷.
		*/
		virtual void OnReceive(uint64_t id, Packet packet) = 0;

		/**
		* @brief 세션과의 연결이 끊어졌을 때 호출됩니다.
		* @param id 세션 식별자.
		*/
		virtual void OnDisconnect(uint64_t id) = 0;

		/**
		* @brief 지정한 식별자를 갖는 세션에게 패킷을 송신합니다. 지정한 id를 갖는 세션이 없거나 유효하지 않은 패킷(예시: std::move된 경우)을 전달하는 경우 함수는 아무것도 수행하지 않습니다.
		*
		* 이 함수로 패킷을 전달한 이후에는 패킷을 수정해서는 안됩니다. 여러 세션에게 동일한 패킷을 전송하는 것은 허용됩니다.
		* 
		* 패킷을 단일 세션에게만 전송하는 경우 std::move를 통해 Packet을 전달하면 성능이 향상됩니다.
		* 
		* 여러 세션에게 동일한 패킷을 보내는 경우에도 마지막 세션에게 전송 시에는 std::move를 사용하면 성능이 향상됩니다.
		* 
		* @param id 패킷을 전송할 세션의 식별자.
		* @param packet 송신할 패킷.
		*/
		void Send(uint64_t id, Packet packet);

		/**
		* @brief 세션과의 연결을 종료합니다.
		* 
		* @param id 연결을 종료할 세션의 식별자.
		*/
		void Disconnect(uint64_t id);

		bool GetIP(uint64_t id, wchar_t* pBuf, size_t len);
		bool GetPort(uint64_t id, uint16_t& port);
		bool GetIPAndPort(uint64_t id, wchar_t* pBuf, size_t len, uint16_t& port);
	private:
		void DirectDisconnect(TCPSession& session);	// 라이브러리 내부 전용
		void DisconnectAllSessions();
		void ReleaseSession(TCPSession& session);
		void DoSessionReleaseJob(TCPSession& session);
		void OnReceiveData(TCPSession& session, size_t numOfBytesTransferred);
		void PostRecv(TCPSession& session);
		void PostSend(TCPSession& session);
		static unsigned int __stdcall AcceptThreadEntry(void* pArg);
		static unsigned int __stdcall WorkerThreadEntry(void* pArg);
	private:
		// vftable
		bool m_init;
		bool m_isInstallableFileSystemLSP;
		SYSTEM_INFO m_si;
		FileLogger m_fileLogger;
		std::wstring m_bindAddr;
		uint16_t m_bindPort;
		uint32_t m_headerCode;
		uint32_t m_maxSessionCount;
		uint32_t m_sessionRecvBufSize;		// 4KB, 8KB, 16KB, 32KB
		uint32_t m_sessionSendQueueSize;	// 64, 128, 256
		bool m_tcpNoDelay;
		bool m_zeroByteSendBuf;
		void* m_pLargeMemRecvBuf;
		void* m_pLargeMemSendQueue;
		TCPSession* m_pSessions;
		HANDLE m_hIoCompletionPort;
		SOCKET m_listenSock;
		HANDLE m_hAcceptThread;
		std::vector<HANDLE> m_workerThreads;
		uint32_t m_numOfWorkerThreads;
		uint32_t m_numOfConcurrentThreads;

		SRWLOCK m_readySessionIndicesLock;
		std::vector<uint32_t> m_readySessionIndices;
		uint32_t m_sessionCount;

		AcceptThreadContext m_acceptThreadContext;
		WorkerThreadContext m_workerThreadContext;
	};
}
