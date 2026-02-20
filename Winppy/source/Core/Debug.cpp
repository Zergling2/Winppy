#include <winppy/Core/Debug.h>

bool Debug::GetWinErrString(DWORD errorCode, wchar_t* pBuf, size_t cchSize)
{
	DWORD outLen = FormatMessageW(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
		nullptr,
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		pBuf,
		static_cast<DWORD>(cchSize),
		nullptr
	);

	return outLen != 0;
}

bool Debug::GetWinErrString(DWORD errorCode, char* pBuf, size_t cchSize)
{
	DWORD outLen = FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
		nullptr,
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		pBuf,
		static_cast<DWORD>(cchSize),
		nullptr
	);

	return outLen != 0;
}

void Debug::ForceCrash()
{
	*reinterpret_cast<int*>(0) = 0;
}
