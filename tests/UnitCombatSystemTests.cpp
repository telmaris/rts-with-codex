#include "core/GameWorld.h"
#include "core/GameSession.h"
#include "warfare/UnitCombatSystem.h"
#include "warfare/BattleUnit.h"
#include "simulation/MapGenerator.h"

#include <gtest/gtest.h>

// TD(etap-5.2/5.3) — road-combat integration scenarios, built on top of the
// deploy+march plumbing already covered by UnitMarchSystemTests.cpp. Uses
// world.UpdateSimulation() throughout (which calls UnitCombatSystem::Update
// then UnitMarchSystem::Update every tick), the same way real gameplay does.

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

    // Mirrors UnitMarchSystemTests.cpp's helper: adds a unit straight into the
    // roster, skipping the recruitment queue/timer.
    int AddUnitToRoster(Player& player, const std::string& unitDefId)
    {
        int instanceId = player.id * 100000 + player.nextUnitInstanceId++;
        BattleUnit unit(instanceId, player.id, unitDefId);
        unit.currentHp = unit.GetEffectiveMaxHp(player);
        player.roster.AddUnit(std::move(unit));
        return instanceId;
    }

    struct DuelSetup
    {
        int attackerId; // player 0, marching 0->1
        int defenderId; // player 1, marching 1->0
    };

    // Deploys one unit per side toward each other on the single ring route
    // connecting players 0 and 1, so they eventually meet head-on.
    DuelSetup DeployDuel(GameWorld& world, const std::string& attackerDefId, const std::string& defenderDefId)
    {
        Player* p0 = world.GetPlayerHandler().players.at(0).get();
        Player* p1 = world.GetPlayerHandler().players.at(1).get();
        int attackerId = AddUnitToRoster(*p0, attackerDefId);
        int defenderId = AddUnitToRoster(*p1, defenderDefId);
        world.SubmitCommand(GameCommand::DeployUnits(0, 1, {attackerId}));
        world.SubmitCommand(GameCommand::DeployUnits(1, 0, {defenderId}));
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
        return {attackerId, defenderId};
    }
}

TEST(UnitCombatSystemTests, EqualFightIsDeterministicForSameSeed)
{
    GameWorld worldA;
    GameWorld worldB;
    worldA.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(500));
    worldB.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(500));

    DeployDuel(worldA, "militia", "militia");
    DeployDuel(worldB, "militia", "militia");

    for (int i = 0; i < 6000; i++)
    {
        worldA.UpdateSimulation(FixedSimulationClock::FixedDt);
        worldB.UpdateSimulation(FixedSimulationClock::FixedDt);
    }

    ASSERT_EQ(worldA.GetDeployedUnits().size(), worldB.GetDeployedUnits().size());
    for (const auto& [id, unitA] : worldA.GetDeployedUnits())
    {
        ASSERT_EQ(worldB.GetDeployedUnits().count(id), 1u) << "id=" << id;
        const BattleUnit& unitB = worldB.GetDeployedUnits().at(id);
        EXPECT_EQ(unitA.state, unitB.state) << "id=" << id;
        EXPECT_DOUBLE_EQ(unitA.currentHp, unitB.currentHp) << "id=" << id;
        EXPECT_EQ(unitA.tileIndex, unitB.tileIndex) << "id=" << id;
    }
}

TEST(UnitCombatSystemTests, StrongerUnitWinsAndResumesMarchingTowardHq)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(600));
    DuelSetup duel = DeployDuel(world, "swordsman", "militia");

    bool defenderDied = false;
    for (int i = 0; i < 8000 && !defenderDied; i++)
    {
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
        if (world.GetDeployedUnits().count(duel.defenderId) == 0)
            defenderDied = true;
    }
    ASSERT_TRUE(defenderDied) << "militia should have died to the stronger swordsman within the time budget";
    ASSERT_EQ(world.GetDeployedUnits().count(duel.attackerId), 1u);
    const BattleUnit& survivor = world.GetDeployedUnits().at(duel.attackerId);
    EXPECT_GT(survivor.currentHp, 0.0);

    // With the opponent gone, nothing blocks the route anymore — the
    // swordsman should be free to resume marching and eventually reach the
    // enemy HQ door.
    bool reachedHq = false;
    for (int i = 0; i < 8000 && !reachedHq; i++)
    {
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
        auto it = world.GetDeployedUnits().find(duel.attackerId);
        if (it != world.GetDeployedUnits().end() && it->second.state == BattleUnitState::AttackingHq)
            reachedHq = true;
    }
    EXPECT_TRUE(reachedHq) << "surviving unit should resume marching and reach the HQ door";
}

