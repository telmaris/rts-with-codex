#include "../inc/Technology.h"

#include <cctype>
#include <fstream>
#include <algorithm>

namespace
{
    constexpr const char* technologyDataPath = "assets/data/technologies.rtsdata";
    constexpr const char* focusDataPath = "assets/data/focuses.rtsdata";

    // Splits a data line into tokens while preserving quoted strings.
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

    // Reads tokenized technology data lines from disk.
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

    // Converts text to a balance stat identifier.
    BalanceStat ParseBalanceStat(const std::string& value)
    {
        if (value == "BuildTime") return BalanceStat::BuildTime;
        if (value == "ProductionCycleTime") return BalanceStat::ProductionCycleTime;
        if (value == "ProductionOutputAmount") return BalanceStat::ProductionOutputAmount;
        if (value == "WorkerCapacity") return BalanceStat::WorkerCapacity;
        if (value == "TransportTime") return BalanceStat::TransportTime;
        if (value == "RoadCapacity") return BalanceStat::RoadCapacity;
        if (value == "RoadSpeed") return BalanceStat::RoadSpeed;
        if (value == "MilitaryStrength") return BalanceStat::MilitaryStrength;
        if (value == "AttackDamage") return BalanceStat::AttackDamage;
        if (value == "HitPoints") return BalanceStat::HitPoints;
        if (value == "TerritoryRadius") return BalanceStat::TerritoryRadius;
        if (value == "GarrisonCapacity") return BalanceStat::GarrisonCapacity;
        if (value == "SupplyCapacity") return BalanceStat::SupplyCapacity;
        if (value == "SupplyConsumption") return BalanceStat::SupplyConsumption;
        if (value == "ManpowerRate") return BalanceStat::ManpowerRate;
        if (value == "PopulationCap") return BalanceStat::PopulationCap;
        if (value == "RecruitmentTime") return BalanceStat::RecruitmentTime;
        if (value == "RecruitmentManpowerCost") return BalanceStat::RecruitmentManpowerCost;
        return BalanceStat::BuildTime;
    }

    void AddTag(std::vector<std::string>& tags, std::string tag)
    {
        std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        if (std::find(tags.begin(), tags.end(), tag) == tags.end())
            tags.push_back(std::move(tag));
    }

    void AddBuildingTags(std::vector<std::string>& tags, BuildingType type)
    {
        switch (type)
        {
            case BuildingType::Woodcutter: AddTag(tags, "wood"); AddTag(tags, "raw_resource"); break;
            case BuildingType::LumberMill: AddTag(tags, "planks"); AddTag(tags, "processing"); break;
            case BuildingType::Mine: AddTag(tags, "stone"); AddTag(tags, "ore"); AddTag(tags, "raw_resource"); break;
            case BuildingType::Foundry: AddTag(tags, "iron"); AddTag(tags, "processing"); break;
            case BuildingType::StorageBuilding: AddTag(tags, "storage"); AddTag(tags, "logistics"); break;
            case BuildingType::Road: AddTag(tags, "roads"); AddTag(tags, "logistics"); break;
            case BuildingType::Village: AddTag(tags, "population"); AddTag(tags, "social"); break;
            case BuildingType::University: AddTag(tags, "research"); break;
            case BuildingType::Barracks:
            case BuildingType::GuardTower:
            case BuildingType::Fortress:
            case BuildingType::Castle:
                AddTag(tags, "military");
                break;
            default:
                break;
        }
    }

