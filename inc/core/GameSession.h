#ifndef GAME_SESSION_H
#define GAME_SESSION_H

#include "core/GameWorld.h"

#include <cstdint>
#include <deque>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Minimal transport contract for command/result payloads.
class IGameTransport
{
public:
    virtual ~IGameTransport() = default;
    virtual void SendClientCommand(const std::string& payload) = 0;
    virtual std::vector<std::string> ReceiveHostCommands() = 0;
    virtual void SendHostResult(const std::string& payload) = 0;
    virtual std::vector<std::string> ReceiveClientResults() = 0;
    virtual void SendHostFrame(const std::string& payload) { (void)payload; }
    virtual std::vector<std::string> ReceiveClientFrames() { return {}; }
    // Reserved for compact/chunked snapshot payloads. Do not stream full maps over TCP.
    virtual void SendHostSnapshot(const std::string& payload) { (void)payload; }
    virtual std::vector<std::string> ReceiveClientSnapshots() { return {}; }
    virtual bool IsConnected() const { return true; }
    virtual bool HasFailed() const { return false; }
    virtual std::string GetStatus() const { return {}; }
    virtual int GetPingMs() const { return -1; }
};

// In-process localhost transport used to prototype multiplayer flow before Steam/sockets.
class LocalhostGameTransport : public IGameTransport
{
public:
    void SendClientCommand(const std::string& payload) override;
    std::vector<std::string> ReceiveHostCommands() override;
    void SendHostResult(const std::string& payload) override;
    std::vector<std::string> ReceiveClientResults() override;
    void SendHostFrame(const std::string& payload) override;
    std::vector<std::string> ReceiveClientFrames() override;
    void SendHostSnapshot(const std::string& payload) override;
    std::vector<std::string> ReceiveClientSnapshots() override;

private:
    static std::vector<std::string> Drain(std::deque<std::string>& queue);

    std::deque<std::string> clientToHost;
    std::deque<std::string> hostToClient;
    std::deque<std::string> hostFrames;
    std::deque<std::string> hostSnapshots;
};

// Abstracts the authority that advances a game world.
class IGameSession
{
public:
    virtual ~IGameSession() = default;
    virtual std::uint64_t SubmitCommand(const GameCommand& command) = 0;
    virtual void Update(double dt) = 0;
    virtual GameWorld* GetWorld() = 0;
    virtual std::vector<GameCommandResult> ConsumeCommandResults() = 0;
    virtual bool ConsumeLatestSnapshot(GameSnapshot& snapshot) { (void)snapshot; return false; }
    virtual bool IsConnectionClosed() const { return false; }
    virtual std::string GetConnectionStatus() const { return {}; }
    virtual int GetPingMs() const { return -1; }
    virtual bool IsReadyForGameplay() const { return true; }
    virtual std::recursive_mutex* GetWorldMutex() { return nullptr; }
};

struct FixedSimulationClock
{
    static constexpr double FixedDt = 1.0 / 100.0;
    static constexpr int MaxTicksPerUpdate = 12;

    double accumulator{0.0};

    int AddFrameTime(double dt)
    {
        accumulator += std::clamp(dt, 0.0, 0.25);
        int ticks = 0;
        while (accumulator >= FixedDt && ticks < MaxTicksPerUpdate)
        {
            accumulator -= FixedDt;
            ticks++;
        }
        if (ticks == MaxTicksPerUpdate && accumulator >= FixedDt)
            accumulator = FixedDt - 0.000001;
        return ticks;
    }
};

// Authoritative host session. Multiplayer transports can feed commands before this tick.
class HostGameSession : public IGameSession
{
public:
    explicit HostGameSession(GameWorld& world);

    std::uint64_t SubmitCommand(const GameCommand& command) override;
    void Update(double dt) override;
    GameWorld* GetWorld() override;
    std::vector<GameCommandResult> ConsumeCommandResults() override;

protected:
    GameWorld* world{nullptr};
    FixedSimulationClock clock;
    std::uint64_t inputDelayTicks{1};
    std::vector<GameCommandResult> commandResults;
};

// Single player uses the same authoritative hosted simulation as multiplayer.
class LocalSinglePlayerSession : public HostGameSession
{
public:
    using HostGameSession::HostGameSession;
};

// Authoritative multiplayer host that receives serialized commands from a transport.
class LocalhostHostSession : public IGameSession
{
public:
    LocalhostHostSession(GameWorld& world, std::shared_ptr<IGameTransport> transport, int remotePlayerId = 0, bool requireRemoteSync = true);

