#pragma once

#include <winppy/Network/Packet.h>
#include <winppy/Network/TCPError.h>

namespace winppy
{
	class TCPClient;

	class ITCPClientEventHandler
	{
	public:
		ITCPClientEventHandler() = default;
		virtual ~ITCPClientEventHandler() = default;

		virtual void OnConnect() = 0;
		virtual void OnReceive(Packet packet) = 0;
		virtual void OnDisconnect(TCPError error) = 0;
		virtual void OnError(TCPError error) = 0;
	};
}
