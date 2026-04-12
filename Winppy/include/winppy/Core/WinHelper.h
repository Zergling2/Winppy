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

	/**
	* @brief 주소 구조체에서 문자열 IP 주소와 호스트 바이트 포트 번호를 획득합니다.
	* 
	* @param pSockAddrStorage 주소 구조체의 주소.
	* @param pIPAddrBuf 문자열 IP 주소를 저장할 버퍼 주소.
	* @param len pIPAddrBuf가 가리키는 버퍼의 문자 단위 크기.
	* @param pPort 포트 번호를 전달받을 버퍼.
	* @return 성공 시 true, 실패 시 false를 반환합니다. 버퍼의 길이가 충분하지 않거나 주소 구조체에 유효하지 않은 값이 있을 경우 함수는 실패합니다.
	*/
	bool SockAddrToString(const SOCKADDR_STORAGE& sockAddrStorage, wchar_t* pIPAddrBuf, size_t len, uint16_t& port);

	HANDLE LogBeginThreadEx(FileLogger& fileLogger, void* pSecurity, unsigned int stackSize, unsigned int(__stdcall* pStartAddress)(void*),
		void* pArgList, unsigned int initFlag, unsigned int* pThrdAddr);
}
