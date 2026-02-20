#pragma once

#include <cstddef>

namespace winppy
{
	class SerializeBuffer;

	// SerializeBuffer pointer circular queue
	// MT Unsafe
	class SendQueue
	{
	public:
		SendQueue() noexcept;
		~SendQueue();

		bool IsValid() const { return m_pBufferBegin != nullptr; }
		void BindMem(SerializeBuffer** pMem, size_t size) noexcept;
		SerializeBuffer** UnbindMem() noexcept;

		size_t Capacity() const { return m_bufSize - 1; }
		size_t Size() const noexcept;
		size_t FreeSize() const { return Capacity() - Size(); }
		bool Full() const noexcept;
		bool Empty() const { return m_pWriteCursor == m_pReadCursor; }
		bool Push(SerializeBuffer* pSerBuf) noexcept;
		void Peek(SerializeBuffer** pBuf, size_t size) noexcept;	// 반드시 Size 확인 후 호출해야 함.
		[[nodiscard]]SerializeBuffer* Pop() noexcept;
	private:
		void AdvanceWriteCursor(size_t offset) { m_pWriteCursor = ComputeAdvancedCursorPos(m_pWriteCursor, offset); }
		void AdvanceReadCursor(size_t offset) { m_pReadCursor = ComputeAdvancedCursorPos(m_pReadCursor, offset); }
		SerializeBuffer** ComputeAdvancedCursorPos(SerializeBuffer** pCursor, size_t offset) const noexcept;
	private:
		SerializeBuffer** m_pBufferBegin;
		SerializeBuffer** m_pBufferEnd;
		size_t m_bufSize;	// 할당된 버퍼의 SerializeBuffer* 단위 크기
		SerializeBuffer** m_pReadCursor;
		SerializeBuffer** m_pWriteCursor;
	};
}
