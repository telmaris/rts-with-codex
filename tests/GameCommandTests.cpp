#include "core/GameCommand.h"
#include "core/GameSession.h"
#include "core/GameWorld.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

TEST(GameCommandTests, SerializesAndDeserializesBuildCommand)
{
    GameCommand original = GameCommand::BuildBuilding(2, BuildingType::LumberMill, {12, 34}, false);
    original.commandId = 42;
    original.targetTick = 7;

    GameCommand parsed;
    ASSERT_TRUE(GameCommand::TryDeserialize(original.Serialize(), parsed));

    EXPECT_EQ(parsed.commandId, 42u);
    EXPECT_EQ(parsed.targetTick, 7u);
    EXPECT_EQ(parsed.playerId, 2);
    EXPECT_EQ(parsed.type, GameCommandType::BuildBuilding);
    EXPECT_EQ(parsed.buildingType, BuildingType::LumberMill);
    EXPECT_EQ(parsed.tilePos.x, 12);
    EXPECT_EQ(parsed.tilePos.y, 34);
    EXPECT_FALSE(parsed.chargeCost);
}

TEST(GameCommandTests, SerializesAndDeserializesFocusAndResearchIds)
{
    GameCommand focus = GameCommand::StartFocus(3, "tribal_council");
    GameCommand research = GameCommand::StartTechnologyResearch(3, "sawmill blades", 99);

    GameCommand parsedFocus;
    GameCommand parsedResearch;
    ASSERT_TRUE(GameCommand::TryDeserialize(focus.Serialize(), parsedFocus));
    ASSERT_TRUE(GameCommand::TryDeserialize(research.Serialize(), parsedResearch));

    EXPECT_EQ(parsedFocus.type, GameCommandType::StartFocus);
    EXPECT_EQ(parsedFocus.researchId, "tribal_council");

    EXPECT_EQ(parsedResearch.type, GameCommandType::StartTechnologyResearch);
    EXPECT_EQ(parsedResearch.researchId, "sawmill blades");
    EXPECT_EQ(parsedResearch.sourceTileId, 99);
}

TEST(GameCommandTests, SerializesAndDeserializesTowerTargetMode)
{
    GameCommand original = GameCommand::SetTowerTargetMode(
        2, 123, static_cast<int>(TowerTargetMode::StrongestUnit));

    GameCommand parsed;
    ASSERT_TRUE(GameCommand::TryDeserialize(original.Serialize(), parsed));

    EXPECT_EQ(parsed.type, GameCommandType::SetTowerTargetMode);
    EXPECT_EQ(parsed.playerId, 2);
    EXPECT_EQ(parsed.sourceTileId, 123);
    EXPECT_EQ(parsed.targetTileId, static_cast<int>(TowerTargetMode::StrongestUnit));
}

TEST(GameCommandTests, RejectsMalformedPayload)
{
    GameCommand parsed;
    EXPECT_FALSE(GameCommand::TryDeserialize("1 999 0", parsed));
    EXPECT_FALSE(GameCommand::TryDeserialize("0 0 1 2 3 4 5 6 1 0 0 -1 \"bad\"", parsed));
    EXPECT_FALSE(GameCommand::TryDeserialize("not a command", parsed));
}

TEST(GameCommandTests, SerializesAndDeserializesCommandResult)
{
    GameCommandResult original;
    original.commandId = 42;
    original.simulationTick = 9;
    original.targetTick = 8;
    original.playerId = 2;
    original.type = GameCommandType::BuildBuilding;
    original.accepted = true;
    original.reason = "accepted";

    GameCommandResult parsed;
    ASSERT_TRUE(GameCommandResult::TryDeserialize(original.Serialize(), parsed));

    EXPECT_EQ(parsed.commandId, 42u);
    EXPECT_EQ(parsed.simulationTick, 9u);
    EXPECT_EQ(parsed.targetTick, 8u);
    EXPECT_EQ(parsed.playerId, 2);
    EXPECT_EQ(parsed.type, GameCommandType::BuildBuilding);
    EXPECT_TRUE(parsed.accepted);
    EXPECT_EQ(parsed.reason, "accepted");
}

TEST(GameCommandTests, GameWorldPublishesCommandResultAfterSimulationTick)
{
    GameWorld world;
    std::uint64_t commandId = world.SubmitCommand(GameCommand::DestroyBuilding(999, 123));

    world.UpdateSimulation(0.016);
    auto results = world.ConsumeCommandResults();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().commandId, commandId);
    EXPECT_EQ(results.front().simulationTick, 1u);
    EXPECT_EQ(results.front().targetTick, 1u);
    EXPECT_EQ(results.front().playerId, 999);
    EXPECT_EQ(results.front().type, GameCommandType::DestroyBuilding);
    EXPECT_FALSE(results.front().accepted);
}

// DEPRECATED: LocalhostMultiplayerSession test removed in Etap 1.2
// LocalhostMultiplayerSession merged into HostSession + ClientSession architecture
// Full integration test will be added in Etap 1.4 after IGameRuntimeLoop refactor

TEST(GameCommandTests, ThreadedSessionAdvancesSimulationAndPublishesCommandResult)
{
    GameWorld world;
    HostSession session(world);

    std::uint64_t commandId = session.SubmitCommand(GameCommand::DestroyBuilding(world.GetLocalPlayerId(), 123));
    std::vector<GameCommandResult> results;
    for (int i = 0; i < 30 && results.empty(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        results = session.ConsumeCommandResults();
    }

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().commandId, commandId);
    EXPECT_EQ(results.front().playerId, world.GetLocalPlayerId());
    EXPECT_EQ(results.front().type, GameCommandType::DestroyBuilding);
    EXPECT_FALSE(results.front().accepted);
    EXPECT_EQ(session.GetWorld(), &world);
    EXPECT_NE(session.GetWorldMutex(), nullptr);
}

