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
        if (value == "COPPER_SWORD") return ResourceType::COPPER_SWORD;
        if (value == "IRON_SWORD") return ResourceType::IRON_SWORD;
        if (value == "BRONZE_SWORD") return ResourceType::BRONZE_SWORD;
        if (value == "STEEL_SWORD") return ResourceType::STEEL_SWORD;
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
                bias.difficultyScale[i] = std::stod(tokens[1 + i]);
            continue;
        }
        if (tokens[0] == "consumption" && tokens.size() >= 3)
        {
            ResourceType type = ParseResourceType(tokens[1]);
            int amount = std::stoi(tokens[2]);
            if (type != ResourceType::Null && amount > 0)
                bias.virtualConsumptionPerMinute[type] = amount;
        }
    }
    return bias;
}

const AIEconomyBias& GetAIEconomyBias()
{
    static AIEconomyBias cached = LoadAIEconomyBiasFromFile("assets/data/ai.rtsdata");
    return cached;
}
