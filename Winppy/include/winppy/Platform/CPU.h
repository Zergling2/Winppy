#pragma once

#include <cstddef>

namespace winppy
{
	class Cache
	{
	public:
		static constexpr size_t L1LineSize() { return 64; }
	};
}