TEST(GameCommandTests, HostRejectsTransportCommandForWrongPlayerSlot)
{
    GameWorld world;
    auto transport = std::make_shared<LocalhostGameTransport>();
    HostSession host(world, transport, 1);

    GameCommand command = GameCommand::DestroyBuilding(0, 123);
    command.commandId = 77;
    transport->SendClientCommand(command.Serialize());

    std::vector<GameCommandResult> results;
    for (int i = 0; i < 30 && results.empty(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        results = host.ConsumeCommandResults();
    }

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().commandId, command.commandId);
    EXPECT_EQ(results.front().playerId, 0);
    EXPECT_FALSE(results.front().accepted);
    EXPECT_EQ(results.front().reason, "rejected: wrong player slot");
    EXPECT_LT(results.front().targetTick, 999999u);
}

TEST(GameCommandTests, LocalhostTransportPreservesLargeFrameBacklog)
{
    LocalhostGameTransport transport;
    constexpr int frameCount = 600;
    for (int i = 0; i < frameCount; ++i)
    {
        GameServerFrame frame;
        frame.tick = static_cast<std::uint64_t>(i + 1);
        GameCommandResult result;
        result.commandId = static_cast<std::uint64_t>(i + 1);
        result.simulationTick = frame.tick;
        result.playerId = 0;
        result.type = GameCommandType::DestroyBuilding;
        result.accepted = false;
        frame.results.push_back(std::move(result));
        transport.SendHostFrame(frame.Serialize());
    }

    const auto frames = transport.ReceiveClientFrames();
    ASSERT_EQ(frames.size(), static_cast<std::size_t>(frameCount));
    for (int i = 0; i < frameCount; ++i)
    {
        GameServerFrame frame;
        ASSERT_TRUE(GameServerFrame::TryDeserialize(frames[static_cast<std::size_t>(i)], frame));
        ASSERT_EQ(frame.results.size(), 1u);
        EXPECT_EQ(frame.results.front().commandId, static_cast<std::uint64_t>(i + 1));
    }
}

TEST(GameCommandTests, LocalhostTransportSupportsConcurrentSendAndDrain)
{
    LocalhostGameTransport transport;
    constexpr int payloadCount = 1000;
    std::atomic<bool> producerDone{false};
    std::vector<std::string> received;
    received.reserve(payloadCount);

    std::thread producer([&]()
    {
        for (int i = 0; i < payloadCount; ++i)
            transport.SendClientCommand(std::to_string(i));
        producerDone = true;
    });

    while (!producerDone || received.size() < static_cast<std::size_t>(payloadCount))
    {
        auto batch = transport.ReceiveHostCommands();
        received.insert(received.end(), batch.begin(), batch.end());
        if (!producerDone)
            std::this_thread::yield();
    }
    producer.join();

    ASSERT_EQ(received.size(), static_cast<std::size_t>(payloadCount));
    for (int i = 0; i < payloadCount; ++i)
        EXPECT_EQ(received[static_cast<std::size_t>(i)], std::to_string(i));
}

TEST(GameCommandTests, MultiplayerWorldAssignsStableServerSlotsAndColors)
{
    MapParameters params;
    params.aiOpponentCount = 1;
    params.seed = 27015;

    GameWorld hostWorld;
    GameWorld clientWorld;
    hostWorld.InitMultiplayerWorld("test", nullptr, nullptr, params, 0, true);
    clientWorld.InitMultiplayerWorld("test", nullptr, nullptr, params, 1, false);

    ASSERT_EQ(hostWorld.GetLocalPlayerId(), 0);
    ASSERT_EQ(clientWorld.GetLocalPlayerId(), 1);
    ASSERT_TRUE(hostWorld.GetPlayerHandlerForTesting().players.contains(0));
    ASSERT_TRUE(hostWorld.GetPlayerHandlerForTesting().players.contains(1));
    ASSERT_TRUE(clientWorld.GetPlayerHandlerForTesting().players.contains(0));
    ASSERT_TRUE(clientWorld.GetPlayerHandlerForTesting().players.contains(1));

    Color hostSlot0 = hostWorld.GetPlayerHandlerForTesting().players[0]->color;
    Color clientSlot0 = clientWorld.GetPlayerHandlerForTesting().players[0]->color;
    Color hostSlot1 = hostWorld.GetPlayerHandlerForTesting().players[1]->color;
    Color clientSlot1 = clientWorld.GetPlayerHandlerForTesting().players[1]->color;

    EXPECT_EQ(hostSlot0.r, clientSlot0.r);
    EXPECT_EQ(hostSlot0.g, clientSlot0.g);
    EXPECT_EQ(hostSlot0.b, clientSlot0.b);
    EXPECT_EQ(hostSlot1.r, clientSlot1.r);
    EXPECT_EQ(hostSlot1.g, clientSlot1.g);
    EXPECT_EQ(hostSlot1.b, clientSlot1.b);
    EXPECT_EQ(hostWorld.GetPlayerHandlerForTesting().players[1]->controllerType, PlayerControllerType::Remote);
    EXPECT_EQ(clientWorld.GetPlayerHandlerForTesting().players[1]->controllerType, PlayerControllerType::LocalHuman);
    EXPECT_EQ(clientWorld.GetPlayerHandlerForTesting().players[2]->controllerType, PlayerControllerType::Remote);
}
