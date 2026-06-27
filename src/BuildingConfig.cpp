#include "../inc/BuildingConfig.h"

#include <algorithm>
#include <cctype>

namespace
{
    constexpr const char* buildingDataPath = "assets/data/buildings.rtsdata";

    // Initializes MakeProduction.
    ProductionDefinition MakeProduction(
        double cycleTime,
        std::vector<ResourceAmountDefinition> inputs,
        std::vector<ResourceAmountDefinition> outputs,
        std::vector<ResourceBufferDefinition> inputBuffers,
        std::vector<ResourceBufferDefinition> outputBuffers)
    {
        return ProductionDefinition{
            cycleTime,
            std::move(inputs),
            std::move(outputs),
            std::move(inputBuffers),
            std::move(outputBuffers)};
    }

    // Initializes MakeDefaultDefinitions.
    std::vector<BuildingDefinition> MakeDefaultDefinitions()
    {
        return {
            BuildingDefinition{
                BuildingType::Headquarters,
                "Headquarters",
                "[Headquarters]",
                "assets/textures/building/headquarters.png",
                "Starting building",
                {},
                {3, 3},
                4,
                0.0,
                0.0,
                {},
                {},
                {
                    {ResourceType::IRON, 80, 30},
                    {ResourceType::IRON_ORE, 100, 40},
                    {ResourceType::COPPER_ORE, 80, 0},
                    {ResourceType::COPPER, 80, 0},
                    {ResourceType::SILVER_ORE, 80, 0},
                    {ResourceType::SILVER, 80, 0},
                    {ResourceType::GOLD_ORE, 80, 0},
                    {ResourceType::GOLD, 80, 0},
                    {ResourceType::WOOD, 160, 120},
                    {ResourceType::PLANKS, 100, 40},
                    {ResourceType::LEATHER, 80, 0},
                    {ResourceType::COAL, 80, 20},
                    {ResourceType::STONE, 160, 120},
                    {ResourceType::WATER, 120, 40},
                    {ResourceType::MEAT, 80, 0},
                    {ResourceType::WHEAT, 100, 20},
                    {ResourceType::FLOUR, 80, 0},
                    {ResourceType::BREAD, 80, 0},
                    {ResourceType::FOOD_PROVISIONS, 100, 20},
                    {ResourceType::PAPER, 80, 0},
                    {ResourceType::TOOLS, 60, 0},
                    {ResourceType::COPPER_SWORD, 60, 0},
                    {ResourceType::IRON_SWORD, 60, 0},
                    {ResourceType::STEEL_SWORD, 60, 0},
                    {ResourceType::BOW, 60, 0},
                    {ResourceType::ARROWS, 120, 0},
                    {ResourceType::HORSE, 60, 0},
                    {ResourceType::WEAPON_SUPPLY, 60, 0}
                },
                {},
                {},
                {13, 1000}},
            BuildingDefinition{
                BuildingType::Village,
                "Village",
                "[Village]",
                "assets/textures/building/cottage.png",
                "Cost TBD",
                {{ResourceType::WOOD, 45}, {ResourceType::STONE, 20}, {ResourceType::PLANKS, 12}},
                {4, 4},
                4,
                18.0,
                0.0,
                {},
                {},
                {},
                {},
                {0.2, 80, 60.0, 1.0}},
            BuildingDefinition{
                BuildingType::StorageBuilding,
                "Storage Building",
                "[StorageBuilding]",
                "assets/textures/building/storage.png",
                "Cost TBD",
                {{ResourceType::WOOD, 65}, {ResourceType::STONE, 40}, {ResourceType::PLANKS, 20}},
                {3, 3},
                4,
                14.0,
                0.0,
                {},
                {},
                {
                    {ResourceType::IRON, 80, 0},
                    {ResourceType::IRON_ORE, 80, 0},
                    {ResourceType::COPPER_ORE, 80, 0},
                    {ResourceType::COPPER, 80, 0},
                    {ResourceType::SILVER_ORE, 80, 0},
                    {ResourceType::SILVER, 80, 0},
                    {ResourceType::GOLD_ORE, 80, 0},
                    {ResourceType::GOLD, 80, 0},
                    {ResourceType::WOOD, 160, 0},
                    {ResourceType::PLANKS, 100, 0},
                    {ResourceType::LEATHER, 80, 0},
                    {ResourceType::COAL, 80, 0},
                    {ResourceType::STONE, 160, 0},
                    {ResourceType::WATER, 120, 0},
                    {ResourceType::MEAT, 80, 0},
                    {ResourceType::WHEAT, 100, 0},
                    {ResourceType::FLOUR, 80, 0},
                    {ResourceType::BREAD, 80, 0},
                    {ResourceType::FOOD_PROVISIONS, 100, 0},
                    {ResourceType::PAPER, 80, 0},
                    {ResourceType::TOOLS, 60, 0},
                    {ResourceType::COPPER_SWORD, 60, 0},
                    {ResourceType::IRON_SWORD, 60, 0},
                    {ResourceType::STEEL_SWORD, 60, 0},
                    {ResourceType::BOW, 60, 0},
                    {ResourceType::ARROWS, 120, 0},
                    {ResourceType::HORSE, 60, 0},
                    {ResourceType::WEAPON_SUPPLY, 60, 0}
                }},
            BuildingDefinition{
                BuildingType::Woodcutter,
                "Woodcutter",
                "[Woodcutter]",
                "assets/textures/building/woodcutter.png",
                "Cost TBD",
                {{ResourceType::WOOD, 25}, {ResourceType::STONE, 10}},
                {2, 2},
                0,
                8.0,
                0.0,
                MakeProduction(
                    5.0,
                    {},
                    {{ResourceType::WOOD, 1}},
                    {},
                    {{ResourceType::WOOD, 3}})},
            BuildingDefinition{
                BuildingType::LumberMill,
                "Lumber Mill",
                "[Lumber Mill]",
                "assets/textures/building/lumbermill.png",
                "Cost TBD",
                {{ResourceType::WOOD, 55}, {ResourceType::STONE, 25}, {ResourceType::PLANKS, 15}},
                {3, 3},
                1,
                16.0,
                0.0,
                MakeProduction(
                    10.0,
                    {{ResourceType::WOOD, 1}},
                    {{ResourceType::PLANKS, 2}},
                    {{ResourceType::WOOD, 8}},
                    {{ResourceType::PLANKS, 16}})},
            BuildingDefinition{
                BuildingType::Mine,
                "Mine",
                "[Mine]",
                "assets/textures/building/mine.png",
                "Cost TBD",
                {{ResourceType::WOOD, 45}, {ResourceType::STONE, 25}, {ResourceType::PLANKS, 10}},
                {2, 2},
                2,
                12.0,
                0.0,
                {},
                {
                    {TileType::IRON_ORE, MakeProduction(
                        2.0,
                        {},
                        {{ResourceType::IRON_ORE, 2}},
                        {},
                        {{ResourceType::IRON_ORE, 10}})},
                    {TileType::COAL, MakeProduction(
                        2.0,
                        {},
                        {{ResourceType::COAL, 2}},
                        {},
                        {{ResourceType::COAL, 10}})},
                    {TileType::STONE, MakeProduction(
                        4.0,
                        {},
                        {{ResourceType::STONE, 1}},
                        {},
                        {{ResourceType::STONE, 8}})}
                }},
            BuildingDefinition{
                BuildingType::Foundry,
                "Foundry",
                "[Foundry]",
                "assets/textures/building/foundry.png",
                "Cost TBD",
                {{ResourceType::WOOD, 90}, {ResourceType::STONE, 70}, {ResourceType::PLANKS, 35}, {ResourceType::IRON, 20}},
                {3, 3},
                3,
                28.0,
                0.0,
                MakeProduction(
                    2.0,
                    {{ResourceType::IRON_ORE, 1}, {ResourceType::COAL, 1}},
                    {{ResourceType::IRON, 2}},
                {{ResourceType::IRON_ORE, 8}, {ResourceType::COAL, 8}},
                {{ResourceType::IRON, 16}})},
            BuildingDefinition{
                BuildingType::GuardTower,
                "Guard Tower",
                "[GuardTower]",
                "assets/textures/building/guard_tower.png",
                "Cost TBD",
                {{ResourceType::WOOD, 80}, {ResourceType::STONE, 90}, {ResourceType::PLANKS, 30}},
                {2, 2},
                6,
                24.0,
                0.0,
                {},
                {},
                {},
                {},
                {},
                {6, 300, 25, 10, 20, 20, 40}},
            BuildingDefinition{
                BuildingType::Fortress,
                "Fortress",
                "[Fortress]",
                "assets/textures/building/fortress.png",
                "Cost TBD",
                {{ResourceType::WOOD, 150}, {ResourceType::STONE, 220}, {ResourceType::PLANKS, 75}, {ResourceType::IRON, 45}},
                {3, 3},
                7,
                45.0,
                0.0,
                {},
                {},
                {},
                {},
                {},
                {10, 800, 70, 35, 70, 80, 160}},
            BuildingDefinition{
                BuildingType::Castle,
                "Castle",
                "[Castle]",
                "assets/textures/building/castle.png",
                "Cost TBD",
                {{ResourceType::WOOD, 260}, {ResourceType::STONE, 420}, {ResourceType::PLANKS, 140}, {ResourceType::IRON, 120}},
                {4, 4},
                8,
                75.0,
                0.0,
                {},
                {},
                {},
                {},
                {},
                {15, 1600, 140, 80, 160, 200, 400}},
            BuildingDefinition{
                BuildingType::Road,
                "Road",
                "[Road]",
                "assets/textures/building/road.png",
                "Cost TBD",
                {{ResourceType::STONE, 2}},
                {1, 1},
                5,
                1.0,
                1.0,
                {},
                {},
                {},
                {1, 5, 1.0}}
        };
    }

