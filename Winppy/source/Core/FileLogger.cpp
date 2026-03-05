#include <winppy/Core/FileLogger.h>
#include <winppy/Platform/Platform.h>
#include <strsafe.h>
#include <cstdarg>

using namespace winppy;

FileLogger::FileLogger()
	: m_file(nullptr)
{
}

FileLogger::~FileLogger()
{
	this->Close();
}

bool FileLogger::Open(const wchar_t* logFileName)
{
	errno_t e = _wfopen_s(&m_file, logFileName, L"wt, ccs=UTF-8");
    
    return e == 0;
}

void FileLogger::Close()
{
    if (m_file)
    {
        // fflush(m_file);
        fclose(m_file);

        m_file = nullptr;
    }
}

bool FileLogger::Write(const wchar_t* fmt, ...)
{
    if (!m_file)
        return false;

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t logTime[32];
    StringCbPrintfW(logTime, sizeof(logTime), L"%04d/%02d/%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    wchar_t logMsg[256];

    va_list args;
    va_start(args, fmt);

    StringCbVPrintfW(logMsg, sizeof(logMsg), fmt, args);

    va_end(args);

    // Windows _wfopen ccs=UTF-8 확장기능으로 자동 인코딩 변환 및 저장
    fwprintf_s(m_file, L"%ls %ls", logTime, logMsg);

    return true;
}
