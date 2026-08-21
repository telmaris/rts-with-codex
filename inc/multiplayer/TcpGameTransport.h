#ifndef TCP_GAME_TRANSPORT_H
#define TCP_GAME_TRANSPORT_H

#include "core/GameSession.h"
#include "multiplayer/NetworkProtocol.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <cstddef>
#include <string>
#include <thread>

class TcpGameTransport : public IGameTransport
{
public:
    enum class Mode
    {
        Host,
        Client
    };

    ~TcpGameTransport() override;

    static std::shared_ptr<TcpGameTransport> CreateHost(unsigned short port);
    static std::shared_ptr<TcpGameTransport> CreateClient(const std::string& address, unsigned short port);

    void SendClientCommand(const std::string& payload) override;
    std::vector<std::string> ReceiveHostCommands() override;
    void SendHostResult(const std::string& payload) override;
    std::vector<std::string> ReceiveClientResults() override;
    void SendHostFrame(const std::string& payload) override;
    std::vector<std::string> ReceiveClientFrames() override;
    void SendHostSnapshot(const std::string& payload) override;
    std::vector<std::string> ReceiveClientSnapshots() override;
    void SendLobbyMessage(const std::string& payload);
    std::vector<std::string> ReceiveLobbyMessages();

    bool IsConnected() const override { return connected; }
    bool HasFailed() const override { return failed; }
    std::string GetStatus() const override;
    int GetPingMs() const override { return pingMs; }

private:
    explicit TcpGameTransport(Mode mode);

    bool StartHost(unsigned short port);
    bool StartClient(const std::string& address, unsigned short port);
    void Stop();
    void NetworkLoop();
    void QueueIncomingFrame(const NetworkFrame& frame);
    bool SendFrame(NetworkMessageType type, NetworkChannel channel, const std::string& payload,
                   bool priority = false);
    bool QueueBounded(std::deque<std::string>& queue, std::size_t& queuedBytes, std::string payload);
    std::vector<std::string> Drain(std::deque<std::string>& queue, std::size_t& queuedBytes);
    void SetStatus(std::string value);

    Mode mode;
    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};
    std::atomic<bool> failed{false};
    std::thread worker;
    mutable std::mutex mutex;
    std::deque<std::string> hostCommands;
    std::deque<std::string> clientResults;
    // Frames carrying command results are reliable events and are never
    // coalesced. Tick/checksum-only frames are a latest-value stream.
    std::deque<std::string> clientReliableFrames;
    std::optional<std::string> clientLatestFrame;
    std::deque<std::string> clientSnapshots;
    std::deque<std::string> lobbyMessages;
    std::deque<std::string> outboundFrames;
    std::size_t outboundBytes{0};
    std::size_t hostCommandsBytes{0};
    std::size_t clientResultsBytes{0};
    std::size_t clientReliableFramesBytes{0};
    std::size_t clientLatestFrameBytes{0};
    std::size_t clientSnapshotsBytes{0};
    std::size_t lobbyMessagesBytes{0};
    std::uint64_t nextOutboundSequence{1};
    NetworkSequenceTracker inboundSequence;
    std::string status{"Idle"};
    std::atomic<int> pingMs{-1};

    unsigned short port{0};
    std::string address;
};

#endif
