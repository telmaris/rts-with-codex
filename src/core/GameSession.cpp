#include "core/GameSession.h"
#include "core/Log.h"

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
    std::lock_guard<std::mutex> lock(mutex);
    clientToHost.push_back(payload);
}

std::vector<std::string> LocalhostGameTransport::ReceiveHostCommands()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(clientToHost);
}

void LocalhostGameTransport::SendHostResult(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(mutex);
    hostToClient.push_back(payload);
}

std::vector<std::string> LocalhostGameTransport::ReceiveClientResults()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(hostToClient);
}

void LocalhostGameTransport::SendHostFrame(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(mutex);
    hostFrames.push_back(payload);
}

std::vector<std::string> LocalhostGameTransport::ReceiveClientFrames()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(hostFrames);
}

void LocalhostGameTransport::SendHostSnapshot(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(mutex);
    hostSnapshots.push_back(payload);
}

std::vector<std::string> LocalhostGameTransport::ReceiveClientSnapshots()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(hostSnapshots);
}

// ============================================================================
// HostSession Implementation - Scalona klasa dla SP i MP
// ============================================================================

// Single-player constructor
HostSession::HostSession(GameWorld& world)
    : world(&world), transport(nullptr), requireRemoteSync(false)
{
    running = true;
    worker = std::thread(&HostSession::RunSimulation, this);
}

// Multiplayer constructor
HostSession::HostSession(GameWorld& world, std::shared_ptr<IGameTransport> transport, int remotePlayerId, bool requireRemoteSync)
    : world(&world), transport(std::move(transport)), remotePlayerId(remotePlayerId), requireRemoteSync(requireRemoteSync)
{
    running = true;
    worker = std::thread(&HostSession::RunSimulation, this);
}

HostSession::~HostSession()
{
    Stop();
}

std::uint64_t HostSession::SubmitCommand(const GameCommand& command)
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    if (world == nullptr)
        return 0;
    GameCommand authoritative = command;
    authoritative.targetTick = world->GetSimulationTick() + inputDelayTicks;
    return world->SubmitCommand(authoritative, authoritative.targetTick);
}

void HostSession::Update(double dt)
{
    // Simulation runs on background thread - nothing to do here
    (void)dt;
}

GameWorld* HostSession::GetWorld()
{
    // No lock needed: world pointer is const after construction. Removing lock fixes GUI freeze
    // caused by main thread (GameScene) waiting for background thread (RunSimulation) to release mutex.
    return world;
}

std::vector<GameCommandResult> HostSession::ConsumeCommandResults()
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    std::vector<GameCommandResult> results = std::move(commandResults);
    commandResults.clear();
    return results;
}

bool HostSession::ConsumeLatestSnapshot(GameSnapshot& snapshot)
{
    std::lock_guard<std::mutex> lock(snapshotMutex);
    if (!hasSnapshot)
        return false;
    snapshot = latestSnapshot;
    hasSnapshot = false;
    return true;
}

bool HostSession::IsConnectionClosed() const
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    return transport != nullptr && hadConnection && (!transport->IsConnected() || transport->HasFailed());
}

std::string HostSession::GetConnectionStatus() const
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    if (transport == nullptr)
        return "Single player";
    if (transport != nullptr && requireRemoteSync && !remoteInitialSnapshotReady)
        return initialSnapshotSent ? "Waiting for client map sync" : "Preparing map sync";
    return transport != nullptr ? transport->GetStatus() : std::string{};
}

int HostSession::GetPingMs() const
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    return transport != nullptr ? transport->GetPingMs() : -1;
}

bool HostSession::IsReadyForGameplay() const
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    return transport == nullptr || !requireRemoteSync || remoteInitialSnapshotReady;
}

std::recursive_mutex* HostSession::GetWorldMutex()
{
    return &worldMutex;
}

bool HostSession::ShouldPauseWhenSceneInactive() const
{
    return transport == nullptr;
}

void HostSession::SetPaused(bool shouldPause)
{
    paused.store(shouldPause);

    // When pausing, wait for an in-flight tick to leave the critical section.
    // Once this barrier returns, RunSimulationTick observes `paused` before it
    // can mutate the world again.
    if (shouldPause)
    {
        std::lock_guard<std::recursive_mutex> lock(worldMutex);
    }

    cv.notify_all();
}

bool HostSession::IsPaused() const
{
    return paused.load();
}

void HostSession::Stop()
{
    running = false;
    cv.notify_all();
    if (worker.joinable())
        worker.join();
}

