#include <winppy/Network/TCPClientEngine.h>
#include <winppy/Network/TCPClient.h>
#include <clocale>
#include <ctime>

const char* IP = "127.0.0.1";
constexpr uint16_t SERVER_PORT = 37015;
constexpr size_t CLIENT_COUNT = 512;
constexpr uint32_t HEADER_CODE = 0xaabb1122;

constexpr size_t MESSAGE_COUNT = 10;
const wchar_t* MESSAGE[MESSAGE_COUNT] =
{
	L"Sudden Attack",
	L"톰 클랜시의 레인보우식스 시즈 X",
	L"Biohazard 4",
	L"StarCraft: Remastered",
	L"Counter-Strike 2",
	L"Left 4 Dead 2",
	L"Alien Swarm: Reactive Drop",
	L"Half-Life",
	L"Black Mesa",
	L"PUBG: Battlegrounds"
};
size_t MESSAGE_LENGTH[MESSAGE_COUNT];

class TCPEchoClient : public winppy::TCPClient
{
public:
	/**
	* @brief 연결이 성공한 경우 호출됩니다.
	*/
	virtual void OnConnect() override;

	/**
	* @brief 패킷을 수신한 경우 호출됩니다.
	*/
	virtual void OnReceive(winppy::Packet packet) override;

	/**
	* @brief 연결이 끊어진 경우 호출됩니다. 이 함수 내에서 즉시 재연결을 호출하는 경우 실패합니다.
	*/
	virtual void OnDisconnect() override;
private:
	size_t m_waitingIndex;
};

void TCPEchoClient::OnConnect()
{
	printf("Connected!\n");

	winppy::Packet packet;

	size_t index = rand() % MESSAGE_COUNT;
	packet->WriteBytes(MESSAGE[index], (MESSAGE_LENGTH[index] + 1) * sizeof(wchar_t));

	m_waitingIndex = index;

	Send(std::move(packet));
}

void TCPEchoClient::OnReceive(winppy::Packet packet)
{
	if (wcscmp(MESSAGE[m_waitingIndex], reinterpret_cast<wchar_t*>(packet->Payload())) != 0)
		this->Disconnect();

	winppy::Packet pk;
	size_t index = rand() % MESSAGE_COUNT;
	pk->WriteBytes(MESSAGE[index], (MESSAGE_LENGTH[index] + 1) * sizeof(wchar_t));

	m_waitingIndex = index;

	Send(std::move(pk));
}

void TCPEchoClient::OnDisconnect()
{
	printf("Disconnected!\n");
}


int main(void)
{
	_wsetlocale(LC_ALL, L"");
	srand(static_cast<unsigned int>(time(nullptr)));

	for (size_t i = 0; i < MESSAGE_COUNT; ++i)
		MESSAGE_LENGTH[i] = wcslen(MESSAGE[i]);

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

	winppy::TCPClientEngine ce;
	winppy::TCPClientEngineConfig config;
	config.m_headerCode = HEADER_CODE;
	config.m_numOfConcurrentThreads = 2;
	config.m_numOfWorkerThreads = 2;
	ce.Init(config);

	TCPEchoClient client[500];
	winppy::TCPClientInitDesc desc;
	desc.m_pEngine = &ce;

	for (size_t i = 0; i < 500; ++i)
	{
		client[i].Init(desc);
		client[i].Connect(L"127.0.0.1", SERVER_PORT);
	}

	Sleep(INFINITE);

	// WSA 리소스 해제
	if (WSACleanup() == SOCKET_ERROR)
	{
		printf("WSACleanup failed with error: %d.\n", WSAGetLastError());
		return -1;
	}

	return 0;
}