    std::uint64_t SubmitCommand(const GameCommand& command) override;
    void Update(double dt) override;
    GameWorld* GetWorld() override;
    bool IsConnectionClosed() const override;
    int GetPingMs() const override;
    std::string GetConnectionStatus() const override;
    bool IsReadyForGameplay() const override;
    std::vector<GameCommandResult> ConsumeCommandResults() override;

private:
    void SendInitialSnapshot();
    void SendCorrectionSnapshot();

    GameWorld* world{nullptr};
    std::shared_ptr<IGameTransport> transport;
    FixedSimulationClock clock;
    std::uint64_t inputDelayTicks{1};
    int remotePlayerId{0};
    bool hadConnection{false};
    bool initialSnapshotSent{false};
    bool remoteInitialSnapshotReady{false};
    bool requireRemoteSync{true};
    bool hasLastSentSnapshot{false};
    GameSnapshot lastSentSnapshot;
    double checksumTimer{0.0};
    double correctionSnapshotCooldown{0.0};
    std::vector<GameCommandResult> commandResults;
};

// Prototype local client. It sends serialized commands and observes a local world mirror.
class LocalhostClientSession : public IGameSession
{
public:
    LocalhostClientSession(GameWorld* observedWorld, std::shared_ptr<IGameTransport> transport, int assignedPlayerId = 0);

    std::uint64_t SubmitCommand(const GameCommand& command) override;
    void Update(double dt) override;
    GameWorld* GetWorld() override;
    bool ConsumeLatestSnapshot(GameSnapshot& snapshot) override;
    bool IsConnectionClosed() const override;
    int GetPingMs() const override;
    std::string GetConnectionStatus() const override;
    bool IsReadyForGameplay() const override;
    std::vector<GameCommandResult> ConsumeCommandResults() override;

private:
    void HandleSnapshotPayload(const std::string& payload);

    GameWorld* observedWorld{nullptr};
    std::shared_ptr<IGameTransport> transport;
    std::uint64_t nextClientCommandId{1};
    int assignedPlayerId{0};
    bool hadConnection{false};
    FixedSimulationClock clock;
    GameSnapshot latestNetworkSnapshot;
    bool hasNetworkSnapshot{false};
    bool initialSnapshotReceived{false};
    std::string syncStatus{"Waiting for map sync"};
    size_t expectedInitialSnapshotBytes{0};
    size_t expectedInitialSnapshotChunks{0};
    size_t receivedInitialSnapshotChunks{0};
    std::string initialSnapshotBuffer;
    std::vector<std::string> initialSnapshotChunks;
    std::vector<bool> initialSnapshotChunkReceived;
    double resyncRequestCooldown{0.0};
    std::vector<GameCommandResult> commandResults;
};

// Runs a localhost client and authoritative host in one process for transport tests/prototyping.
class LocalhostMultiplayerSession : public IGameSession
{
public:
    explicit LocalhostMultiplayerSession(GameWorld& world);

    std::uint64_t SubmitCommand(const GameCommand& command) override;
    void Update(double dt) override;
    GameWorld* GetWorld() override;
    std::vector<GameCommandResult> ConsumeCommandResults() override;

    LocalhostHostSession& GetHostSession();
    LocalhostClientSession& GetClientSession();

private:
    std::shared_ptr<IGameTransport> transport;
    LocalhostHostSession host;
    LocalhostClientSession client;
};

// Runs an existing session's fixed-tick simulation on a background thread.
class ThreadedGameSession : public IGameSession
{
public:
    explicit ThreadedGameSession(std::unique_ptr<IGameSession> innerSession);
    ~ThreadedGameSession() override;

    ThreadedGameSession(const ThreadedGameSession&) = delete;
    ThreadedGameSession& operator=(const ThreadedGameSession&) = delete;

    std::uint64_t SubmitCommand(const GameCommand& command) override;
    void Update(double dt) override;
    GameWorld* GetWorld() override;
    std::vector<GameCommandResult> ConsumeCommandResults() override;
    bool ConsumeLatestSnapshot(GameSnapshot& snapshot) override;
    bool IsConnectionClosed() const override;
    std::string GetConnectionStatus() const override;
    int GetPingMs() const override;
    bool IsReadyForGameplay() const override;
    std::recursive_mutex* GetWorldMutex() override;

private:
    void Stop();
    void RunSimulation();

    std::unique_ptr<IGameSession> inner;
    mutable std::recursive_mutex worldMutex;
    std::atomic<bool> running{false};
    std::thread worker;
    std::mutex sleepMutex;
    std::condition_variable cv;
    std::mutex snapshotMutex;
    GameSnapshot latestSnapshot;
    bool hasSnapshot{false};
};

#endif
