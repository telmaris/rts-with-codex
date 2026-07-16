#include "core/GameWorld.h"
#include "simulation/MapGenerator.h"
#include "economy/BuildingComponents.h"
#include "warfare/BattleUnit.h"

#include <gtest/gtest.h>

namespace
{
    // Finds a free footprint-sized spot near `origin` by scanning an expanding
    // square — mirrors what a human/AI build UI would do, without pulling in
    // PrimitiveAIModel's private FindBuildAnchor.
    Vec2i FindFreeSpotNear(TileMap& map, Player* player, BuildingType type, Vec2i origin)
    {
        Vec2i footprint = GetBuildingDefinition(type).footprint;
        for (int radius = 5; radius <= 40; radius += 2)
        {
            for (int y = origin.y - radius; y <= origin.y + radius; y++)
            {
                for (int x = origin.x - radius; x <= origin.x + radius; x++)
                {
                    Vec2i pos{x, y};
                    if (!map.IsInside(pos))
                        continue;
                    if (map.CanPlaceBuilding(type, pos, footprint, player))
                        return pos;
                }
            }
        }
        return Vec2i{-1, -1};
    }
}

// C1 (docs/work_plan_2026-07-13.md) acceptance criterion: given a Barracks
// and the resources/manpower a unit costs, the AI should recruit units and
// eventually deploy a wave (a real GameCommand::DeployUnits reaching
// UpdateSimulation) — the new Military axis + Recruit/Attack pipeline this
// task added. A Barracks is placed directly (Player::Build<T>(..., false),
// bypassing cost/construction time) and the roster is pre-seeded most of the
// way to the (personality-scaled, unknown at test-write-time) attack
// threshold, rather than waiting through several full 8s+ recruit cycles —
// the AI's own recruitment is still exercised and verified (nextUnitInstanceId
// must advance), just not relied on for the entire roster. This also avoids a
// separate, pre-existing issue found while writing this test: a long-running
// economy's own HQ/StorageBuilding resource redistribution can fall into a
// slow repeating transport pattern (see docs/tech_debt.md) — keeping this
// test's required sim-time short avoids depending on that unrelated behavior.
TEST(AIMilitaryPipelineTests, AIWithABarracksRecruitsAndDeploysAWave)
{
    MapParameters params;      // defaults: 301x301
    params.aiOpponentCount = 1;
    params.debugMode = true;

    GameWorld world;
    world.InitWorld("ai-military-pipeline", nullptr, nullptr, params);

    Player* ai = world.GetPlayerHandler().players.at(1).get();
    ASSERT_NE(ai, nullptr);

    Building* hq = nullptr;
    for (Building* b : ai->GetTrackedBuildingsWithComponent<HqComponent>())
        hq = b;
    ASSERT_NE(hq, nullptr);
    Vec2i hqPos = world.GetTileMap().GetCoordsFromId(hq->positionId);

    Vec2i barracksPos = FindFreeSpotNear(world.GetTileMap(), ai, BuildingType::Barracks, hqPos);
    ASSERT_NE(barracksPos.x, -1) << "couldn't find a free spot for a test Barracks near the AI's HQ";

    Building* barracks = ai->Build<Barracks>(barracksPos, false);
    ASSERT_NE(barracks, nullptr);
    ASSERT_FALSE(barracks->IsUnderConstruction());

    // Stock only militia's own cost (FOOD_PROVISIONS), enough for a couple of
    // real recruits — proves QueueRecruitment fires without over-seeding
    // every unit-cost buffer (which pulled in unrelated storage-redistribution
    // activity — see the test-level comment above).
    auto* storage = barracks->GetComponent<StorageComponent>();
    ASSERT_NE(storage, nullptr);
    auto foodIt = storage->buffers.find(ResourceType::FOOD_PROVISIONS);
    ASSERT_NE(foodIt, storage->buffers.end());
    for (int i = 0; i < 20; i++)
        foodIt->second.GenerateResource(ResourceType::FOOD_PROVISIONS);
    ai->strategicResources.Add(StrategicResourceType::Manpower, 100.0);

    // Pre-seed a SMALL head start on the roster — enough to shorten the wait
    // for AttackReadyRosterSize, but deliberately not enough to fill it
    // outright: EvaluateAxis(Military) reads roster size against a
    // personality-scaled "comfortable garrison" (~3 for this fixed AI
    // personality, since playerId always derives the same one), and if the
    // pre-seed already covers that, the AI correctly sees no pressure to
    // recruit more and the recruit half of this test would never fire even
    // though recruitment itself works fine.
    for (int i = 0; i < 2; i++)
    {
        int instanceId = ai->id * 100000 + ai->nextUnitInstanceId++;
        BattleUnit unit(instanceId, ai->id, "militia");
        unit.currentHp = unit.GetEffectiveMaxHp(*ai);
        ai->roster.AddUnit(std::move(unit));
    }
    int seededRoster = static_cast<int>(ai->roster.units.size());

    const int ticks = 6000;   // 60 sim-seconds — comfortably past attackTimer's 30s default
    const double dt = 0.01;

    bool sawDeploy = false;
    for (int i = 0; i < ticks && !sawDeploy; i++)
    {
        world.UpdateSimulation(dt);
        for (const auto& [instanceId, unit] : world.GetDeployedUnits())
            if (unit.ownerPlayerId == ai->id) { sawDeploy = true; break; }
    }

    EXPECT_GT(ai->nextUnitInstanceId, seededRoster + 1) << "AI never recruited a unit from the seeded Barracks";
    EXPECT_TRUE(sawDeploy) << "AI never deployed a wave (GameCommand::DeployUnits)";
}
