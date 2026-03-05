#pragma once

#include <cstddef>
#include <cstdint>

namespace winppy
{
	constexpr uint32_t SESSION_COUNT_MAX = 1u << 13;
	constexpr uint32_t RECV_BUFFER_SIZE_MIN = 1u << 13;
	constexpr uint32_t RECV_BUFFER_SIZE_MAX = 1u << 15;
	constexpr uint32_t SEND_QUEUE_SIZE_MIN = 1u << 7;
	constexpr uint32_t SEND_QUEUE_SIZE_MAX = 1u << 9;
	constexpr uint32_t DEFAULT_HEADER_CODE = 0x5f3759df;
	constexpr size_t WSABUF_LEN_MAX = 64;
}