    const BuildingDefinition fallbackDefinition{
        BuildingType::Building,
        "Building - Generic",
        "[Building]",
        "",
        "Cost TBD",
        {},
        {1, 1},
        0};

    // Initializes TokenizeLine.
    std::vector<std::string> TokenizeLine(const std::string& line)
    {
        std::vector<std::string> tokens;
        std::string token;
        bool inQuote = false;

        for (char c : line)
        {
            if (!inQuote && c == '#')
                break;

            if (c == '"')
            {
                if (inQuote)
                {
                    tokens.push_back(token);
                    token.clear();
                }
                inQuote = !inQuote;
                continue;
            }

            if (!inQuote && std::isspace(static_cast<unsigned char>(c)))
            {
                if (!token.empty())
                {
                    tokens.push_back(token);
                    token.clear();
                }
                continue;
            }

            token.push_back(c);
        }

        if (!token.empty())
            tokens.push_back(token);

        return tokens;
    }

    // Initializes ReadDataLines.
    std::vector<std::vector<std::string>> ReadDataLines(const std::string& path)
    {
        std::ifstream file(path);
        std::vector<std::vector<std::string>> lines;
        if (!file.is_open())
            return lines;

        std::string line;
        while (std::getline(file, line))
        {
            auto tokens = TokenizeLine(line);
            if (!tokens.empty())
                lines.push_back(std::move(tokens));
        }

        return lines;
    }