    void AddResourceTags(std::vector<std::string>& tags, ResourceType type)
    {
        switch (type)
        {
            case ResourceType::WOOD: AddTag(tags, "wood"); break;
            case ResourceType::STONE: AddTag(tags, "stone"); break;
            case ResourceType::PLANKS: AddTag(tags, "planks"); break;
            case ResourceType::IRON:
            case ResourceType::IRON_ORE:
            case ResourceType::COAL:
                AddTag(tags, "iron");
                AddTag(tags, "ore");
                break;
            case ResourceType::WHEAT:
            case ResourceType::FLOUR:
            case ResourceType::BREAD:
            case ResourceType::FOOD_PROVISIONS:
            case ResourceType::MEAT:
            case ResourceType::WATER:
                AddTag(tags, "food");
                break;
            case ResourceType::PAPER: AddTag(tags, "research"); break;
            case ResourceType::WEAPON_SUPPLY:
            case ResourceType::COPPER_SWORD:
            case ResourceType::IRON_SWORD:
            case ResourceType::STEEL_SWORD:
            case ResourceType::BOW:
            case ResourceType::ARROWS:
                AddTag(tags, "military");
                break;
            default:
                break;
        }
    }

    void InferTags(TechnologyDefinition& definition)
    {
        auto explicitTags = definition.tags;
        definition.tags.clear();
        for (const auto& tag : explicitTags)
            AddTag(definition.tags, tag);
        AddTag(definition.tags, definition.category);
        if (!definition.governmentId.empty())
            AddTag(definition.tags, "government");

        for (const auto& cost : definition.costs)
            AddResourceTags(definition.tags, cost.type);

        for (const auto& modifier : definition.modifiers)
        {
            switch (modifier.stat)
            {
                case BalanceStat::BuildTime:
                case BalanceStat::ProductionCycleTime:
                case BalanceStat::ProductionOutputAmount:
                case BalanceStat::WorkerCapacity:
                    AddTag(definition.tags, "production");
                    break;
                case BalanceStat::TransportTime:
                case BalanceStat::RoadCapacity:
                case BalanceStat::RoadSpeed:
                case BalanceStat::SupplyCapacity:
                case BalanceStat::SupplyConsumption:
                    AddTag(definition.tags, "logistics");
                    break;
                case BalanceStat::MilitaryStrength:
                case BalanceStat::AttackDamage:
                case BalanceStat::HitPoints:
                case BalanceStat::TerritoryRadius:
                case BalanceStat::GarrisonCapacity:
                case BalanceStat::RecruitmentTime:
                case BalanceStat::RecruitmentManpowerCost:
                    AddTag(definition.tags, "military");
                    break;
                case BalanceStat::ManpowerRate:
                case BalanceStat::PopulationCap:
                    AddTag(definition.tags, "population");
                    AddTag(definition.tags, "social");
                    break;
            }
            if (modifier.buildingType.has_value())
                AddBuildingTags(definition.tags, modifier.buildingType.value());
            if (modifier.resourceType.has_value())
                AddResourceTags(definition.tags, modifier.resourceType.value());
        }
    }

    // Converts text to a building type identifier.
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

    // Converts text to a resource type identifier.
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

    // Converts text to a military unit type identifier.
    MilitaryUnitType ParseMilitaryUnitType(const std::string& value)
    {
        if (value == "Swordsman") return MilitaryUnitType::Swordsman;
        if (value == "Archer") return MilitaryUnitType::Archer;
        return MilitaryUnitType::Militia;
    }

