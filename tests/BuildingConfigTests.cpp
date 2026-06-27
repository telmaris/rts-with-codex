#include "../inc/BuildingConfig.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace
{
    // Writes a temporary building data file for parser-level tests.
    std::filesystem::path WriteBuildingFixture(const std::string& text)
    {
        const auto path = std::filesystem::temp_directory_path() / "rts_building_fixture.rtsdata";
        std::ofstream file(path);
        file << text;
        return path;
    }
}

TEST(BuildingConfigTests, CoreDefinitionsAreLoaded)
{
    const auto& definitions = GetBuildingDefinitions();
    ASSERT_FALSE(definitions.empty());

    const auto& hq = GetBuildingDefinition(BuildingType::Headquarters);
    EXPECT_EQ(hq.type, BuildingType::Headquarters);
    EXPECT_EQ(hq.footprint.x, 3);
    EXPECT_EQ(hq.footprint.y, 3);
    EXPECT_FALSE(hq.storageBuffers.empty());
    auto paper = std::find_if(hq.storageBuffers.begin(), hq.storageBuffers.end(), [](const ResourceBufferDefinition& buffer)
    {
        return buffer.type == ResourceType::PAPER;
    });
    ASSERT_NE(paper, hq.storageBuffers.end());
    EXPECT_GT(paper->capacity, 0);

    const auto& road = GetBuildingDefinition(BuildingType::Road);
    EXPECT_EQ(road.road.upgradeLevel, 1);
    EXPECT_GT(road.road.maxCapacity, 0);
    EXPECT_GT(road.road.speedModifier, 0.0);

    const auto& university = GetBuildingDefinition(BuildingType::University);
    EXPECT_EQ(university.production.workerCapacity, 40);
}

TEST(BuildingConfigTests, BuildPanelListsContainExpectedTypes)
{
    const auto& buildings = GetBuildableBuildingTypes();
    const auto& roads = GetBuildableRoadTypes();

    EXPECT_NE(std::find(buildings.begin(), buildings.end(), BuildingType::Woodcutter), buildings.end());
    EXPECT_NE(std::find(buildings.begin(), buildings.end(), BuildingType::Barracks), buildings.end());
    EXPECT_EQ(std::find(buildings.begin(), buildings.end(), BuildingType::Headquarters), buildings.end());

    ASSERT_EQ(roads.size(), 1u);
    EXPECT_EQ(roads.front(), BuildingType::Road);
}

TEST(BuildingConfigTests, TerrainSpecificProductionCanBeFound)
{
    const auto* stoneMine = FindTerrainProductionDefinition(BuildingType::Mine, TileType::STONE);
    ASSERT_NE(stoneMine, nullptr);
    ASSERT_FALSE(stoneMine->production.outputs.empty());
    EXPECT_EQ(stoneMine->production.outputs.front().type, ResourceType::STONE);

    EXPECT_EQ(FindTerrainProductionDefinition(BuildingType::Mine, TileType::GRASS), nullptr);
}

TEST(BuildingConfigTests, AppliesDefinitionsToRuntimeBuildings)
{
    ProductionBuilding production{1};
    const auto& mill = GetBuildingDefinition(BuildingType::LumberMill);
    ApplyBuildingDefinition(production, mill);
    ApplyProductionDefinition(production, mill.production);

    EXPECT_EQ(production.name, mill.name);
    EXPECT_EQ(production.buildingType, BuildingType::LumberMill);
    EXPECT_FALSE(production.ingredients.empty());
    EXPECT_FALSE(production.products.empty());

    StorageBuilding storage{2};
    const auto& hq = GetBuildingDefinition(BuildingType::Headquarters);
    ApplyStorageDefinition(storage, hq);
    EXPECT_FALSE(storage.resourceBuffers.empty());

    MilitaryBuilding military{3};
    const auto& tower = GetBuildingDefinition(BuildingType::GuardTower);
    ApplyMilitaryDefinition(military, tower);
    EXPECT_EQ(military.hitPoints, tower.military.hitPoints);
    EXPECT_EQ(military.GetTotalTroops(), 0);
    EXPECT_EQ(military.garrison, 0);
    EXPECT_EQ(military.supply, tower.military.supply);
}

