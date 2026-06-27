#ifndef BUILDING_CONFIG_H
#define BUILDING_CONFIG_H

#include "Building.h"

struct ResourceAmountDefinition
{
    ResourceType type{ResourceType::Null};
    int amount{0};
};

struct ResourceBufferDefinition
{
    ResourceType type{ResourceType::Null};
    int capacity{0};
    int initialAmount{0};
};

struct ProductionDefinition
{
    double cycleTime{0.0};
    std::vector<ResourceAmountDefinition> inputs;
    std::vector<ResourceAmountDefinition> outputs;
    std::vector<ResourceBufferDefinition> inputBuffers;
    std::vector<ResourceBufferDefinition> outputBuffers;
    int workerCapacity{5};
};

struct TerrainProductionDefinition
{
    TileType tileType{TileType::GRASS};
    ProductionDefinition production;
};

struct ProductionRecipeDefinition
{
    std::string name;
    ProductionDefinition production;
};

struct RoadDefinition
{
    int upgradeLevel{1};
    int maxCapacity{5};
    double speedModifier{1.0};
};

struct VillageDefinition
{
    double manpowerRate{0.2};
    int populationCap{80};
    double upkeepInterval{60.0};
    double foodPackageUpkeep{1.0};
};

struct MilitaryDefinition
{
    int territoryRadius{0};
    int hitPoints{0};
    int combatStrength{0};
    int garrison{0};
    int garrisonCapacity{0};
    int supply{0};
    int supplyCapacity{0};
};

struct BuildingDefinition
{
    BuildingType type{BuildingType::Building};
    std::string name;
    std::string tag;
    std::string texturePath;
    std::string buildCostText{"Cost TBD"};
    std::vector<ResourceAmountDefinition> buildCosts;
    Vec2i footprint{1, 1};
    int textureId{0};
    double buildTime{0.0};
    double transportTime{0.0};
    ProductionDefinition production;
    std::vector<TerrainProductionDefinition> terrainProductions;
    std::vector<ResourceBufferDefinition> storageBuffers;
    RoadDefinition road;
    VillageDefinition village;
    MilitaryDefinition military;
    std::vector<std::string> requiredTechnologies;
    std::vector<std::string> requiredFocuses;
    std::vector<ProductionRecipeDefinition> recipes;
};

// Returns all configured building definitions.
const std::vector<BuildingDefinition>& GetBuildingDefinitions();

// Loads building definitions from a specific data file.
std::vector<BuildingDefinition> LoadBuildingDefinitionsFromFile(const std::string& path);

// Returns the definition matching one building type.
const BuildingDefinition& GetBuildingDefinition(BuildingType type);

// Returns building types shown in the standard build panel.
const std::vector<BuildingType>& GetBuildableBuildingTypes();

// Returns building types shown in the road build panel.
const std::vector<BuildingType>& GetBuildableRoadTypes();

// Finds a terrain-specific production definition for a building.
const TerrainProductionDefinition* FindTerrainProductionDefinition(BuildingType type, TileType tileType);

// Applies common definition fields to a building instance.
void ApplyBuildingDefinition(Building& building, const BuildingDefinition& definition);

// Applies recipe and buffer data to a production building.
void ApplyProductionDefinition(ProductionBuilding& building, const ProductionDefinition& definition);
void ApplyProductionRecipes(ProductionBuilding& building, const BuildingDefinition& definition);

// Applies storage buffer data to a storage building.
void ApplyStorageDefinition(StorageBuilding& building, const BuildingDefinition& definition);

// Applies military stats to a military building.
void ApplyMilitaryDefinition(MilitaryBuilding& building, const BuildingDefinition& definition);

#endif
