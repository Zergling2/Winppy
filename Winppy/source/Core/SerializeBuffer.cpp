#include <winppy/Core/SerializeBuffer.h>
#include <cassert>
#include <winppy/Common/Alignment.h>
#include <winppy/Common/Math.h>
#include <winppy/Common/GlobalConstant.h>
#include <winppy/Core/TlsSerializeBufferBatch.h>

using namespace winppy;

static_assert(Math::IsPowerOf2(SerializeBuffer::InstanceSize()), "SerializeBuffer size must be power of 2!");
static_assert(std::is_standard_layout<SerializeBuffer>::value, "SerializeBuffer must be standard layout!");	// 반드시 Header, Data, 제어 블록 순서여야 함
static_assert(SerializeBuffer::Capacity() < RECV_BUFFER_SIZE_MIN, "The capacity of the SerializeBuffer must be less than the receive buffer size!");

SerializeBuffer::ControlBlock::ControlBlock(long refCount, const uint8_t* pReadCursor, uint8_t* pWriteCursor)
	: m_refCount(refCount)
	, m_pReadCursor(pReadCursor)
	, m_pWriteCursor(pWriteCursor)
{
}

SerializeBuffer::SerializeBuffer(uint32_t code)
	: m_header(code, 0)
	, m_payload()
	, m_ctrlBlock(0, m_payload, m_payload)
{
	assert(alignof(SerializeBuffer) == 8);
	assert(winppy::IsAligned8(this));
	// ZeroMemory(m_payload, sizeof(m_payload));
}

void SerializeBuffer::CopyFrom(const SerializeBuffer& other)
{
	// 데이터의 논리적 복사. 참조 카운트는 복사 대상이 아님.
	m_header = other.m_header;
	std::memcpy(m_payload, other.m_payload, other.m_header.m_size);
	m_ctrlBlock.m_pReadCursor = m_payload + (other.m_ctrlBlock.m_pReadCursor - other.m_payload);
	m_ctrlBlock.m_pWriteCursor = m_payload + (other.m_ctrlBlock.m_pWriteCursor - other.m_payload);
}

void SerializeBuffer::Clear()
{
	// ZeroMemory(m_payload, sizeof(m_payload));
	m_header.m_size = 0;
	m_ctrlBlock.m_pReadCursor = m_payload;
	m_ctrlBlock.m_pWriteCursor = m_payload;
}

bool SerializeBuffer::WriteBytes(const void* pData, size_t size)
{
	if (this->WriteableSize() < size)
		return false;

	std::memcpy(m_ctrlBlock.m_pWriteCursor, pData, size);
	m_ctrlBlock.m_pWriteCursor = static_cast<uint8_t*>(m_ctrlBlock.m_pWriteCursor) + size;
	m_header.m_size += static_cast<uint32_t>(size);

	return true;
}

bool SerializeBuffer::ReadBytes(void* pOut, size_t size)
{
	if (this->ReadableSize() < size)
		return false;

	std::memcpy(pOut, m_ctrlBlock.m_pReadCursor, size);
	m_ctrlBlock.m_pReadCursor = static_cast<const uint8_t*>(m_ctrlBlock.m_pReadCursor) + size;

	return true;
}

uint32_t SerializeBuffer::Release()
{
	uint32_t refCount = InterlockedDecrement(&m_ctrlBlock.m_refCount);
	if (refCount == 0)
		TlsSerializeBufferBatch::ReturnSerializeBuffer(this);

	return refCount;
}

void SerializeBuffer::Init()
{
	// 버퍼를 논리적으로 초기 상태와 동일하게 만드는 함수
	assert(m_ctrlBlock.m_refCount == 0);	// 풀 내에서 사용자에게 반환되기 직전 호출되어야 하므로 참조 카운트가 0이어야 한다.

	// ZeroMemory(m_data, sizeof(m_data));
	this->Clear();
}
