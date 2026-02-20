#pragma once

#include <cstddef>
#include <winppy/Platform/Platform.h>

class Debug
{
public:

	/**
	* @brief GetLastError 함수가 반환한 오류 코드에 대응하는 오류 메시지를 조회합니다.
	*
	* @param errorCode GetLastError 함수가 반환한 오류 코드를 전달합니다.
	* @param pBuf 오류 메시지를 저장할 버퍼 주소를 전달합니다.
	* @param cchSize 오류 메시지 버퍼의 길이를 전달합니다.
	* 
	* @return 함수 실행이 실패하면 반환 값은 false입니다. 자세한 오류 정보를 얻으려면 GetLastError를 호출하십시오.
	*/
	static bool GetWinErrString(DWORD errorCode, wchar_t* pBuf, size_t cchSize);

	/**
	* @brief GetLastError 함수가 반환한 오류 코드에 대응하는 오류 메시지를 조회합니다.
	*
	* @param errorCode GetLastError 함수가 반환한 오류 코드를 전달합니다.
	* @param pBuf 오류 메시지를 저장할 버퍼 주소를 전달합니다.
	* @param cchSize 오류 메시지 버퍼의 길이를 전달합니다.
	*
	* @return 함수 실행이 실패하면 반환 값은 false입니다. 자세한 오류 정보를 얻으려면 GetLastError를 호출하십시오.
	*/
	static bool GetWinErrString(DWORD errorCode, char* pBuf, size_t cchSize);

	static void ForceCrash();
};
