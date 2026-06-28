#ifndef GAME_SESSION_H
#define GAME_SESSION_H

#include "GameWorld.h"

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
    void SendClientCommand(const std::string& payload) override
    {
        clientToHost.push_back(payload);
    }

    std::vector<std::string> ReceiveHostCommands() override
    {
        return Drain(clientToHost);
    }

    void SendHostResult(const std::string& payload) override
    {
        hostToClient.push_back(payload);
    }

    std::vector<std::string> ReceiveClientResults() override
    {
        return Drain(hostToClient);
    }

    void SendHostFrame(const std::string& payload) override
    {
        hostFrames.push_back(payload);
    }

    std::vector<std::string> ReceiveClientFrames() override
    {
        return Drain(hostFrames);
    }

    void SendHostSnapshot(const std::string& payload) override
    {
        hostSnapshots.push_back(payload);
    }

    std::vector<std::string> ReceiveClientSnapshots() override
    {
        return Drain(hostSnapshots);
    }

private:
    static std::vector<std::string> Drain(std::deque<std::string>& queue)
    {
        std::vector<std::string> result;
        while (!queue.empty())
        {
            result.push_back(std::move(queue.front()));
            queue.pop_front();
        }
        return result;
    }

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
    explicit HostGameSession(GameWorld& world) : world(&world) {}

    std::uint64_t SubmitCommand(const GameCommand& command) override
    {
        if (world != nullptr)
            return world->SubmitCommand(command, world->GetSimulationTick() + inputDelayTicks);
        return 0;
    }

    void Update(double dt) override
    {
        if (world != nullptr)
        {
            int ticks = clock.AddFrameTime(dt);
            for (int i = 0; i < ticks; i++)
            {
                world->UpdateSimulation(FixedSimulationClock::FixedDt);
                auto results = world->ConsumeCommandResults();
                commandResults.insert(commandResults.end(), results.begin(), results.end());
            }
        }
    }

    GameWorld* GetWorld() override { return world; }

    std::vector<GameCommandResult> ConsumeCommandResults() override
    {
        std::vector<GameCommandResult> results = std::move(commandResults);
        commandResults.clear();
        return results;
    }

