#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "../inc/TcpGameTransport.h"

#include <chrono>

namespace
{
#ifdef _WIN32
    bool EnsureWinsock()
    {
        static bool initialized = []()
        {
            WSADATA data{};
            return WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }();
        return initialized;
    }

    void CloseSocket(SOCKET socket)
    {
        if (socket != INVALID_SOCKET)
            closesocket(socket);
    }

    bool WouldBlock()
    {
        int error = WSAGetLastError();
        return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
    }
#endif
}

TcpGameTransport::TcpGameTransport(Mode mode) : mode(mode)
{
}

TcpGameTransport::~TcpGameTransport()
{
    Stop();
}

std::shared_ptr<TcpGameTransport> TcpGameTransport::CreateHost(unsigned short port)
{
    auto transport = std::shared_ptr<TcpGameTransport>(new TcpGameTransport(Mode::Host));
    transport->StartHost(port);
    return transport;
}

std::shared_ptr<TcpGameTransport> TcpGameTransport::CreateClient(const std::string& address, unsigned short port)
{
    auto transport = std::shared_ptr<TcpGameTransport>(new TcpGameTransport(Mode::Client));
    transport->StartClient(address, port);
    return transport;
}

bool TcpGameTransport::StartHost(unsigned short hostPort)
{
    port = hostPort;
    running = true;
    status = "Hosting on port " + std::to_string(port);
    Log::Msg("[TCP]", "Starting host on port ", port);
    worker = std::thread(&TcpGameTransport::NetworkLoop, this);
    return true;
}

bool TcpGameTransport::StartClient(const std::string& hostAddress, unsigned short hostPort)
{
    address = hostAddress;
    port = hostPort;
    running = true;
    status = "Connecting to " + address + ":" + std::to_string(port);
    Log::Msg("[TCP]", "Connecting to ", address, ":", port);
    worker = std::thread(&TcpGameTransport::NetworkLoop, this);
    return true;
}

void TcpGameTransport::Stop()
{
    if (running)
        Log::Msg("[TCP]", "Stopping transport");
    running = false;
    if (worker.joinable())
        worker.join();
}

std::string TcpGameTransport::GetStatus() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return status;
}

void TcpGameTransport::SendClientCommand(const std::string& payload)
{
    if (mode == Mode::Client)
    {
        Log::Msg("[TCP]", "Queue client command payload");
        SendLine("C " + payload);
    }
}

std::vector<std::string> TcpGameTransport::ReceiveHostCommands()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(hostCommands);
}

void TcpGameTransport::SendHostResult(const std::string& payload)
{
    if (mode == Mode::Host)
    {
        Log::Msg("[TCP]", "Queue host result payload");
        SendLine("R " + payload);
    }
}

std::vector<std::string> TcpGameTransport::ReceiveClientResults()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(clientResults);
}

void TcpGameTransport::SendLobbyMessage(const std::string& payload)
{
    Log::Msg("[TCP]", "Queue lobby message: ", payload);
    SendLine("L " + payload);
}

std::vector<std::string> TcpGameTransport::ReceiveLobbyMessages()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(lobbyMessages);
}

void TcpGameTransport::SendLine(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(mutex);
    outboundLines.push_back(payload + "\n");
}

std::vector<std::string> TcpGameTransport::Drain(std::deque<std::string>& queue)
{
    std::vector<std::string> result;
    while (!queue.empty())
    {
        result.push_back(std::move(queue.front()));
        queue.pop_front();
    }
    return result;
}

void TcpGameTransport::QueueIncomingLine(const std::string& line)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (line.size() < 3)
        return;

    if (line[0] == 'C' && line[1] == ' ')
    {
        Log::Msg("[TCP]", "Received client command payload");
        hostCommands.push_back(line.substr(2));
    }
    else if (line[0] == 'R' && line[1] == ' ')
    {
        Log::Msg("[TCP]", "Received host result payload");
        clientResults.push_back(line.substr(2));
    }
    else if (line[0] == 'L' && line[1] == ' ')
    {
        Log::Msg("[TCP]", "Received lobby message: ", line.substr(2));
        lobbyMessages.push_back(line.substr(2));
    }
}

