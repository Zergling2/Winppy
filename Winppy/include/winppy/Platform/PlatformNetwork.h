#pragma once

#include <winppy/Platform/Platform.h>

namespace winppy
{
	BOOL ConnectEx(SOCKET s, const sockaddr* name, int namelen, PVOID lpSendBuffer, DWORD dwSendDataLength, LPDWORD lpdwBytesSent, LPOVERLAPPED lpOverlapped);
}