private:
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
    LocalhostHostSession(GameWorld& world, std::shared_ptr<IGameTransport> transport, int remotePlayerId = 0, bool requireRemoteSync = true)
    : world(&world), transport(std::move(transport)), remotePlayerId(remotePlayerId), requireRemoteSync(requireRemoteSync)
    {
    }

    std::uint64_t SubmitCommand(const GameCommand& command) override
    {
        if (world == nullptr)
            return 0;
        return world->SubmitCommand(command, world->GetSimulationTick() + inputDelayTicks);
    }

    void Update(double dt) override
    {
        if (world == nullptr)
            return;

        std::uint64_t minimumTargetTick = world->GetSimulationTick() + inputDelayTicks;
        if (correctionSnapshotCooldown > 0.0)
            correctionSnapshotCooldown = std::max(0.0, correctionSnapshotCooldown - dt);
        if (transport != nullptr)
        {
            hadConnection = hadConnection || transport->IsConnected();
            if (requireRemoteSync && !initialSnapshotSent && transport->IsConnected())
                SendInitialSnapshot();
            for (const auto& payload : transport->ReceiveHostCommands())
            {
                if (payload == "RESYNC_REQUEST")
                {
                    if (correctionSnapshotCooldown <= 0.0)
                    {
                        SendCorrectionSnapshot();
                        correctionSnapshotCooldown = 5.0;
                    }
                    else
                    {
                        Log::Msg("[Session]", "Ignoring resync request during cooldown");
                    }
                    continue;
                }

                if (payload == "SYNC_READY")
                {
                    remoteInitialSnapshotReady = true;
                    Log::Msg("[Session]", "Remote client confirmed initial map sync");
                    continue;
                }

                GameCommand command;
                if (GameCommand::TryDeserialize(payload, command))
                {
                    if (command.playerId != remotePlayerId)
                    {
                        GameCommandResult rejected{
                            command.commandId,
                            world->GetSimulationTick(),
                            command.targetTick,
                            command.playerId,
                            command.type,
                            false,
                            "rejected: wrong player slot",
                            command.Serialize()};
                        commandResults.push_back(rejected);
                        transport->SendHostResult(rejected.Serialize());
                        continue;
                    }
                    world->SubmitCommand(command, minimumTargetTick);
                }
            }

            if (requireRemoteSync && !remoteInitialSnapshotReady)
                return;
        }

        int ticks = clock.AddFrameTime(dt);
        for (int i = 0; i < ticks; i++)
        {
            world->UpdateSimulation(FixedSimulationClock::FixedDt);
            auto results = world->ConsumeCommandResults();
            GameServerFrame frame;
            frame.tick = world->GetSimulationTick();
            checksumTimer += FixedSimulationClock::FixedDt;
            if (checksumTimer >= 1.0)
            {
                checksumTimer = 0.0;
                frame.hasChecksum = true;
                frame.checksum = world->BuildChecksum();
            }

            for (const auto& result : results)
            {
                commandResults.push_back(result);
                frame.results.push_back(result);
            }

            if (transport != nullptr)
                transport->SendHostFrame(frame.Serialize());
        }
    }

    GameWorld* GetWorld() override { return world; }

    bool IsConnectionClosed() const override
    {
        return transport != nullptr && hadConnection && (!transport->IsConnected() || transport->HasFailed());
    }

    int GetPingMs() const override
    {
        return transport != nullptr ? transport->GetPingMs() : -1;
    }

    std::string GetConnectionStatus() const override
    {
        if (transport != nullptr && requireRemoteSync && !remoteInitialSnapshotReady)
            return initialSnapshotSent ? "Waiting for client map sync" : "Preparing map sync";
        return transport != nullptr ? transport->GetStatus() : std::string{};
    }

    bool IsReadyForGameplay() const override
    {
        return transport == nullptr || !requireRemoteSync || remoteInitialSnapshotReady;
    }

    std::vector<GameCommandResult> ConsumeCommandResults() override
    {
        std::vector<GameCommandResult> results = std::move(commandResults);
        commandResults.clear();
        return results;
    }

