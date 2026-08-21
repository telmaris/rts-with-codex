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

#include "multiplayer/TcpGameTransport.h"
#include "core/Log.h"

#include <algorithm>
#include <charconv>
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
    SetStatus("Hosting on port " + std::to_string(port));
    Log::Msg("[TCP]", "Starting host on port ", port);
    worker = std::thread(&TcpGameTransport::NetworkLoop, this);
    return true;
}

bool TcpGameTransport::StartClient(const std::string& hostAddress, unsigned short hostPort)
{
    address = hostAddress;
    port = hostPort;
    running = true;
    SetStatus("Connecting to " + address + ":" + std::to_string(port));
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

void TcpGameTransport::SetStatus(std::string value)
{
    std::lock_guard<std::mutex> lock(mutex);
    status = std::move(value);
}

void TcpGameTransport::SendClientCommand(const std::string& payload)
{
    if (mode == Mode::Client)
    {
        Log::Msg("[TCP]", "Queue client command payload");
        SendFrame(NetworkMessageType::ClientCommand, NetworkChannel::Command, payload);
    }
}

std::vector<std::string> TcpGameTransport::ReceiveHostCommands()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(hostCommands, hostCommandsBytes);
}

void TcpGameTransport::SendHostResult(const std::string& payload)
{
    if (mode == Mode::Host)
    {
        Log::Msg("[TCP]", "Queue host result payload");
        SendFrame(NetworkMessageType::CommandResult, NetworkChannel::Event, payload);
    }
}

std::vector<std::string> TcpGameTransport::ReceiveClientResults()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(clientResults, clientResultsBytes);
}

void TcpGameTransport::SendHostFrame(const std::string& payload)
{
    if (mode == Mode::Host)
    {
        SendFrame(NetworkMessageType::ServerFrame, NetworkChannel::Event, payload);
    }
}

std::vector<std::string> TcpGameTransport::ReceiveClientFrames()
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<std::string> result = Drain(clientReliableFrames, clientReliableFramesBytes);
    if (clientLatestFrame.has_value())
    {
        result.push_back(std::move(*clientLatestFrame));
        clientLatestFrame.reset();
        clientLatestFrameBytes = 0;
    }
    return result;
}

void TcpGameTransport::SendHostSnapshot(const std::string& payload)
{
    if (mode == Mode::Host)
    {
        Log::Msg("[TCP]", "Queue host snapshot payload bytes=", payload.size());
        // The current session envelope remains textual for compatibility with
        // GameSession, while the outer protocol makes its begin/chunk/end
        // boundaries explicit and imposes a strict per-frame payload limit.
        NetworkMessageType type = NetworkMessageType::SnapshotChunk;
        if (payload.rfind("INIT_BEGIN ", 0) == 0)
            type = NetworkMessageType::SnapshotBegin;
        else if (payload == "INIT_END")
            type = NetworkMessageType::SnapshotEnd;
        SendFrame(type, NetworkChannel::Snapshot, payload);
    }
}

std::vector<std::string> TcpGameTransport::ReceiveClientSnapshots()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(clientSnapshots, clientSnapshotsBytes);
}

void TcpGameTransport::SendLobbyMessage(const std::string& payload)
{
    Log::Msg("[TCP]", "Queue lobby message: ", payload);
    SendFrame(NetworkMessageType::LobbyChat, NetworkChannel::Lobby, payload);
}

std::vector<std::string> TcpGameTransport::ReceiveLobbyMessages()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(lobbyMessages, lobbyMessagesBytes);
}

bool TcpGameTransport::SendFrame(NetworkMessageType type, NetworkChannel channel, const std::string& payload,
                                 bool priority)
{
    std::lock_guard<std::mutex> lock(mutex);
    NetworkFrame frame;
    frame.type = type;
    frame.channel = channel;
    frame.sequence = nextOutboundSequence++;
    frame.payload = payload;

    std::string encoded;
    std::string error;
    if (!NetworkProtocolCodec::Encode(frame, encoded, error))
    {
        failed = true;
        running = false;
        status = "Protocol encode failed: " + error;
        Log::Msg("[TCP]", status);
        return false;
    }

    constexpr std::size_t MaxOutboundFrames = 1024;
    constexpr std::size_t MaxOutboundBytes = 8u * 1024u * 1024u;
    if (outboundFrames.size() >= MaxOutboundFrames ||
        encoded.size() > MaxOutboundBytes - std::min(outboundBytes, MaxOutboundBytes))
    {
        failed = true;
        running = false;
        status = "Outbound queue budget exceeded; connection closed";
        Log::Msg("[TCP]", status);
        return false;
    }

    outboundBytes += encoded.size();
    // A connection has one global sequence stream. Reordering a priority
    // frame ahead of already queued data would make the peer observe a gap
    // even though every frame is present. Keep wire order identical to the
    // sequence order; priority may still be used by callers as a semantic
    // hint, but cannot violate transport ordering.
    (void)priority;
    outboundFrames.push_back(std::move(encoded));
    return true;
}

