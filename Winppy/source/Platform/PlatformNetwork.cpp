#include <winppy/Platform/PlatformNetwork.h>
#include <MSWSock.h>
#include <mutex>

BOOL winppy::ConnectEx(SOCKET s, const sockaddr* name, int namelen, PVOID lpSendBuffer, DWORD dwSendDataLength, LPDWORD lpdwBytesSent, LPOVERLAPPED lpOverlapped)
{
	static LPFN_CONNECTEX fa = nullptr;
	static std::once_flag initFlag;

	std::call_once(
		initFlag,
		[&]()
		{
			GUID guid = WSAID_CONNECTEX;
			DWORD bytes = 0;

			int result = WSAIoctl(
				s,
				SIO_GET_EXTENSION_FUNCTION_POINTER,
				&guid,
				sizeof(guid),
				&fa,
				sizeof(fa),
				&bytes,
				nullptr,
				nullptr
			);

			if (result == SOCKET_ERROR)
				fa = nullptr;
		}
	);

	return fa(s, name, namelen, lpSendBuffer, dwSendDataLength, lpdwBytesSent, lpOverlapped);
}
