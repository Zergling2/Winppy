#include <winppy/Core/LogPrefix.h>

using namespace winppy;

const wchar_t* LogPrefixString::Info()
{
    return L"[INFO]";
}

const wchar_t* LogPrefixString::Warning()
{
    return L"[WARNING]";
}

const wchar_t* LogPrefixString::Fail()
{
    return L"[FAIL]";
}

const wchar_t* LogPrefixString::Error()
{
    return L"[ERROR]";
}

const wchar_t* LogPrefixString::Fatal()
{
    return L"[FATAL]";
}
