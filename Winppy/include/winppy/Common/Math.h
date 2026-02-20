#pragma once

#include <cstddef>
#include <algorithm>

namespace winppy
{
	class Math
	{
	public:
		static constexpr size_t OneKiB() { return 1 << 10; }
		static constexpr size_t OneMiB() { return OneKiB() << 10; }
		static constexpr size_t OneGiB() { return OneMiB() << 10; }

		template<typename T>
		static T Clamp(T value, T min, T max) { return (std::max)(min, (std::min)(value, max)); }

		/**
		* @brief 전달된 매개변수보다 크거나 같은 가장 가까운 2의 승수를 반환합니다.
		*
		* @param value 값을 전달합니다.
		*
		* @return value보다 크거나 같은 가장 가까운 2의 승수입니다.
		*/
		template<typename T>
		static T NextPowerOf2(T value);

		template<typename T>
		static constexpr bool IsPowerOf2(T value);
	};

	template<typename T>
	T Math::NextPowerOf2(T value)
	{
		if (value <= 1)
			return 1;

		T next = 2;
		while (next < value)
			next = next << 1;

		return next;
	}

	template<typename T>
	constexpr bool Math::IsPowerOf2(T value)
	{
		return value > 0 && (value & (value - 1)) == 0;
	}
}
