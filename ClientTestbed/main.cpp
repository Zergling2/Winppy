#define _CRT_SECURE_NO_WARNINGS
#define FD_SETSIZE 512
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>
#include <clocale>

#pragma comment(lib, "ws2_32.lib")

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

struct Buffer
{
    struct Header
    {
        uint32_t m_code;
        uint32_t m_size;
    }m_header;
    uint8_t m_payload[1024 - sizeof(Header) - sizeof(void*) * 3];
    void* m_reserved0;
    void* m_reserved1;
    void* m_reserved2;
};

class Client
{
public:
    Client()
        : m_sock(INVALID_SOCKET)
        , m_isSending(false)
        , m_waitingEcho(false)
        , m_sendIndex(0)
        , m_recvBuf()
        , m_recvBytes(0)
        , m_sendBuf()
        , m_sendBytes(0)
        , m_targetSendBytes(0)
    {
		m_recvBuf.m_header.m_code = HEADER_CODE;
        m_sendBuf.m_header.m_code = HEADER_CODE;
	}
public:
    SOCKET m_sock;
	bool m_isSending;
    bool m_waitingEcho;
    size_t m_sendIndex;
    Buffer m_recvBuf;
	uint32_t m_recvBytes;
	Buffer m_sendBuf;
	uint32_t m_sendBytes;
    uint32_t m_targetSendBytes;
};

fd_set g_readSet;
fd_set g_writeSet;

int main()
{
    _wsetlocale(LC_ALL, L"");
    srand(static_cast<int>(time(nullptr)));

    for (size_t i = 0; i < MESSAGE_COUNT; ++i)
        MESSAGE_LENGTH[i] = wcslen(MESSAGE[i]);


    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    std::vector<Client> clients(CLIENT_COUNT);

    // 모든 클라이언트 소켓 생성 & connect
    for (int i = 0; i < CLIENT_COUNT; ++i)
    {
        clients[i].m_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, IP, &serverAddr.sin_addr);

        if (connect(clients[i].m_sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
        {
            std::cerr << "Client " << i << " failed to connect\n";
            closesocket(clients[i].m_sock);
            clients[i].m_sock = INVALID_SOCKET;
        }
        // non-blocking
        u_long mode = 1;
        ioctlsocket(clients[i].m_sock, FIONBIO, &mode);
    }

    std::cout << "All clients connected. Starting select loop...\n";

    while (true)
    {
        FD_ZERO(&g_readSet);
        FD_ZERO(&g_writeSet);

        for (auto& client : clients)
        {
            if (client.m_sock != INVALID_SOCKET)
            {
                FD_SET(client.m_sock, &g_readSet);
                FD_SET(client.m_sock, &g_writeSet);
            }
        }

        int count = select(0, &g_readSet, &g_writeSet, nullptr, nullptr);
        if (count == SOCKET_ERROR)
        {
            std::cerr << "select error\n";
            break;
        }

        for (size_t i = 0; i < CLIENT_COUNT; ++i)
        {
			Client& client = clients[i];

            SOCKET sock = client.m_sock;
            if (sock == INVALID_SOCKET)
                continue;

            // 송신 가능 여부 확인
            if (FD_ISSET(sock, &g_writeSet))
            {
                if (!client.m_isSending && !client.m_waitingEcho)
                {
                    // 랜덤 메시지 선택
                    client.m_sendIndex = rand() % MESSAGE_COUNT;

                    client.m_sendBuf.m_header.m_size = static_cast<uint32_t>((MESSAGE_LENGTH[client.m_sendIndex] + 1) * sizeof(wchar_t));
                    wcscpy(reinterpret_cast<wchar_t*>(client.m_sendBuf.m_payload), MESSAGE[client.m_sendIndex]);

                    client.m_targetSendBytes = sizeof(client.m_sendBuf.m_header) + client.m_sendBuf.m_header.m_size;
                    client.m_sendBytes = 0;

                    client.m_isSending = true;
                }

                if (client.m_sendBytes < client.m_targetSendBytes)
                {
                    int ret = send(
                        sock,
                        reinterpret_cast<const char*>(&client.m_sendBuf) + client.m_sendBytes,
                        client.m_targetSendBytes - client.m_sendBytes,
                        0
                    );

                    if (ret == SOCKET_ERROR)
                    {
                        closesocket(client.m_sock);
                        client.m_sock = INVALID_SOCKET;
                    }
                    else
                    {
                        client.m_sendBytes += ret;
                        if (client.m_sendBytes >= client.m_targetSendBytes)
                        {
                            client.m_isSending = false; // 전송 완료
                            client.m_waitingEcho = true;
                        }
                    }
                }
            }

            // 수신 가능 여부 확인
            if (FD_ISSET(sock, &g_readSet))
            {
                int ret = recv(
                    sock,
                    reinterpret_cast<char*>(&client.m_recvBuf) + client.m_recvBytes,
                    sizeof(client.m_recvBuf) - client.m_recvBytes,
                    0
                );

                if (ret == 0) // 연결 종료
                {
                    closesocket(client.m_sock);
                    client.m_sock = INVALID_SOCKET;
                }
                else if (ret == SOCKET_ERROR)
                {
                    // 논블로킹 소켓에서 EWOULDBLOCK는 무시
                    int err = WSAGetLastError();
                    if (err != WSAEWOULDBLOCK)
                    {
                        closesocket(client.m_sock);
                        client.m_sock = INVALID_SOCKET;
                    }
                }
                else
                {
                    client.m_recvBytes += ret;

                    // 메시지 완전 수신 체크
                    if (client.m_recvBytes >= sizeof(Buffer::Header) &&
                        client.m_recvBuf.m_header.m_code == HEADER_CODE &&
                        client.m_recvBytes >= sizeof(Buffer::Header) + client.m_recvBuf.m_header.m_size)
                    {
                        // 메시지 일치 확인
                        if (wcscmp(MESSAGE[client.m_sendIndex], reinterpret_cast<const wchar_t*>(client.m_recvBuf.m_payload)) != 0)
                        {
                            std::wcout << L"missmatch!\n";
                        }

                        // 수신 버퍼 초기화
                        client.m_recvBytes = 0;
                        client.m_waitingEcho = false;
                    }
                }
            }
        }

        Sleep(0);
    }

    for (auto& client : clients)
        if (client.m_sock != INVALID_SOCKET)
            closesocket(client.m_sock);

    WSACleanup();
    return 0;
}
