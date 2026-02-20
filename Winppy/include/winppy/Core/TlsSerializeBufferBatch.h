#pragma once

namespace winppy
{
	class SerializeBuffer;
	class SerializeBufferBatch;

	/**
	* @brief 직렬화 버퍼 배치의 TLS Wrapper 클래스.
	*/
	class TlsSerializeBufferBatch
	{
	public:
		[[nodiscard]] static SerializeBuffer* GetSerializeBuffer() noexcept;
		static void ReturnSerializeBuffer(SerializeBuffer* pSerBuf);
	private:
		static thread_local SerializeBufferBatch* t_pUsingSerBufBatch;
		static thread_local SerializeBufferBatch* t_pReturnSerBufBatch;
	};
}