TEST(BuildingConfigTests, LoadsBuildingDataFileWithProductionStorageRoadVillageAndMilitarySections)
{
    const auto path = WriteBuildingFixture(R"DATA(
# parser fixture
building Headquarters
    name "Custom HQ"
    tag "[HQ]"
    texture "assets/custom_hq.png"
    build_cost "Starting building"
    build_time 0
    transport_time 1.5
    footprint 3 3
    texture_id 42
    storage WOOD 100 50
    storage STONE 90
    military territory_radius 14 hit_points 1100 strength 5 garrison 3 garrison_capacity 40 supply 12 supply_capacity 60
end
building Woodcutter
    name "Fast Woodcutter"
    tag "[Fast]"
    texture "assets/fast.png"
    build_cost WOOD 11
    build_cost STONE 4
    build_time 6
    footprint 2 2
    texture_id 7
    production
        workers 4
        cycle_time 2.5
        input WATER 1
        output WOOD 2
        input_buffer WATER 5
        output_buffer WOOD 8
    end
end
building Mine
    terrain_production IRON_ORE
        workers 5
        cycle_time 3.5
        output IRON_ORE 3
        output_buffer IRON_ORE 12
    end
end
building Road
    road upgrade_level 2 max_capacity 9 speed_modifier 1.75
end
building Village
    village manpower_rate 0.4 population_cap 120 upkeep_interval 9 food_package_upkeep 2
end
)DATA");

    const auto definitions = LoadBuildingDefinitionsFromFile(path.string());
    ASSERT_EQ(definitions.size(), 5u);

    const auto& hq = definitions[0];
    EXPECT_EQ(hq.type, BuildingType::Headquarters);
    EXPECT_EQ(hq.name, "Custom HQ");
    EXPECT_EQ(hq.texturePath, "assets/custom_hq.png");
    EXPECT_EQ(hq.buildCostText, "Starting building");
    EXPECT_DOUBLE_EQ(hq.transportTime, 1.5);
    EXPECT_EQ(hq.textureId, 42);
    ASSERT_EQ(hq.storageBuffers.size(), 2u);
    EXPECT_EQ(hq.storageBuffers[0].type, ResourceType::WOOD);
    EXPECT_EQ(hq.storageBuffers[0].initialAmount, 50);
    EXPECT_EQ(hq.storageBuffers[1].initialAmount, 0);
    EXPECT_EQ(hq.military.territoryRadius, 14);
    EXPECT_EQ(hq.military.hitPoints, 1100);
    EXPECT_EQ(hq.military.supplyCapacity, 60);

    const auto& woodcutter = definitions[1];
    EXPECT_EQ(woodcutter.buildCostText, "WOOD 11, STONE 4");
    ASSERT_EQ(woodcutter.buildCosts.size(), 2u);
    EXPECT_EQ(woodcutter.production.workerCapacity, 4);
    EXPECT_DOUBLE_EQ(woodcutter.production.cycleTime, 2.5);
    ASSERT_EQ(woodcutter.production.inputs.size(), 1u);
    EXPECT_EQ(woodcutter.production.inputs[0].type, ResourceType::WATER);
    ASSERT_EQ(woodcutter.production.outputs.size(), 1u);
    EXPECT_EQ(woodcutter.production.outputs[0].amount, 2);
    ASSERT_EQ(woodcutter.production.inputBuffers.size(), 1u);
    ASSERT_EQ(woodcutter.production.outputBuffers.size(), 1u);

    const auto& mine = definitions[2];
    ASSERT_EQ(mine.terrainProductions.size(), 1u);
    EXPECT_EQ(mine.terrainProductions[0].tileType, TileType::IRON_ORE);
    EXPECT_EQ(mine.terrainProductions[0].production.workerCapacity, 5);

    const auto& road = definitions[3];
    EXPECT_EQ(road.road.upgradeLevel, 2);
    EXPECT_EQ(road.road.maxCapacity, 9);
    EXPECT_DOUBLE_EQ(road.road.speedModifier, 1.75);

    const auto& village = definitions[4];
    EXPECT_DOUBLE_EQ(village.village.manpowerRate, 0.4);
    EXPECT_EQ(village.village.populationCap, 120);
    EXPECT_DOUBLE_EQ(village.village.foodPackageUpkeep, 2.0);
}

TEST(BuildingConfigTests, MissingBuildingDataUsesBuiltInDefaults)
{
    const auto definitions = LoadBuildingDefinitionsFromFile("missing_building_fixture.rtsdata");

    EXPECT_NE(std::find_if(definitions.begin(), definitions.end(), [](const BuildingDefinition& definition)
    {
        return definition.type == BuildingType::Headquarters;
    }), definitions.end());
}
