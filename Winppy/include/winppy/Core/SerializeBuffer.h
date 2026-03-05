#pragma once

#include <winppy/Platform/Platform.h>
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace winppy
{
	/**
	* @brief 직렬화 버퍼 클래스입니다. 이 클래스는 네트워크 통신에서 데이터를 직렬화하여 전송하기 위한 버퍼로 사용됩니다.
	* 
	* 하나의 직렬화 버퍼 인스턴스로 전송할 수 있는 데이터의 최대 크기는 Capacity 함수로 반환되는 값으로 제한됩니다.
	* 
	* 라이브러리 사용자는 절대 이 클래스를 직접 인스턴스화하여 사용해서는 안됩니다. 대신 Packet 클래스의 인스턴스를 생성하고 인터페이스를 통해 직렬화 버퍼를 사용해야 합니다.
	*/
	class SerializeBuffer
	{
	public:
		struct Header
		{
		public:
			Header() = default;
			Header(uint32_t code, uint32_t size)
				: m_code(code)
				, m_size(size)
			{
			}
		public:
			uint32_t m_code;
			uint32_t m_size;	// Payload size
		};
		struct ControlBlock
		{
		public:
			ControlBlock(long refCount, const uint8_t* pReadCursor, uint8_t* pWriteCursor);
		public:
			long m_refCount;
			const uint8_t* m_pReadCursor;
			uint8_t* m_pWriteCursor;
		};
	private:
		// 2의 승수 강제
		static constexpr size_t INSTANCE_SIZE = 1 << 10;

		// INSTANCE_SIZE - (sizeof(m_payload) + sizeof(m_ctrlBlock))
		static constexpr size_t CAPACITY = INSTANCE_SIZE - (sizeof(Header) + sizeof(ControlBlock));
	public:
		SerializeBuffer(uint32_t code);
		~SerializeBuffer() = default;
		SerializeBuffer(const SerializeBuffer&) = delete;
		SerializeBuffer(SerializeBuffer&&) = delete;
		SerializeBuffer& operator=(const SerializeBuffer&) = delete;
		SerializeBuffer& operator=(SerializeBuffer&&) = delete;

		/**
		* @brief 다른 SerializeBuffer 객체의 데이터를 현재 객체로 복사합니다. 이 함수는 데이터 및 읽기 커서와 쓰기 커서의 오프셋을 논리적으로 복사합니다.
		* 
		* 읽기 커서와 쓰기 커서의 상대적 위치는 복사 대상과 동일하게 유지됩니다.
		* 
		* @param other 복사할 직렬화 버퍼 객체.
		*/
		void CopyFrom(const SerializeBuffer& other);

		/**
		* @brief 직렬화 버퍼 객체의 크기를 반환합니다. 이 함수가 반환하는 값은 직렬화 버퍼를 통해 전송할 수 있는 데이터의 크기와는 관련이 없습니다.
		*/
		static constexpr size_t InstanceSize() { return INSTANCE_SIZE; }

		/**
		* @brief 직렬화 버퍼에 저장할 수 있는 데이터의 최대 크기(바이트 단위)를 반환합니다.
		* 
		* @return 직렬화 버퍼에 저장할 수 있는 최대 크기(바이트 단위).
		*/
		static constexpr size_t Capacity() { return CAPACITY; }

		/**
		* @brief 페이로드의 주소를 반환합니다. 라이브러리 사용자는 이 함수를 직접 호출하지 마세요.
		* 
		* @return 페이로드 시작 주소.
		*/
		void* Payload() { return m_payload; }

		/**
		* @brief 페이로드의 주소를 반환합니다. 라이브러리 사용자는 이 함수를 호출하는 것을 권장하지 않습니다.
		*
		* @return 페이로드 시작 주소.
		*/
		const void* Payload() const { return m_payload; }
		
		/**
		* @brief 직렬화 버퍼의 헤더 및 저장된 데이터를 포함한 전체 크기를 반환하는 함수입니다.
		* 
		* @return 직렬화 버퍼의 헤더 및 저장된 데이터를 포함한 전체 크기.
		*/
		size_t SizeIncludingHeader() const { return sizeof(m_header) + Size(); }

		/**
		* @brief 데이터의 크기를 반환하는 함수입니다. 이 함수로 반환되는 값은 헤더를 제외한 크기입니다.
		*
		* @return 저장된 데이터의 크기.
		*/
		size_t Size() const { return m_header.m_size; }

		/**
		* @brief 데이터를 저장할 수 있는 여유 공간의 바이트 단위 크기를 반환합니다.
		* 
		* @return 데이터를 저장할 수 있는 여유 공간 크기.
		*/
		size_t WriteableSize() const { return Capacity() - this->Size(); }

		/**
		* @brief 현재 읽기 커서를 기준으로 한 읽기 가능한 크기를 반환합니다.
		*
		* @return 읽기 가능한 크기.
		*/
		size_t ReadableSize() const { return m_ctrlBlock.m_pWriteCursor - m_ctrlBlock.m_pReadCursor; }

		/**
		* @brief 읽기 커서를 버퍼의 시작 지점으로 이동합니다. 버퍼를 다시 읽으려는 경우 유용한 함수입니다.
		*/
		void ResetReadCursor() { m_ctrlBlock.m_pReadCursor = m_payload; }

		/**
		* @brief 버퍼의 내용을 초기화합니다. 읽기 및 쓰기 커서가 버퍼의 시작 지점으로 이동되며, 크기 정보도 0으로 초기화됩니다.
		*/
		void Clear();

		bool Write(char data) { return WritePod(data); }
		bool Write(wchar_t data) { return WritePod(data); }
		bool Write(int8_t data) { return WritePod(data); }
		bool Write(uint8_t data) { return WritePod(data); }
		bool Write(int16_t data) { return WritePod(data); }
		bool Write(uint16_t data) { return WritePod(data); }
		bool Write(int32_t data) { return WritePod(data); }
		bool Write(uint32_t data) { return WritePod(data); }
		bool Write(int64_t data) { return WritePod(data); }
		bool Write(uint64_t data) { return WritePod(data); }
		bool Write(float data) { return WritePod(data); }
		bool Write(double data) { return WritePod(data); }
		bool WriteBytes(const void* pData, size_t size);

		bool Read(char* pOut) { return ReadPod(pOut); }
		bool Read(wchar_t* pOut) { return ReadPod(pOut); }
		bool Read(int8_t* pOut) { return ReadPod(pOut); }
		bool Read(uint8_t* pOut) { return ReadPod(pOut); }
		bool Read(int16_t* pOut) { return ReadPod(pOut); }
		bool Read(uint16_t* pOut) { return ReadPod(pOut); }
		bool Read(int32_t* pOut) { return ReadPod(pOut); }
		bool Read(uint32_t* pOut) { return ReadPod(pOut); }
		bool Read(int64_t* pOut) { return ReadPod(pOut); }
		bool Read(uint64_t* pOut) { return ReadPod(pOut); }
		bool Read(float* pOut) { return ReadPod(pOut); }
		bool Read(double* pOut) { return ReadPod(pOut); }
		bool ReadBytes(void* pOut, size_t size);

		/**
		* @brief 참조 카운트를 증가시킵니다.
		* 
		* 특히 라이브러리 사용자의 경우 Packet 객체를 사용해 접근하는 경우 이 함수를 호출해서는 안됩니다.
		*/
		void AddRef() { InterlockedIncrement(&m_ctrlBlock.m_refCount); }

		/**
		* @brief 참조 카운트를 감소시킵니다.
		*
		* 특히 라이브러리 사용자의 경우 Packet 객체를 사용해 접근하는 경우 이 함수를 호출해서는 안됩니다.
		*/
		uint32_t Release();

		/**
		* @brief 참조 카운트를 얻습니다.
		* 
		* @return 현재 참조 카운트.
		*/
		uint32_t GetRefCount() const { return m_ctrlBlock.m_refCount; }

		/**
		* @brief 네트워크를 통해 전송되는 데이터 시작 주소를 반환합니다. 라이브러리 사용자는 이 함수를 호출하는 것을 권장하지 않습니다.
		* 
		* (응용 프로그램 계층 헤더 + 데이터 주소)
		*/
		void* Message() { return &m_header; }

		/**
		* @brief 네트워크를 통해 전송되는 데이터 시작 주소를 반환합니다. 라이브러리 사용자는 이 함수를 호출하는 것을 권장하지 않습니다.
		*
		* (응용 프로그램 계층 헤더 + 데이터 주소)
		*/
		const void* Message() const { return &m_header; }	// 응용 프로그램 계층 헤더 + 데이터 주소

		/**
		* @brief 읽기 커서의 위치를 설정합니다. 라이브러리 사용자는 이 함수를 호출하지 마세요.
		*/
		void SetReadCursorOffset(size_t offset) { m_ctrlBlock.m_pReadCursor = m_payload + offset; }

		/**
		* @brief 쓰기 커서의 위치를 설정합니다. 라이브러리 사용자는 이 함수를 호출하지 마세요.
		*/
		void SetWriteCursorOffset(size_t offset) { m_ctrlBlock.m_pWriteCursor = m_payload + offset; }

		/**
		* @brief 객체를 논리적으로 초기 상태로 되돌립니다. 라이브러리 사용자는 절대 이 함수를 호출하지 마세요.
		*/
		void Init();
	private:
		template<typename T>
		bool WritePod(T data);	// Write Plain Old Data
		template<typename T>
		bool ReadPod(T* pOut);	// Read Plain Old Data
	private:
		Header m_header;
		uint8_t m_payload[CAPACITY];
		ControlBlock m_ctrlBlock;	// 버퍼의 후방에 위치시키는것이 컨트롤 블록 카운터 증감으로 인한 캐시 무효화로부터 상대적으로 자유롭다. (컨트롤 블록이 24바이트밖에 안되기 때문에)
	};

	template<typename T>
	bool SerializeBuffer::WritePod(T data)
	{
		if (this->WriteableSize() < sizeof(T))
			return false;

		std::memcpy(m_ctrlBlock.m_pWriteCursor, &data, sizeof(T));
		m_ctrlBlock.m_pWriteCursor = m_ctrlBlock.m_pWriteCursor + sizeof(T);
		m_header.m_size += sizeof(T);

		return true;
	}

	template<typename T>
	bool SerializeBuffer::ReadPod(T* pOut)
	{
		if (this->ReadableSize() < sizeof(T))
			return false;

		std::memcpy(pOut, m_ctrlBlock.m_pReadCursor, sizeof(T));
		m_ctrlBlock.m_pReadCursor = m_ctrlBlock.m_pReadCursor + sizeof(T);

		return true;
	}
}
