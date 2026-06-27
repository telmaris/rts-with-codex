#include "../inc/MapGenerator.h"
#include "../inc/Player.h"

#include <gtest/gtest.h>

namespace
{
    // Creates a rectangular grass map fully owned by the supplied player.
    void FillOwnedGrassMap(TileMap& map, Player* owner, int width = 12, int height = 12)
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
            tile.resourceRichness = 0;
            map.tilemap.push_back(std::move(tile));
        }
    }

    // Marks a footprint-sized area with resource terrain and finite richness.
    void PaintResource(TileMap& map, Vec2i anchor, Vec2i footprint, TileType type, int richness = 10)
    {
        for (int y = 0; y < footprint.y; y++)
        {
            for (int x = 0; x < footprint.x; x++)
            {
                Vec2i pos{anchor.x + x, anchor.y + y};
                Tile& tile = map.tilemap[map.GetIdFromCoords(pos)];
                tile.tileType = type;
                tile.resourceRichness = richness;
            }
        }
    }
}

TEST(BuildingPlacementTests, FootprintMustFitInsideMap)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrassMap(map, &player, 8, 8);

    EXPECT_TRUE(map.IsInsideFootprint({5, 5}, {3, 3}));
    EXPECT_FALSE(map.IsInsideFootprint({6, 6}, {3, 3}));
}

TEST(BuildingPlacementTests, FootprintRequiresSingleOwnerAndNoOccupancy)
{
    TileMap map;
    Player player{0, map};
    Player enemy{1, map};
    FillOwnedGrassMap(map, &player);

    EXPECT_TRUE(map.CanBuildFootprint({2, 2}, {2, 2}, &player));

    map.tilemap[map.GetIdFromCoords({3, 3})].owner = &enemy;
    EXPECT_FALSE(map.CanBuildFootprint({2, 2}, {2, 2}, &player));

    map.tilemap[map.GetIdFromCoords({3, 3})].owner = &player;
    map.tilemap[map.GetIdFromCoords({2, 2})].buildingRef = reinterpret_cast<Building*>(0x1);
    EXPECT_FALSE(map.CanBuildFootprint({2, 2}, {2, 2}, &player));
}

TEST(BuildingPlacementTests, ResourceProducerRequiresMatchingTerrainAndRichness)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrassMap(map, &player);

    const Vec2i footprint = GetBuildingDefinition(BuildingType::Woodcutter).footprint;
    EXPECT_FALSE(map.CanPlaceBuilding(BuildingType::Woodcutter, {2, 2}, footprint, &player));

    PaintResource(map, {2, 2}, {1, 2}, TileType::WOOD);
    EXPECT_TRUE(map.CanPlaceBuilding(BuildingType::Woodcutter, {2, 2}, footprint, &player));

    map.tilemap[map.GetIdFromCoords({2, 2})].resourceRichness = 0;
    EXPECT_FALSE(map.CanPlaceBuilding(BuildingType::Woodcutter, {2, 2}, footprint, &player));
}

TEST(BuildingPlacementTests, FootprintReferencesPointBackToAnchorBuilding)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrassMap(map, &player);

    Vec2i anchor{2, 2};
    Vec2i footprint = GetBuildingDefinition(BuildingType::Woodcutter).footprint;
    PaintResource(map, anchor, footprint, TileType::WOOD);

    int anchorId = map.GetIdFromCoords(anchor);
    map.BuildOnTile(anchorId, &player, std::make_unique<Woodcutter>(42));

    Building* anchorBuilding = map.GetBuilding(anchor);
    ASSERT_NE(anchorBuilding, nullptr);
    EXPECT_EQ(map.GetBuilding({anchor.x + 1, anchor.y}), anchorBuilding);
    EXPECT_EQ(map.GetBuilding({anchor.x, anchor.y + 1}), anchorBuilding);
}
