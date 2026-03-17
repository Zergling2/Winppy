#pragma once

#include <winppy/Platform/Platform.h>
#include <winppy/Core/ReceiveBuffer.h>
#include <winppy/Core/SendQueue.h>
#include <winppy/Network/Packet.h>

namespace winppy
{
	class TCPClientEngine;

	struct TCPClientInitDesc
	{
	public:
		TCPClientInitDesc();
	public:
		TCPClientEngine* m_pEngine;

		/// 클라이언트 수신 버퍼의 크기. 4096(4KB)보다 크거나 같은 2의 승수만 지원됩니다.
		uint32_t m_recvBufSize;

		/// 클라이언트 송신 큐 크기. 단위는 패킷 개수이며 128보다 크거나 같은 2의 승수만 지원됩니다.
		uint32_t m_sendQueueSize;

		/// TCP_NODELAY 옵션 적용 유무.
		bool m_tcpNoDelay;

		/// 운영체제 소켓 송신 버퍼 크기를 0으로 설정할지 선택.
		bool m_zeroByteSendBuf;
	};

	enum class ClientState : short
	{
		Idle,		// 새로운 연결을 수립할 수 있는 상태.
		Connecting,	// 비동기 연결이 진행중인 상태.
		Connected	// 연결이 수립된 상태.
	};

	class TCPClient
	{
		friend class TCPClientEngine;
	public:
		TCPClient();
		~TCPClient();

		int Init(const TCPClientInitDesc& desc);

		/**
		* @brief 지정된 원격 호스트에 연결을 시도합니다. 이 함수는 비동기 연결을 시도하므로 함수의 반환 시점에 연결 결과를 보장할 수 없습니다.
		* 연결에 성공하는 경우에는 OnConnect 함수가 호출됩니다.
		* 
		* @param ip 연결할 대상의 IP 주소. 표준 점선-소수 표기법으로 IPv4 주소의 텍스트 표현을 가리켜야 합니다. (예시: L"192.168.0.55")
		* @param port 연결할 포트 번호.
		* @return 연결 요청에 성공하면 true, 이미 다른 연결을 시도중인 경우 false를 반환합니다. 반환값 true가 연결 성공을 의미하는 것은 아닙니다.
		*/
		bool Connect(const wchar_t* ip, uint16_t port);	// 비동기 connect. 연결되었을 시 released 플래그를 0으로 초기화하는 등, 마치 Session의 Start 함수 작업과 같은 일들을 해주어야 한다.

		/**
		* @brief 현재 연결을 종료합니다.
		*/
		void Disconnect();

		/**
		* @brief 현재 연결 상태를 조회합니다.
		* 
		* @return 현재 연결 상태에 대한 열거형 타입입니다. 자세한 사항은 ClientState 열거형을 참조하세요.
		*/
		ClientState GetState() const { return m_state; }

		/**
		* @brief 원격 호스트에 패킷을 송신합니다. 이 함수로 전달한 패킷은 더 이상 수정해서는 안됩니다.
		* 
		* @param packet 송신할 패킷.
		*/
		void Send(Packet packet);

		/**
		* @brief 연결이 성공한 경우 호출됩니다.
		*/
		virtual void OnConnect() = 0;

		/**
		* @brief 패킷을 수신한 경우 호출됩니다.
		*/
		virtual void OnReceive(Packet packet) = 0;

		/**
		* @brief 연결이 끊어진 경우 호출됩니다. 이 함수 내에서 즉시 재연결을 호출하는 경우 실패합니다.
		*/
		virtual void OnDisconnect() = 0;
	private:
		union
		{
			struct
			{
				short m_refCount;	// X. 반드시 동일 캐시 라인에 위치시켜야 함. (0xabcd)
				volatile short m_released;	// Y. 반드시 동일 캐시 라인에 위치시켜야 함. (0x1234)
			};
			long m_releasedAndRefCount;	// 메모리 레이아웃: (0x1234 abcd)
		}m_flag;	// 인터락 동기화 단위가 하드웨어 레벨에서 최소한 캐시 라인 단위로 되는 플랫폼에서만 가능한 방법. (대표적으로 x86, AMD64 아키텍쳐...)
		char m_cancelIo;
		char m_isSending;
		ClientState m_state;
		uint16_t m_numOfPacketsPending;	// I/O pending packet count (이 변수에 대한 접근은 isSending 플래그 인터락 매커니즘으로 단일 스레드만 변경을 보장해야 함.)
		
		bool m_init;
		bool m_tcpNoDelay;
		bool m_zeroByteSendBuf;
		TCPClientEngine* m_pEngine;
		uint32_t m_recvBufSize;
		uint32_t m_sendQueueSize;

		SOCKET m_sock;
		WSAOVERLAPPED m_connOverlapped;

		WSAOVERLAPPED m_recvOverlapped;
		ReceiveBuffer m_recvBuf;

		WSAOVERLAPPED m_sendOverlapped;
		SRWLOCK m_sendQueueLock;
		SendQueue m_sendQueue;
	};
}
