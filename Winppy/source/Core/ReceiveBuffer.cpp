#include <winppy/Core/ReceiveBuffer.h>
#include <winppy/Common/Math.h>
#include <cassert>

using namespace winppy;

ReceiveBuffer::ReceiveBuffer() noexcept
	: m_pBufferBegin(nullptr)
	, m_pBufferEnd(nullptr)
	, m_bufSize(0)
	, m_pReadCursor(nullptr)
	, m_pWriteCursor(nullptr)
{
}

ReceiveBuffer::~ReceiveBuffer()
{
	// this->UnbindMem();
}

void ReceiveBuffer::BindMem(uint8_t* pMem, size_t size) noexcept
{
	assert(Math::IsPowerOf2(size));

	m_pBufferBegin = pMem;
	m_pBufferEnd = pMem + size;
	m_bufSize = size;

	m_pReadCursor = m_pBufferBegin;
	m_pWriteCursor = m_pBufferBegin;
}

uint8_t* ReceiveBuffer::UnbindMem() noexcept
{
	uint8_t* pBuf = m_pBufferBegin;

	m_pBufferBegin = nullptr;
	m_pBufferEnd = nullptr;
	m_bufSize = 0;

	m_pReadCursor = nullptr;
	m_pWriteCursor = nullptr;

	return pBuf;
}

void ReceiveBuffer::Clear() noexcept
{
	m_pReadCursor = m_pBufferBegin;
	m_pWriteCursor = m_pBufferBegin;
}

size_t ReceiveBuffer::Size() const noexcept
{
	uint8_t* const pReadCursor = m_pReadCursor;
	uint8_t* pVirtualWriteCursor = m_pWriteCursor;

	if (pVirtualWriteCursor < pReadCursor)
		pVirtualWriteCursor += m_bufSize;

	return pVirtualWriteCursor - pReadCursor;
}

void ReceiveBuffer::Peek(void* pBuf, size_t size) const
{
	const uint8_t* const pReadCursor = m_pReadCursor;

	size_t contiguousSize = m_pBufferEnd - m_pReadCursor;
	assert(contiguousSize > 0);

	if (size <= contiguousSize)
	{
		std::memcpy(pBuf, pReadCursor, size);
	}
	else
	{
		std::memcpy(pBuf, pReadCursor, contiguousSize);
		std::memcpy(static_cast<uint8_t*>(pBuf) + contiguousSize, m_pBufferBegin, size - contiguousSize);
	}
}

void ReceiveBuffer::GetReadableArea(ReceiveBufferArea& out) const noexcept
{
	uint8_t* const pReadCursor = m_pReadCursor;
	uint8_t* pVirtualWriteCursor = m_pWriteCursor;

	if (pVirtualWriteCursor == m_pBufferBegin)	// 이렇게 하면 분기 간략화 가능.
		pVirtualWriteCursor = m_pBufferEnd;

	if (pReadCursor <= pVirtualWriteCursor)
	{
		out.m_pContiguous = pReadCursor;
		out.m_contiguousSize = pVirtualWriteCursor - pReadCursor;
		out.m_pWrap = nullptr;
		out.m_wrapSize = 0;
	}
	else // (pReadCursor > pVirtualWriteCursor) 이 조건문에서는 pVirtualWriteCursor가 Begin or End 지점에 있을 수 없음. (끝 지점에 있었으면 반드시 위쪽 분기문을 타게 된다.)
	{
		out.m_pContiguous = pReadCursor;
		out.m_contiguousSize = m_pBufferEnd - pReadCursor;
		out.m_pWrap = m_pBufferBegin;
		out.m_wrapSize = pVirtualWriteCursor - m_pBufferBegin;
	}
}

void ReceiveBuffer::GetWritableArea(ReceiveBufferArea& out) const noexcept
{
	uint8_t* pVirtualReadCursor = m_pReadCursor;
	uint8_t* const pWriteCursor = m_pWriteCursor;

	if (pVirtualReadCursor == m_pBufferBegin)	// 이렇게 하면 분기 간략화 가능.
		pVirtualReadCursor = m_pBufferEnd;

	if (pWriteCursor < pVirtualReadCursor)
	{
		out.m_pContiguous = pWriteCursor;
		out.m_contiguousSize = pVirtualReadCursor - pWriteCursor - 1;
		out.m_pWrap = nullptr;
		out.m_wrapSize = 0;
	}
	else // (pWriteCursor >= pVirtualReadCursor) 이 조건문에서는 pVirtualReadCursor가 Begin or End 지점에 있을 수 없음. (끝 지점에 있었으면 반드시 위쪽 분기문을 타게 된다.)
	{
		out.m_pContiguous = pWriteCursor;
		out.m_contiguousSize = m_pBufferEnd - pWriteCursor;
		out.m_pWrap = m_pBufferBegin;
		out.m_wrapSize = pVirtualReadCursor - m_pBufferBegin - 1;
	}

	assert(out.m_contiguousSize <= Capacity());
	assert(out.m_wrapSize < Capacity());
}

uint8_t* ReceiveBuffer::ComputeAdvancedCursorPos(uint8_t* pCursor, size_t offset) const noexcept
{
	assert(pCursor < m_pBufferEnd);

	uint8_t* pNewCursorPos = pCursor + offset;
	const ptrdiff_t over = pNewCursorPos - m_pBufferEnd;

	if (over >= 0)
		pNewCursorPos = m_pBufferBegin + over;

	assert(m_pBufferBegin <= pNewCursorPos && pNewCursorPos < m_pBufferEnd);

	return pNewCursorPos;
}
