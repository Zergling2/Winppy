#pragma once

#include <cstddef>
#include <cstdint>

namespace winppy
{
	inline bool IsAligned(const void* p, size_t alignment) { return (reinterpret_cast<uintptr_t>(p) % alignment) == 0; }
	inline bool IsAligned2(const void* p) { return (reinterpret_cast<uintptr_t>(p) & 0b1) == 0; }
	inline bool IsAligned4(const void* p) { return (reinterpret_cast<uintptr_t>(p) & 0b11) == 0; }
	inline bool IsAligned8(const void* p) { return (reinterpret_cast<uintptr_t>(p) & 0b111) == 0; }
	inline bool IsAligned16(const void* p) { return (reinterpret_cast<uintptr_t>(p) & 0b1111) == 0; }
	inline bool IsAligned32(const void* p) { return (reinterpret_cast<uintptr_t>(p) & 0b11111) == 0; }
	inline bool IsAligned64(const void* p) { return (reinterpret_cast<uintptr_t>(p) & 0b111111) == 0; }
	inline bool IsAligned128(const void* p) { return (reinterpret_cast<uintptr_t>(p) & 0b1111111) == 0; }
}
