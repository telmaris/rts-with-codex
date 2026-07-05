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