void HostSession::SendInitialSnapshot()
{
    if (world == nullptr || transport == nullptr)
        return;

    std::string payload = world->SerializeSimulationState();
    if (payload.empty())
    {
        Log::Msg("[Session]", "Initial simulation-state serialization failed");
        return;
    }
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

void HostSession::SendCorrectionSnapshot()
{
    if (world == nullptr || transport == nullptr)
        return;

    // The TCP protocol limits each snapshot frame to 32 KiB. Reuse the
    // established initial-sync envelope so a corrective snapshot cannot turn
    // into one unbounded transport message on a larger map.
    std::string payload = world->SerializeSimulationState();
    if (payload.empty())
    {
        Log::Msg("[Session]", "Correction simulation-state serialization failed");
        return;
    }
    constexpr size_t ChunkSize = 12000;
    size_t totalChunks = payload.empty() ? 0 : (payload.size() + ChunkSize - 1) / ChunkSize;
    transport->SendHostSnapshot("INIT_BEGIN " + std::to_string(world->GetSimulationTick()) + " " +
                                std::to_string(payload.size()) + " " + std::to_string(totalChunks));
    for (size_t i = 0; i < totalChunks; ++i)
    {
        size_t offset = i * ChunkSize;
        transport->SendHostSnapshot("INIT_CHUNK " + std::to_string(i) + " " + payload.substr(offset, ChunkSize));
    }
    transport->SendHostSnapshot("INIT_END");
    Log::Msg("[Session]", "Correction snapshot queued: bytes=", payload.size(), " chunks=", totalChunks);
}

void HostSession::RememberRemoteCommandResult(const GameCommandResult& result)
{
    if (pendingRemoteCommandIds.erase(result.commandId) == 0 &&
        !completedRemoteCommandResults.contains(result.commandId))
        return;

    if (!completedRemoteCommandResults.contains(result.commandId))
        completedRemoteCommandOrder.push_back(result.commandId);
    completedRemoteCommandResults[result.commandId] = result;

    while (completedRemoteCommandOrder.size() > MaxRememberedRemoteCommands)
    {
        const std::uint64_t expiredId = completedRemoteCommandOrder.front();
        completedRemoteCommandOrder.pop_front();
        completedRemoteCommandResults.erase(expiredId);
    }
}

void HostSession::RunSimulationTick()
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    if (world == nullptr || paused.load())
        return;

    // Handle transport commands
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
                const std::uint64_t authoritativeTargetTick = world->GetSimulationTick() + inputDelayTicks;
                command.targetTick = authoritativeTargetTick;
                if (command.commandId == 0)
                {
                    GameCommandResult rejected{
                        command.commandId,
                        world->GetSimulationTick(),
                        authoritativeTargetTick,
                        command.playerId,
                        command.type,
                        false,
                        "rejected: missing command id",
                        command.Serialize()};
                    RememberRemoteCommandResult(rejected);
                    commandResults.push_back(rejected);
                    transport->SendHostResult(rejected.Serialize());
                    continue;
                }
                if (const auto completed = completedRemoteCommandResults.find(command.commandId);
                    completed != completedRemoteCommandResults.end())
                {
                    // Replayed input gets the original answer; the world
                    // mutation must happen at most once.
                    transport->SendHostResult(completed->second.Serialize());
                    continue;
                }
                if (pendingRemoteCommandIds.contains(command.commandId))
                    continue;
                if (command.playerId != remotePlayerId)
                {
                    GameCommandResult rejected{
                        command.commandId,
                        world->GetSimulationTick(),
                        authoritativeTargetTick,
                        command.playerId,
                        command.type,
                        false,
                        "rejected: wrong player slot",
                        command.Serialize()};
                    pendingRemoteCommandIds.insert(command.commandId);
                    RememberRemoteCommandResult(rejected);
                    commandResults.push_back(rejected);
                    transport->SendHostResult(rejected.Serialize());
                    continue;
                }
                pendingRemoteCommandIds.insert(command.commandId);
                world->SubmitCommand(command, authoritativeTargetTick);
            }
        }

        if (requireRemoteSync && !remoteInitialSnapshotReady)
            return;
    }

    // Update simulation
    int ticks = clock.AddFrameTime(FixedSimulationClock::FixedDt);
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
            RememberRemoteCommandResult(result);
            commandResults.push_back(result);
            frame.results.push_back(result);
        }

        if (transport != nullptr)
            transport->SendHostFrame(frame.Serialize());

        // Snapshot capture for threaded access
        if (frame.hasChecksum)
        {
            std::lock_guard<std::mutex> snapshotLock(snapshotMutex);
            latestSnapshot = world->BuildSnapshot();
            hasSnapshot = latestSnapshot.IsValid();
        }
    }
}

