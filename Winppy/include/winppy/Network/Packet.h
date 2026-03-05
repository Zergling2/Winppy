#pragma once

#include <winppy/Core/SerializeBuffer.h>
#include <utility>

namespace winppy
{
	class SerializeBuffer;

	/**
	* @brief SerializeBuffer에 대한 침습형 래퍼 클래스입니다.
	* 
	* 여러 세션에게 패킷을 전송하는 경우 Send 함수로 전달한 패킷을 더 이상 수정해서는 안됩니다.
	*/
	class Packet
	{
		friend class TCPServer;
	public:
		Packet() noexcept;
		Packet(const Packet& other) noexcept
			: m_pObj(other.m_pObj)
		{
			if (m_pObj)
				m_pObj->AddRef();
		}
		Packet(Packet&& other) noexcept
			: m_pObj(other.m_pObj)
		{
			other.m_pObj = nullptr;
		}
		~Packet()
		{
			if (m_pObj)
				m_pObj->Release();
		}

		/**
		* @brief 다른 Packet 객체의 데이터를 현재 객체로 복사합니다. 이 함수는 데이터 및 읽기 커서와 쓰기 커서의 오프셋을 논리적으로 복사합니다.
		* 
		* 읽기 커서와 쓰기 커서의 상대적 위치는 복사 대상과 동일하게 유지됩니다.
		* 
		* @param other 복사할 패킷 객체.
		*/
		Packet& operator=(const Packet& other) noexcept { m_pObj->CopyFrom(*other.m_pObj); }
		Packet& operator=(Packet&& other) noexcept = delete;

		SerializeBuffer* operator->() { return m_pObj; }
		const SerializeBuffer* operator->() const { return m_pObj; }
		SerializeBuffer* Get() { return m_pObj; }
		const SerializeBuffer* Get() const { return m_pObj; }
		operator bool() const { return m_pObj != nullptr; }

		SerializeBuffer* Detach() { return std::exchange(m_pObj, nullptr); }
	private:
		SerializeBuffer* m_pObj;
	};
}
