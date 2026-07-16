#include "core/GameWorld.h"
#include "core/GameSession.h"
#include "ai/AIActions.h"
#include "ai/AIModel.h"
#include "economy/Player.h"
#include "economy/BuildingComponents.h"
#include "simulation/MapGenerator.h"
#include "warfare/BattleUnit.h"
#include "warfare/UnitDefinition.h"

#include <gtest/gtest.h>

// AI rework etap 2 (TODO #2): the utility-based UtilityAIModel's economy +
// logistics layer, and the LogisticsComponent::IsConnectedToRoadNetwork check
// it keeps every building honest with.

namespace
{
    // Bare grass map, no Tile::owner — matches production maps post-pivot
    // (same rationale as RoadNetworkTests::FillUnownedMap).
    void FillGrassMap(TileMap& map, int width, int height)
    {
        map.params.sizeX = width;
        map.params.sizeY = height;
        map.tilemap.clear();
        map.tilemap.reserve(width * height);
        for (int i = 0; i < width * height; i++)
        {
            Tile tile{i};
            tile.tileType = TileType::GRASS;
            map.tilemap.push_back(std::move(tile));
        }
    }
}

TEST(UtilityAIModelTests, IsConnectedToRoadNetworkDetectsRoadPathToStorage)
{
    TileMap map;
    FillGrassMap(map, 10, 6);
    Player player{0, map};

    // Proven delivery layout (RoadNetworkTests): storage {0,1}, consumer
    // {5,1}, the two roads at {3,2},{4,2} complete the only path.
    auto* storage = player.Build<StorageBuilding>(Vec2i{0, 1}, false);
    auto* barracks = player.Build<Barracks>(Vec2i{5, 1}, false);
    ASSERT_NE(storage, nullptr);
    ASSERT_NE(barracks, nullptr);

    auto* logistics = barracks->GetComponent<LogisticsComponent>();
    ASSERT_NE(logistics, nullptr);

    EXPECT_FALSE(logistics->IsConnectedToRoadNetwork(*barracks))
        << "no roads yet - there is no path to any storage";

    ASSERT_NE(player.Build<Road>(Vec2i{3, 2}, false), nullptr);
    ASSERT_NE(player.Build<Road>(Vec2i{4, 2}, false), nullptr);

    EXPECT_TRUE(logistics->IsConnectedToRoadNetwork(*barracks))
        << "with the road pair placed, the path to the storage exists";
}

// Acceptance for etap 2: an AI player with resources available builds out
// its economy (opening plan / deficit chain) and keeps wiring it into the
// road network — real GameCommands flowing through UpdateSimulation, no
// direct state pokes.
TEST(UtilityAIModelTests, AIBuildsEconomyAndConnectsIt)
{
    MapParameters params;  // defaults: 301x301
    params.aiOpponentCount = 1;
    params.seed = 4242;

    GameWorld world;
    world.InitWorld("utility-ai-economy", nullptr, nullptr, params);

    Player* ai = world.GetPlayerHandler().players.at(1).get();
    ASSERT_NE(ai, nullptr);

    // Stock the AI's HQ so the opening plan is affordability-gated by design,
    // not by waiting out the (slow, separately-tested) natural economy.
    Building* hq = AIActions::FindOwnedHeadquarters(ai);
    ASSERT_NE(hq, nullptr);
    auto* hqStorage = hq->GetComponent<StorageComponent>();
    ASSERT_NE(hqStorage, nullptr);
    for (ResourceType type : {ResourceType::WOOD, ResourceType::PLANKS, ResourceType::STONE,
                              ResourceType::IRON, ResourceType::TOOLS})
    {
        auto it = hqStorage->buffers.find(type);
        if (it != hqStorage->buffers.end())
            it->second.SetStoredAmount(100);
        else
        {
            hqStorage->buffers[type] = ResourceBuffer{type, 200};
            hqStorage->buffers[type].SetStoredAmount(100);
        }
    }

    int initialBuildings = static_cast<int>(ai->GetTrackedBuildings().size());
    int initialRoads = AIActions::CountOwnedBuildings(ai, BuildingType::Road);

    // 120 sim-seconds at the fixed 100 Hz tick — several decision cycles.
    auto newRoadCount = [&]()
    { return AIActions::CountOwnedBuildings(ai, BuildingType::Road) - initialRoads; };
    auto newNonRoadCount = [&]()
    {
        int newTotal = static_cast<int>(ai->GetTrackedBuildings().size()) - initialBuildings;
        return newTotal - newRoadCount();
    };
    for (int tick = 0; tick < 12000 && !(newNonRoadCount() >= 1 && newRoadCount() >= 1); tick++)
        world.UpdateSimulation(0.01);

    int newRoads = newRoadCount();
    int newBuildings = newNonRoadCount();

    EXPECT_GE(newBuildings, 1) << "the AI should have placed at least one economy building";
    EXPECT_GE(newRoads, 1) << "the AI should be wiring its base into the road network";
}

