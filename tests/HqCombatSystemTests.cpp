#include "core/GameWorld.h"
#include "core/GameCommand.h"
#include "core/GameSession.h"
#include "warfare/UnitMarchSystem.h"
#include "warfare/UnitCombatSystem.h"
#include "warfare/HqCombatSystem.h"
#include "simulation/MapGenerator.h"

#include <gtest/gtest.h>

// TD(etap-6.1/6.2) — HQ siege damage, thorns AoE, grouping, the "no free
// first hit" rule, elimination triggering, and the mode-conflict proposal
// (a fresh defender engages a besieger without interrupting its siege).

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

    int AddUnitToRoster(Player& player, const std::string& unitDefId)
    {
        int instanceId = player.id * 100000 + player.nextUnitInstanceId++;
        BattleUnit unit(instanceId, player.id, unitDefId);
        unit.currentHp = unit.GetEffectiveMaxHp(player);
        player.roster.AddUnit(std::move(unit));
        return instanceId;
    }

    HqComponent* FindHq(Player& player)
    {
        for (Building* building : player.GetTrackedBuildingsWithComponent<HqComponent>())
            return building->GetComponent<HqComponent>();
        return nullptr;
    }

    // Deploys a unit and fast-forwards it to AttackingHq via a single huge-dt
    // UnitMarchSystem tick (the pattern established in UnitMarchSystemTests).
    int DeployAndRushToHqDoor(GameWorld& world, int fromPlayerId, int toPlayerId, const std::string& unitDefId)
    {
        Player* player = world.GetPlayerHandler().players.at(fromPlayerId).get();
        int id = AddUnitToRoster(*player, unitDefId);
        world.SubmitCommand(GameCommand::DeployUnits(fromPlayerId, toPlayerId, {id}));
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
        UnitMarchSystem::Update(world, 100000.0);
        return id;
    }
}

TEST(HqCombatSystemTests, SiegeDamageAccountsForHardDefenseAndFloorsAtOne)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(101));
    Player* aiOwner = world.GetPlayerHandler().players.at(1).get();
    HqComponent* hq = FindHq(*aiOwner);
    ASSERT_NE(hq, nullptr);

    int besiegerId = DeployAndRushToHqDoor(world, 0, 1, "militia");
    ASSERT_EQ(world.GetDeployedUnits().at(besiegerId).state, BattleUnitState::AttackingHq);

    world.GetDeployedUnits().at(besiegerId).attackTimer = 0.0;
    double hpBefore = hq->currentHp;

    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    // militia siege_attack=1 vs hardDefense=10 floors at max(1, 1-10)=1,
    // scaled by the [0.9,1.1] variance and floored again at 1.0 — so the hit
    // lands in [1.0, 1.1].
    double dealt = hpBefore - hq->currentHp;
    EXPECT_GE(dealt, 1.0);
    EXPECT_LE(dealt, 1.1);
}

TEST(HqCombatSystemTests, GroupedBesiegersDealParallelDamageInTheSameTick)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(102));
    Player* aiOwner = world.GetPlayerHandler().players.at(1).get();
    Player* human = world.GetPlayerHandler().players.at(0).get();
    HqComponent* hq = FindHq(*aiOwner);
    ASSERT_NE(hq, nullptr);

    std::vector<int> ids;
    for (int i = 0; i < 3; i++)
        ids.push_back(AddUnitToRoster(*human, "militia"));
    world.SubmitCommand(GameCommand::DeployUnits(0, 1, ids));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);
    // One huge-dt Update only rushes whichever single unit currently holds
    // the gate tile all the way to the door, then pops the next queued unit
    // onto the now-free gate (without moving it yet, since the spawn phase
    // runs after the movement phase within the same call) — so draining all
    // 3 through the queue and to AttackingHq takes 3 such calls.
    for (int i = 0; i < 3; i++)
        UnitMarchSystem::Update(world, 100000.0);

    for (int id : ids)
    {
        ASSERT_EQ(world.GetDeployedUnits().at(id).state, BattleUnitState::AttackingHq)
            << "grouping (TD etap-6.2) should let all 3 units besiege in parallel";
        world.GetDeployedUnits().at(id).attackTimer = 0.0;
    }

    double hpBefore = hq->currentHp;
    world.UpdateSimulation(FixedSimulationClock::FixedDt);
    double dealt = hpBefore - hq->currentHp;

    // 3 independent hits, each in [1.0, 1.1] — no front-only restriction.
    EXPECT_GE(dealt, 3.0);
    EXPECT_LE(dealt, 3.3);
}