private:
    void SendInitialSnapshot()
    {
        if (world == nullptr || transport == nullptr)
            return;

        lastSentSnapshot = world->BuildSnapshot();
        hasLastSentSnapshot = lastSentSnapshot.IsValid();
        std::string payload = lastSentSnapshot.Serialize();
        constexpr size_t ChunkSize = 12000;
        size_t totalChunks = payload.empty() ? 0 : (payload.size() + ChunkSize - 1) / ChunkSize;
        transport->SendHostSnapshot("INIT_BEGIN " + std::to_string(world->GetSimulationTick()) + " " +
                                    std::to_string(payload.size()) + " " + std::to_string(totalChunks));
        for (size_t i = 0; i < totalChunks; i++)
        {
            size_t offset = i * ChunkSize;
            transport->SendHostSnapshot("INIT_CHUNK " + std::to_string(i) + " " + payload.substr(offset, ChunkSize));
        }
        transport->SendHostSnapshot("INIT_END");
        initialSnapshotSent = true;
        Log::Msg("[Session]", "Initial snapshot queued: bytes=", payload.size(), " chunks=", totalChunks);
    }

    void SendCorrectionSnapshot()
    {
        if (world == nullptr || transport == nullptr)
            return;

        std::string payload = world->BuildSnapshot().Serialize();
        transport->SendHostSnapshot(payload);
        Log::Msg("[Session]", "Correction snapshot queued: bytes=", payload.size());
    }

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
    LocalhostClientSession(GameWorld* observedWorld, std::shared_ptr<IGameTransport> transport, int assignedPlayerId = 0)
    : observedWorld(observedWorld), transport(std::move(transport)), assignedPlayerId(assignedPlayerId)
    {
    }

    std::uint64_t SubmitCommand(const GameCommand& command) override
    {
        GameCommand outbound = command;
        outbound.playerId = assignedPlayerId;
        if (outbound.commandId == 0)
            outbound.commandId = nextClientCommandId++;
        if (transport != nullptr)
            transport->SendClientCommand(outbound.Serialize());
        return outbound.commandId;
    }

    void Update(double dt) override
    {
        if (transport == nullptr)
            return;

        if (resyncRequestCooldown > 0.0)
            resyncRequestCooldown = std::max(0.0, resyncRequestCooldown - dt);

        hadConnection = hadConnection || transport->IsConnected();
        for (const auto& payload : transport->ReceiveClientFrames())
        {
            GameServerFrame frame;
            if (!GameServerFrame::TryDeserialize(payload, frame))
                continue;

            for (const auto& result : frame.results)
            {
                if (result.accepted && observedWorld != nullptr && !result.commandPayload.empty())
                {
                    GameCommand command;
                    if (GameCommand::TryDeserialize(result.commandPayload, command))
                    {
                        if (observedWorld->GetSimulationTick() < result.simulationTick)
                            observedWorld->SubmitCommand(command, command.targetTick);
                        else
                            observedWorld->ApplyAuthoritativeCommand(command);
                    }
                }
                commandResults.push_back(result);
            }

            if (observedWorld != nullptr && initialSnapshotReceived)
            {
                while (observedWorld->GetSimulationTick() < frame.tick)
                {
                    observedWorld->UpdateSimulation(FixedSimulationClock::FixedDt);
                    observedWorld->ConsumeCommandResults();
                }

                if (frame.hasChecksum)
                {
                    std::uint64_t localChecksum = observedWorld->BuildChecksum();
                    if (localChecksum != frame.checksum)
                    {
                        syncStatus = "Desync detected, requesting snapshot";
                        if (resyncRequestCooldown <= 0.0)
                        {
                            transport->SendClientCommand("RESYNC_REQUEST");
                            resyncRequestCooldown = 5.0;
                            Log::Msg("[Session]", "Checksum mismatch: local=", localChecksum, " host=", frame.checksum, " requesting snapshot");
                        }
                        else
                        {
                            Log::Msg("[Session]", "Checksum mismatch during resync cooldown: local=", localChecksum, " host=", frame.checksum);
                        }
                    }
                }
            }
        }

        for (const auto& payload : transport->ReceiveClientResults())
        {
            GameCommandResult result;
            if (GameCommandResult::TryDeserialize(payload, result))
            {
                if (result.accepted && observedWorld != nullptr && !result.commandPayload.empty())
                {
                    GameCommand command;
                    if (GameCommand::TryDeserialize(result.commandPayload, command))
                    {
                        if (observedWorld->GetSimulationTick() < result.simulationTick)
                            observedWorld->SubmitCommand(command, command.targetTick);
                        else
                            observedWorld->ApplyAuthoritativeCommand(command);
                    }
                }
                commandResults.push_back(std::move(result));
            }
        }

        for (const auto& payload : transport->ReceiveClientSnapshots())
        {
            HandleSnapshotPayload(payload);
        }

        (void)dt;
    }

    GameWorld* GetWorld() override { return observedWorld; }

    bool ConsumeLatestSnapshot(GameSnapshot& snapshot) override
    {
        if (!hasNetworkSnapshot)
            return false;
        snapshot = latestNetworkSnapshot;
        hasNetworkSnapshot = false;
        return true;
    }

    bool IsConnectionClosed() const override
    {
        return transport != nullptr && hadConnection && (!transport->IsConnected() || transport->HasFailed());
    }

    int GetPingMs() const override
    {
        return transport != nullptr ? transport->GetPingMs() : -1;
    }

    std::string GetConnectionStatus() const override
    {
        if (!initialSnapshotReceived)
            return syncStatus;
        return transport != nullptr ? transport->GetStatus() : std::string{};
    }

    bool IsReadyForGameplay() const override
    {
        return initialSnapshotReceived;
    }

    std::vector<GameCommandResult> ConsumeCommandResults() override
    {
        std::vector<GameCommandResult> results = std::move(commandResults);
        commandResults.clear();
        return results;
    }

