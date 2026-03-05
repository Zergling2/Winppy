#include <winppy/Core/SerializeBufferBatchPool.h>
#include <winppy/Core/SerializeBufferBatch.h>
#include <winppy/Core/SerializeBuffer.h>
#include <winppy/Core/SRWLock.h>
#include <winppy/Core/Debug.h>
#include <winppy/Core/LogPrefix.h>
#include <cassert>

using namespace winppy;

SerializeBufferBatchPool::SerializeBufferBatchPool()
	: m_headerCode(0)
	// , m_si()
	// , m_vmLock()
	// , m_fullBatchLock()
	// , m_emptyBatchLock()
	, m_vm()
	, m_fullBatch()
	, m_emptyBatch()
{
	GetSystemInfo(&m_si);

	InitializeSRWLock(&m_vmLock);
	InitializeSRWLock(&m_fullBatchLock);
	InitializeSRWLock(&m_emptyBatchLock);

	m_vm.reserve(64);
	m_fullBatch.reserve(64);
	m_emptyBatch.reserve(64);
}

SerializeBufferBatchPool::~SerializeBufferBatchPool()
{
	for (void* pLargeMem : m_vm)
	{
		BOOL ret = VirtualFree(pLargeMem, 0, MEM_RELEASE);
		assert(ret != FALSE);
	}
	m_vm.clear();

	for (SerializeBufferBatch* pSerBufBatch : m_fullBatch)
	{
		// Batch 내부의 SerializeBuffer들은 개별 할당된 객체들이 아니라 VirtualAlloc으로 할당된 영역의 분할들이므로 여기서 해제할 수 없다.
		delete pSerBufBatch;	// 배치 자체는 해당 클래스에서 생성하였으므로 해제 가능
	}
	m_fullBatch.clear();

	for (SerializeBufferBatch* pSerBufBatch : m_emptyBatch)
	{
		// Batch 내부의 SerializeBuffer들은 개별 할당된 객체들이 아니라 VirtualAlloc으로 할당된 영역의 분할들이므로 여기서 해제할 수 없다.
		delete pSerBufBatch;	// 배치 자체는 해당 클래스에서 생성하였으므로 해제 가능
	}
	m_emptyBatch.clear();
}

void winppy::SerializeBufferBatchPool::Init(uint32_t code)
{
	m_headerCode = code;

	// const size_t initialCount = static_cast<size_t>(m_si.dwNumberOfProcessors * 1.5);
	const size_t initialCount = m_si.dwNumberOfProcessors;	// Worker threads & Content threads... 평균적인 최소 예상치

	for (size_t i = 0; i < initialCount; ++i)
	{
		SerializeBufferBatch* pNewSerBufBatch = this->CreateFullBatch();
		m_fullBatch.push_back(pNewSerBufBatch);
	}

	for (size_t i = 0; i < initialCount; ++i)
	{
		SerializeBufferBatch* pNewSerBufBatch = new SerializeBufferBatch();
		m_emptyBatch.push_back(pNewSerBufBatch);
	}
}

SerializeBufferBatch* SerializeBufferBatchPool::GetFullSerializeBufferBatch()
{
	SRWLockExclusiveGuard lock(m_fullBatchLock);

	if (m_fullBatch.empty())
	{
		SerializeBufferBatch* pNewSerBufBatch = this->CreateFullBatch();
		m_fullBatch.push_back(pNewSerBufBatch);
	}

	SerializeBufferBatch* pRet = m_fullBatch.back();
	m_fullBatch.pop_back();

	return pRet;
}

SerializeBufferBatch* SerializeBufferBatchPool::GetEmptySerializeBufferBatch()
{
	SRWLockExclusiveGuard lock(m_emptyBatchLock);

	if (m_emptyBatch.empty())
	{
		SerializeBufferBatch* pNewSerBufBatch = new SerializeBufferBatch();
		m_emptyBatch.push_back(pNewSerBufBatch);
	}

	SerializeBufferBatch* pRet = m_emptyBatch.back();
	m_emptyBatch.pop_back();

	return pRet;
}

void SerializeBufferBatchPool::ReturnFullSerializeBufferBatch(SerializeBufferBatch* pSerBufBatch)
{
	SRWLockExclusiveGuard lock(m_fullBatchLock);

	m_fullBatch.push_back(pSerBufBatch);
}

void SerializeBufferBatchPool::ReturnEmptySerializeBufferBatch(SerializeBufferBatch* pSerBufBatch)
{
	SRWLockExclusiveGuard lock(m_emptyBatchLock);

	m_emptyBatch.push_back(pSerBufBatch);
}

size_t SerializeBufferBatchPool::GetFullBatchCount()
{
	SRWLockExclusiveGuard lock(m_fullBatchLock);

	size_t count = m_fullBatch.size();

	return count;
}

size_t SerializeBufferBatchPool::GetEmptyBatchCount()
{
	SRWLockExclusiveGuard lock(m_emptyBatchLock);

	size_t count = m_emptyBatch.size();

	return count;
}

SerializeBufferBatch* SerializeBufferBatchPool::CreateFullBatch()
{
	// 빈 배치 생성
	SerializeBufferBatch* pNewSerBufBatch = new SerializeBufferBatch();

	// 빈 배치를 풀로 채운다.
	// SerializeBuffer 인스턴스 크기 * SerializeBufferBatch 묶음 수 바이트 할당 후 Placement new로 SerializeBuffer 생성 및 포인터 저장

	// size: 할당하는 직렬화 버퍼 개수

	constexpr size_t allocSize = SerializeBuffer::InstanceSize() * SerializeBufferBatch::Capacity();
	assert(allocSize >= m_si.dwAllocationGranularity);

	void* const pLargeMemSerBufs = VirtualAlloc(nullptr, allocSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!pLargeMemSerBufs)
	{
		DWORD ec = GetLastError();
		Debug::ForceCrash();
	}

	{
		SRWLockExclusiveGuard lock(m_vmLock);
		m_vm.push_back(pLargeMemSerBufs);
	}

	// LargeMem 분배
	SerializeBuffer* pBaseAddr = static_cast<SerializeBuffer*>(pLargeMemSerBufs);
	for (size_t i = 0; i < SerializeBufferBatch::Capacity(); ++i)
	{
		// Placement new
		SerializeBuffer* pSerBuf = new(pBaseAddr + i) SerializeBuffer(m_headerCode);

		// 생성된 SerializeBuffer 포인터 저장
		pNewSerBufBatch->Push(pSerBuf);
	}

	return pNewSerBufBatch;
}
