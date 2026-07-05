#include "simulation/PathingService.h"
#include "simulation/MapGenerator.h"
#include "simulation/RoadNetwork.h"
#include "economy/Player.h"

#include <gtest/gtest.h>

namespace
{
    // Creates a rectangular grass map with owner assigned
    void FillOwnedMap(TileMap& map, Player* owner, int width = 10, int height = 10)
    {
        map.params.sizeX = width;
        map.params.sizeY = height;
        map.tilemap.clear();
        map.tilemap.reserve(width * height);
        for (int i = 0; i < width * height; i++)
        {
            Tile tile{i};
            tile.owner = owner;
            tile.tileType = TileType::GRASS;
            map.tilemap.push_back(std::move(tile));
        }
    }

    // Places a building on map and returns it
    template <typename T>
    T* PlaceBuilding(TileMap& map, Player* owner, Vec2i anchor, int id)
    {
        int tileId = map.GetIdFromCoords(anchor);
        auto* placed = dynamic_cast<T*>(map.PlaceLoadedBuilding(tileId, owner, std::make_unique<T>(id)));
        return placed;
    }
}

TEST(PathingServiceTests, DijkstraPathFindsSimpleRoute)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player, 10, 10);
    RoadNetwork network{map};
    PathingService pather{map, network};

    RoadPath path = pather.FindRoadPath({0, 0}, {5, 5});

    EXPECT_TRUE(path.IsValid());
    EXPECT_GT(path.tileCount, 0);
    EXPECT_GT(path.totalCost, 0.0);
}

TEST(PathingServiceTests, DijkstraPathSameSourceAndDestination)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player, 10, 10);
    RoadNetwork network{map};
    PathingService pather{map, network};

    RoadPath path = pather.FindRoadPath({3, 3}, {3, 3});

    EXPECT_TRUE(path.IsValid());
    EXPECT_EQ(path.tileCount, 1);
    EXPECT_EQ(path.totalCost, 0.0);
}

TEST(PathingServiceTests, DijkstraPathInvalidCoordinates)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player, 10, 10);
    RoadNetwork network{map};
    PathingService pather{map, network};

    RoadPath path = pather.FindRoadPath({-1, -1}, {5, 5});

    EXPECT_FALSE(path.IsValid());
}

TEST(PathingServiceTests, BFSFindNearestBuilding)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player, 10, 10);
    RoadNetwork network{map};
    PathingService pather{map, network};

    auto* source = PlaceBuilding<StorageBuilding>(map, &player, {0, 0}, 1);
    auto* target = PlaceBuilding<StorageBuilding>(map, &player, {3, 3}, 2);

    ASSERT_NE(source, nullptr);
    ASSERT_NE(target, nullptr);

    auto predicate = [&](const Building* b) -> bool {
        return b != nullptr && b != source && b->buildingType == BuildingType::StorageBuilding;
    };

    Building* found = pather.FindNearestBuilding({0, 0}, predicate, Domain::Global());

    EXPECT_EQ(found, target);
}

TEST(PathingServiceTests, BFSFindNearestBuildingNoMatch)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player, 10, 10);
    RoadNetwork network{map};
    PathingService pather{map, network};

    auto* source = PlaceBuilding<StorageBuilding>(map, &player, {0, 0}, 1);
    ASSERT_NE(source, nullptr);

    auto predicate = [&](const Building* b) -> bool {
        return b != nullptr && b->buildingType == BuildingType::Barracks;
    };

    Building* found = pather.FindNearestBuilding({0, 0}, predicate, Domain::Global());

    EXPECT_EQ(found, nullptr);
}

TEST(PathingServiceTests, EuclideanDistance)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};
    PathingService pather{map, network};

    double dist = pather.Distance({0.0f, 0.0f}, {3.0f, 4.0f});

    EXPECT_FLOAT_EQ(dist, 5.0f);
}

TEST(PathingServiceTests, ManhattanDistance)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};
    PathingService pather{map, network};

    int dist = pather.TileDistance({0, 0}, {3, 4});

    EXPECT_EQ(dist, 7);
}

TEST(PathingServiceTests, DeterministicTieBreaking)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player, 10, 10);
    RoadNetwork network{map};
    PathingService pather{map, network};

    // Place two identical buildings equidistant from source
    auto* left = PlaceBuilding<StorageBuilding>(map, &player, {2, 0}, 1);
    auto* right = PlaceBuilding<StorageBuilding>(map, &player, {5, 0}, 2);

    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);

    auto predicate = [&](const Building* b) -> bool {
        return b != nullptr && b->buildingType == BuildingType::StorageBuilding;
    };

    // BFS should consistently find same building (deterministic via tile order)
    Building* found = pather.FindNearestBuilding({3, 3}, predicate, Domain::Global());

    EXPECT_NE(found, nullptr);

    // Run multiple times to verify determinism
    for (int i = 0; i < 5; i++)
    {
        Building* foundAgain = pather.FindNearestBuilding({3, 3}, predicate, Domain::Global());
        EXPECT_EQ(found, foundAgain) << "Pathfinding result not deterministic on iteration " << i;
    }
}