bool TcpGameTransport::QueueBounded(std::deque<std::string>& queue, std::size_t& queuedBytes,
                                    std::string payload)
{
    constexpr std::size_t MaxQueuedFrames = 1024;
    constexpr std::size_t MaxQueuedBytes = 8u * 1024u * 1024u;
    if (queue.size() >= MaxQueuedFrames ||
        payload.size() > MaxQueuedBytes - std::min(queuedBytes, MaxQueuedBytes))
    {
        failed = true;
        running = false;
        status = "Inbound queue budget exceeded; connection closed";
        Log::Msg("[TCP]", status);
        return false;
    }
    queuedBytes += payload.size();
    queue.push_back(std::move(payload));
    return true;
}

std::vector<std::string> TcpGameTransport::Drain(std::deque<std::string>& queue, std::size_t& queuedBytes)
{
    std::vector<std::string> result;
    while (!queue.empty())
    {
        queuedBytes -= std::min(queuedBytes, queue.front().size());
        result.push_back(std::move(queue.front()));
        queue.pop_front();
    }
    return result;
}

void TcpGameTransport::QueueIncomingFrame(const NetworkFrame& frame)
{
    if (!inboundSequence.Accept(frame.sequence))
    {
        failed = true;
        running = false;
        SetStatus("Protocol sequence gap or duplicate; connection closed");
        Log::Msg("[TCP] ", GetStatus());
        return;
    }

    if (frame.type == NetworkMessageType::ClientCommand)
    {
        Log::Msg("[TCP]", "Received client command payload");
        std::lock_guard<std::mutex> lock(mutex);
        QueueBounded(hostCommands, hostCommandsBytes, frame.payload);
    }
    else if (frame.type == NetworkMessageType::CommandResult)
    {
        Log::Msg("[TCP]", "Received host result payload");
        std::lock_guard<std::mutex> lock(mutex);
        QueueBounded(clientResults, clientResultsBytes, frame.payload);
    }
    else if (frame.type == NetworkMessageType::ServerFrame)
    {
        GameServerFrame serverFrame;
        if (!GameServerFrame::TryDeserialize(frame.payload, serverFrame) || !serverFrame.results.empty())
        {
            std::lock_guard<std::mutex> lock(mutex);
            QueueBounded(clientReliableFrames, clientReliableFramesBytes, frame.payload);
        }
        else
        {
            std::lock_guard<std::mutex> lock(mutex);
            clientLatestFrameBytes = frame.payload.size();
            clientLatestFrame = frame.payload;
        }
    }
    else if (frame.channel == NetworkChannel::Lobby)
    {
        Log::Msg("[TCP]", "Received lobby message");
        std::lock_guard<std::mutex> lock(mutex);
        QueueBounded(lobbyMessages, lobbyMessagesBytes, frame.payload);
    }
    else if (frame.channel == NetworkChannel::Snapshot)
    {
        Log::Msg("[TCP]", "Received host snapshot payload");
        std::lock_guard<std::mutex> lock(mutex);
        QueueBounded(clientSnapshots, clientSnapshotsBytes, frame.payload);
    }
    else if (frame.type == NetworkMessageType::Ping)
    {
        SendFrame(NetworkMessageType::Pong, NetworkChannel::Control, frame.payload, true);
    }
    else if (frame.type == NetworkMessageType::Pong)
    {
        long long sentAt = 0;
        const char* first = frame.payload.data();
        const char* last = first + frame.payload.size();
        const auto parsed = std::from_chars(first, last, sentAt);
        if (parsed.ec == std::errc{} && parsed.ptr == last)
        {
            int measuredPing = static_cast<int>(std::max<long long>(0, NowMillis() - sentAt));
            pingMs = measuredPing;
        }
        else
            pingMs = -1;
    }
}