// Etap 3: composition rule, pure and world-free. Under attack the pick
// maximizes staying power per cost (assets/data/units.rtsdata: knight);
// on the offensive an empty roster starts with a siege unit (ram), then
// tops the 2:1 mix back up with the best lane-clearer (knight by
// moveSpeed x roadAttack).
TEST(UtilityAIModelTests, RosterCompositionPrefersDefensiveUnitsUnderAttack)
{
    AISituation s;
    s.enemyIncomingCount = 4;
    s.myDeployedCount = 0;

    auto ranked = UtilityAIModel::RankUnitChoices(s);
    ASSERT_FALSE(ranked.empty());
    EXPECT_EQ(ranked.front()->id, "knight");
}

TEST(UtilityAIModelTests, RosterCompositionKeepsSiegeInTheOffensiveMix)
{
    AISituation s;  // nobody incoming — offensive posture

    auto ranked = UtilityAIModel::RankUnitChoices(s);
    ASSERT_FALSE(ranked.empty());
    EXPECT_EQ(ranked.front()->id, "knight")
        << "an empty offensive roster starts with lane-fighters (siege needs an escort)";

    s.rosterByDef["militia"] = 2;
    s.rosterCount = 2;
    ranked = UtilityAIModel::RankUnitChoices(s);
    ASSERT_FALSE(ranked.empty());
    EXPECT_EQ(ranked.front()->id, "ram") << "with an escort standing, top the 2:1 mix up with siege";

    s.rosterByDef["ram"] = 1;
    s.rosterCount = 3;
    ranked = UtilityAIModel::RankUnitChoices(s);
    ASSERT_FALSE(ranked.empty());
    EXPECT_EQ(ranked.front()->id, "knight") << "at 1 siege per 2 fighters, back to lane-clearers";
}

// Etap 3 acceptance (successor of the removed AIMilitaryPipelineTests):
// given a Barracks, stocked unit costs and manpower, the AI recruits real
// units and deploys a wave — a real GameCommand::DeployUnits reaching
// UpdateSimulation. Barracks placed directly and the roster pre-seeded near
// the wave threshold, so the test exercises recruit+deploy without waiting
// out the natural economy.
TEST(UtilityAIModelTests, AIRecruitsAndDeploysAWave)
{
    MapParameters params;  // defaults: 301x301
    params.aiOpponentCount = 1;
    params.seed = 777;

    GameWorld world;
    world.InitWorld("utility-ai-military", nullptr, nullptr, params);

    Player* ai = world.GetPlayerHandler().players.at(1).get();
    ASSERT_NE(ai, nullptr);

    Building* hq = AIActions::FindOwnedHeadquarters(ai);
    ASSERT_NE(hq, nullptr);
    Vec2i hqPos = world.GetTileMap().GetCoordsFromId(hq->positionId);

    // Free spot for a test Barracks near the AI's HQ — expanding square scan.
    Vec2i barracksPos{-1, -1};
    Vec2i footprint = GetBuildingDefinition(BuildingType::Barracks).footprint;
    for (int radius = 5; radius <= 40 && barracksPos.x < 0; radius += 2)
        for (int y = hqPos.y - radius; y <= hqPos.y + radius && barracksPos.x < 0; y++)
            for (int x = hqPos.x - radius; x <= hqPos.x + radius; x++)
            {
                Vec2i pos{x, y};
                if (world.GetTileMap().IsInside(pos) &&
                    world.GetTileMap().CanPlaceBuilding(BuildingType::Barracks, pos, footprint, ai))
                {
                    barracksPos = pos;
                    break;
                }
            }
    ASSERT_GE(barracksPos.x, 0) << "no free spot for a test Barracks near the AI's HQ";

    Building* barracks = ai->Build<Barracks>(barracksPos, false);
    ASSERT_NE(barracks, nullptr);
    ASSERT_FALSE(barracks->IsUnderConstruction());

    // Stock militia's cost locally + manpower for the remaining recruits.
    auto* storage = barracks->GetComponent<StorageComponent>();
    ASSERT_NE(storage, nullptr);
    auto foodIt = storage->buffers.find(ResourceType::FOOD_PROVISIONS);
    if (foodIt == storage->buffers.end())
    {
        storage->buffers[ResourceType::FOOD_PROVISIONS] = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 40};
        foodIt = storage->buffers.find(ResourceType::FOOD_PROVISIONS);
    }
    foodIt->second.SetStoredAmount(40);
    ai->strategicResources.Set(StrategicResourceType::Manpower, 200);

    // Drain sword/siege-cost stocks from every AI storage so militia is the
    // only affordable pick — keeps the test's timeline on the 8 s militia
    // recruit instead of a 26 s ram plus multi-resource deliveries.
    for (Building* building : ai->GetTrackedBuildingsWithComponent<StorageComponent>())
    {
        auto* buildingStorage = building != nullptr ? building->GetComponent<StorageComponent>() : nullptr;
        if (buildingStorage == nullptr || building == barracks)
            continue;
        for (ResourceType type : {ResourceType::PLANKS, ResourceType::IRON,
                                  ResourceType::IRON_SWORD, ResourceType::STEEL_SWORD})
        {
            auto it = buildingStorage->buffers.find(type);
            if (it != buildingStorage->buffers.end())
                it->second.Clear();
        }
    }

    // Pre-seed most of the wave (threshold is the model's WaveSize, 6).
    for (int i = 0; i < 5; i++)
    {
        int instanceId = ai->id * 100000 + ai->nextUnitInstanceId++;
        BattleUnit unit(instanceId, ai->id, "militia");
        unit.currentHp = unit.GetEffectiveMaxHp(*ai);
        ai->roster.AddUnit(std::move(unit));
    }
    int instanceCounterBefore = ai->nextUnitInstanceId;

    // 60 sim-seconds: recruit the missing unit(s), deploy, first unit spawns.
    bool deployed = false;
    for (int tick = 0; tick < 6000 && !deployed; tick++)
    {
        world.UpdateSimulation(0.01);
        for (const auto& [instanceId, unit] : world.GetDeployedUnits())
            if (unit.ownerPlayerId == ai->id)
            {
                deployed = true;
                break;
            }
    }

    EXPECT_TRUE(deployed) << "the AI never got a unit marching on the military road";
    EXPECT_GT(ai->nextUnitInstanceId, instanceCounterBefore)
        << "the AI should have recruited at least one real unit itself";
}

