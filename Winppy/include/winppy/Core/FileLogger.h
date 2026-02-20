#pragma once

#include <cstddef>
#include <cstdio>

namespace winppy
{
	class FileLogger
	{
		static constexpr size_t BUFFER_SIZE = 256;
	public:
		FileLogger();
		~FileLogger();

		bool Open(const wchar_t* logFileName);
		void Close();

		bool Write(const wchar_t* fmt, ...);
	private:
		FILE* m_file;
	};
}
