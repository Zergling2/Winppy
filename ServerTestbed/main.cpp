#include <winppy/Network/TCPServer.h>
#include <iostream>
#include <cstdint>
#include <clocale>
#include <process.h>

constexpr uint32_t HEADER_CODE = 0xaabb1122;
constexpr uint16_t PORT = 37015;

long g_tps = 0;

class TCPEchoServer : public winppy::TCPServer
{
public:
	virtual bool OnConnect(const wchar_t* ip, uint16_t port, uint64_t id) override;
	virtual void OnReceive(uint64_t id, winppy::Packet packet) override;
	virtual void OnDisconnect(uint64_t id) override;
};

bool TCPEchoServer::OnConnect(const wchar_t* ip, uint16_t port, uint64_t id)
{
	wprintf(L"New Session: %ls:%u. Id: %llu\n", ip, static_cast<uint32_t>(port), id);

	return true;
}

void TCPEchoServer::OnReceive(uint64_t id, winppy::Packet packet)
{
	InterlockedIncrement(&g_tps);
	// const wchar_t* pStr = reinterpret_cast<const wchar_t*>(packet->Payload());
	// wprintf(L"%ls\n", pStr);
	this->Send(id, std::move(packet));
}

void TCPEchoServer::OnDisconnect(uint64_t id)
{
	wprintf(L"Session disconnected. Id: %llu\n", id);
}

unsigned int TPSPrintThreadEntry(void* pArg)
{
	while (true)
	{
		printf("%ld\n", InterlockedExchange(&g_tps, 0));
		// printf("Full Batch Count: %zu, Empty Batch Count: %zu\n", winppy::SerializeBufferBatchPool::GetInstance().GetFullBatchCount(), winppy::SerializeBufferBatchPool::GetInstance().GetEmptyBatchCount());
		Sleep(1000);
	}

	return 0;
}

int main(void)
{
	_wsetlocale(LC_ALL, L"");

	// ########################################################################################################
	// WSA 초기화
	WSADATA wsaData;
	int ec = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (ec != 0)
	{
		printf("WSAStartup failed with error: %d.\n", ec);
		return -1;
	}
	// ########################################################################################################


	HANDLE hTPSPrintThread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, TPSPrintThreadEntry, nullptr, 0, nullptr));
	CloseHandle(hTPSPrintThread);

	TCPEchoServer server;

	winppy::TCPServerConfig desc;
	desc.m_logFileName = nullptr;
	desc.m_headerCode = HEADER_CODE;
	desc.m_bindAddr = nullptr;
	desc.m_bindPort = PORT;
	desc.m_maxSessionCount = 5000;
	desc.m_numOfWorkerThreads = 6;
	desc.m_numOfConcurrentThreads = 6;
	// desc.m_zeroByteSendBuf = true;
	// desc.m_tcpNoDelay = false;
	// desc.m_endKey = winppy::EndKey::Q;
	int ret = server.Run(desc);
	if (ret == 0)
		server.Shutdown();


	// WSA 리소스 해제
	if (WSACleanup() == SOCKET_ERROR)
	{
		printf("WSACleanup failed with error: %d.\n", WSAGetLastError());
		return -1;
	}

	return 0;
}
