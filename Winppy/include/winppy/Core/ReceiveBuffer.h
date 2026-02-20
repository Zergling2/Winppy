#pragma once

#include <cstdint>
#include <cstddef>

namespace winppy
{
	struct ReceiveBufferArea
	{
		void* m_pContiguous;
		size_t m_contiguousSize;
		void* m_pWrap;
		size_t m_wrapSize;
	};

	// 네트워크 코어 전용 수신 링버퍼 클래스
	// SPSC MT Safe
	class ReceiveBuffer
	{
	public:
		ReceiveBuffer() noexcept;
		~ReceiveBuffer();

		bool IsValid() const { return m_pBufferBegin != nullptr; }
		void BindMem(uint8_t* pMem, size_t size) noexcept;
		uint8_t* UnbindMem() noexcept;
		void Clear() noexcept;

		size_t Capacity() const { return m_bufSize - 1; }
		size_t Size() const noexcept;	// 사용중 크기 반환
		bool Empty() const { return m_pWriteCursor == m_pReadCursor; }
		// 실제 Readable 가능한 양에 대한 검증 생략. 반드시 Size 함수 호출 뒤 호출해야 함.
		void Peek(void* pBuf, size_t size) const;
		void GetReadableArea(ReceiveBufferArea& out) const noexcept;
		void GetWritableArea(ReceiveBufferArea& out) const noexcept;
		void AdvanceWriteCursor(size_t offset) { m_pWriteCursor = ComputeAdvancedCursorPos(m_pWriteCursor, offset); }
		void AdvanceReadCursor(size_t offset) { m_pReadCursor = ComputeAdvancedCursorPos(m_pReadCursor, offset); }
	private:
		uint8_t* ComputeAdvancedCursorPos(uint8_t* pCursor, size_t offset) const noexcept;
	private:
		uint8_t* m_pBufferBegin;
		uint8_t* m_pBufferEnd;
		size_t m_bufSize;	// 할당된 버퍼의 바이트 크기
		uint8_t* m_pReadCursor;
		uint8_t* m_pWriteCursor;
	};
}