    // Initializes ParseBuildingType.
    BuildingType ParseBuildingType(const std::string& value)
    {
        if (value == "Headquarters") return BuildingType::Headquarters;
        if (value == "Village") return BuildingType::Village;
        if (value == "StorageBuilding") return BuildingType::StorageBuilding;
        if (value == "Woodcutter") return BuildingType::Woodcutter;
        if (value == "HuntersHut") return BuildingType::HuntersHut;
        if (value == "LumberMill") return BuildingType::LumberMill;
        if (value == "Mine") return BuildingType::Mine;
        if (value == "Foundry") return BuildingType::Foundry;
        if (value == "Well") return BuildingType::Well;
        if (value == "WheatFarm") return BuildingType::WheatFarm;
        if (value == "Windmill") return BuildingType::Windmill;
        if (value == "Bakery") return BuildingType::Bakery;
        if (value == "Inn") return BuildingType::Inn;
        if (value == "Paperworks") return BuildingType::Paperworks;
        if (value == "Smith") return BuildingType::Smith;
        if (value == "University") return BuildingType::University;
        if (value == "GuardTower") return BuildingType::GuardTower;
        if (value == "Fortress") return BuildingType::Fortress;
        if (value == "Castle") return BuildingType::Castle;
        if (value == "Barracks") return BuildingType::Barracks;
        if (value == "Road") return BuildingType::Road;
        return BuildingType::Building;
    }

