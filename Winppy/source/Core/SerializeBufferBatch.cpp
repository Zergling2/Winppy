#include <winppy/Core/SerializeBufferBatch.h>
#include <cassert>

using namespace winppy;

SerializeBufferBatch::SerializeBufferBatch()
	: m_bufs{}
	, m_size(0)
{
}

void SerializeBufferBatch::Push(SerializeBuffer* pSerBuf)
{
	assert(this->Size() < SerializeBufferBatch::Capacity());

	m_bufs[m_size] = pSerBuf;
	++m_size;
}

SerializeBuffer* SerializeBufferBatch::Pop()
{
	assert(m_size > 0);

	SerializeBuffer* pRet = m_bufs[m_size - 1];
	--m_size;

	return pRet;
}