void TcpGameTransport::NetworkLoop()
{
#ifndef _WIN32
        failed = true;
        std::lock_guard<std::mutex> lock(mutex);
        status = "TCP transport is currently implemented for Windows";
        Log::Msg("[TCP]", status);
#else
    if (!EnsureWinsock())
    {
        failed = true;
        std::lock_guard<std::mutex> lock(mutex);
        status = "WSAStartup failed";
        Log::Msg("[TCP]", status);
        return;
    }

    SOCKET listenSocket = INVALID_SOCKET;
    SOCKET socket = INVALID_SOCKET;

    if (mode == Mode::Host)
    {
        listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket == INVALID_SOCKET)
        {
            failed = true;
            status = "Host socket failed";
            Log::Msg("[TCP]", status);
            return;
        }

        sockaddr_in service{};
        service.sin_family = AF_INET;
        service.sin_addr.s_addr = INADDR_ANY;
        service.sin_port = htons(port);

        if (bind(listenSocket, reinterpret_cast<sockaddr*>(&service), sizeof(service)) == SOCKET_ERROR ||
            listen(listenSocket, 1) == SOCKET_ERROR)
        {
            failed = true;
            status = "Host bind/listen failed";
            Log::Msg("[TCP]", status, " on port ", port);
            CloseSocket(listenSocket);
            return;
        }

        u_long nonBlocking = 1;
        ioctlsocket(listenSocket, FIONBIO, &nonBlocking);
        {
            std::lock_guard<std::mutex> lock(mutex);
            status = "Waiting for client on port " + std::to_string(port);
            Log::Msg("[TCP]", status);
        }
        while (running && socket == INVALID_SOCKET)
        {
            socket = accept(listenSocket, nullptr, nullptr);
            if (socket == INVALID_SOCKET && !WouldBlock())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        CloseSocket(listenSocket);
    }
    else
    {
        socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == INVALID_SOCKET)
        {
            failed = true;
            status = "Client socket failed";
            Log::Msg("[TCP]", status);
            return;
        }

        sockaddr_in service{};
        service.sin_family = AF_INET;
        service.sin_port = htons(port);
        inet_pton(AF_INET, address.c_str(), &service.sin_addr);

        if (connect(socket, reinterpret_cast<sockaddr*>(&service), sizeof(service)) == SOCKET_ERROR)
        {
            failed = true;
            status = "Connect failed";
            Log::Msg("[TCP]", status, " to ", address, ":", port);
            CloseSocket(socket);
            return;
        }
    }

    if (socket == INVALID_SOCKET)
    {
        failed = true;
        status = "Socket closed before connection";
        Log::Msg("[TCP]", status);
        return;
    }

    u_long nonBlocking = 1;
    ioctlsocket(socket, FIONBIO, &nonBlocking);
    connected = true;
    {
        std::lock_guard<std::mutex> lock(mutex);
        status = "Connected";
        Log::Msg("[TCP]", status);
    }

    std::string incoming;
    char buffer[2048];
    while (running)
    {
        int received = recv(socket, buffer, sizeof(buffer), 0);
        if (received > 0)
        {
            incoming.append(buffer, buffer + received);
            size_t newline = std::string::npos;
            while ((newline = incoming.find('\n')) != std::string::npos)
            {
                std::string line = incoming.substr(0, newline);
                incoming.erase(0, newline + 1);
                QueueIncomingLine(line);
            }
        }
        else if (received == 0)
        {
            Log::Msg("[TCP]", "Peer closed connection");
            running = false;
            break;
        }
        else if (!WouldBlock())
        {
            Log::Msg("[TCP]", "Socket receive failed");
            running = false;
            failed = true;
            break;
        }

        std::deque<std::string> outgoing;
        {
            std::lock_guard<std::mutex> lock(mutex);
            outgoing.swap(outboundLines);
        }
        while (!outgoing.empty())
        {
            const std::string& line = outgoing.front();
            Log::Msg("[TCP]", "Sending line bytes=", line.size());
            send(socket, line.c_str(), static_cast<int>(line.size()), 0);
            outgoing.pop_front();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    connected = false;
    CloseSocket(socket);
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!failed)
        {
            status = "Disconnected";
            Log::Msg("[TCP]", status);
        }
    }
#endif
}