    // Returns built-in technologies used when the data file is missing.
    std::vector<TechnologyDefinition> MakeDefaultTechnologies()
    {
        return {
            TechnologyDefinition{
                "forestry",
                "Forestry",
                "Woodcutters work faster and produce more wood.",
                "PRODUCTION",
                12.0,
                "",
                {},
                {{ResourceType::PAPER, 10}, {ResourceType::WOOD, 30}, {ResourceType::TOOLS, 2}},
                {
                    BalanceModifier{BalanceStat::ProductionCycleTime, 0.0, 0.85, BalanceModifierScope::Global(), BuildingType::Woodcutter, std::nullopt, std::nullopt, "tech:forestry"},
                    BalanceModifier{BalanceStat::ProductionOutputAmount, 1.0, 1.0, BalanceModifierScope::Global(), BuildingType::Woodcutter, ResourceType::WOOD, std::nullopt, "tech:forestry"}
                }},
            TechnologyDefinition{
                "masonry",
                "Masonry",
                "Stonework improves defensive building durability.",
                "WARFARE",
                16.0,
                "",
                {},
                {{ResourceType::PAPER, 12}, {ResourceType::STONE, 30}},
                {
                    BalanceModifier{BalanceStat::HitPoints, 0.0, 1.15, BalanceModifierScope::Global(), BuildingType::Headquarters, std::nullopt, std::nullopt, "tech:masonry"},
                    BalanceModifier{BalanceStat::HitPoints, 0.0, 1.15, BalanceModifierScope::Global(), BuildingType::GuardTower, std::nullopt, std::nullopt, "tech:masonry"},
                    BalanceModifier{BalanceStat::HitPoints, 0.0, 1.15, BalanceModifierScope::Global(), BuildingType::Fortress, std::nullopt, std::nullopt, "tech:masonry"},
                    BalanceModifier{BalanceStat::HitPoints, 0.0, 1.15, BalanceModifierScope::Global(), BuildingType::Castle, std::nullopt, std::nullopt, "tech:masonry"}
                }},
            TechnologyDefinition{
                "logistics",
                "Logistics",
                "Roads move goods faster and carry more traffic.",
                "PRODUCTION",
                20.0,
                "",
                {"forestry"},
                {{ResourceType::PAPER, 18}, {ResourceType::PLANKS, 25}},
                {
                    BalanceModifier{BalanceStat::RoadSpeed, 0.0, 1.20, BalanceModifierScope::Global(), BuildingType::Road, std::nullopt, std::nullopt, "tech:logistics"},
                    BalanceModifier{BalanceStat::RoadCapacity, 2.0, 1.0, BalanceModifierScope::Global(), BuildingType::Road, std::nullopt, std::nullopt, "tech:logistics"}
                }},
            TechnologyDefinition{
                "village_records",
                "Village Records",
                "Better records increase population administration.",
                "SOCIAL",
                18.0,
                "",
                {"forestry"},
                {{ResourceType::PAPER, 15}, {ResourceType::FOOD_PROVISIONS, 10}},
                {
                    BalanceModifier{BalanceStat::PopulationCap, 20.0, 1.0, BalanceModifierScope::Global(), BuildingType::Village, std::nullopt, std::nullopt, "tech:village_records"},
                    BalanceModifier{BalanceStat::ManpowerRate, 0.0, 1.10, BalanceModifierScope::Global(), BuildingType::Village, std::nullopt, std::nullopt, "tech:village_records"}
                }},
            TechnologyDefinition{
                "sawmill_blades",
                "Sawmill Blades",
                "Better saws improve plank production.",
                "PRODUCTION",
                24.0,
                "",
                {"forestry"},
                {{ResourceType::PAPER, 16}, {ResourceType::TOOLS, 6}, {ResourceType::IRON, 12}},
                {
                    BalanceModifier{BalanceStat::ProductionCycleTime, 0.0, 0.80, BalanceModifierScope::Global(), BuildingType::LumberMill, std::nullopt, std::nullopt, "tech:sawmill_blades"},
                    BalanceModifier{BalanceStat::ProductionOutputAmount, 1.0, 1.0, BalanceModifierScope::Global(), BuildingType::LumberMill, ResourceType::PLANKS, std::nullopt, "tech:sawmill_blades"}
                }},
            TechnologyDefinition{
                "deep_mining",
                "Deep Mining",
                "Mines extract ore and stone more efficiently.",
                "PRODUCTION",
                30.0,
                "",
                {"masonry"},
                {{ResourceType::PAPER, 22}, {ResourceType::TOOLS, 8}, {ResourceType::PLANKS, 20}},
                {
                    BalanceModifier{BalanceStat::ProductionOutputAmount, 1.0, 1.0, BalanceModifierScope::Global(), BuildingType::Mine, ResourceType::IRON_ORE, std::nullopt, "tech:deep_mining"},
                    BalanceModifier{BalanceStat::ProductionOutputAmount, 1.0, 1.0, BalanceModifierScope::Global(), BuildingType::Mine, ResourceType::COAL, std::nullopt, "tech:deep_mining"},
                    BalanceModifier{BalanceStat::ProductionOutputAmount, 1.0, 1.0, BalanceModifierScope::Global(), BuildingType::Mine, ResourceType::STONE, std::nullopt, "tech:deep_mining"}
                }}
        };
    }

