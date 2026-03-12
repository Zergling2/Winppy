#pragma once

#include <winppy/Platform/Platform.h>
#include <winppy/Platform/CPU.h>
#include <winppy/Core/ReceiveBuffer.h>
#include <winppy/Core/SendQueue.h>
#include <cstdint>
#include <cstddef>

namespace winppy
{
	struct TCPSessionInitDesc
	{
	public:
		TCPSessionInitDesc();
	public:
		uint8_t* m_pRecvBufAddr;
		size_t m_recvBufSize;
		SerializeBuffer** m_pSendQueueAddr;
		size_t m_sendQueueSize;
	};

	struct TCPSessionStartDesc
	{
	public:
		uint64_t m_id;
		SOCKET m_sock;
	};

	class alignas(Cache::L1LineSize()) TCPSession
	{
	public:
		TCPSession();
		~TCPSession() = default;

		void Init(const TCPSessionInitDesc& desc);
		void Start(const TCPSessionStartDesc& desc);

		uint64_t GetId() const { return m_id; }
		SOCKET GetSocket() const { return m_sock; }
		LPWSAOVERLAPPED GetRecvOverlapped() { return &m_recvOverlapped; }
		ReceiveBuffer& GetReceiveBuffer() { return m_recvBuf; }
		LPWSAOVERLAPPED GetSendOverlapped() { return &m_sendOverlapped; }
		SRWLOCK* GetSendQueueLock() { return &m_sendQueueLock; }
		SendQueue& GetSendQueue() { return m_sendQueue; }
	public:
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
		uint16_t m_numOfPacketsPending;	// I/O pending packet count (이 변수에 대한 접근은 isSending 플래그 인터락 매커니즘으로 단일 스레드만 변경을 보장해야 함.)
	private:
		uint64_t m_id;
		SOCKET m_sock;

		WSAOVERLAPPED m_recvOverlapped;
		ReceiveBuffer m_recvBuf;

		WSAOVERLAPPED m_sendOverlapped;
		SRWLOCK m_sendQueueLock;
		SendQueue m_sendQueue;
	};
}
