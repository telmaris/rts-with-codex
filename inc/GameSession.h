#ifndef GAME_SESSION_H
#define GAME_SESSION_H

#include "GameWorld.h"

#include <cstdint>
#include <deque>
#include <algorithm>
#include <memory>
#include <string>
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
};

struct FixedSimulationClock
{
    static constexpr double FixedDt = 1.0 / 30.0;
    static constexpr int MaxTicksPerUpdate = 5;

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
    std::uint64_t inputDelayTicks{3};
    std::vector<GameCommandResult> commandResults;
};

// Single player is a hosted game with one local player plus AI controllers.
class LocalSinglePlayerSession : public HostGameSession
{
public:
    using HostGameSession::HostGameSession;
};

// Authoritative multiplayer host that receives serialized commands from a transport.
class LocalhostHostSession : public IGameSession
{
public:
    LocalhostHostSession(GameWorld& world, std::shared_ptr<IGameTransport> transport)
    : world(&world), transport(std::move(transport))
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
        if (transport != nullptr)
        {
            for (const auto& payload : transport->ReceiveHostCommands())
            {
                GameCommand command;
                if (GameCommand::TryDeserialize(payload, command))
                    world->SubmitCommand(command, minimumTargetTick);
            }
        }

        int ticks = clock.AddFrameTime(dt);
        for (int i = 0; i < ticks; i++)
        {
            world->UpdateSimulation(FixedSimulationClock::FixedDt);
            auto results = world->ConsumeCommandResults();
            for (const auto& result : results)
            {
                commandResults.push_back(result);
                if (transport != nullptr)
                    transport->SendHostResult(result.Serialize());
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
    std::shared_ptr<IGameTransport> transport;
    FixedSimulationClock clock;
    std::uint64_t inputDelayTicks{3};
    std::vector<GameCommandResult> commandResults;
};

// Prototype local client. It sends serialized commands and observes a local world mirror.
class LocalhostClientSession : public IGameSession
{
public:
    LocalhostClientSession(GameWorld* observedWorld, std::shared_ptr<IGameTransport> transport)
    : observedWorld(observedWorld), transport(std::move(transport))
    {
    }

    std::uint64_t SubmitCommand(const GameCommand& command) override
    {
        GameCommand outbound = command;
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

        for (const auto& payload : transport->ReceiveClientResults())
        {
            GameCommandResult result;
            if (GameCommandResult::TryDeserialize(payload, result))
            {
                if (result.accepted && observedWorld != nullptr && !result.commandPayload.empty())
                {
                    GameCommand command;
                    if (GameCommand::TryDeserialize(result.commandPayload, command))
                        observedWorld->SubmitCommand(command, command.targetTick);
                }
                commandResults.push_back(std::move(result));
            }
        }

        if (observedWorld != nullptr)
        {
            int ticks = clock.AddFrameTime(dt);
            for (int i = 0; i < ticks; i++)
                observedWorld->UpdateSimulation(FixedSimulationClock::FixedDt);
            observedWorld->ConsumeCommandResults();
        }
    }

    GameWorld* GetWorld() override { return observedWorld; }

    std::vector<GameCommandResult> ConsumeCommandResults() override
    {
        std::vector<GameCommandResult> results = std::move(commandResults);
        commandResults.clear();
        return results;
    }

private:
    GameWorld* observedWorld{nullptr};
    std::shared_ptr<IGameTransport> transport;
    std::uint64_t nextClientCommandId{1};
    FixedSimulationClock clock;
    std::vector<GameCommandResult> commandResults;
};

// Runs a localhost client and authoritative host in one process.
class LocalhostMultiplayerSession : public IGameSession
{
public:
    explicit LocalhostMultiplayerSession(GameWorld& world)
    : transport(std::make_shared<LocalhostGameTransport>()),
      host(world, transport),
      client(nullptr, transport)
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

#endif