    // Initializes ParseResourceType.
    ResourceType ParseResourceType(const std::string& value)
    {
        if (value == "WOOD") return ResourceType::WOOD;
        if (value == "PLANKS") return ResourceType::PLANKS;
        if (value == "COAL") return ResourceType::COAL;
        if (value == "STONE") return ResourceType::STONE;
        if (value == "IRON_ORE") return ResourceType::IRON_ORE;
        if (value == "IRON") return ResourceType::IRON;
        if (value == "COPPER_ORE") return ResourceType::COPPER_ORE;
        if (value == "COPPER") return ResourceType::COPPER;
        if (value == "SILVER_ORE") return ResourceType::SILVER_ORE;
        if (value == "SILVER") return ResourceType::SILVER;
        if (value == "GOLD_ORE") return ResourceType::GOLD_ORE;
        if (value == "GOLD") return ResourceType::GOLD;
        if (value == "LEATHER") return ResourceType::LEATHER;
        if (value == "MEAT") return ResourceType::MEAT;
        if (value == "WHEAT") return ResourceType::WHEAT;
        if (value == "BREAD") return ResourceType::BREAD;
        if (value == "FLOUR") return ResourceType::FLOUR;
        if (value == "WATER") return ResourceType::WATER;
        if (value == "BEER") return ResourceType::BEER;
        if (value == "COINS") return ResourceType::COINS;
        if (value == "FOOD_PROVISIONS") return ResourceType::FOOD_PROVISIONS;
        if (value == "WEAPON_SUPPLY") return ResourceType::WEAPON_SUPPLY;
        if (value == "PAPER") return ResourceType::PAPER;
        if (value == "TOOLS") return ResourceType::TOOLS;
        if (value == "COPPER_SWORD") return ResourceType::COPPER_SWORD;
        if (value == "IRON_SWORD") return ResourceType::IRON_SWORD;
        if (value == "STEEL_SWORD") return ResourceType::STEEL_SWORD;
        if (value == "BOW") return ResourceType::BOW;
        if (value == "ARROWS") return ResourceType::ARROWS;
        if (value == "HORSE") return ResourceType::HORSE;
        return ResourceType::Null;
    }

    // Initializes ParseTileType.
    TileType ParseTileType(const std::string& value)
    {
        if (value == "GRASS") return TileType::GRASS;
        if (value == "WOOD") return TileType::WOOD;
        if (value == "COAL") return TileType::COAL;
        if (value == "IRON_ORE") return TileType::IRON_ORE;
        if (value == "STONE") return TileType::STONE;
        return TileType::GRASS;
    }

    // Initializes FormatBuildCostText.
    std::string FormatBuildCostText(const std::vector<ResourceAmountDefinition>& costs)
    {
        if (costs.empty())
            return "Free";

        std::string text;
        for (size_t i = 0; i < costs.size(); i++)
        {
            if (i > 0)
                text += ", ";
            text += rt2s(costs[i].type) + " " + std::to_string(costs[i].amount);
        }
        return text;
    }

    // Initializes DefinitionSeed.
    BuildingDefinition DefinitionSeed(BuildingType type)
    {
        for (const auto& definition : MakeDefaultDefinitions())
        {
            if (definition.type == type)
                return definition;
        }

        return fallbackDefinition;
    }

    // Initializes ParseProduction.
    void ParseProduction(
        const std::vector<std::vector<std::string>>& lines,
        size_t& index,
        ProductionDefinition& production)
    {
        production = ProductionDefinition{};

        while (++index < lines.size())
        {
            const auto& tokens = lines[index];
            const auto& command = tokens[0];
            if (command == "end")
                return;

            if (command == "cycle_time" && tokens.size() >= 2)
                production.cycleTime = std::stod(tokens[1]);
            else if (command == "input" && tokens.size() >= 3)
                production.inputs.push_back({ParseResourceType(tokens[1]), std::stoi(tokens[2])});
            else if (command == "output" && tokens.size() >= 3)
                production.outputs.push_back({ParseResourceType(tokens[1]), std::stoi(tokens[2])});
            else if (command == "input_buffer" && tokens.size() >= 3)
                production.inputBuffers.push_back({ParseResourceType(tokens[1]), std::stoi(tokens[2])});
            else if (command == "output_buffer" && tokens.size() >= 3)
                production.outputBuffers.push_back({ParseResourceType(tokens[1]), std::stoi(tokens[2])});
            else if (command == "workers" && tokens.size() >= 2)
                production.workerCapacity = std::stoi(tokens[1]);
        }
    }

