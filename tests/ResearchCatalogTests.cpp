#include "../inc/MapGenerator.h"
#include "../inc/Player.h"
#include "../inc/ResearchCatalog.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
    // Creates a minimal tile map so Player can resolve debug-mode research rules.
    void PrepareMap(TileMap& map)
    {
        map.params.sizeX = 4;
        map.params.sizeY = 4;
        map.params.debugMode = true;
        map.tilemap.clear();
        for (int i = 0; i < map.params.sizeX * map.params.sizeY; i++)
        {
            Tile tile{i};
            tile.tileType = TileType::GRASS;
            map.tilemap.push_back(std::move(tile));
        }
    }
}

TEST(ResearchCatalogTests, BuildsPlayerSpecificResearchNodeState)
{
    TileMap map;
    PrepareMap(map);
    Player player{0, map};

    auto nodes = ResearchCatalog::BuildView(player);
    ASSERT_FALSE(nodes.empty());

    auto forestry = std::find_if(nodes.begin(), nodes.end(), [](const ResearchNodeView& node)
    {
        return node.id == "forestry";
    });
    ASSERT_NE(forestry, nodes.end());
    EXPECT_EQ(forestry->category, "PRODUCTION");
    EXPECT_TRUE(forestry->available);
    EXPECT_EQ(forestry->stateText, "Available");

    ASSERT_TRUE(player.UnlockTechnology("forestry"));
    nodes = ResearchCatalog::BuildView(player);
    forestry = std::find_if(nodes.begin(), nodes.end(), [](const ResearchNodeView& node)
    {
        return node.id == "forestry";
    });
    ASSERT_NE(forestry, nodes.end());
    EXPECT_TRUE(forestry->researched);
    EXPECT_EQ(forestry->stateText, "Researched");
}
