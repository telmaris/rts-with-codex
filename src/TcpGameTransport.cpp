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

#include <algorithm>
#include <chrono>
#include <cstdlib>

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

    long long NowMillis()
    {
        using Clock = std::chrono::steady_clock;
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    }
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

void TcpGameTransport::SendHostFrame(const std::string& payload)
{
    if (mode == Mode::Host)
    {
        SendLine("F " + payload);
    }
}

std::vector<std::string> TcpGameTransport::ReceiveClientFrames()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(clientFrames);
}

void TcpGameTransport::SendHostSnapshot(const std::string& payload)
{
    if (mode == Mode::Host)
    {
        Log::Msg("[TCP]", "Queue host snapshot payload bytes=", payload.size());
        SendLine("S " + payload);
    }
}

std::vector<std::string> TcpGameTransport::ReceiveClientSnapshots()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(clientSnapshots);
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
    else if (line[0] == 'F' && line[1] == ' ')
    {
        clientFrames.push_back(line.substr(2));
        while (clientFrames.size() > 120)
            clientFrames.pop_front();
    }
    else if (line[0] == 'L' && line[1] == ' ')
    {
        Log::Msg("[TCP]", "Received lobby message: ", line.substr(2));
        lobbyMessages.push_back(line.substr(2));
    }
    else if (line[0] == 'S' && line[1] == ' ')
    {
        Log::Msg("[TCP]", "Received host snapshot payload");
        clientSnapshots.push_back(line.substr(2));
    }
    else if (line[0] == 'P' && line[1] == ' ')
    {
        outboundLines.push_front("O " + line.substr(2) + "\n");
    }
    else if (line[0] == 'O' && line[1] == ' ')
    {
        try
        {
            long long sentAt = std::stoll(line.substr(2));
            int measuredPing = static_cast<int>(std::max<long long>(0, NowMillis() - sentAt));
            pingMs = measuredPing;
        }
        catch (...)
        {
            pingMs = -1;
        }
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

        u_long nonBlocking = 1;
        ioctlsocket(socket, FIONBIO, &nonBlocking);
        int connectResult = connect(socket, reinterpret_cast<sockaddr*>(&service), sizeof(service));
        if (connectResult == SOCKET_ERROR && !WouldBlock())
        {
            failed = true;
            status = "Connect failed";
            Log::Msg("[TCP]", status, " to ", address, ":", port);
            CloseSocket(socket);
            return;
        }
        if (connectResult == SOCKET_ERROR)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            bool connectedNow = false;
            while (running && std::chrono::steady_clock::now() < deadline)
            {
                fd_set writeSet;
                FD_ZERO(&writeSet);
                FD_SET(socket, &writeSet);
                timeval timeout{};
                timeout.tv_sec = 0;
                timeout.tv_usec = 100000;
                int selected = select(0, nullptr, &writeSet, nullptr, &timeout);
                if (selected > 0 && FD_ISSET(socket, &writeSet))
                {
                    int error = 0;
                    int len = sizeof(error);
                    getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &len);
                    if (error == 0)
                        connectedNow = true;
                    break;
                }
                if (selected == SOCKET_ERROR)
                    break;
            }

            if (!connectedNow)
            {
                failed = true;
                status = "Connect timed out";
                Log::Msg("[TCP]", status, " to ", address, ":", port);
                CloseSocket(socket);
                return;
            }
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
    int noDelay = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&noDelay), sizeof(noDelay));
    connected = true;
    {
        std::lock_guard<std::mutex> lock(mutex);
        status = "Connected";
        Log::Msg("[TCP]", status);
    }

    std::string incoming;
    char buffer[16384];
    auto lastPingSent = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    std::deque<std::string> outgoing;
    std::string activeOutgoingLine;
    size_t activeOutgoingOffset = 0;
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

        auto now = std::chrono::steady_clock::now();
        if (now - lastPingSent >= std::chrono::seconds(1))
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                outboundLines.push_front("P " + std::to_string(NowMillis()) + "\n");
            }
            lastPingSent = now;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            constexpr size_t MaxBufferedLines = 256;
            while (!outboundLines.empty() && outgoing.size() < MaxBufferedLines)
            {
                outgoing.push_back(std::move(outboundLines.front()));
                outboundLines.pop_front();
            }
        }

        constexpr size_t SendBudgetBytes = 64 * 1024;
        size_t sendBudget = SendBudgetBytes;
        while (sendBudget > 0 && running)
        {
            if (activeOutgoingLine.empty())
            {
                if (outgoing.empty())
                    break;
                activeOutgoingLine = std::move(outgoing.front());
                activeOutgoingOffset = 0;
                outgoing.pop_front();
                if (activeOutgoingLine.size() > 2048)
                    Log::Msg("[TCP]", "Sending large line bytes=", activeOutgoingLine.size());
            }

            size_t remaining = activeOutgoingLine.size() - activeOutgoingOffset;
            int requested = static_cast<int>(std::min(remaining, sendBudget));
            int sent = send(socket, activeOutgoingLine.c_str() + activeOutgoingOffset, requested, 0);
            if (sent <= 0)
            {
                if (WouldBlock())
                    break;
                Log::Msg("[TCP]", "Socket send failed");
                failed = true;
                running = false;
                break;
            }

            activeOutgoingOffset += static_cast<size_t>(sent);
            sendBudget -= static_cast<size_t>(sent);
            if (activeOutgoingOffset >= activeOutgoingLine.size())
            {
                activeOutgoingLine.clear();
                activeOutgoingOffset = 0;
            }
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
