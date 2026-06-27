#include "../inc/GameCommand.h"
#include "../inc/GameSession.h"
#include "../inc/GameWorld.h"

#include <gtest/gtest.h>

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

TEST(GameCommandTests, SerializesAndDeserializesMilitaryOrder)
{
    GameCommand original = GameCommand::IssueMilitaryOrder(1, MilitaryOrderType::Support, 11, 22, 7);

    GameCommand parsed;
    ASSERT_TRUE(GameCommand::TryDeserialize(original.Serialize(), parsed));

    EXPECT_EQ(parsed.playerId, 1);
    EXPECT_EQ(parsed.type, GameCommandType::IssueMilitaryOrder);
    EXPECT_EQ(parsed.militaryOrderType, MilitaryOrderType::Support);
    EXPECT_EQ(parsed.sourceTileId, 11);
    EXPECT_EQ(parsed.targetTileId, 22);
    EXPECT_EQ(parsed.divisionId, 7);
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

TEST(GameCommandTests, LocalhostMultiplayerSessionRoundTripsCommandResult)
{
    GameWorld world;
    LocalhostMultiplayerSession session(world);

    std::uint64_t commandId = session.SubmitCommand(GameCommand::DestroyBuilding(999, 123));
    session.Update(0.20);
    auto results = session.ConsumeCommandResults();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().commandId, commandId);
    EXPECT_EQ(results.front().simulationTick, 3u);
    EXPECT_EQ(results.front().targetTick, 3u);
    EXPECT_EQ(results.front().playerId, 999);
    EXPECT_EQ(results.front().type, GameCommandType::DestroyBuilding);
    EXPECT_FALSE(results.front().accepted);
    EXPECT_EQ(session.GetWorld(), &world);
}
