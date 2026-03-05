#pragma once

#include <winppy/Platform/Platform.h>
#include <vector>

namespace winppy
{
	class SerializeBufferBatch;

	class SerializeBufferBatchPool
	{
	public:
		SerializeBufferBatchPool();
		~SerializeBufferBatchPool();

		static SerializeBufferBatchPool& GetInstance()
		{
			static SerializeBufferBatchPool s_instance;
			return s_instance;
		}

		void Init(uint32_t code);
		SerializeBufferBatch* GetFullSerializeBufferBatch();
		SerializeBufferBatch* GetEmptySerializeBufferBatch();
		void ReturnFullSerializeBufferBatch(SerializeBufferBatch* pSerBufBatch);
		void ReturnEmptySerializeBufferBatch(SerializeBufferBatch* pSerBufBatch);
		size_t GetFullBatchCount();
		size_t GetEmptyBatchCount();
	private:
		SerializeBufferBatch* CreateFullBatch();
	private:
		uint32_t m_headerCode;
		SYSTEM_INFO m_si;
		SRWLOCK m_vmLock;			// m_vm 접근 동기화
		SRWLOCK m_fullBatchLock;	// m_fullBatch 접근 동기화
		SRWLOCK m_emptyBatchLock;	// m_emptyBatch 접근 동기화
		std::vector<void*> m_vm;
		std::vector<SerializeBufferBatch*> m_fullBatch;
		std::vector<SerializeBufferBatch*> m_emptyBatch;
	};
}
