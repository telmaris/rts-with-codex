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

TEST(PlayerEconomyTests, NewPlayerStartsWithTribalStateModifiers)
{
    TileMap map;
    Player player{0, map};

    EXPECT_EQ(player.stateDevelopment.GetDefinition().id, "tribal_society");
    EXPECT_GT(player.ModifyBalance(BalanceStat::ManpowerRate, 1.0, BuildingType::Building), 1.0);
    EXPECT_LT(player.ModifyBalance(BalanceStat::RecruitmentTime, 10.0, BuildingType::Building), 10.0);
}

TEST(PlayerEconomyTests, TribalManpowerGrowthAppliesToPopulationComponent)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    auto* village = dynamic_cast<Village*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Village>(1)));
    ASSERT_NE(village, nullptr);
    village->population.manpowerRate = 0.2;

    EXPECT_NEAR(player.ResolveStat(village->population.manpowerRate, village), 0.224, 0.0001);
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

TEST(PlayerEconomyTests, TelemetryDoesNotReportTheoreticalProduction)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    Woodcutter woodcutter{7};
    woodcutter.owner = &player;
    woodcutter.workers.assigned = woodcutter.workers.capacity.GetBase();

    player.UpdateEconomyTelemetry(1.0);

    EXPECT_FALSE(player.economyTelemetry.current.productionRatesPerMinute.contains(ResourceType::WOOD));
}

TEST(PlayerEconomyTests, TelemetryRecordsActualProductionAndInputConsumption)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    LumberMill lumberMill{7};
    lumberMill.owner = &player;
    lumberMill.workers.assigned = lumberMill.workers.capacity.GetBase();

    const int woodPerCycle = lumberMill.production.ingredients[ResourceType::WOOD];
    ASSERT_GT(woodPerCycle, 0);
    lumberMill.production.inputBuffers[ResourceType::WOOD].SetStoredAmount(woodPerCycle);

    lumberMill.production.Produce(lumberMill, 0.01);
    player.UpdateEconomyTelemetry(1.0);

    EXPECT_GE(player.economyTelemetry.current.consumptionRatesPerMinute[ResourceType::WOOD],
              woodPerCycle * 60);
    EXPECT_FALSE(player.economyTelemetry.current.productionRatesPerMinute.contains(ResourceType::PLANKS));

    lumberMill.production.elapsed = lumberMill.production.GetModifiedCycleTime(lumberMill);
    lumberMill.production.Produce(lumberMill, 0.01);
    player.UpdateEconomyTelemetry(1.0);

    EXPECT_GT(player.economyTelemetry.current.productionRatesPerMinute[ResourceType::PLANKS], 0);
}

TEST(PlayerEconomyTests, TelemetryRecordsBuildCostConsumption)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    auto* storage = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Headquarters>(1)));
    ASSERT_NE(storage, nullptr);
    storage->storage.buffers[ResourceType::WOOD].SetStoredAmount(5);

    ASSERT_TRUE(player.TryPayBuildCost({{ResourceType::WOOD, 3}}));
    player.UpdateEconomyTelemetry(1.0);

    EXPECT_EQ(player.economyTelemetry.current.consumptionRatesPerMinute[ResourceType::WOOD], 180);
}

TEST(PlayerEconomyTests, TelemetryRecordsBuildingPlacementCostConsumption)
{
    TileMap map;
    Player player{0, map};
    PrepareOwnedMap(map, &player);

    auto* storage = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({4, 4}), &player, std::make_unique<Headquarters>(1)));
    ASSERT_NE(storage, nullptr);
    storage->storage.buffers[ResourceType::STONE] = ResourceBuffer{ResourceType::STONE, 10};
    storage->storage.buffers[ResourceType::STONE].SetStoredAmount(10);

    const auto& roadDefinition = GetBuildingDefinition(BuildingType::Road);
    ASSERT_FALSE(roadDefinition.buildCosts.empty());
    ASSERT_NE(player.Build<Road>(map.GetIdFromCoords({0, 0})), nullptr);
    player.UpdateEconomyTelemetry(1.0);

    EXPECT_EQ(player.economyTelemetry.current.consumptionRatesPerMinute[ResourceType::STONE], 120);
}

TEST(PlayerEconomyTests, TelemetryRecordsTechnologyMilestones)
{
    TileMap map;
    map.params.debugMode = true;
    Player player{0, map};

    ASSERT_TRUE(player.UnlockTechnology("forestry"));

    ASSERT_EQ(player.economyTelemetry.researchMilestones.size(), 1u);
    EXPECT_EQ(player.economyTelemetry.researchMilestones.front().type, ResearchMilestoneType::Technology);
    EXPECT_EQ(player.economyTelemetry.researchMilestones.front().id, "forestry");
}
