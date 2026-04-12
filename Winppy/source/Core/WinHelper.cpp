#include <winppy/Core/WinHelper.h>
#include <winppy/Core/FileLogger.h>
#include <winppy/Core/LogPrefix.h>
#include <strsafe.h>
#include <process.h>

bool winppy::SockAddrToString(const SOCKADDR_STORAGE& sockAddrStorage, wchar_t* pIPAddrBuf, size_t len, uint16_t& port)
{
    bool result;

    switch (sockAddrStorage.ss_family)
    {
    case AF_INET:
        result = InetNtopW(AF_INET, &reinterpret_cast<const SOCKADDR_IN&>(sockAddrStorage).sin_addr, pIPAddrBuf, len) != nullptr;
        port = ntohs(reinterpret_cast<const SOCKADDR_IN&>(sockAddrStorage).sin_port);
        break;
    case AF_INET6:
        result = InetNtopW(AF_INET6, &reinterpret_cast<const SOCKADDR_IN6&>(sockAddrStorage).sin6_addr, pIPAddrBuf, len) != nullptr;
        port = ntohs(reinterpret_cast<const SOCKADDR_IN6&>(sockAddrStorage).sin6_port);
        break;
    default:
        result = false;
        break;
    }

    return result;
}

HANDLE winppy::LogBeginThreadEx(FileLogger& fileLogger, void* pSecurity, unsigned int stackSize, unsigned int(__stdcall* pStartAddress)(void*),
	void* pArgList, unsigned int initFlag, unsigned int* pThrdAddr)
{
	/*
	* https://learn.microsoft.com/en-us/cpp/c-runtime-library/errno-doserrno-sys-errlist-and-sys-nerr?view=msvc-170
	* _beginthreadex 함수의 반환값을 보고 조회할 것이기 때문에 _set_errno(0), _set_doserrno(0) 호출이 필수는 아님.
	*/
	// _set_errno(0);
	// _set_doserrno(0);

	HANDLE hNewThread = NULL;
	uintptr_t ret = _beginthreadex(pSecurity, stackSize, pStartAddress, pArgList, initFlag, pThrdAddr);

	switch (ret)
	{
	case -1L:
		switch (errno)
		{
		case EAGAIN:	// if there are too many threads
			fileLogger.Write(L"%s _beginthreadex failed. (too many threads)\n", LogPrefixString::Error());
			break;
		case EINVAL:	// if the argument is invalid or the stack size is incorrect
			fileLogger.Write(L"%s _beginthreadex failed. (argument is invalid or the stack size is incorrect)\n", LogPrefixString::Error());
			break;
		case EACCES:	// if there are insufficient resources (such as memory)
			fileLogger.Write(L"%s _beginthreadex failed. (insufficient resources (such as memory))\n", LogPrefixString::Error());
			break;
		default:
			fileLogger.Write(L"%s _beginthreadex failed. (unknown reasons)\n", LogPrefixString::Error());
			break;
		}
		break;
	case 0:
		fileLogger.Write(L"%s _beginthreadex failed. (_doserrno: %d)\n", LogPrefixString::Error(), _doserrno);
		break;
	default:
		hNewThread = reinterpret_cast<HANDLE>(ret);
		break;
	}

	return hNewThread;
}
