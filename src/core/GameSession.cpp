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
