#include "core/GameWorld.h"
#include "simulation/MilitaryRoadNetwork.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <set>

namespace
{
    MapParameters MakeParams(int aiOpponentCount, unsigned int seed)
    {
        MapParameters params;
        params.sizePreset = MapSizePreset::S;
        params.aiOpponentCount = aiOpponentCount;
        params.seed = seed;
        return params;
    }

    // Verifies the ring is a single connected cycle touching every player
    // exactly once, by walking neighbor links starting from player 0.
    void ExpectSingleConnectedRing(const MilitaryRoadNetwork& roads, int playerCount)
    {
        std::set<int> visited;
        int current = 0;
        int previous = -1;
        for (int step = 0; step < playerCount; step++)
        {
            visited.insert(current);
            auto neighbors = roads.GetNeighbors(current);
            ASSERT_EQ(neighbors.size(), playerCount == 2 ? 1u : 2u) << "player " << current;
            int next = -1;
            for (int n : neighbors)
                if (n != previous)
                {
                    next = n;
                    break;
                }
            if (next == -1)
                next = neighbors.front();
            previous = current;
            current = next;
        }
        EXPECT_EQ(visited.size(), static_cast<size_t>(playerCount));
    }
}

TEST(MilitaryRoadNetworkTests, TwoPlayersGetExactlyOneMutualRoute)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeParams(1, 111));

    const auto& roads = world.GetMilitaryRoads();
    ASSERT_EQ(roads.GetRoutes().size(), 1u);
    EXPECT_TRUE(roads.AreConnected(0, 1));
    EXPECT_EQ(roads.GetNeighbors(0), std::vector<int>{1});
    EXPECT_EQ(roads.GetNeighbors(1), std::vector<int>{0});

    const MilitaryRoute* route = roads.FindRoute(0, 1);
    ASSERT_NE(route, nullptr);
    EXPECT_FALSE(route->tiles.empty());
}

TEST(MilitaryRoadNetworkTests, ThreeFourAndFivePlayersFormASingleRing)
{
    for (int aiOpponentCount : {2, 3, 4})
    {
        GameWorld world;
        world.InitWorld("test", nullptr, nullptr, MakeParams(aiOpponentCount, 222));

        int playerCount = aiOpponentCount + 1;
        const auto& roads = world.GetMilitaryRoads();
        EXPECT_EQ(roads.GetRoutes().size(), static_cast<size_t>(playerCount))
            << "playerCount=" << playerCount;
        ExpectSingleConnectedRing(roads, playerCount);
    }
}

TEST(MilitaryRoadNetworkTests, GenerationIsDeterministicForSameSeedAndParams)
{
    GameWorld worldA;
    GameWorld worldB;
    worldA.InitWorld("test", nullptr, nullptr, MakeParams(3, 777));
    worldB.InitWorld("test", nullptr, nullptr, MakeParams(3, 777));

    const auto& routesA = worldA.GetMilitaryRoads().GetRoutes();
    const auto& routesB = worldB.GetMilitaryRoads().GetRoutes();
    ASSERT_EQ(routesA.size(), routesB.size());

    for (const auto& routeA : routesA)
    {
        const MilitaryRoute* matched = worldB.GetMilitaryRoads().FindRoute(routeA.playerA, routeA.playerB);
        ASSERT_NE(matched, nullptr);
        EXPECT_EQ(routeA.tiles, matched->tiles) << "route " << routeA.playerA << "-" << routeA.playerB;
    }
}

TEST(MilitaryRoadNetworkTests, NoBuildingOrResourceRoadCanBePlacedOnMilitaryRoadTile)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeParams(1, 333));

    const auto& roads = world.GetMilitaryRoads();
    ASSERT_FALSE(roads.GetRoutes().empty());
    ASSERT_FALSE(roads.GetRoutes().front().tiles.empty());
    int militaryTileId = roads.GetRoutes().front().tiles[roads.GetRoutes().front().tiles.size() / 2];

    TileMap& map = world.GetTileMap();
    Vec2i pos = map.GetCoordsFromId(militaryTileId);
    EXPECT_TRUE(map[pos].isMilitaryRoad);

    Player* player = world.GetPlayerHandler().players.at(0).get();
    EXPECT_FALSE(map.CanPlaceBuilding(BuildingType::Road, pos, {1, 1}, player));
    EXPECT_FALSE(map.CanPlaceBuilding(BuildingType::StorageBuilding, pos, GetBuildingDefinition(BuildingType::StorageBuilding).footprint, player));
}

TEST(MilitaryRoadNetworkTests, SaveAndLoadPreservesRoutesAndTileFlags)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeParams(2, 444));

    const auto path = (std::filesystem::temp_directory_path() / "rts_military_road_test.save").string();
    ASSERT_TRUE(world.SaveToFile(path));

    GameWorld loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path, nullptr, nullptr));

    const auto& originalRoutes = world.GetMilitaryRoads().GetRoutes();
    const auto& loadedRoutes = loaded.GetMilitaryRoads().GetRoutes();
    ASSERT_EQ(originalRoutes.size(), loadedRoutes.size());

    for (const auto& route : originalRoutes)
    {
        const MilitaryRoute* matched = loaded.GetMilitaryRoads().FindRoute(route.playerA, route.playerB);
        ASSERT_NE(matched, nullptr);
        EXPECT_EQ(route.tiles, matched->tiles);
    }

    for (const auto& route : loadedRoutes)
        for (int tileId : route.tiles)
            EXPECT_TRUE(loaded.GetTileMap().tilemap[tileId].isMilitaryRoad);

    std::filesystem::remove(path);
}