void TcpGameTransport::NetworkLoop()
{
#ifndef _WIN32
        failed = true;
        SetStatus("TCP transport is currently implemented for Windows");
        Log::Msg("[TCP]", GetStatus());
#else
    if (!EnsureWinsock())
    {
        failed = true;
        SetStatus("WSAStartup failed");
        Log::Msg("[TCP]", GetStatus());
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
            SetStatus("Host socket failed");
            Log::Msg("[TCP]", GetStatus());
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
            SetStatus("Host bind/listen failed");
            Log::Msg("[TCP]", GetStatus(), " on port ", port);
            CloseSocket(listenSocket);
            return;
        }

        u_long nonBlocking = 1;
        ioctlsocket(listenSocket, FIONBIO, &nonBlocking);
        {
            SetStatus("Waiting for client on port " + std::to_string(port));
            Log::Msg("[TCP]", GetStatus());
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
            SetStatus("Client socket failed");
            Log::Msg("[TCP]", GetStatus());
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
            SetStatus("Connect failed");
            Log::Msg("[TCP]", GetStatus(), " to ", address, ":", port);
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
                SetStatus("Connect timed out");
                Log::Msg("[TCP]", GetStatus(), " to ", address, ":", port);
                CloseSocket(socket);
                return;
            }
        }
    }

    if (socket == INVALID_SOCKET)
    {
        failed = true;
        SetStatus("Socket closed before connection");
        Log::Msg("[TCP]", GetStatus());
        return;
    }

    u_long nonBlocking = 1;
    ioctlsocket(socket, FIONBIO, &nonBlocking);
    int noDelay = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&noDelay), sizeof(noDelay));
    connected = true;
    SetStatus("Connected");
    Log::Msg("[TCP]", GetStatus());

    NetworkFrameDecoder decoder;
    std::vector<NetworkFrame> receivedFrames;
    char buffer[16384];
    auto lastPingSent = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    std::deque<std::string> outgoing;
    std::string activeOutgoingFrame;
    size_t activeOutgoingOffset = 0;
    while (running)
    {
        int received = recv(socket, buffer, sizeof(buffer), 0);
        if (received > 0)
        {
            receivedFrames.clear();
            FrameDecodeResult decodeResult = decoder.Push(
                std::string_view(buffer, static_cast<size_t>(received)), receivedFrames);
            if (!decodeResult)
            {
                Log::Msg("[TCP]", "Protocol decode failed, error=", static_cast<int>(decodeResult.error));
                failed = true;
                running = false;
                break;
            }
            for (const NetworkFrame& receivedFrame : receivedFrames)
                QueueIncomingFrame(receivedFrame);
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
            SendFrame(NetworkMessageType::Ping, NetworkChannel::Control, std::to_string(NowMillis()), true);
            lastPingSent = now;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            constexpr size_t MaxBufferedFrames = 256;
            while (!outboundFrames.empty() && outgoing.size() < MaxBufferedFrames)
            {
                outboundBytes -= std::min(outboundBytes, outboundFrames.front().size());
                outgoing.push_back(std::move(outboundFrames.front()));
                outboundFrames.pop_front();
            }
        }

        constexpr size_t SendBudgetBytes = 64 * 1024;
        size_t sendBudget = SendBudgetBytes;
        while (sendBudget > 0 && running)
        {
            if (activeOutgoingFrame.empty())
            {
                if (outgoing.empty())
                    break;
                activeOutgoingFrame = std::move(outgoing.front());
                activeOutgoingOffset = 0;
                outgoing.pop_front();
                if (activeOutgoingFrame.size() > 2048)
                    Log::Msg("[TCP]", "Sending large frame bytes=", activeOutgoingFrame.size());
            }

            size_t remaining = activeOutgoingFrame.size() - activeOutgoingOffset;
            int requested = static_cast<int>(std::min(remaining, sendBudget));
            int sent = send(socket, activeOutgoingFrame.data() + activeOutgoingOffset, requested, 0);
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
            if (activeOutgoingOffset >= activeOutgoingFrame.size())
            {
                activeOutgoingFrame.clear();
                activeOutgoingOffset = 0;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    connected = false;
    CloseSocket(socket);
    {
        if (!failed)
        {
            SetStatus("Disconnected");
            Log::Msg("[TCP]", GetStatus());
        }
    }
#endif
}