TEST(UnitCombatSystemTests, ColumnFightThreeVsTwoResolvesToOneSurvivingSide)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(700));
    Player* p0 = world.GetPlayerHandler().players.at(0).get();
    Player* p1 = world.GetPlayerHandler().players.at(1).get();

    std::vector<int> sideA;
    std::vector<int> sideB;
    for (int i = 0; i < 3; i++)
        sideA.push_back(AddUnitToRoster(*p0, "militia"));
    for (int i = 0; i < 2; i++)
        sideB.push_back(AddUnitToRoster(*p1, "militia"));

    world.SubmitCommand(GameCommand::DeployUnits(0, 1, sideA));
    world.SubmitCommand(GameCommand::DeployUnits(1, 0, sideB));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    // Poll and stop the instant one side is wiped out on the road, rather
    // than running a fixed extra duration — TD(etap-6) means a surviving
    // column that reaches the enemy HQ starts taking siege/thorns damage
    // too, which would (correctly) eventually kill them off as well and
    // falsely look like "neither side survived" for THIS road-combat test.
    int survivorsA = 0;
    int survivorsB = 0;
    for (int i = 0; i < 15000; i++)
    {
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
        survivorsA = 0;
        survivorsB = 0;
        for (const auto& [id, unit] : world.GetDeployedUnits())
        {
            if (unit.ownerPlayerId == 0)
                survivorsA++;
            else if (unit.ownerPlayerId == 1)
                survivorsB++;
        }
        if (survivorsA == 0 || survivorsB == 0)
            break;
    }

    EXPECT_TRUE((survivorsA == 0 && survivorsB >= 1 && survivorsB <= 2) ||
                (survivorsB == 0 && survivorsA >= 1 && survivorsA <= 3))
        << "expected exactly one side wiped out; survivorsA=" << survivorsA << " survivorsB=" << survivorsB;
}

TEST(UnitCombatSystemTests, NoZombieAttackAfterDeathInSameTick)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(800));
    DuelSetup duel = DeployDuel(world, "militia", "militia");

    bool locked = false;
    for (int i = 0; i < 6000 && !locked; i++)
    {
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
        const auto& units = world.GetDeployedUnits();
        auto itA = units.find(duel.attackerId);
        auto itB = units.find(duel.defenderId);
        if (itA != units.end() && itB != units.end() && itA->second.state == BattleUnitState::FightingUnit &&
            itB->second.state == BattleUnitState::FightingUnit)
            locked = true;
    }
    ASSERT_TRUE(locked) << "the two spearheads never made contact within the time budget";

    auto& units = world.GetDeployedUnits();
    BattleUnit& attacker = units.at(duel.attackerId);
    BattleUnit& defender = units.at(duel.defenderId);

    attacker.attackTimer = 0.0;
    defender.attackTimer = 0.0;
    double attackerHpBefore = attacker.currentHp;
    // Guaranteed one-hit kill regardless of the [0.9,1.1] variance: militia's
    // road_attack (3) minus 0 armor floors at 3, and the resolver never drops
    // below 3*0.9=2.7 — comfortably more than this 1.0 hp.
    defender.currentHp = 1.0;

    // Attacker (instance id 0) sorts before defender (instance id 100000) in
    // UnitCombatSystem's ascending-id processing order, so it acts first and
    // kills the weakened defender within this single Update call. Without the
    // currentHp<=0 guard, the now-dead defender would still see
    // state==FightingUnit on its own turn later in the same pass and land a
    // "zombie" counter-hit — this test is the regression guard for that bug.
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    EXPECT_EQ(world.GetDeployedUnits().count(duel.defenderId), 0u)
        << "the weakened defender should have died this tick";
    ASSERT_EQ(world.GetDeployedUnits().count(duel.attackerId), 1u);
    EXPECT_DOUBLE_EQ(world.GetDeployedUnits().at(duel.attackerId).currentHp, attackerHpBefore)
        << "a unit that died this same tick must never land a retaliation hit (no zombies)";
}

TEST(UnitCombatSystemTests, SimultaneousLethalSetupResolvesToSingleSurvivorViaIdOrder)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(900));
    DuelSetup duel = DeployDuel(world, "militia", "militia");

    bool locked = false;
    for (int i = 0; i < 6000 && !locked; i++)
    {
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
        const auto& units = world.GetDeployedUnits();
        auto itA = units.find(duel.attackerId);
        auto itB = units.find(duel.defenderId);
        if (itA != units.end() && itB != units.end() && itA->second.state == BattleUnitState::FightingUnit &&
            itB->second.state == BattleUnitState::FightingUnit)
            locked = true;
    }
    ASSERT_TRUE(locked) << "the two spearheads never made contact within the time budget";

    auto& units = world.GetDeployedUnits();
    BattleUnit& attacker = units.at(duel.attackerId);
    BattleUnit& defender = units.at(duel.defenderId);
    attacker.attackTimer = 0.0;
    defender.attackTimer = 0.0;
    // Both units enter this tick lethally weak ("simultaneous death" setup) —
    // if the resolver let both attacks land, they'd both die (a mutual kill).
    attacker.currentHp = 1.0;
    defender.currentHp = 1.0;

    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    // The ascending-instanceId tie-break means only one attack is ever
    // resolved per pair per tick: attacker (lower id) acts first and kills
    // the defender before the defender's own turn, which is then skipped —
    // exactly one survivor, never a mutual kill.
    EXPECT_EQ(world.GetDeployedUnits().count(duel.defenderId), 0u);
    ASSERT_EQ(world.GetDeployedUnits().count(duel.attackerId), 1u);
    EXPECT_DOUBLE_EQ(world.GetDeployedUnits().at(duel.attackerId).currentHp, 1.0);
}