    ProductionRecipeRuntime MakeRuntimeRecipe(const ProductionRecipeDefinition& definition)
    {
        ProductionRecipeRuntime recipe;
        recipe.name = definition.name;
        recipe.cycleTime = definition.production.cycleTime;
        recipe.workerCapacity = definition.production.workerCapacity;
        for (const auto& input : definition.production.inputs)
            recipe.inputs[input.type] = input.amount;
        for (const auto& output : definition.production.outputs)
            recipe.outputs[output.type] = output.amount;
        for (const auto& buffer : definition.production.inputBuffers)
            recipe.inputBufferCapacities[buffer.type] = buffer.capacity;
        for (const auto& buffer : definition.production.outputBuffers)
            recipe.outputBufferCapacities[buffer.type] = buffer.capacity;
        return recipe;
    }

    // Initializes ParseKeyValueLine.
    void ParseKeyValueLine(const std::vector<std::string>& tokens, size_t start, const std::function<void(const std::string&, const std::string&)>& setter)
    {
        for (size_t i = start; i + 1 < tokens.size(); i += 2)
            setter(tokens[i], tokens[i + 1]);
    }

    // Initializes ParseBuilding.
    BuildingDefinition ParseBuilding(
        const std::vector<std::vector<std::string>>& lines,
        size_t& index)
    {
        BuildingType type = ParseBuildingType(lines[index][1]);
        BuildingDefinition definition = DefinitionSeed(type);
        definition.type = type;
        definition.production = {};
        definition.recipes.clear();
        definition.terrainProductions.clear();
        definition.storageBuffers.clear();
        definition.buildCosts.clear();
        definition.requiredTechnologies.clear();
        definition.requiredFocuses.clear();

        while (++index < lines.size())
        {
            const auto& tokens = lines[index];
            const auto& command = tokens[0];
            if (command == "end")
                return definition;

            if (command == "name" && tokens.size() >= 2)
                definition.name = tokens[1];
            else if (command == "tag" && tokens.size() >= 2)
                definition.tag = tokens[1];
            else if (command == "texture" && tokens.size() >= 2)
                definition.texturePath = tokens[1];
            else if (command == "build_cost" && tokens.size() >= 2)
            {
                if (tokens.size() >= 3)
                    definition.buildCosts.push_back({ParseResourceType(tokens[1]), std::stoi(tokens[2])});
                else
                    definition.buildCostText = tokens[1];
            }
            else if (command == "requires_tech" && tokens.size() >= 2)
                definition.requiredTechnologies.push_back(tokens[1]);
            else if (command == "requires_focus" && tokens.size() >= 2)
                definition.requiredFocuses.push_back(tokens[1]);
            else if (command == "footprint" && tokens.size() >= 3)
                definition.footprint = {std::stoi(tokens[1]), std::stoi(tokens[2])};
            else if (command == "texture_id" && tokens.size() >= 2)
                definition.textureId = std::stoi(tokens[1]);
            else if (command == "build_time" && tokens.size() >= 2)
                definition.buildTime = std::stod(tokens[1]);
            else if (command == "transport_time" && tokens.size() >= 2)
                definition.transportTime = std::stod(tokens[1]);
            else if (command == "storage" && tokens.size() >= 3)
            {
                int initialAmount = tokens.size() >= 4 ? std::stoi(tokens[3]) : 0;
                definition.storageBuffers.push_back({ParseResourceType(tokens[1]), std::stoi(tokens[2]), initialAmount});
            }
            else if (command == "production")
                ParseProduction(lines, index, definition.production);
            else if (command == "recipe" && tokens.size() >= 2)
            {
                ProductionRecipeDefinition recipe;
                recipe.name = tokens[1];
                ParseProduction(lines, index, recipe.production);
                definition.recipes.push_back(std::move(recipe));
            }
            else if (command == "terrain_production" && tokens.size() >= 2)
            {
                TerrainProductionDefinition terrainProduction;
                terrainProduction.tileType = ParseTileType(tokens[1]);
                ParseProduction(lines, index, terrainProduction.production);
                definition.terrainProductions.push_back(std::move(terrainProduction));
            }
            else if (command == "road")
            {
                ParseKeyValueLine(tokens, 1, [&](const std::string& key, const std::string& value)
                {
                    if (key == "upgrade_level") definition.road.upgradeLevel = std::stoi(value);
                    else if (key == "max_capacity") definition.road.maxCapacity = std::stoi(value);
                    else if (key == "speed_modifier") definition.road.speedModifier = std::stod(value);
                });
            }
            else if (command == "village")
            {
                ParseKeyValueLine(tokens, 1, [&](const std::string& key, const std::string& value)
                {
                    if (key == "manpower_rate") definition.village.manpowerRate = std::stod(value);
                    else if (key == "population_cap") definition.village.populationCap = std::stoi(value);
                    else if (key == "upkeep_interval") definition.village.upkeepInterval = std::stod(value);
                    else if (key == "food_package_upkeep") definition.village.foodPackageUpkeep = std::stod(value);
                });
            }
            else if (command == "military")
            {
                ParseKeyValueLine(tokens, 1, [&](const std::string& key, const std::string& value)
                {
                    if (key == "territory_radius") definition.military.territoryRadius = std::stoi(value);
                    else if (key == "hit_points") definition.military.hitPoints = std::stoi(value);
                    else if (key == "strength") definition.military.combatStrength = std::stoi(value);
                    else if (key == "garrison") definition.military.garrison = std::stoi(value);
                    else if (key == "garrison_capacity") definition.military.garrisonCapacity = std::stoi(value);
                    else if (key == "supply") definition.military.supply = std::stoi(value);
                    else if (key == "supply_capacity") definition.military.supplyCapacity = std::stoi(value);
                });
            }
        }

        return definition;
    }

