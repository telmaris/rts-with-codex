#include "core/GameWorld.h"
#include "ai/AIActions.h"
#include "economy/Player.h"
#include "economy/BuildingComponents.h"
#include "simulation/MapGenerator.h"

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
