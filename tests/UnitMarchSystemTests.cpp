#include "core/GameWorld.h"
#include "core/GameSession.h"
#include "warfare/UnitMarchSystem.h"
#include "warfare/BattleUnit.h"
#include "simulation/MapGenerator.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace
{
    MapParameters MakeSmallRingParams(unsigned int seed)
    {
        MapParameters params;
        params.sizeX = 81;
        params.sizeY = 81;
        params.aiOpponentCount = 1;
        params.seed = seed;
        return params;
    }

    // Adds a unit straight into the roster (state InRoster), skipping the
    // recruitment queue/timer covered by BattleUnitTests — ETAP 4's own tests
    // are about deploy + march, not recruitment.
    int AddUnitToRoster(Player& player, const std::string& unitDefId)
    {
        int instanceId = player.id * 100000 + player.nextUnitInstanceId++;
        BattleUnit unit(instanceId, player.id, unitDefId);
        unit.currentHp = unit.GetEffectiveMaxHp(player);
        player.roster.AddUnit(std::move(unit));
        return instanceId;
    }
}

TEST(UnitMarchSystemTests, DeployRemovesFromRosterAndSpawnsColumnInOrder)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(1));

    Player* human = world.GetPlayerHandler().players.at(0).get();
    ASSERT_TRUE(world.GetMilitaryRoads().AreConnected(0, 1));

    int spearheadId = AddUnitToRoster(*human, "militia");
    int secondId = AddUnitToRoster(*human, "militia");
    ASSERT_EQ(human->roster.units.size(), 2u);

    world.SubmitCommand(GameCommand::DeployUnits(0, 1, {spearheadId, secondId}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    EXPECT_TRUE(human->roster.units.empty());
    ASSERT_EQ(world.GetDeployedUnits().size(), 2u);
    EXPECT_EQ(world.GetDeployedUnits().at(spearheadId).state, BattleUnitState::Marching);
    EXPECT_EQ(world.GetDeployedUnits().at(secondId).state, BattleUnitState::Marching);

    // Spearhead (submitted first) takes the gate tile; the second unit waits
    // in the spawn queue since only one unit may occupy a tile at a time.
    EXPECT_EQ(world.GetDeployedUnits().at(spearheadId).tileIndex, 0);
    EXPECT_EQ(world.GetDeployedUnits().at(secondId).tileIndex, -1);

    auto& spawnQueues = world.GetSpawnQueues();
    auto it = spawnQueues.find({0, 1});
    ASSERT_NE(it, spawnQueues.end());
    ASSERT_EQ(it->second.size(), 1u);
    EXPECT_EQ(it->second.front(), secondId);
}

TEST(UnitMarchSystemTests, DeployRejectsUnknownTargetAndNonRosterUnits)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(2));
    Player* human = world.GetPlayerHandler().players.at(0).get();
    int unitId = AddUnitToRoster(*human, "militia");

    // Not a ring neighbor of player 0.
    std::uint64_t badTargetCmd = world.SubmitCommand(GameCommand::DeployUnits(0, 0, {unitId}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);
    auto results = world.ConsumeCommandResults();
    ASSERT_FALSE(results.empty());
    EXPECT_FALSE(results.back().accepted);
    EXPECT_EQ(human->roster.units.size(), 1u);

    // Unknown unit instance id.
    world.SubmitCommand(GameCommand::DeployUnits(0, 1, {999999}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);
    results = world.ConsumeCommandResults();
    ASSERT_FALSE(results.empty());
    EXPECT_FALSE(results.back().accepted);
    EXPECT_EQ(human->roster.units.size(), 1u);
    (void)badTargetCmd;
}

TEST(UnitMarchSystemTests, MarchIsDeterministicForSameSeed)
{
    GameWorld worldA;
    GameWorld worldB;
    worldA.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(42));
    worldB.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(42));

    Player* humanA = worldA.GetPlayerHandler().players.at(0).get();
    Player* humanB = worldB.GetPlayerHandler().players.at(0).get();
    int idA = AddUnitToRoster(*humanA, "militia");
    int idB = AddUnitToRoster(*humanB, "militia");
    ASSERT_EQ(idA, idB);

    worldA.SubmitCommand(GameCommand::DeployUnits(0, 1, {idA}));
    worldB.SubmitCommand(GameCommand::DeployUnits(0, 1, {idB}));

    for (int i = 0; i < 500; i++)
    {
        worldA.UpdateSimulation(FixedSimulationClock::FixedDt);
        worldB.UpdateSimulation(FixedSimulationClock::FixedDt);
    }

    ASSERT_EQ(worldA.GetDeployedUnits().size(), 1u);
    ASSERT_EQ(worldB.GetDeployedUnits().size(), 1u);
    const BattleUnit& unitA = worldA.GetDeployedUnits().at(idA);
    const BattleUnit& unitB = worldB.GetDeployedUnits().at(idB);
    EXPECT_EQ(unitA.tileIndex, unitB.tileIndex);
    EXPECT_DOUBLE_EQ(unitA.tileProgress, unitB.tileProgress);
    EXPECT_EQ(unitA.state, unitB.state);
}

