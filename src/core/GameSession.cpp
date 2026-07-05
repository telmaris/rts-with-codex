#include "core/GameSession.h"
#include "core/Utils.h"

// ============================================================================
// LocalhostGameTransport Implementation
// ============================================================================

std::vector<std::string> LocalhostGameTransport::Drain(std::deque<std::string>& queue)
{
    std::vector<std::string> result;
    while (!queue.empty())
    {
        result.push_back(std::move(queue.front()));
        queue.pop_front();
    }
    return result;
}

void LocalhostGameTransport::SendClientCommand(const std::string& payload)
{
    clientToHost.push_back(payload);
}

std::vector<std::string> LocalhostGameTransport::ReceiveHostCommands()
{
    return Drain(clientToHost);
}

void LocalhostGameTransport::SendHostResult(const std::string& payload)
{
    hostToClient.push_back(payload);
}

std::vector<std::string> LocalhostGameTransport::ReceiveClientResults()
{
    return Drain(hostToClient);
}

void LocalhostGameTransport::SendHostFrame(const std::string& payload)
{
    hostFrames.push_back(payload);
}

std::vector<std::string> LocalhostGameTransport::ReceiveClientFrames()
{
    return Drain(hostFrames);
}

void LocalhostGameTransport::SendHostSnapshot(const std::string& payload)
{
    hostSnapshots.push_back(payload);
}

std::vector<std::string> LocalhostGameTransport::ReceiveClientSnapshots()
{
    return Drain(hostSnapshots);
}

// ============================================================================
// HostGameSession Implementation
// ============================================================================

HostGameSession::HostGameSession(GameWorld& world) : world(&world) {}

std::uint64_t HostGameSession::SubmitCommand(const GameCommand& command)
{
    if (world != nullptr)
        return world->SubmitCommand(command, world->GetSimulationTick() + inputDelayTicks);
    return 0;
}

void HostGameSession::Update(double dt)
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

GameWorld* HostGameSession::GetWorld()
{
    return world;
}

std::vector<GameCommandResult> HostGameSession::ConsumeCommandResults()
{
    std::vector<GameCommandResult> results = std::move(commandResults);
    commandResults.clear();
    return results;
}

// ============================================================================
// LocalhostHostSession Implementation
// ============================================================================

LocalhostHostSession::LocalhostHostSession(GameWorld& world, std::shared_ptr<IGameTransport> transport, int remotePlayerId, bool requireRemoteSync)
    : world(&world), transport(std::move(transport)), remotePlayerId(remotePlayerId), requireRemoteSync(requireRemoteSync)
{
}

std::uint64_t LocalhostHostSession::SubmitCommand(const GameCommand& command)
{
    if (world == nullptr)
        return 0;
    return world->SubmitCommand(command, world->GetSimulationTick() + inputDelayTicks);
}

void LocalhostHostSession::Update(double dt)
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
                lastSentSnapshot = GameSnapshot{};
                hasLastSentSnapshot = false;
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

GameWorld* LocalhostHostSession::GetWorld()
{
    return world;
}

bool LocalhostHostSession::IsConnectionClosed() const
{
    return transport != nullptr && hadConnection && (!transport->IsConnected() || transport->HasFailed());
}

int LocalhostHostSession::GetPingMs() const
{
    return transport != nullptr ? transport->GetPingMs() : -1;
}

std::string LocalhostHostSession::GetConnectionStatus() const
{
    if (transport != nullptr && requireRemoteSync && !remoteInitialSnapshotReady)
        return initialSnapshotSent ? "Waiting for client map sync" : "Preparing map sync";
    return transport != nullptr ? transport->GetStatus() : std::string{};
}

bool LocalhostHostSession::IsReadyForGameplay() const
{
    return transport == nullptr || !requireRemoteSync || remoteInitialSnapshotReady;
}

std::vector<GameCommandResult> LocalhostHostSession::ConsumeCommandResults()
{
    std::vector<GameCommandResult> results = std::move(commandResults);
    commandResults.clear();
    return results;
}

void LocalhostHostSession::SendInitialSnapshot()
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

void LocalhostHostSession::SendCorrectionSnapshot()
{
    if (world == nullptr || transport == nullptr)
        return;

    std::string payload = world->BuildSnapshot().Serialize();
    transport->SendHostSnapshot(payload);
    Log::Msg("[Session]", "Correction snapshot queued: bytes=", payload.size());
}

// ============================================================================
// LocalhostClientSession Implementation
// ============================================================================

LocalhostClientSession::LocalhostClientSession(GameWorld* observedWorld, std::shared_ptr<IGameTransport> transport, int assignedPlayerId)
    : observedWorld(observedWorld), transport(std::move(transport)), assignedPlayerId(assignedPlayerId)
{
}

