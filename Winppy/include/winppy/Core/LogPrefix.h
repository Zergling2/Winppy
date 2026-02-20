#pragma once

namespace winppy
{
	class LogPrefixString
	{
	public:
		static const wchar_t* Info();
		static const wchar_t* Warning();
		static const wchar_t* Fail();
		static const wchar_t* Error();
		static const wchar_t* Fatal();
	};
}