TEST(HqCombatSystemTests, NoFreeFirstHitOnHqArrival)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(103));
    Player* aiOwner = world.GetPlayerHandler().players.at(1).get();
    HqComponent* hq = FindHq(*aiOwner);
    ASSERT_NE(hq, nullptr);
    double maxHp = hq->currentHp;

    int besiegerId = DeployAndRushToHqDoor(world, 0, 1, "militia");
    ASSERT_EQ(world.GetDeployedUnits().at(besiegerId).state, BattleUnitState::AttackingHq);

    // The huge-dt march-only fast-forward doesn't run HqCombatSystem, so
    // advance one real tick to let it process the just-arrived unit.
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    EXPECT_GT(world.GetDeployedUnits().at(besiegerId).attackTimer, 0.5)
        << "arrival should set a full attack cooldown, not fire immediately";
    EXPECT_DOUBLE_EQ(hq->currentHp, maxHp) << "no damage should land the same tick the unit arrives";
}

TEST(HqCombatSystemTests, ThornsKillsAWeakenedBesiegerWithinRange)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(104));
    Player* aiOwner = world.GetPlayerHandler().players.at(1).get();
    HqComponent* hq = FindHq(*aiOwner);
    ASSERT_NE(hq, nullptr);

    int besiegerId = DeployAndRushToHqDoor(world, 0, 1, "militia");
    ASSERT_EQ(world.GetDeployedUnits().at(besiegerId).state, BattleUnitState::AttackingHq);

    // Guaranteed kill regardless of variance: thorns_damage=3 vs 0 armor
    // floors at 3, never drops below 3*0.9=2.7 — comfortably more than 1 hp.
    // Push the besieger's own attack timer out so this tick is thorns-only.
    world.GetDeployedUnits().at(besiegerId).currentHp = 1.0;
    world.GetDeployedUnits().at(besiegerId).attackTimer = 1000.0;
    hq->thornsTimer = 0.0;

    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    EXPECT_EQ(world.GetDeployedUnits().count(besiegerId), 0u)
        << "thorns should have killed the weakened besieger this tick";
}

TEST(HqCombatSystemTests, HqDeathTriggersElimination)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(105));
    Player* aiOwner = world.GetPlayerHandler().players.at(1).get();
    HqComponent* hq = FindHq(*aiOwner);
    ASSERT_NE(hq, nullptr);

    int besiegerId = DeployAndRushToHqDoor(world, 0, 1, "militia");
    world.GetDeployedUnits().at(besiegerId).attackTimer = 0.0;
    hq->currentHp = 0.5; // guaranteed to die to militia's >=1.0 hit

    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    EXPECT_TRUE(world.IsPlayerDefeated(1));
    EXPECT_EQ(world.GetVictorPlayerId(), 0);
}

