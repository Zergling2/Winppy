#pragma once

#include <winppy/Platform/Platform.h>
#include <cstddef>
#include <cstdint>

namespace winppy
{
	class FileLogger;

	inline HANDLE CreateNewCompletionPort(DWORD numOfConcurrentThreads)
	{
		return CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, numOfConcurrentThreads);
	}

	inline bool AssociateDeviceWithCompletionPort(HANDLE hCompletionPort, HANDLE hDevice, ULONG_PTR completionKey)
	{
		return CreateIoCompletionPort(hDevice, hCompletionPort, completionKey, 0) == hCompletionPort;
	}

	HANDLE LogBeginThreadEx(FileLogger& fileLogger, void* pSecurity, unsigned int stackSize, unsigned int(__stdcall* pStartAddress)(void*),
		void* pArgList, unsigned int initFlag, unsigned int* pThrdAddr);
}