void HostSession::RunSimulation()
{
    auto nextTick = std::chrono::steady_clock::now();
    while (running)
    {
        if (paused.load())
        {
            std::unique_lock<std::mutex> pauseLock(sleepMutex);
            cv.wait(pauseLock, [&]() { return !running.load() || !paused.load(); });
            nextTick = std::chrono::steady_clock::now();
            continue;
        }

        nextTick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(FixedSimulationClock::FixedDt));

        RunSimulationTick();

        std::unique_lock<std::mutex> sleepLock(sleepMutex);
        cv.wait_until(sleepLock, nextTick, [&]() { return !running.load() || paused.load(); });
        if (std::chrono::steady_clock::now() > nextTick + std::chrono::milliseconds(250))
            nextTick = std::chrono::steady_clock::now();
    }
}

// LocalhostHostSession is now deprecated - use HostSession instead instead

// ============================================================================
// ClientSession Implementation
// ============================================================================

ClientSession::ClientSession(GameWorld* observedWorld, std::shared_ptr<IGameTransport> transport, int assignedPlayerId)
    : observedWorld(observedWorld), transport(std::move(transport)), assignedPlayerId(assignedPlayerId)
{
}

void ClientSession::HandleAuthoritativeResult(const GameCommandResult& result)
{
    const auto key = std::make_pair(result.playerId, result.commandId);
    if (!observedCommandResults.insert(key).second)
        return;
    observedCommandResultOrder.push_back(key);
    while (observedCommandResultOrder.size() > MaxObservedCommandResults)
    {
        observedCommandResults.erase(observedCommandResultOrder.front());
        observedCommandResultOrder.pop_front();
    }

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
    if (result.playerId == assignedPlayerId)
        pendingClientCommandPayloads.erase(result.commandId);
    commandResults.push_back(result);
}

std::uint64_t ClientSession::SubmitCommand(const GameCommand& command)
{
    GameCommand outbound = command;
    outbound.playerId = assignedPlayerId;
    if (outbound.commandId == 0)
        outbound.commandId = nextClientCommandId++;
    const std::string payload = outbound.Serialize();
    pendingClientCommandPayloads[outbound.commandId] = payload;
    if (transport != nullptr)
    {
        transport->SendClientCommand(payload);
        wasConnected = transport->IsConnected();
    }
    return outbound.commandId;
}

void ClientSession::Update(double dt)
{
    if (transport == nullptr)
        return;

    if (resyncRequestCooldown > 0.0)
        resyncRequestCooldown = std::max(0.0, resyncRequestCooldown - dt);

    const bool connectedNow = transport->IsConnected();
    hadConnection = hadConnection || connectedNow;
    if (connectedNow && !wasConnected)
    {
        for (const auto& [commandId, payload] : pendingClientCommandPayloads)
        {
            (void)commandId;
            transport->SendClientCommand(payload);
        }
    }
    wasConnected = connectedNow;
    // TCP preserves wire order, but IGameTransport exposes snapshot and event
    // queues separately. Drain/apply the recovery state first so frames sent
    // by the host after INIT_END are applied to the restored world, never the
    // stale mirror that existed before correction.
    for (const auto& payload : transport->ReceiveClientSnapshots())
        HandleSnapshotPayload(payload);

    for (const auto& payload : transport->ReceiveClientFrames())
    {
        GameServerFrame frame;
        if (!GameServerFrame::TryDeserialize(payload, frame))
            continue;

        for (const auto& result : frame.results)
            HandleAuthoritativeResult(result);

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
            HandleAuthoritativeResult(result);
    }

    (void)dt;
}

GameWorld* ClientSession::GetWorld()
{
    return observedWorld;
}

bool ClientSession::ConsumeLatestSnapshot(GameSnapshot& snapshot)
{
    if (!hasNetworkSnapshot)
        return false;
    snapshot = latestNetworkSnapshot;
    hasNetworkSnapshot = false;
    return true;
}

bool ClientSession::IsConnectionClosed() const
{
    return transport != nullptr && hadConnection && (!transport->IsConnected() || transport->HasFailed());
}

int ClientSession::GetPingMs() const
{
    return transport != nullptr ? transport->GetPingMs() : -1;
}

std::string ClientSession::GetConnectionStatus() const
{
    if (!initialSnapshotReceived)
        return syncStatus;
    return transport != nullptr ? transport->GetStatus() : std::string{};
}