TEST(HqCombatSystemTests, DefenderEngagesBesiegerWithoutInterruptingItsSiege)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(106));
    Player* human = world.GetPlayerHandler().players.at(0).get();
    HqComponent* humanHq = FindHq(*human);
    ASSERT_NE(humanHq, nullptr);

    // Player 1 (AI) besieges player 0's HQ.
    int besiegerId = DeployAndRushToHqDoor(world, 1, 0, "militia");
    ASSERT_EQ(world.GetDeployedUnits().at(besiegerId).state, BattleUnitState::AttackingHq);
    double hqHpAfterArrival = humanHq->currentHp;

    // Player 0 deploys a fresh defender back down the same route.
    int defenderId = AddUnitToRoster(*human, "militia");
    world.SubmitCommand(GameCommand::DeployUnits(0, 1, {defenderId}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    bool locked = false;
    for (int i = 0; i < 200 && !locked; i++)
    {
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
        auto it = world.GetDeployedUnits().find(defenderId);
        if (it != world.GetDeployedUnits().end() && it->second.state == BattleUnitState::FightingUnit)
            locked = true;
    }
    ASSERT_TRUE(locked) << "the fresh defender should lock onto the besieger parked at its own gate";
    EXPECT_EQ(world.GetDeployedUnits().at(besiegerId).state, BattleUnitState::AttackingHq)
        << "besiegers must never be interrupted by being targeted";

    // The HQ should still be taking siege damage from the (uninterrupted)
    // besieger while the defender separately duels it — run long enough for
    // a few of the besieger's ~1/s siege attacks to land (far less than the
    // ~7s a militia-vs-militia road duel needs to resolve, so the besieger is
    // still alive and still sieging throughout).
    for (int i = 0; i < 400; i++)
        world.UpdateSimulation(FixedSimulationClock::FixedDt);

    ASSERT_EQ(world.GetDeployedUnits().count(besiegerId), 1u)
        << "the besieger shouldn't have died to the defender's road attacks yet";
    EXPECT_LT(humanHq->currentHp, hqHpAfterArrival)
        << "the besieger should have kept sieging independently while being fought";
}

TEST(HqCombatSystemTests, SiegeToEliminationIsDeterministicForSameSeed)
{
    GameWorld worldA;
    GameWorld worldB;
    worldA.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(200));
    worldB.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(200));

    // Weaken both HQs identically so 3 militia bring it down within a bounded
    // tick budget, then deploy the same attackers on both worlds.
    FindHq(*worldA.GetPlayerHandler().players.at(1))->currentHp = 50.0;
    FindHq(*worldB.GetPlayerHandler().players.at(1))->currentHp = 50.0;

    std::vector<int> idsA, idsB;
    Player* humanA = worldA.GetPlayerHandler().players.at(0).get();
    Player* humanB = worldB.GetPlayerHandler().players.at(0).get();
    for (int i = 0; i < 3; i++)
    {
        idsA.push_back(AddUnitToRoster(*humanA, "militia"));
        idsB.push_back(AddUnitToRoster(*humanB, "militia"));
    }
    ASSERT_EQ(idsA, idsB);
    worldA.SubmitCommand(GameCommand::DeployUnits(0, 1, idsA));
    worldB.SubmitCommand(GameCommand::DeployUnits(0, 1, idsB));

    bool eliminatedA = false;
    for (int i = 0; i < 6000 && !eliminatedA; i++)
    {
        worldA.UpdateSimulation(FixedSimulationClock::FixedDt);
        worldB.UpdateSimulation(FixedSimulationClock::FixedDt);
        eliminatedA = worldA.IsPlayerDefeated(1);
        ASSERT_EQ(eliminatedA, worldB.IsPlayerDefeated(1)) << "tick=" << i;
        ASSERT_EQ(worldA.BuildChecksum(), worldB.BuildChecksum()) << "tick=" << i;
    }
    ASSERT_TRUE(eliminatedA) << "player 1's HQ should have fallen within the tick budget";

    // A few more ticks so ConqueredEconomy's ramp advances identically too.
    for (int i = 0; i < 200; i++)
    {
        worldA.UpdateSimulation(FixedSimulationClock::FixedDt);
        worldB.UpdateSimulation(FixedSimulationClock::FixedDt);
    }
    EXPECT_EQ(worldA.BuildChecksum(), worldB.BuildChecksum());
    EXPECT_EQ(worldA.GetVictorPlayerId(), worldB.GetVictorPlayerId());
}