std::uint64_t LocalhostClientSession::SubmitCommand(const GameCommand& command)
{
    GameCommand outbound = command;
    outbound.playerId = assignedPlayerId;
    if (outbound.commandId == 0)
        outbound.commandId = nextClientCommandId++;
    if (transport != nullptr)
        transport->SendClientCommand(outbound.Serialize());
    return outbound.commandId;
}

void LocalhostClientSession::Update(double dt)
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
                        resyncRequestCooldown = 30.0;
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

GameWorld* LocalhostClientSession::GetWorld()
{
    return observedWorld;
}

bool LocalhostClientSession::ConsumeLatestSnapshot(GameSnapshot& snapshot)
{
    if (!hasNetworkSnapshot)
        return false;
    snapshot = latestNetworkSnapshot;
    hasNetworkSnapshot = false;
    return true;
}

bool LocalhostClientSession::IsConnectionClosed() const
{
    return transport != nullptr && hadConnection && (!transport->IsConnected() || transport->HasFailed());
}

int LocalhostClientSession::GetPingMs() const
{
    return transport != nullptr ? transport->GetPingMs() : -1;
}

std::string LocalhostClientSession::GetConnectionStatus() const
{
    if (!initialSnapshotReceived)
        return syncStatus;
    return transport != nullptr ? transport->GetStatus() : std::string{};
}

bool LocalhostClientSession::IsReadyForGameplay() const
{
    return initialSnapshotReceived;
}

std::vector<GameCommandResult> LocalhostClientSession::ConsumeCommandResults()
{
    std::vector<GameCommandResult> results = std::move(commandResults);
    commandResults.clear();
    return results;
}

void LocalhostClientSession::HandleSnapshotPayload(const std::string& payload)
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
    }
}

// ============================================================================
// LocalhostMultiplayerSession Implementation
// ============================================================================

LocalhostMultiplayerSession::LocalhostMultiplayerSession(GameWorld& world)
    : transport(std::make_shared<LocalhostGameTransport>()),
      host(world, transport, world.GetLocalPlayerId()),
      client(nullptr, transport, world.GetLocalPlayerId())
{
}

std::uint64_t LocalhostMultiplayerSession::SubmitCommand(const GameCommand& command)
{
    return client.SubmitCommand(command);
}

void LocalhostMultiplayerSession::Update(double dt)
{
    host.Update(dt);
    client.Update(dt);
}

GameWorld* LocalhostMultiplayerSession::GetWorld()
{
    return host.GetWorld();
}

std::vector<GameCommandResult> LocalhostMultiplayerSession::ConsumeCommandResults()
{
    return client.ConsumeCommandResults();
}

LocalhostHostSession& LocalhostMultiplayerSession::GetHostSession()
{
    return host;
}

LocalhostClientSession& LocalhostMultiplayerSession::GetClientSession()
{
    return client;
}

// ============================================================================
// ThreadedGameSession Implementation
// ============================================================================

ThreadedGameSession::ThreadedGameSession(std::unique_ptr<IGameSession> innerSession)
    : inner(std::move(innerSession))
{
    running = true;
    worker = std::thread(&ThreadedGameSession::RunSimulation, this);
}

ThreadedGameSession::~ThreadedGameSession()
{
    Stop();
}

std::uint64_t ThreadedGameSession::SubmitCommand(const GameCommand& command)
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    return inner != nullptr ? inner->SubmitCommand(command) : 0;
}

void ThreadedGameSession::Update(double dt)
{
    (void)dt;
}

GameWorld* ThreadedGameSession::GetWorld()
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    return inner != nullptr ? inner->GetWorld() : nullptr;
}

std::vector<GameCommandResult> ThreadedGameSession::ConsumeCommandResults()
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    return inner != nullptr ? inner->ConsumeCommandResults() : std::vector<GameCommandResult>{};
}

bool ThreadedGameSession::ConsumeLatestSnapshot(GameSnapshot& snapshot)
{
    std::lock_guard<std::mutex> lock(snapshotMutex);
    if (!hasSnapshot)
        return false;
    snapshot = latestSnapshot;
    hasSnapshot = false;
    return true;
}

bool ThreadedGameSession::IsConnectionClosed() const
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    return inner != nullptr && inner->IsConnectionClosed();
}

std::string ThreadedGameSession::GetConnectionStatus() const
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    return inner != nullptr ? inner->GetConnectionStatus() : std::string{};
}

int ThreadedGameSession::GetPingMs() const
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    return inner != nullptr ? inner->GetPingMs() : -1;
}

bool ThreadedGameSession::IsReadyForGameplay() const
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    return inner == nullptr || inner->IsReadyForGameplay();
}

std::recursive_mutex* ThreadedGameSession::GetWorldMutex()
{
    return &worldMutex;
}

void ThreadedGameSession::Stop()
{
    running = false;
    cv.notify_all();
    if (worker.joinable())
        worker.join();
}

void ThreadedGameSession::RunSimulation()
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
