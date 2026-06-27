#include "../inc/MapGenerator.h"
#include "../inc/Player.h"
#include "../inc/RoadNetwork.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
    // Creates a rectangular player-owned grass map for pathing tests.
    void FillOwnedMap(TileMap& map, Player* owner, int width = 10, int height = 6)
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

    // Places a loaded building and registers every footprint tile in the road network.
    template <typename T>
    T* PlaceAndRegister(TileMap& map, RoadNetwork& network, Player* owner, Vec2i anchor, int id)
    {
        int tileId = map.GetIdFromCoords(anchor);
        auto* placed = dynamic_cast<T*>(map.PlaceLoadedBuilding(tileId, owner, std::make_unique<T>(id)));
        if (placed == nullptr)
            return nullptr;

        for (int occupiedTileId : map.GetBuildingTileIds(placed))
            network.UpdateNavMap(occupiedTileId, placed);
        return placed;
    }
}

TEST(RoadNetworkTests, CalculatesPathAcrossRoadTilesBetweenBuildingFootprints)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* source = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* destination = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);

    auto* roadA = PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3);
    auto* roadB = PlaceAndRegister<Road>(map, network, &player, {4, 2}, 4);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);

    std::vector<int> path = network.CalculatePath(source, destination);

    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.front(), map.GetIdFromCoords({2, 2}));
    EXPECT_EQ(path.back(), map.GetIdFromCoords({5, 2}));
    EXPECT_NE(std::find(path.begin(), path.end(), map.GetIdFromCoords({3, 2})), path.end());
    EXPECT_NE(std::find(path.begin(), path.end(), map.GetIdFromCoords({4, 2})), path.end());
}

TEST(RoadNetworkTests, ReturnsEmptyPathWhenRoadConnectionIsBroken)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* source = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* destination = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);

    PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3);

    EXPECT_TRUE(network.CalculatePath(source, destination).empty());
}

TEST(RoadNetworkTests, BeginTransportQueuesResourceOnSourceWhenPathAndCapacityExist)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* source = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* destination = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    auto* roadA = PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3);
    auto* roadB = PlaceAndRegister<Road>(map, network, &player, {4, 2}, 4);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);

    source->resourceBuffers.clear();
    destination->resourceBuffers.clear();
    destination->resourceBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};

    Resource wood{ResourceType::WOOD};
    ASSERT_TRUE(network.BeginTransport(source, destination, &wood));

    ASSERT_EQ(source->transportables.size(), 1u);
    EXPECT_EQ(source->transportables.front(), &wood);
    EXPECT_EQ(wood.sourceBuilding, source);
    EXPECT_EQ(wood.targetBuilding, destination);
    EXPECT_FALSE(wood.transportPath.empty());
}

TEST(RoadNetworkTests, HeadquartersAcceptsPaperResource)
{
    Headquarters destination{2};
    ASSERT_TRUE(destination.CanAcceptResource(ResourceType::PAPER));
    ASSERT_TRUE(destination.CanReceiveResource(ResourceType::PAPER));

    Resource paper{ResourceType::PAPER};
    destination.AddResource(&paper);

    auto paperIt = destination.resourceBuffers.find(ResourceType::PAPER);
    ASSERT_NE(paperIt, destination.resourceBuffers.end());
    EXPECT_EQ(paperIt->second.buffer.size(), 1u);
}

TEST(RoadNetworkTests, BeginTransportRejectsFullDestination)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* source = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* destination = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3);
    PlaceAndRegister<Road>(map, network, &player, {4, 2}, 4);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);

    destination->resourceBuffers.clear();
    destination->resourceBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 1};
    destination->resourceBuffers[ResourceType::WOOD].SetStoredAmount(1);

    Resource wood{ResourceType::WOOD};
    EXPECT_FALSE(network.BeginTransport(source, destination, &wood));
    EXPECT_TRUE(source->transportables.empty());
    destination->resourceBuffers[ResourceType::WOOD].Clear();
}

TEST(RoadNetworkTests, TransportableCancelsWhenPathLeavesOwnerTerritory)
{
    TileMap map;
    Player player{0, map};
    Player enemy{1, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* source = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* destination = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3);
    PlaceAndRegister<Road>(map, network, &player, {4, 2}, 4);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);

    source->resourceBuffers.clear();
    source->resourceBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};
    destination->resourceBuffers.clear();
    destination->resourceBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};

    Resource wood{ResourceType::WOOD};
    ASSERT_TRUE(network.BeginTransport(source, destination, &wood));
    ASSERT_FALSE(wood.transportPath.empty());

    map.tilemap[wood.transportPath.front()].owner = &enemy;
    EXPECT_TRUE(wood.Update(0.1));
    EXPECT_EQ(source->resourceBuffers[ResourceType::WOOD].buffer.size(), 1u);
    source->resourceBuffers[ResourceType::WOOD].Clear();
}
