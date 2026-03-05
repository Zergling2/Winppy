#pragma once

namespace winppy
{
	class SerializeBuffer;

	// SerializeBuffer 묶음
	class SerializeBufferBatch
	{
	private:
		static constexpr size_t CAPACITY = 1 << 8;
	public:
		SerializeBufferBatch();
		~SerializeBufferBatch() = default;

		// SerializeBufferBatch 한 개당 SerializeBuffer의 개수
		static constexpr size_t Capacity() { return CAPACITY; }

		size_t Size() const { return m_size; }
		void Push(SerializeBuffer* pSerBuf);
		SerializeBuffer* Pop();
	private:
		SerializeBuffer* m_bufs[CAPACITY];
		size_t m_size;
	};
}
