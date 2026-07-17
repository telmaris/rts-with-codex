#include "core/GameWorld.h"
#include "ai/AIActions.h"
#include "economy/Player.h"
#include "economy/BuildingComponents.h"
#include "simulation/MapGenerator.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

// AI behavior verification harness (user request after the 2026-07-16
// playtest): a repeatable, headless acceptance run that catches "wedged AI"
// regressions the unit tests can't see — like the bridge stall, whose
// signature was a stream of rejected build commands and zero progress at the
// military track. It runs a real world for several sim-minutes, samples
// progress in 30-second windows, counts every accepted/rejected AI command
// (with rejection reasons), and prints the whole behavioral table when any
// expectation fails — so a future stall is diagnosed from the test output
// alone, not by replaying the game.
//
// Budget note: ~5 sim-minutes on the default 301x301 map costs tens of
// seconds of wall time in Debug — deliberate, this is the coarse behavioral
// gate, not a unit test.
namespace
{
    struct WindowSample
    {
        double simSeconds{0.0};
        int buildings{0};
        int roads{0};
        int roster{0};
        int deployed{0};
        int accepted{0};
        int rejected{0};
    };
}

TEST(AIBehaviorHarnessTests, HardAIMakesSteadyProgressAndAttacks)
{
    MapParameters params;
    // Pinned to the pre-2026-07-17 minimum: these tests exercise AI behavior,
    // not map size, and the 401x401 default costs ~1.8x the sim time.
    params.sizeX = 301;
    params.sizeY = 301;
    params.aiOpponentCount = 1;
    params.seed = 20260716;
    params.aiDifficulty = 3;  // Hard: the production head start compresses the timeline

    GameWorld world;
    world.InitWorld("ai-behavior-harness", nullptr, nullptr, params);

    Player* ai = world.GetPlayerHandler().players.at(1).get();
    ASSERT_NE(ai, nullptr);

    int initialBuildings = static_cast<int>(ai->GetTrackedBuildings().size());
    int initialRoads = AIActions::CountOwnedBuildings(ai, BuildingType::Road);
    int initialInstanceCounter = ai->nextUnitInstanceId;

    std::vector<WindowSample> samples;
    std::map<std::string, int> rejectionReasons;
    int accepted = 0;
    int rejected = 0;
    int maxDeployedSeen = 0;

    constexpr int TicksPerWindow = 3000;  // 30 sim-seconds at the fixed 100 Hz tick
    constexpr int Windows = 10;           // 5 sim-minutes total
    for (int window = 0; window < Windows; window++)
    {
        for (int tick = 0; tick < TicksPerWindow; tick++)
        {
            world.UpdateSimulation(0.01);
            for (const auto& result : world.ConsumeCommandResults())
            {
                if (result.playerId != ai->id)
                    continue;
                if (result.accepted)
                    accepted++;
                else
                {
                    rejected++;
                    rejectionReasons[result.reason.empty() ? "(no reason)" : result.reason]++;
                }
            }
        }

        int deployedNow = 0;
        for (const auto& [instanceId, unit] : world.GetDeployedUnits())
            if (unit.ownerPlayerId == ai->id && unit.state != BattleUnitState::Dying)
                deployedNow++;
        maxDeployedSeen = std::max(maxDeployedSeen, deployedNow);

        WindowSample sample;
        sample.simSeconds = (window + 1) * 30.0;
        sample.buildings = static_cast<int>(ai->GetTrackedBuildings().size()) - initialBuildings;
        sample.roads = AIActions::CountOwnedBuildings(ai, BuildingType::Road) - initialRoads;
        sample.roster = static_cast<int>(ai->roster.units.size());
        sample.deployed = deployedNow;
        sample.accepted = accepted;
        sample.rejected = rejected;
        samples.push_back(sample);
    }

    auto report = [&]()
    {
        std::string out = "\nAI behavior report (deltas vs. world init):\n"
                          "  sim_s | +bldg | +road | roster | deployed | cmd_ok | cmd_rej\n";
        char line[128];
        for (const auto& s : samples)
        {
            std::snprintf(line, sizeof(line), "  %5.0f | %5d | %5d | %6d | %8d | %6d | %7d\n",
                          s.simSeconds, s.buildings - s.roads, s.roads, s.roster,
                          s.deployed, s.accepted, s.rejected);
            out += line;
        }
        if (!rejectionReasons.empty())
        {
            out += "  rejection reasons:\n";
            for (const auto& [reason, count] : rejectionReasons)
            {
                std::snprintf(line, sizeof(line), "    %4d x %s\n", count, reason.c_str());
                out += line;
            }
        }
        return out;
    };

    // Wedge detector — the bridge-stall signature: the AI keeps submitting
    // commands the simulation refuses, while accepted work stagnates.
    EXPECT_LE(rejected, accepted * 2 + 20)
        << "the AI spams commands the simulation refuses" << report();

    // Economy progress: it builds, and wires what it builds.
    const WindowSample& last = samples.back();
    EXPECT_GE(last.buildings - last.roads, 4)
        << "the AI placed almost no buildings" << report();
    EXPECT_GE(last.roads, 4)
        << "the AI is not wiring its base into the road network" << report();

    // Military pipeline end-to-end: Barracks -> recruits -> units on the track.
    EXPECT_GE(AIActions::CountOwnedBuildings(ai, BuildingType::Barracks), 1)
        << "no Barracks placed" << report();
    EXPECT_GT(ai->nextUnitInstanceId, initialInstanceCounter)
        << "no unit was ever recruited" << report();
    EXPECT_GE(maxDeployedSeen, 1)
        << "no wave was ever deployed onto the military road" << report();

    // Logistics quality (playtest 2026-07-17 "cuda" report): by the end of
    // the run every COMPLETED producer must have a real road path to a
    // storage — at most one building may still be waiting on roads under
    // construction. Catches both the "orphan stubs everywhere, nothing
    // connected" wedge (the old 8-tile path cap) and future regressions.
    int unconnected = 0;
    std::string unconnectedList;
    for (const auto* building : ai->GetTrackedBuildingsWithComponent<LogisticsComponent>())
    {
        if (building == nullptr || building->owner != ai || building->IsUnderConstruction())
            continue;
        if (IsRoadLike(building->buildingType) || building->IsStorageLike() ||
            building->buildingType == BuildingType::Headquarters)
            continue;
        const auto* logistics = building->GetComponent<LogisticsComponent>();
        if (logistics == nullptr)
            continue;
        if (!logistics->IsConnectedToRoadNetwork(*const_cast<Building*>(building)))
        {
            unconnected++;
            unconnectedList += " " + building->name;
        }
    }
    EXPECT_LE(unconnected, 1)
        << "completed buildings without a road path to any storage:" << unconnectedList << report();
}
