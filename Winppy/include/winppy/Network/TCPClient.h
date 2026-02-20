#pragma once

#include <memory>
#include <winppy/Platform/Platform.h>
#include <winppy/Core/ReceiveBuffer.h>
#include <winppy/Core/SendQueue.h>

namespace winppy
{
	class ITCPClientEventHandler;

	struct TCPClientInitDesc
	{
	public:
		TCPClientInitDesc();
	public:
		/// 네트워크 이벤트 핸들러 객체를 설정합니다.
		std::shared_ptr<ITCPClientEventHandler> m_eventHandler;

		/// 클라이언트 수신 버퍼의 크기. 4096(4KB)보다 크거나 같은 2의 승수만 지원됩니다.
		uint32_t m_recvBufSize;

		/// 클라이언트 송신 큐 크기. 단위는 패킷 개수이며 128보다 크거나 같은 2의 승수만 지원됩니다.
		uint32_t m_sendQueueSize;

		/// TCP_NODELAY 옵션 적용 유무.
		bool m_tcpNoDelay;

		/// 운영체제 소켓 송신 버퍼 크기를 0으로 설정할지 선택.
		bool m_zeroByteSendBuf;
	};

	class TCPClient
	{
		friend class TCPClientEngine;
	private:
		TCPClient();
		~TCPClient();

		int Init(const TCPClientInitDesc& desc);
		void Release();

		void Connect(const wchar_t* ip, uint16_t port);
		void Disconnect();

		bool IsConnected() const { return m_connected; }
		// SOCKET GetSocket() const { return m_sock; }
		// LPWSAOVERLAPPED GetRecvOverlapped() { return &m_recvOverlapped; }
		// ReceiveBuffer& GetReceiveBuffer() { return m_recvBuf; }
		// LPWSAOVERLAPPED GetSendOverlapped() { return &m_sendOverlapped; }
		// SRWLOCK* GetSendQueueLock() { return &m_sendQueueLock; }
		// SendQueue& GetSendQueue() { return m_sendQueue; }
	private:
		bool m_init;
		char m_connected;
		bool m_tcpNoDelay;
		bool m_zeroByteSendBuf;
		std::shared_ptr<ITCPClientEventHandler> m_eventHandler;
		uint32_t m_recvBufSize;
		uint32_t m_sendQueueSize;

		SOCKET m_sock;

		WSAOVERLAPPED m_recvOverlapped;
		ReceiveBuffer m_recvBuf;

		WSAOVERLAPPED m_sendOverlapped;
		SRWLOCK m_sendQueueLock;
		SendQueue m_sendQueue;
	};
}
