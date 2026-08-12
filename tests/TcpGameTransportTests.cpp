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

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <iterator>
#include <thread>

namespace
{
#ifdef _WIN32
    class WinsockScope
    {
    public:
        WinsockScope()
        {
            WSADATA data{};
            valid = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }

        ~WinsockScope()
        {
            if (valid)
                WSACleanup();
        }

        bool valid{false};
    };

    unsigned short FindFreeLoopbackPort()
    {
        SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socketHandle == INVALID_SOCKET)
            return 0;

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        {
            closesocket(socketHandle);
            return 0;
        }

        int length = sizeof(address);
        if (getsockname(socketHandle, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR)
        {
            closesocket(socketHandle);
            return 0;
        }

        closesocket(socketHandle);
        return ntohs(address.sin_port);
    }
#endif

    bool WaitFor(const std::function<bool()>& predicate,
                 std::chrono::milliseconds timeout = std::chrono::seconds(2))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return predicate();
    }
}

TEST(TcpGameTransportTests, TransfersTypedFramesOverLoopbackWithoutNewlineFraming)
{
#ifndef _WIN32
    GTEST_SKIP() << "TcpGameTransport is implemented only for Windows.";
#else
    WinsockScope winsock;
    ASSERT_TRUE(winsock.valid);
    const unsigned short port = FindFreeLoopbackPort();
    ASSERT_NE(port, 0);

    auto host = TcpGameTransport::CreateHost(port);
    ASSERT_TRUE(WaitFor([&host]() { return host->GetStatus().starts_with("Waiting for client"); }));

    auto client = TcpGameTransport::CreateClient("127.0.0.1", port);
    ASSERT_TRUE(WaitFor([&host, &client]() { return host->IsConnected() && client->IsConnected(); }));
    ASSERT_FALSE(host->HasFailed()) << host->GetStatus();
    ASSERT_FALSE(client->HasFailed()) << client->GetStatus();

    const std::string command{"command\nwith\0binary", 19};
    client->SendClientCommand(command);
    std::vector<std::string> hostCommands;
    ASSERT_TRUE(WaitFor([&]()
    {
        auto received = host->ReceiveHostCommands();
        hostCommands.insert(hostCommands.end(), std::make_move_iterator(received.begin()), std::make_move_iterator(received.end()));
        return !hostCommands.empty();
    }));
    EXPECT_EQ(hostCommands, (std::vector<std::string>{command}));

    GameServerFrame frame;
    frame.tick = 77;
    frame.checksum = 0xAABBCCDDu;
    const std::string framePayload = frame.Serialize();
    host->SendHostFrame(framePayload);
    std::vector<std::string> clientFrames;
    ASSERT_TRUE(WaitFor([&]()
    {
        auto received = client->ReceiveClientFrames();
        clientFrames.insert(clientFrames.end(), std::make_move_iterator(received.begin()), std::make_move_iterator(received.end()));
        return !clientFrames.empty();
    }));
    EXPECT_EQ(clientFrames, (std::vector<std::string>{framePayload}));

    const std::string snapshot{"INIT_CHUNK 0 snapshot\nbytes", 27};
    host->SendHostSnapshot(snapshot);
    std::vector<std::string> snapshots;
    ASSERT_TRUE(WaitFor([&]()
    {
        auto received = client->ReceiveClientSnapshots();
        snapshots.insert(snapshots.end(), std::make_move_iterator(received.begin()), std::make_move_iterator(received.end()));
        return !snapshots.empty();
    }));
    EXPECT_EQ(snapshots, (std::vector<std::string>{snapshot}));
#endif
}
