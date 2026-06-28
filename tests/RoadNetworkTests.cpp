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

    source->storage.buffers.clear();
    destination->storage.buffers.clear();
    destination->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};

    Resource wood{ResourceType::WOOD};
    ASSERT_TRUE(network.BeginTransport(source, destination, &wood));

    ASSERT_EQ(source->transportables.size(), 1u);
    EXPECT_EQ(source->transportables.front(), &wood);
    EXPECT_EQ(wood.sourceBuilding, source);
    EXPECT_EQ(wood.targetBuilding, destination);
    EXPECT_FALSE(wood.transportPath.empty());
}

TEST(RoadNetworkTests, RoadCapacityLimitsEntryAndQueuesOverflowAtSource)
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

    roadA->road.maxCapacity.SetBase(1);
    roadB->road.maxCapacity.SetBase(1);
    destination->storage.buffers.clear();
    destination->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 4};

    Resource woodA{ResourceType::WOOD};
    Resource woodB{ResourceType::WOOD};
    Resource woodC{ResourceType::WOOD};
    ASSERT_TRUE(network.BeginTransport(source, destination, &woodA));
    ASSERT_TRUE(network.BeginTransport(source, destination, &woodB));
    ASSERT_TRUE(network.BeginTransport(source, destination, &woodC));

    source->UpdateTransportables(1.1);

    EXPECT_EQ(roadA->transportables.size(), 1u);
    EXPECT_EQ(source->transportables.size(), 2u);
}

TEST(RoadNetworkTests, OpposingFullRoadSegmentsSwapToBreakDeadlock)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* leftStorage = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* rightStorage = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    auto* roadA = PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3);
    auto* roadB = PlaceAndRegister<Road>(map, network, &player, {4, 2}, 4);
    ASSERT_NE(leftStorage, nullptr);
    ASSERT_NE(rightStorage, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);

    roadA->road.maxCapacity.SetBase(1);
    roadB->road.maxCapacity.SetBase(1);
    leftStorage->storage.buffers.clear();
    rightStorage->storage.buffers.clear();
    leftStorage->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 4};
    rightStorage->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 4};

    Resource eastbound{ResourceType::WOOD};
    Resource westbound{ResourceType::WOOD};
    ASSERT_TRUE(network.BeginTransport(leftStorage, rightStorage, &eastbound));
    ASSERT_TRUE(network.BeginTransport(rightStorage, leftStorage, &westbound));

    leftStorage->UpdateTransportables(1.1);
    rightStorage->UpdateTransportables(1.1);
    ASSERT_EQ(roadA->transportables.size(), 1u);
    ASSERT_EQ(roadB->transportables.size(), 1u);
    ASSERT_EQ(roadA->transportables.front(), &eastbound);
    ASSERT_EQ(roadB->transportables.front(), &westbound);

    eastbound.elapsedTime = eastbound.transportTime;
    westbound.elapsedTime = westbound.transportTime;
    roadA->UpdateTransportables(0.1);

    EXPECT_EQ(roadA->transportables.size(), 1u);
    EXPECT_EQ(roadB->transportables.size(), 1u);
    EXPECT_EQ(roadA->transportables.front(), &westbound);
    EXPECT_EQ(roadB->transportables.front(), &eastbound);
}

TEST(RoadNetworkTests, HeadquartersAcceptsPaperResource)
{
    Headquarters destination{2};
    ASSERT_TRUE(destination.CanAcceptResource(ResourceType::PAPER));
    ASSERT_TRUE(destination.CanReceiveResource(ResourceType::PAPER));

    Resource paper{ResourceType::PAPER};
    destination.AddResource(&paper);

    auto paperIt = destination.storage.buffers.find(ResourceType::PAPER);
    ASSERT_NE(paperIt, destination.storage.buffers.end());
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

    destination->storage.buffers.clear();
    destination->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 1};
    destination->storage.buffers[ResourceType::WOOD].SetStoredAmount(1);

    Resource wood{ResourceType::WOOD};
    EXPECT_FALSE(network.BeginTransport(source, destination, &wood));
    EXPECT_TRUE(source->transportables.empty());
    destination->storage.buffers[ResourceType::WOOD].Clear();
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

    source->storage.buffers.clear();
    source->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};
    destination->storage.buffers.clear();
    destination->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};

    Resource wood{ResourceType::WOOD};
    ASSERT_TRUE(network.BeginTransport(source, destination, &wood));
    ASSERT_FALSE(wood.transportPath.empty());

    map.tilemap[wood.transportPath.front()].owner = &enemy;
    EXPECT_TRUE(wood.Update(0.1));
    EXPECT_EQ(source->storage.buffers[ResourceType::WOOD].buffer.size(), 1u);
    source->storage.buffers[ResourceType::WOOD].Clear();
}