    std::vector<TechnologyDefinition> MakeDefaultFocuses()
    {
        return {
            TechnologyDefinition{
                "frontier_settlement",
                "Frontier Settlement",
                "Organize early settlement logistics and unlock civic expansion paths.",
                "SOCIAL",
                10.0,
                "",
                {},
                {{ResourceType::PAPER, 5}, {ResourceType::WOOD, 20}},
                {
                    BalanceModifier{BalanceStat::PopulationCap, 10.0, 1.0, BalanceModifierScope::Global(), BuildingType::Village, std::nullopt, std::nullopt, "focus:frontier_settlement"}
                }},
            TechnologyDefinition{
                "militia_charter",
                "Militia Charter",
                "Formalize local defense and improve early garrison capacity.",
                "WARFARE",
                12.0,
                "",
                {"frontier_settlement"},
                {{ResourceType::PAPER, 8}, {ResourceType::FOOD_PROVISIONS, 8}},
                {
                    BalanceModifier{BalanceStat::GarrisonCapacity, 5.0, 1.0, BalanceModifierScope::Global(), BuildingType::GuardTower, std::nullopt, std::nullopt, "focus:militia_charter"},
                    BalanceModifier{BalanceStat::RecruitmentTime, 0.0, 0.90, BalanceModifierScope::Global(), BuildingType::Barracks, std::nullopt, MilitaryUnitType::Militia, "focus:militia_charter"}
                }},
            TechnologyDefinition{
                "academic_patronage",
                "Academic Patronage",
                "Prepare state support for formal research institutions.",
                "SOCIAL",
                14.0,
                "",
                {"frontier_settlement"},
                {{ResourceType::PAPER, 12}, {ResourceType::COINS, 5}},
                {
                    BalanceModifier{BalanceStat::BuildTime, 0.0, 0.90, BalanceModifierScope::Global(), BuildingType::University, std::nullopt, std::nullopt, "focus:academic_patronage"}
                }}
        };
    }

    // Parses one modifier line in a technology block.
    BalanceModifier ParseModifier(const std::vector<std::string>& tokens, const std::string& techId)
    {
        BalanceModifier modifier;
        modifier.stat = tokens.size() > 1 ? ParseBalanceStat(tokens[1]) : BalanceStat::BuildTime;
        modifier.scope = BalanceModifierScope::Global();
        modifier.source = "tech:" + techId;

        for (size_t i = 2; i + 1 < tokens.size(); i += 2)
        {
            const std::string& key = tokens[i];
            const std::string& value = tokens[i + 1];
            if (key == "additive")
                modifier.additive = std::stod(value);
            else if (key == "multiplier")
                modifier.multiplier = std::stod(value);
            else if (key == "building")
                modifier.buildingType = ParseBuildingType(value);
            else if (key == "resource")
                modifier.resourceType = ParseResourceType(value);
            else if (key == "unit")
                modifier.unitType = ParseMilitaryUnitType(value);
        }
        return modifier;
    }

