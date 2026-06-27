#include "../inc/MapGenerator.h"
#include "../inc/Player.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

namespace
{
    // Creates an owned grass map for TileMap tests.
    void FillMap(TileMap& map, Player* owner, int width = 12, int height = 12)
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
}

TEST(TileMapDomainTests, TileOwnershipBuildingLifecycleAndBuildRules)
{
    Tile tile{3};
    Player* player = reinterpret_cast<Player*>(0x1);
    tile.SetOwner(player);
    EXPECT_EQ(tile.owner, player);
    EXPECT_TRUE(tile.CanBuild(player));

    tile.CreateBuilding(std::make_unique<Road>(9));
    ASSERT_NE(tile.GetBuilding(), nullptr);
    EXPECT_TRUE(tile.IsBuildingAnchor());
    EXPECT_FALSE(tile.CanBuild(player));

    tile.DestroyBuilding();
    EXPECT_EQ(tile.GetBuilding(), nullptr);
    EXPECT_TRUE(tile.CanBuild(player));
}

TEST(TileMapDomainTests, AdjacentTileIdsSkipFootprintAndDiagonals)
{
    TileMap map;
    Player player{0, map};
    FillMap(map, &player);

    auto* storage = map.PlaceLoadedBuilding(map.GetIdFromCoords({4, 4}), &player, std::make_unique<StorageBuilding>(1));
    ASSERT_NE(storage, nullptr);

    std::vector<int> adjacent = map.GetAdjacentTileIds(storage);
    EXPECT_FALSE(adjacent.empty());
    EXPECT_EQ(std::find(adjacent.begin(), adjacent.end(), map.GetIdFromCoords({4, 4})), adjacent.end());
    EXPECT_EQ(std::find(adjacent.begin(), adjacent.end(), map.GetIdFromCoords({3, 3})), adjacent.end());
    EXPECT_NE(std::find(adjacent.begin(), adjacent.end(), map.GetIdFromCoords({4, 3})), adjacent.end());
}

TEST(TileMapDomainTests, TerrainTexturePickerUsesWeightsAndFallbacks)
{
    TileMap map;
    std::mt19937 rng{123};

    EXPECT_EQ(map.GetTerrainTextureId(TileType::GRASS), 9);
    EXPECT_EQ(map.GetTerrainTextureId(static_cast<TileType>(999)), 0);

    map.terrainVariants[TileType::GRASS] = {{77, 0}, {88, 0}};
    EXPECT_EQ(map.PickTerrainTexture(TileType::GRASS, rng), 77);
    EXPECT_EQ(map.PickTerrainTexture(static_cast<TileType>(999), rng), 0);

    map.terrainVariants[TileType::GRASS] = {{77, 1}, {88, 0}};
    EXPECT_EQ(map.PickTerrainTexture(TileType::GRASS, rng), 77);
}

TEST(TileMapDomainTests, RoadAutotileMaskAndRefreshTrackNeighbors)
{
    TileMap map;
    Player player{0, map};
    FillMap(map, &player, 6, 6);

    auto* center = map.PlaceLoadedBuilding(map.GetIdFromCoords({2, 2}), &player, std::make_unique<Road>(1));
    auto* north = map.PlaceLoadedBuilding(map.GetIdFromCoords({2, 1}), &player, std::make_unique<Road>(2));
    auto* east = map.PlaceLoadedBuilding(map.GetIdFromCoords({3, 2}), &player, std::make_unique<Road>(3));
    ASSERT_NE(center, nullptr);
    ASSERT_NE(north, nullptr);
    ASSERT_NE(east, nullptr);

    int mask = map.GetRoadAutotileMask({2, 2});
    EXPECT_NE(mask & (1 << 4), 0);
    EXPECT_NE(mask & (1 << 1), 0);
    EXPECT_NE(mask & (1 << 5), 0);

    map.RefreshRoadTilesAround({2, 2});
    EXPECT_EQ(center->textureId, map.GetRoadTextureId({2, 2}));
    EXPECT_TRUE(map.buildingsDirty);
}

TEST(TileMapDomainTests, AutoConnectAndConnectReceiverToggleProductionLinks)
{
    TileMap map;
    Player player{0, map};
    FillMap(map, &player);

    auto* storage = map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<StorageBuilding>(1));
    auto* mill = dynamic_cast<LumberMill*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({6, 1}), &player, std::make_unique<LumberMill>(2)));
    ASSERT_NE(storage, nullptr);
    ASSERT_NE(mill, nullptr);

    map.AutoConnectBuilding(mill);
    EXPECT_TRUE(mill->HasReceiver(ResourceType::PLANKS));
    EXPECT_TRUE(mill->HasSupplier(ResourceType::WOOD));

    map.ConnectReceiver(mill, storage);
    EXPECT_FALSE(mill->HasReceiver(ResourceType::PLANKS));
    map.ConnectReceiver(mill, storage);
    EXPECT_TRUE(mill->HasReceiver(ResourceType::PLANKS));
}

TEST(TileMapDomainTests, TerritoryRecalculationUsesLivingMilitaryBuildingsWithoutOverwritingEnemy)
{
    TileMap map;
    Player player{0, map};
    Player enemy{1, map};
    FillMap(map, nullptr);

    map.tilemap[map.GetIdFromCoords({5, 5})].owner = &enemy;
    auto* tower = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({3, 3}), &player, std::make_unique<GuardTower>(1)));
    ASSERT_NE(tower, nullptr);
    tower->territoryRadius = 3;
    tower->hitPoints = 100;

    map.RecalculateTerritory(&player);

    EXPECT_EQ(map.tilemap[map.GetIdFromCoords({5, 5})].owner, &enemy);
    EXPECT_EQ(map.tilemap[map.GetIdFromCoords({3, 3})].owner, &player);

    tower->hitPoints = 0;
    map.RecalculateTerritory(&player);
    EXPECT_EQ(map.tilemap[map.GetIdFromCoords({3, 3})].owner, nullptr);
}
