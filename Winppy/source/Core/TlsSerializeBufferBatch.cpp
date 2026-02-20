#include <winppy/Core/TlsSerializeBufferBatch.h>
#include <cassert>
#include <winppy/Core/SerializeBuffer.h>
#include <winppy/Core/SerializeBufferBatch.h>
#include <winppy/Core/SerializeBufferBatchPool.h>

using namespace winppy;

thread_local SerializeBufferBatch* TlsSerializeBufferBatch::t_pUsingSerBufBatch = nullptr;
thread_local SerializeBufferBatch* TlsSerializeBufferBatch::t_pReturnSerBufBatch = nullptr;

SerializeBuffer* TlsSerializeBufferBatch::GetSerializeBuffer() noexcept
{
	if (t_pUsingSerBufBatch == nullptr)
		t_pUsingSerBufBatch = SerializeBufferBatchPool::GetInstance().GetFullSerializeBufferBatch();

	assert(t_pUsingSerBufBatch->Size() > 0);

	SerializeBuffer* pRet = t_pUsingSerBufBatch->Pop();
	if (t_pUsingSerBufBatch->Size() == 0)
	{
		SerializeBufferBatchPool::GetInstance().ReturnEmptySerializeBufferBatch(t_pUsingSerBufBatch);
		t_pUsingSerBufBatch = nullptr;
	}

	assert(pRet->GetRefCount() == 0);
	pRet->Init();
	pRet->AddRef();		// 참조 카운트 증가 및 반환

	return pRet;
}

void TlsSerializeBufferBatch::ReturnSerializeBuffer(SerializeBuffer* pSerBuf)
{
	assert(pSerBuf->GetRefCount() == 0);

	if (t_pReturnSerBufBatch == nullptr)
		t_pReturnSerBufBatch = SerializeBufferBatchPool::GetInstance().GetEmptySerializeBufferBatch();

	assert(t_pReturnSerBufBatch->Size() < t_pReturnSerBufBatch->Capacity());

	t_pReturnSerBufBatch->Push(pSerBuf);
	if (t_pReturnSerBufBatch->Size() == t_pReturnSerBufBatch->Capacity())
	{
		SerializeBufferBatchPool::GetInstance().ReturnFullSerializeBufferBatch(t_pReturnSerBufBatch);
		t_pReturnSerBufBatch = nullptr;
	}
}
