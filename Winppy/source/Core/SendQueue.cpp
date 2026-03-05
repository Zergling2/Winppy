#include <winppy/Core/SendQueue.h>
#include <winppy/Core/SerializeBuffer.h>
#include <winppy/Common/Math.h>
#include <cassert>

using namespace winppy;

SendQueue::SendQueue() noexcept
	: m_pBufferBegin(nullptr)
	, m_pBufferEnd(nullptr)
	, m_bufSize(0)
	, m_pReadCursor(nullptr)
	, m_pWriteCursor(nullptr)
{
}

SendQueue::~SendQueue()
{
	// this->UnbindMem();
}

void SendQueue::BindMem(SerializeBuffer** pMem, size_t size) noexcept
{
	// size: SerializeBuffer* 단위 크기 (개수)
	assert(Math::IsPowerOf2(size));
	
	m_pBufferBegin = pMem;
	m_pBufferEnd = pMem + size;
	m_bufSize = size;

	m_pReadCursor = m_pBufferBegin;
	m_pWriteCursor = m_pBufferBegin;
}

SerializeBuffer** SendQueue::UnbindMem() noexcept
{
	SerializeBuffer** pBuf = m_pBufferBegin;

	m_pBufferBegin = nullptr;
	m_pBufferEnd = nullptr;
	m_bufSize = 0;

	m_pReadCursor = nullptr;
	m_pWriteCursor = nullptr;

	return pBuf;
}

size_t SendQueue::Size() const noexcept
{
	SerializeBuffer** const pReadCursor = m_pReadCursor;
	SerializeBuffer** pVirtualWriteCursor = m_pWriteCursor;

	if (pVirtualWriteCursor < pReadCursor)
		pVirtualWriteCursor += m_bufSize;

	return pVirtualWriteCursor - pReadCursor;
}

bool SendQueue::Full() const noexcept
{
	return this->ComputeAdvancedCursorPos(m_pWriteCursor, 1) == m_pReadCursor;
}

bool SendQueue::Push(SerializeBuffer* pSerBuf) noexcept
{
	if (this->Full())
		return false;

	*m_pWriteCursor = pSerBuf;
	this->AdvanceWriteCursor(1);

	return true;
}

void SendQueue::Peek(SerializeBuffer** pBuf, size_t size) noexcept
{
	// 이 함수는 반드시 Size 확인 후 호출해야 함.

	SerializeBuffer** pReadCursor = m_pReadCursor;

	for (size_t i = 0; i < size; ++i)
	{
		pBuf[i] = *pReadCursor;
		pReadCursor = ComputeAdvancedCursorPos(pReadCursor, 1);
	}
}

SerializeBuffer* SendQueue::Pop() noexcept
{
	assert(!this->Empty());

	SerializeBuffer* pSerBuf = *m_pReadCursor;
	*m_pReadCursor = nullptr;
	m_pReadCursor = this->ComputeAdvancedCursorPos(m_pReadCursor, 1);

	return pSerBuf;
}

SerializeBuffer** SendQueue::ComputeAdvancedCursorPos(SerializeBuffer** pCursor, size_t offset) const noexcept
{
	assert(pCursor < m_pBufferEnd);

	SerializeBuffer** pNewCursorPos = pCursor + offset;
	const ptrdiff_t over = pNewCursorPos - m_pBufferEnd;

	if (over >= 0)
		pNewCursorPos = m_pBufferBegin + over;

	assert(m_pBufferBegin <= pNewCursorPos && pNewCursorPos < m_pBufferEnd);

	return pNewCursorPos;
}