    // Parses one technology block from tokenized data.
    TechnologyDefinition ParseTechnology(const std::vector<std::vector<std::string>>& lines, size_t& index)
    {
        TechnologyDefinition definition;
        definition.id = lines[index].size() > 1 ? lines[index][1] : "";
        definition.name = definition.id;

        while (++index < lines.size())
        {
            const auto& tokens = lines[index];
            const auto& command = tokens[0];
            if (command == "end")
            {
                InferTags(definition);
                return definition;
            }

            if (command == "name" && tokens.size() >= 2)
                definition.name = tokens[1];
            else if (command == "description" && tokens.size() >= 2)
                definition.description = tokens[1];
            else if (command == "category" && tokens.size() >= 2)
                definition.category = tokens[1];
            else if (command == "research_time" && tokens.size() >= 2)
                definition.researchTime = std::stod(tokens[1]);
            else if (command == "set_government" && tokens.size() >= 2)
                definition.governmentId = tokens[1];
            else if (command == "layout_lane" && tokens.size() >= 2)
                definition.layoutLane = tokens[1];
            else if (command == "layout_order" && tokens.size() >= 2)
                definition.layoutOrder = std::stoi(tokens[1]);
            else if ((command == "tag" || command == "tags") && tokens.size() >= 2)
            {
                for (size_t tokenIndex = 1; tokenIndex < tokens.size(); tokenIndex++)
                    definition.tags.push_back(tokens[tokenIndex]);
            }
            else if (command == "requires" && tokens.size() >= 2)
                definition.prerequisites.push_back(tokens[1]);
            else if (command == "cost" && tokens.size() >= 3)
                definition.costs.push_back({ParseResourceType(tokens[1]), std::stoi(tokens[2])});
            else if (command == "modifier")
                definition.modifiers.push_back(ParseModifier(tokens, definition.id));
        }
        InferTags(definition);
        return definition;
    }

    // Loads technology definitions from tokenized data lines or provided defaults.
    std::vector<TechnologyDefinition> ParseTechnologyDefinitions(
        const std::vector<std::vector<std::string>>& lines,
        std::vector<TechnologyDefinition> defaults)
    {
        if (lines.empty())
            return defaults;

        std::vector<TechnologyDefinition> definitions;
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (lines[i][0] == "technology")
                definitions.push_back(ParseTechnology(lines, i));
        }
        if (!definitions.empty())
            return definitions;

        for (auto& definition : defaults)
            InferTags(definition);
        return defaults;
    }
}

// Loads technology definitions from a specific data file.
std::vector<TechnologyDefinition> LoadTechnologyDefinitionsFromFile(const std::string& path)
{
    return ParseTechnologyDefinitions(ReadDataLines(path), MakeDefaultTechnologies());
}

std::vector<TechnologyDefinition> LoadFocusDefinitionsFromFile(const std::string& path)
{
    return ParseTechnologyDefinitions(ReadDataLines(path), MakeDefaultFocuses());
}

// Returns all loaded technology definitions.
const std::vector<TechnologyDefinition>& GetTechnologyDefinitions()
{
    static const std::vector<TechnologyDefinition> definitions = LoadTechnologyDefinitionsFromFile(technologyDataPath);
    return definitions;
}

const std::vector<TechnologyDefinition>& GetFocusDefinitions()
{
    static const std::vector<TechnologyDefinition> definitions = LoadFocusDefinitionsFromFile(focusDataPath);
    return definitions;
}

// Finds one technology definition by id.
const TechnologyDefinition* FindTechnologyDefinition(const std::string& id)
{
    for (const auto& definition : GetTechnologyDefinitions())
    {
        if (definition.id == id)
            return &definition;
    }
    return nullptr;
}

const TechnologyDefinition* FindFocusDefinition(const std::string& id)
{
    for (const auto& definition : GetFocusDefinitions())
    {
        if (definition.id == id)
            return &definition;
    }
    return nullptr;
}

// Returns whether the technology has already been unlocked.
bool TechnologyState::HasTechnology(const std::string& id) const
{
    return unlocked.find(id) != unlocked.end();
}