private:
    void HandleSnapshotPayload(const std::string& payload)
    {
        if (payload.rfind("INIT_BEGIN ", 0) == 0)
        {
            std::istringstream in(payload.substr(11));
            std::uint64_t tick = 0;
            size_t totalBytes = 0;
            size_t totalChunks = 0;
            if (in >> tick >> totalBytes >> totalChunks)
            {
                initialSnapshotBuffer.clear();
                initialSnapshotChunks.clear();
                initialSnapshotChunks.resize(totalChunks);
                initialSnapshotChunkReceived.assign(totalChunks, false);
                expectedInitialSnapshotBytes = totalBytes;
                expectedInitialSnapshotChunks = totalChunks;
                receivedInitialSnapshotChunks = 0;
                syncStatus = "Syncing map 0/" + std::to_string(totalChunks);
                Log::Msg("[Session]", "Receiving initial snapshot: bytes=", totalBytes, " chunks=", totalChunks);
            }
            return;
        }

        if (payload.rfind("INIT_CHUNK ", 0) == 0)
        {
            size_t firstSpace = payload.find(' ', 11);
            if (firstSpace == std::string::npos)
                return;
            size_t index = 0;
            try
            {
                index = static_cast<size_t>(std::stoull(payload.substr(11, firstSpace - 11)));
            }
            catch (...)
            {
                syncStatus = "Map sync failed";
                return;
            }
            if (index >= initialSnapshotChunks.size())
                return;
            if (!initialSnapshotChunkReceived[index])
            {
                initialSnapshotChunkReceived[index] = true;
                receivedInitialSnapshotChunks++;
            }
            initialSnapshotChunks[index] = payload.substr(firstSpace + 1);
            syncStatus = "Syncing map " + std::to_string(receivedInitialSnapshotChunks) + "/" + std::to_string(expectedInitialSnapshotChunks);
            return;
        }

        if (payload == "INIT_END")
        {
            if (receivedInitialSnapshotChunks != expectedInitialSnapshotChunks)
            {
                syncStatus = "Waiting for map chunks";
                return;
            }
            initialSnapshotBuffer.clear();
            initialSnapshotBuffer.reserve(expectedInitialSnapshotBytes);
            for (const auto& chunk : initialSnapshotChunks)
                initialSnapshotBuffer += chunk;

            GameSnapshot snapshot;
            if (initialSnapshotBuffer.size() == expectedInitialSnapshotBytes &&
                GameSnapshot::TryDeserialize(initialSnapshotBuffer, snapshot))
            {
                latestNetworkSnapshot = std::move(snapshot);
                hasNetworkSnapshot = true;
                initialSnapshotReceived = true;
                syncStatus = "Map synchronized";
                if (transport != nullptr)
                    transport->SendClientCommand("SYNC_READY");
                Log::Msg("[Session]", "Initial snapshot received");
                initialSnapshotBuffer.clear();
                initialSnapshotBuffer.shrink_to_fit();
                initialSnapshotChunks.clear();
                initialSnapshotChunks.shrink_to_fit();
                initialSnapshotChunkReceived.clear();
                initialSnapshotChunkReceived.shrink_to_fit();
            }
            else
            {
                syncStatus = "Map sync failed";
                Log::Msg("[Session]", "Initial snapshot parse failed");
            }
            return;
        }

        if (payload.rfind("DELTA ", 0) == 0)
        {
            if (!initialSnapshotReceived)
                return;
            GameSnapshotDelta delta;
            if (GameSnapshotDelta::TryDeserialize(payload.substr(6), delta) &&
                delta.ApplyTo(latestNetworkSnapshot))
            {
                hasNetworkSnapshot = true;
            }
            return;
        }

        GameSnapshot snapshot;
        if (GameSnapshot::TryDeserialize(payload, snapshot))
        {
                latestNetworkSnapshot = std::move(snapshot);
                hasNetworkSnapshot = true;
                resyncRequestCooldown = 0.0;
            }
        }

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
    explicit LocalhostMultiplayerSession(GameWorld& world)
    : transport(std::make_shared<LocalhostGameTransport>()),
      host(world, transport, world.GetLocalPlayerId()),
      client(nullptr, transport, world.GetLocalPlayerId())
    {
    }

    std::uint64_t SubmitCommand(const GameCommand& command) override
    {
        return client.SubmitCommand(command);
    }

    void Update(double dt) override
    {
        host.Update(dt);
        client.Update(dt);
    }

    GameWorld* GetWorld() override
    {
        return host.GetWorld();
    }

    std::vector<GameCommandResult> ConsumeCommandResults() override
    {
        return client.ConsumeCommandResults();
    }

    LocalhostHostSession& GetHostSession() { return host; }
    LocalhostClientSession& GetClientSession() { return client; }