    // Builds runtime definitions from tokenized data lines.
    std::vector<BuildingDefinition> ParseBuildingDefinitions(const std::vector<std::vector<std::string>>& lines)
    {
        if (lines.empty())
        {
            auto definitions = MakeDefaultDefinitions();
            for (auto& definition : definitions)
                if (!definition.buildCosts.empty())
                    definition.buildCostText = FormatBuildCostText(definition.buildCosts);
            return definitions;
        }

        std::vector<BuildingDefinition> definitions;
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (lines[i][0] == "building" && lines[i].size() >= 2)
                definitions.push_back(ParseBuilding(lines, i));
        }

        if (definitions.empty())
            definitions = MakeDefaultDefinitions();

        for (auto& definition : definitions)
            if (!definition.buildCosts.empty())
                definition.buildCostText = FormatBuildCostText(definition.buildCosts);

        return definitions;
    }
}

// Loads building definitions from a specific data file.
std::vector<BuildingDefinition> LoadBuildingDefinitionsFromFile(const std::string& path)
{
    return ParseBuildingDefinitions(ReadDataLines(path));
}

// Returns all loaded building definitions.
const std::vector<BuildingDefinition>& GetBuildingDefinitions()
{
    static const std::vector<BuildingDefinition> definitions = LoadBuildingDefinitionsFromFile(buildingDataPath);
    return definitions;
}

// Returns the definition for one building type, or fallback data.
const BuildingDefinition& GetBuildingDefinition(BuildingType type)
{
    for (const auto& definition : GetBuildingDefinitions())
    {
        if (definition.type == type)
            return definition;
    }

    return fallbackDefinition;
}

// Returns building types available in the build panel.
const std::vector<BuildingType>& GetBuildableBuildingTypes()
{
    static const std::vector<BuildingType> types{
        BuildingType::Woodcutter,
        BuildingType::HuntersHut,
        BuildingType::LumberMill,
        BuildingType::Mine,
        BuildingType::Foundry,
        BuildingType::Well,
        BuildingType::WheatFarm,
        BuildingType::Windmill,
        BuildingType::Bakery,
        BuildingType::Inn,
        BuildingType::Paperworks,
        BuildingType::Smith,
        BuildingType::University,
        BuildingType::StorageBuilding,
        BuildingType::Village,
        BuildingType::Barracks,
        BuildingType::GuardTower,
        BuildingType::Fortress,
        BuildingType::Castle};
    return types;
}