// Returns whether all prerequisites for the technology are currently met.
bool TechnologyState::CanUnlock(const std::string& id) const
{
    const auto* definition = FindTechnologyDefinition(id);
    if (definition == nullptr || HasTechnology(id))
        return false;

    for (const auto& prerequisite : definition->prerequisites)
    {
        if (!HasTechnology(prerequisite))
            return false;
    }
    return true;
}

// Unlocks a technology and makes its modifiers available.
bool TechnologyState::UnlockTechnology(const std::string& id)
{
    if (!CanUnlock(id))
        return false;

    unlocked.insert(id);
    return true;
}

// Restores a technology from save data without prerequisite checks.
void TechnologyState::RestoreTechnology(const std::string& id)
{
    if (FindTechnologyDefinition(id) != nullptr)
        unlocked.insert(id);
}

// Clears all unlocked technologies.
void TechnologyState::Clear()
{
    unlocked.clear();
}

// Adds unlocked technology modifiers to a balance modifier set.
void TechnologyState::CollectModifiers(BalanceModifierSet& target) const
{
    for (const auto& id : unlocked)
    {
        const auto* definition = FindTechnologyDefinition(id);
        if (definition == nullptr)
            continue;

        for (auto modifier : definition->modifiers)
        {
            modifier.source = "tech:" + id;
            target.AddModifier(std::move(modifier));
        }
    }
}

bool FocusState::HasFocus(const std::string& id) const
{
    return unlocked.find(id) != unlocked.end();
}

bool FocusState::CanUnlock(const std::string& id) const
{
    const auto* definition = FindFocusDefinition(id);
    if (definition == nullptr || HasFocus(id))
        return false;
    for (const auto& prerequisite : definition->prerequisites)
        if (!HasFocus(prerequisite))
            return false;
    return true;
}

bool FocusState::CanStartFocus(const std::string& id) const
{
    return activeFocusId.empty() && CanUnlock(id);
}

bool FocusState::StartFocus(const std::string& id)
{
    if (!CanStartFocus(id))
        return false;

    const auto* definition = FindFocusDefinition(id);
    if (definition == nullptr)
        return false;

    activeFocusId = id;
    activeFocusRemaining = std::max(0.0, definition->researchTime);
    if (activeFocusRemaining <= 0.0)
        return UpdateActiveFocus(0.0);

    return true;
}

bool FocusState::UpdateActiveFocus(double dt)
{
    if (activeFocusId.empty())
        return false;

    activeFocusRemaining = std::max(0.0, activeFocusRemaining - std::max(0.0, dt));
    if (activeFocusRemaining > 0.0)
        return false;

    std::string completed = activeFocusId;
    activeFocusId.clear();
    activeFocusRemaining = 0.0;
    return UnlockFocus(completed);
}

double FocusState::GetActiveFocusProgress() const
{
    const auto* definition = FindFocusDefinition(activeFocusId);
    if (definition == nullptr || definition->researchTime <= 0.0)
        return 0.0;

    return std::clamp(1.0 - activeFocusRemaining / definition->researchTime, 0.0, 1.0);
}

bool FocusState::UnlockFocus(const std::string& id)
{
    if (!CanUnlock(id))
        return false;
    unlocked.insert(id);
    return true;
}

void FocusState::RestoreFocus(const std::string& id)
{
    if (FindFocusDefinition(id) != nullptr)
        unlocked.insert(id);
}

void FocusState::Clear()
{
    unlocked.clear();
    activeFocusId.clear();
    activeFocusRemaining = 0.0;
}

void FocusState::CollectModifiers(BalanceModifierSet& target) const
{
    for (const auto& id : unlocked)
    {
        const auto* definition = FindFocusDefinition(id);
        if (definition == nullptr)
            continue;
        for (auto modifier : definition->modifiers)
        {
            modifier.source = "focus:" + id;
            target.AddModifier(std::move(modifier));
        }
    }
}