private:
    std::shared_ptr<IGameTransport> transport;
    LocalhostHostSession host;
    LocalhostClientSession client;
};

// Runs an existing session's fixed-tick simulation on a background thread.
class ThreadedGameSession : public IGameSession
{
public:
    explicit ThreadedGameSession(std::unique_ptr<IGameSession> innerSession)
    : inner(std::move(innerSession))
    {
        running = true;
        worker = std::thread(&ThreadedGameSession::RunSimulation, this);
    }

    ~ThreadedGameSession() override
    {
        Stop();
    }

    ThreadedGameSession(const ThreadedGameSession&) = delete;
    ThreadedGameSession& operator=(const ThreadedGameSession&) = delete;

    std::uint64_t SubmitCommand(const GameCommand& command) override
    {
        std::lock_guard<std::recursive_mutex> lock(worldMutex);
        return inner != nullptr ? inner->SubmitCommand(command) : 0;
    }

    void Update(double dt) override
    {
        (void)dt;
    }

    GameWorld* GetWorld() override
    {
        std::lock_guard<std::recursive_mutex> lock(worldMutex);
        return inner != nullptr ? inner->GetWorld() : nullptr;
    }

    std::vector<GameCommandResult> ConsumeCommandResults() override
    {
        std::lock_guard<std::recursive_mutex> lock(worldMutex);
        return inner != nullptr ? inner->ConsumeCommandResults() : std::vector<GameCommandResult>{};
    }

    bool ConsumeLatestSnapshot(GameSnapshot& snapshot) override
    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        if (!hasSnapshot)
            return false;
        snapshot = latestSnapshot;
        hasSnapshot = false;
        return true;
    }

    bool IsConnectionClosed() const override
    {
        std::lock_guard<std::recursive_mutex> lock(worldMutex);
        return inner != nullptr && inner->IsConnectionClosed();
    }

    std::string GetConnectionStatus() const override
    {
        std::lock_guard<std::recursive_mutex> lock(worldMutex);
        return inner != nullptr ? inner->GetConnectionStatus() : std::string{};
    }

    int GetPingMs() const override
    {
        std::lock_guard<std::recursive_mutex> lock(worldMutex);
        return inner != nullptr ? inner->GetPingMs() : -1;
    }

    bool IsReadyForGameplay() const override
    {
        std::lock_guard<std::recursive_mutex> lock(worldMutex);
        return inner == nullptr || inner->IsReadyForGameplay();
    }

    std::recursive_mutex* GetWorldMutex() override
    {
        return &worldMutex;
    }

private:
    void Stop()
    {
        running = false;
        cv.notify_all();
        if (worker.joinable())
            worker.join();
    }

    void RunSimulation()
    {
        auto nextTick = std::chrono::steady_clock::now();
        while (running)
        {
            nextTick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(FixedSimulationClock::FixedDt));

            {
                std::lock_guard<std::recursive_mutex> lock(worldMutex);
                if (inner != nullptr)
                {
                    inner->Update(FixedSimulationClock::FixedDt);
                    GameSnapshot snapshot;
                    inner->ConsumeLatestSnapshot(snapshot);
                    if (snapshot.IsValid())
                    {
                        std::lock_guard<std::mutex> snapshotLock(snapshotMutex);
                        latestSnapshot = std::move(snapshot);
                        hasSnapshot = true;
                    }
                }
            }

            std::unique_lock<std::mutex> sleepLock(sleepMutex);
            cv.wait_until(sleepLock, nextTick, [&]() { return !running.load(); });
            if (std::chrono::steady_clock::now() > nextTick + std::chrono::milliseconds(250))
                nextTick = std::chrono::steady_clock::now();
        }
    }

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
