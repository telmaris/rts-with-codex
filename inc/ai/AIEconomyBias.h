#ifndef AI_ECONOMY_BIAS_H
#define AI_ECONOMY_BIAS_H

#include "data/Resource.h"

#include <array>
#include <map>
#include <string>

// User design (2026-07-17): virtual, amortized per-minute consumption of the
// non-constant costs (construction, recruitment, ammunition), loaded from
// assets/data/ai.rtsdata and added to the consumption side of what the AI
// senses — the AI then works to keep production >= consumption, which makes
// it stand up and sustain production for those costs instead of stalling
// when its starting stock runs dry. The bias is also a difficulty lever: a
// fuller (better-tuned) bias means a stronger economy.
struct AIEconomyBias
{
    // Multiplier per MapParameters::aiDifficulty level (0 Primitive .. 3 Hard).
    std::array<double, 4> difficultyScale{0.4, 0.6, 0.8, 1.0};
    std::map<ResourceType, int> virtualConsumptionPerMinute;

    // Bias for one resource at a difficulty level, floor-rounded; 0 for
    // resources without an entry.
    int ScaledConsumption(ResourceType type, int difficulty) const;
    // The whole per-resource map scaled for a difficulty level (entries that
    // scale to 0 are dropped).
    std::map<ResourceType, int> ScaledMap(int difficulty) const;
};

// Cached catalog accessor — loads assets/data/ai.rtsdata once. A missing or
// empty file yields a zero bias (the AI just plays without amortization).
const AIEconomyBias& GetAIEconomyBias();

// Test seam / hot-reload: parse a specific file.
AIEconomyBias LoadAIEconomyBiasFromFile(const std::string& path);

#endif
