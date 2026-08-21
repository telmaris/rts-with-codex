#include "ai/AIEconomyBias.h"
#include "data/RtsDataFile.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Initializes ParseResourceType. Mirrors economy/BuildingConfig.cpp's copy
    // (established per-file convention — see UnitDefinition.cpp's mirror note).
    // Only the resources that plausibly appear as amortized costs are listed;
    // unknown names fall through to Null and the entry is dropped with the
    // same silent tolerance the other .rtsdata parsers use.
    ResourceType ParseResourceType(const std::string& value)
    {
        if (value == "WOOD") return ResourceType::WOOD;
        if (value == "PLANKS") return ResourceType::PLANKS;
        if (value == "COAL") return ResourceType::COAL;
        if (value == "STONE") return ResourceType::STONE;
        if (value == "IRON_ORE") return ResourceType::IRON_ORE;
        if (value == "IRON") return ResourceType::IRON;
        if (value == "TOOLS") return ResourceType::TOOLS;
        if (value == "FOOD_PROVISIONS") return ResourceType::FOOD_PROVISIONS;
        if (value == "ARROWS") return ResourceType::ARROWS;
        if (value == "IRON_SWORD") return ResourceType::IRON_SWORD;
        if (value == "BRONZE_SWORD") return ResourceType::BRONZE_SWORD;
        if (value == "STEEL_SWORD") return ResourceType::STEEL_SWORD;
        // Food chain (added for `priority` lines, 2026-07-19 — the
        // consumption bias never needed these, but build-order priority does:
        // WheatFarm/Windmill/Bakery/Well feed the same early-game tier as
        // Woodcutter/Mine).
        if (value == "WHEAT") return ResourceType::WHEAT;
        if (value == "FLOUR") return ResourceType::FLOUR;
        if (value == "BREAD") return ResourceType::BREAD;
        if (value == "MEAT") return ResourceType::MEAT;
        if (value == "WATER") return ResourceType::WATER;
        return ResourceType::Null;
    }
}

int AIEconomyBias::ScaledConsumption(ResourceType type, int difficulty) const
{
    auto it = virtualConsumptionPerMinute.find(type);
    if (it == virtualConsumptionPerMinute.end())
        return 0;
    int level = std::clamp(difficulty, 0, static_cast<int>(difficultyScale.size()) - 1);
    return static_cast<int>(std::floor(it->second * difficultyScale[level]));
}

std::map<ResourceType, int> AIEconomyBias::ScaledMap(int difficulty) const
{
    std::map<ResourceType, int> scaled;
    for (const auto& [type, amount] : virtualConsumptionPerMinute)
    {
        int value = ScaledConsumption(type, difficulty);
        if (value > 0)
            scaled[type] = value;
    }
    return scaled;
}

double AIEconomyBias::NormalizedPriority(ResourceType type) const
{
    auto it = priorityWeight.find(type);
    if (it == priorityWeight.end())
        return 1.0;
    return std::clamp(it->second / static_cast<double>(PriorityCeiling), 0.0, 1.0);
}

std::map<ResourceType, double> AIEconomyBias::NormalizedPriorityMap() const
{
    std::map<ResourceType, double> normalized;
    for (const auto& [type, weight] : priorityWeight)
        normalized[type] = std::clamp(weight / static_cast<double>(PriorityCeiling), 0.0, 1.0);
    return normalized;
}

AIEconomyBias LoadAIEconomyBiasFromFile(const std::string& path)
{
    AIEconomyBias bias;
    bool inBlock = false;
    for (const RtsDataLine& tokens : ReadRtsDataLines(path))
    {
        if (tokens.empty())
            continue;
        if (!inBlock)
        {
            if (tokens[0] == "ai_economy_bias")
                inBlock = true;
            continue;
        }
        if (tokens[0] == "end")
        {
            inBlock = false;
            continue;
        }
        if (tokens[0] == "difficulty_scale" && tokens.size() >= 1 + bias.difficultyScale.size())
        {
            for (size_t i = 0; i < bias.difficultyScale.size(); i++)
                bias.difficultyScale[i] = RtsDataDoubleOr(tokens[1 + i]);
            continue;
        }
        if (tokens[0] == "consumption" && tokens.size() >= 3)
        {
            ResourceType type = ParseResourceType(tokens[1]);
            int amount = RtsDataIntOr(tokens[2]);
            if (type != ResourceType::Null && amount > 0)
                bias.virtualConsumptionPerMinute[type] = amount;
            continue;
        }
        if (tokens[0] == "priority" && tokens.size() >= 3)
        {
            ResourceType type = ParseResourceType(tokens[1]);
            int weight = RtsDataIntOr(tokens[2]);
            if (type != ResourceType::Null && weight > 0)
                bias.priorityWeight[type] = std::min(weight, PriorityCeiling);
            continue;
        }
        if (tokens[0] == "tower_readiness_buildings" && tokens.size() >= 2)
        {
            bias.towerReadinessBuildings = std::max(0, RtsDataIntOr(tokens[1]));
            continue;
        }
        if (tokens[0] == "decision_interval" && tokens.size() >= 2)
        {
            // Floor at 0.2 s — below that the AI would submit faster than the
            // economy can physically react, burning CPU on refused commands.
            bias.decisionIntervalSeconds = std::max(0.2, RtsDataDoubleOr(tokens[1]));
            continue;
        }
        if (tokens[0] == "manpower_reserve" && tokens.size() >= 2)
        {
            bias.manpowerReserve = std::max(0.0, RtsDataDoubleOr(tokens[1]));
            continue;
        }
    }
    return bias;
}

const AIEconomyBias& GetAIEconomyBias()
{
    static AIEconomyBias cached = LoadAIEconomyBiasFromFile("assets/data/ai.rtsdata");
    return cached;
}
