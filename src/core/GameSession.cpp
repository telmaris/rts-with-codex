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
    return world->SubmitCommand(command, world->GetSimulationTick() + inputDelayTicks);
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

void HostSession::SendCorrectionSnapshot()
{
    if (world == nullptr || transport == nullptr)
        return;

    std::string payload = world->BuildSnapshot().Serialize();
    transport->SendHostSnapshot(payload);
    Log::Msg("[Session]", "Correction snapshot queued: bytes=", payload.size());
}

void HostSession::RunSimulation()
{
    auto nextTick = std::chrono::steady_clock::now();
    while (running)
    {
        nextTick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(FixedSimulationClock::FixedDt));

        {
            std::lock_guard<std::recursive_mutex> lock(worldMutex);
            if (world == nullptr)
                goto sleep_and_wait;

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
                        world->SubmitCommand(command, world->GetSimulationTick() + inputDelayTicks);
                    }
                }

                if (requireRemoteSync && !remoteInitialSnapshotReady)
                    goto sleep_and_wait;
            }

            // Update simulation
            {
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
        }

sleep_and_wait:
        std::unique_lock<std::mutex> sleepLock(sleepMutex);
        cv.wait_until(sleepLock, nextTick, [&]() { return !running.load(); });
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

std::uint64_t ClientSession::SubmitCommand(const GameCommand& command)
{
    GameCommand outbound = command;
    outbound.playerId = assignedPlayerId;
    if (outbound.commandId == 0)
        outbound.commandId = nextClientCommandId++;
    if (transport != nullptr)
        transport->SendClientCommand(outbound.Serialize());
    return outbound.commandId;
}

void ClientSession::Update(double dt)
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

// LocalhostMultiplayerSession is now deprecated - use HostSession instead
// ThreadedGameSession functionality is now integrated into HostSession