bool ClientSession::IsReadyForGameplay() const
{
    return initialSnapshotReceived;
}

std::vector<GameCommandResult> ClientSession::ConsumeCommandResults()
{
    std::vector<GameCommandResult> results = std::move(commandResults);
    commandResults.clear();
    return results;
}

void ClientSession::HandleSnapshotPayload(const std::string& payload)
{
    if (payload.rfind("INIT_BEGIN ", 0) == 0)
    {
        std::istringstream in(payload.substr(11));
        std::uint64_t tick = 0;
        size_t totalBytes = 0;
        size_t totalChunks = 0;
        if (in >> tick >> totalBytes >> totalChunks)
        {
            if (totalBytes > MaxInitialSnapshotBytes || totalChunks > MaxInitialSnapshotChunks ||
                (totalBytes == 0 && totalChunks != 0) || (totalBytes != 0 && totalChunks == 0))
            {
                initialSnapshotFailed = true;
                initialSnapshotReceived = false;
                syncStatus = "Map sync failed: invalid manifest";
                Log::Msg("[Session]", "Rejected initial snapshot manifest: bytes=", totalBytes,
                         " chunks=", totalChunks);
                return;
            }
            initialSnapshotBuffer.clear();
            initialSnapshotChunks.clear();
            initialSnapshotChunks.resize(totalChunks);
            initialSnapshotChunkReceived.assign(totalChunks, false);
            expectedInitialSnapshotBytes = totalBytes;
            expectedInitialSnapshotChunks = totalChunks;
            receivedInitialSnapshotChunks = 0;
            receivedInitialSnapshotBytes = 0;
            expectedInitialSnapshotTick = tick;
            initialSnapshotReceived = false;
            initialSnapshotFailed = false;
            syncStatus = "Syncing map 0/" + std::to_string(totalChunks);
            Log::Msg("[Session]", "Receiving initial snapshot: bytes=", totalBytes, " chunks=", totalChunks);
        }
        else
        {
            initialSnapshotFailed = true;
            initialSnapshotReceived = false;
            syncStatus = "Map sync failed: malformed manifest";
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
            initialSnapshotFailed = true;
            syncStatus = "Map sync failed";
            return;
        }
        if (index >= initialSnapshotChunks.size())
        {
            initialSnapshotFailed = true;
            syncStatus = "Map sync failed: chunk index out of range";
            return;
        }
        std::string chunk = payload.substr(firstSpace + 1);
        if (chunk.size() > expectedInitialSnapshotBytes ||
            (!initialSnapshotChunkReceived[index] &&
             chunk.size() > expectedInitialSnapshotBytes - receivedInitialSnapshotBytes))
        {
            initialSnapshotFailed = true;
            syncStatus = "Map sync failed: chunk exceeds manifest";
            return;
        }
        if (initialSnapshotChunkReceived[index])
        {
            if (initialSnapshotChunks[index] != chunk)
            {
                initialSnapshotFailed = true;
                syncStatus = "Map sync failed: conflicting duplicate chunk";
            }
            return;
        }
        initialSnapshotChunkReceived[index] = true;
        receivedInitialSnapshotChunks++;
        receivedInitialSnapshotBytes += chunk.size();
        initialSnapshotChunks[index] = std::move(chunk);
        syncStatus = "Syncing map " + std::to_string(receivedInitialSnapshotChunks) + "/" + std::to_string(expectedInitialSnapshotChunks);
        return;
    }

    if (payload == "INIT_END")
    {
        if (initialSnapshotFailed)
            return;
        if (receivedInitialSnapshotChunks != expectedInitialSnapshotChunks ||
            receivedInitialSnapshotBytes != expectedInitialSnapshotBytes)
        {
            syncStatus = "Waiting for map chunks";
            return;
        }
        initialSnapshotBuffer.clear();
        initialSnapshotBuffer.reserve(expectedInitialSnapshotBytes);
        for (const auto& chunk : initialSnapshotChunks)
            initialSnapshotBuffer += chunk;

        if (initialSnapshotBuffer.size() == expectedInitialSnapshotBytes && observedWorld != nullptr &&
            observedWorld->RestoreSimulationState(initialSnapshotBuffer, assignedPlayerId) &&
            observedWorld->GetSimulationTick() == expectedInitialSnapshotTick)
        {
            latestNetworkSnapshot = observedWorld->BuildSnapshot();
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
            receivedInitialSnapshotBytes = 0;
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

// LocalhostMultiplayerSession is now deprecated - use HostSession instead
// ThreadedGameSession functionality is now integrated into HostSession