// Returns road types available in road-build mode.
const std::vector<BuildingType>& GetBuildableRoadTypes()
{
    static const std::vector<BuildingType> types{
        BuildingType::Road};
    return types;
}

// Finds the best matching runtime object.
const TerrainProductionDefinition* FindTerrainProductionDefinition(BuildingType type, TileType tileType)
{
    const auto& definition = GetBuildingDefinition(type);
    for (const auto& terrainProduction : definition.terrainProductions)
    {
        if (terrainProduction.tileType == tileType)
            return &terrainProduction;
    }

    return nullptr;
}

// Applies parsed configuration to runtime state.
void ApplyBuildingDefinition(Building& building, const BuildingDefinition& definition)
{
    building.name = definition.name;
    building.tag = definition.tag;
    building.buildingType = definition.type;
    building.textureId = definition.textureId;
    building.footprint = definition.footprint;
    building.transportTime = definition.transportTime;
    building.buildTime = definition.buildTime;
}

// Applies parsed configuration to runtime state.
void ApplyProductionDefinition(ProductionBuilding& building, const ProductionDefinition& definition)
{
    building.productionTime = definition.cycleTime;
    building.ingredients.clear();
    building.products.clear();
    building.inputBuffers.clear();
    building.outputBuffers.clear();
    building.pendingInputRequests.clear();

    for (const auto& input : definition.inputs)
        building.ingredients[input.type] = input.amount;

    for (const auto& output : definition.outputs)
        building.products[output.type] = output.amount;

    for (const auto& buffer : definition.inputBuffers)
        building.inputBuffers[buffer.type] = ResourceBuffer{buffer.type, buffer.capacity};

    for (const auto& buffer : definition.outputBuffers)
        building.outputBuffers[buffer.type] = ResourceBuffer{buffer.type, buffer.capacity};

    building.workerCapacity = std::max(0, definition.workerCapacity);
    building.assignedWorkers = std::min(building.assignedWorkers, building.workerCapacity.GetBase());
}

void ApplyProductionRecipes(ProductionBuilding& building, const BuildingDefinition& definition)
{
    std::vector<ProductionRecipeRuntime> recipes;
    if (!definition.recipes.empty())
    {
        for (const auto& recipe : definition.recipes)
            recipes.push_back(MakeRuntimeRecipe(recipe));
    }
    else if (!definition.production.outputs.empty() || !definition.production.inputs.empty())
    {
        ProductionRecipeDefinition recipe;
        recipe.name = "Default";
        recipe.production = definition.production;
        recipes.push_back(MakeRuntimeRecipe(recipe));
    }

    building.SetRecipes(std::move(recipes));
}

// Applies parsed configuration to runtime state.
void ApplyStorageDefinition(StorageBuilding& building, const BuildingDefinition& definition)
{
    building.resourceBuffers.clear();
    for (const auto& buffer : definition.storageBuffers)
    {
        ResourceBuffer resourceBuffer{buffer.type, buffer.capacity};
        resourceBuffer.SetStoredAmount(buffer.initialAmount);
        building.resourceBuffers[buffer.type] = std::move(resourceBuffer);
    }
}

// Applies parsed configuration to runtime state.
void ApplyMilitaryDefinition(MilitaryBuilding& building, const BuildingDefinition& definition)
{
    building.territoryRadius = definition.military.territoryRadius;
    building.hitPoints = definition.military.hitPoints;
    building.maxHitPoints = definition.military.hitPoints;
    building.combatStrength = definition.military.combatStrength;
    building.garrison = 0;
    building.garrisonCapacity = definition.military.garrisonCapacity;
    building.supplyCapacity = definition.military.supplyCapacity;
    building.supplyBuffer.Clear();
    building.supplyBuffer = ResourceBuffer{ResourceType::FOOD_PROVISIONS, building.supplyCapacity.GetBase()};
    building.supplyBuffer.SetStoredAmount(definition.military.supply);
    building.supply = static_cast<int>(building.supplyBuffer.buffer.size());
}
