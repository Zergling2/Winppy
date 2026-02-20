#include <winppy/Network/Packet.h>
#include <winppy/Core/SerializeBuffer.h>
#include <winppy/Core/TlsSerializeBufferBatch.h>

using namespace winppy;

Packet::Packet() noexcept
	: m_pObj(TlsSerializeBufferBatch::GetSerializeBuffer())
{
}