// Etap 4: the difficulty noise must be seeded, never wall-clock or unseeded —
// two independently constructed same-seed worlds with noise ACTIVE (Easy)
// must draw the identical decision sequence and stay checksum-identical.
TEST(UtilityAIModelTests, TwoWorldsSameSeedWithNoisyAIStayInSync)
{
    MapParameters params;
    params.aiOpponentCount = 1;
    params.seed = 31337;
    params.aiDifficulty = 1;  // Easy — noise amplitude and cycle-skips active

    GameWorld worldA;
    GameWorld worldB;
    worldA.InitWorld("noisy-determinism", nullptr, nullptr, params);
    worldB.InitWorld("noisy-determinism", nullptr, nullptr, params);

    // 30 sim-seconds, checksum compared every sim-second.
    for (int tick = 0; tick < 3000; tick++)
    {
        worldA.UpdateSimulation(FixedSimulationClock::FixedDt);
        worldB.UpdateSimulation(FixedSimulationClock::FixedDt);
        if (tick % 100 == 99)
            ASSERT_EQ(worldA.BuildChecksum(), worldB.BuildChecksum()) << "tick=" << tick;
    }
}

// Etap 4: higher difficulty = a bigger head start (resources + manpower into
// the AI's HQ at init), never a different algorithm.
TEST(UtilityAIModelTests, HigherDifficultyGrantsStartingAdvantage)
{
    MapParameters params;
    params.aiOpponentCount = 1;
    params.seed = 5150;

    params.aiDifficulty = 0;  // Primitive — no head start
    GameWorld primitiveWorld;
    primitiveWorld.InitWorld("difficulty-baseline", nullptr, nullptr, params);

    params.aiDifficulty = 3;  // Hard — biggest head start
    GameWorld hardWorld;
    hardWorld.InitWorld("difficulty-hard", nullptr, nullptr, params);

    Player* aiPrimitive = primitiveWorld.GetPlayerHandler().players.at(1).get();
    Player* aiHard = hardWorld.GetPlayerHandler().players.at(1).get();
    Player* humanHard = hardWorld.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(aiPrimitive, nullptr);
    ASSERT_NE(aiHard, nullptr);
    ASSERT_NE(humanHard, nullptr);

    EXPECT_GT(AIActions::CountStoredResource(aiHard, ResourceType::WOOD),
              AIActions::CountStoredResource(aiPrimitive, ResourceType::WOOD))
        << "Hard AI should start with more resources than a Primitive AI";
    EXPECT_GT(AIActions::CountStoredResource(aiHard, ResourceType::WOOD),
              AIActions::CountStoredResource(humanHard, ResourceType::WOOD))
        << "the head start is the AI's advantage over the human";
    EXPECT_GT(aiHard->strategicResources.Get(StrategicResourceType::Manpower),
              humanHard->strategicResources.Get(StrategicResourceType::Manpower))
        << "Hard AI also starts with a manpower cushion";
}