TEST(UnitMarchSystemTests, FollowerQueuesBehindArrivedLeaderAtFinalTile)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(7));
    Player* human = world.GetPlayerHandler().players.at(0).get();

    int leaderId = AddUnitToRoster(*human, "militia");
    world.SubmitCommand(GameCommand::DeployUnits(0, 1, {leaderId}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    // Fast-forward: a single huge dt lets the leader consume its whole route
    // in one call (UnitMarchSystem's advance loop handles arbitrary dt sizes).
    UnitMarchSystem::Update(world, 100000.0);
    ASSERT_EQ(world.GetDeployedUnits().at(leaderId).state, BattleUnitState::AttackingHq);
    int lastTileIndex = world.GetDeployedUnits().at(leaderId).tileIndex;

    int followerId = AddUnitToRoster(*human, "militia");
    world.SubmitCommand(GameCommand::DeployUnits(0, 1, {followerId}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    for (int i = 0; i < 2000; i++)
        UnitMarchSystem::Update(world, 0.05);

    const BattleUnit& follower = world.GetDeployedUnits().at(followerId);
    EXPECT_NE(follower.state, BattleUnitState::AttackingHq)
        << "follower must queue behind the leader, not share the final tile";
    EXPECT_LT(follower.tileIndex, lastTileIndex);
    EXPECT_GE(follower.tileProgress, 0.9);
}

TEST(UnitMarchSystemTests, SaveAndLoadPreservesMarchingStateAndSpawnQueue)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(11));
    Player* human = world.GetPlayerHandler().players.at(0).get();

    int spearheadId = AddUnitToRoster(*human, "militia");
    int queuedId = AddUnitToRoster(*human, "militia");
    world.SubmitCommand(GameCommand::DeployUnits(0, 1, {spearheadId, queuedId}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    for (int i = 0; i < 50; i++)
        world.UpdateSimulation(FixedSimulationClock::FixedDt);

    const auto path = (std::filesystem::temp_directory_path() / "rts_march_test.save").string();
    ASSERT_TRUE(world.SaveToFile(path));

    GameWorld loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path, nullptr, nullptr));

    ASSERT_EQ(loaded.GetDeployedUnits().size(), 2u);
    const BattleUnit& originalSpearhead = world.GetDeployedUnits().at(spearheadId);
    const BattleUnit& loadedSpearhead = loaded.GetDeployedUnits().at(spearheadId);
    EXPECT_EQ(loadedSpearhead.tileIndex, originalSpearhead.tileIndex);
    EXPECT_DOUBLE_EQ(loadedSpearhead.tileProgress, originalSpearhead.tileProgress);
    EXPECT_EQ(loadedSpearhead.state, originalSpearhead.state);
    EXPECT_EQ(loadedSpearhead.routeFromPlayerId, 0);
    EXPECT_EQ(loadedSpearhead.routeToPlayerId, 1);

    const auto& originalQueues = world.GetSpawnQueues();
    const auto& loadedQueues = loaded.GetSpawnQueues();
    EXPECT_EQ(loadedQueues, originalQueues);

    std::filesystem::remove(path);
}
