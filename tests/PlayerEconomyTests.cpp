#include "../inc/MapGenerator.h"
#include "../inc/Player.h"
#include "../inc/ProductionBuildings.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
    // Creates a tiny owned grass map for player economy tests.
    void PrepareOwnedMap(TileMap& map, Player* owner)
    {
        map.params.sizeX = 8;
        map.params.sizeY = 8;
        map.tilemap.clear();
        for (int i = 0; i < map.params.sizeX * map.params.sizeY; i++)
        {
            Tile tile{i};
            tile.owner = owner;
            tile.tileType = TileType::GRASS;
            map.tilemap.push_back(std::move(tile));
        }
    }
}

TEST(PlayerEconomyTests, PopulationCapCountsFinishedVillages)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    auto* village = dynamic_cast<Village*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Village>(1)));
    ASSERT_NE(village, nullptr);
    village->population.populationCap = 42;
    village->constructionRemaining = 0.0;

    EXPECT_EQ(player.GetPopulationCap(), 42);
}

TEST(PlayerEconomyTests, AddManpowerRespectsPopulationCapIncludingWorkersAndSoldiers)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    auto* village = dynamic_cast<Village*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Village>(1)));
    ASSERT_NE(village, nullptr);
    village->population.populationCap = 10;

    player.strategicResources.Set(StrategicResourceType::Workers, 3);
    player.strategicResources.Set(StrategicResourceType::Soldiers, 2);

    EXPECT_DOUBLE_EQ(player.AddManpower(10.0), 5.0);
    EXPECT_DOUBLE_EQ(player.strategicResources.Get(StrategicResourceType::Manpower), 5.0);
    EXPECT_DOUBLE_EQ(player.GetTotalPopulation(), 10.0);
}

TEST(PlayerEconomyTests, AutoAssignWorkersMovesManpowerIntoProductionBuilding)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    Woodcutter building{7};
    building.owner = &player;
    building.workers.capacity = 4;
    building.workers.assigned = 1;
    player.strategicResources.Set(StrategicResourceType::Manpower, 10);

    EXPECT_EQ(player.AutoAssignWorkers(&building), 3);
    EXPECT_EQ(building.workers.assigned, 4);
    EXPECT_DOUBLE_EQ(player.strategicResources.Get(StrategicResourceType::Manpower), 7.0);
    EXPECT_DOUBLE_EQ(player.strategicResources.Get(StrategicResourceType::Workers), 3.0);
}

TEST(PlayerEconomyTests, TechnologyModifiersRefreshIntoBalanceSet)
{
    TileMap map;
    map.params.debugMode = true;
    Player player{0, map};

    ASSERT_TRUE(player.UnlockTechnology("forestry"));
    EXPECT_FALSE(player.balanceModifiers.GetModifiers().empty());

    double modified = player.ModifyBalance(
        BalanceStat::ProductionCycleTime,
        10.0,
        BuildingType::Woodcutter);
    EXPECT_LT(modified, 10.0);
}

TEST(PlayerEconomyTests, TechnologyOutputBonusIsVisibleInBuildingBufferViews)
{
    TileMap map;
    map.params.debugMode = true;
    Player player{0, map};

    Woodcutter woodcutter{7};
    woodcutter.owner = &player;

    auto before = woodcutter.GetOutputBufferViews();
    auto woodBefore = std::find_if(before.begin(), before.end(), [](const ResourceBufferView& view)
    {
        return view.type == ResourceType::WOOD;
    });
    ASSERT_NE(woodBefore, before.end());
    const int baseAmount = woodBefore->recipeAmount;
    EXPECT_GT(baseAmount, 0);

    ASSERT_TRUE(player.UnlockTechnology("forestry"));

    auto after = woodcutter.GetOutputBufferViews();
    auto woodAfter = std::find_if(after.begin(), after.end(), [](const ResourceBufferView& view)
    {
        return view.type == ResourceType::WOOD;
    });
    ASSERT_NE(woodAfter, after.end());
    EXPECT_EQ(woodAfter->recipeAmount, baseAmount + 1);
}
