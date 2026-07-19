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
// Ceiling a `priority` line in ai.rtsdata is normalized against — see
// AIEconomyBias::NormalizedPriority.
constexpr int PriorityCeiling = 100;

struct AIEconomyBias
{
    // Multiplier per MapParameters::aiDifficulty level (0 Primitive .. 3 Hard).
    std::array<double, 4> difficultyScale{0.4, 0.6, 0.8, 1.0};
    std::map<ResourceType, int> virtualConsumptionPerMinute;

    // Build-order priority weight per resource (0..PriorityCeiling, user
    // design 2026-07-19), serialized in the same block as `consumption` for
    // one-file fine-tuning. Every resource's DiagnoseResourceNeed urgency is
    // multiplied by its normalized weight before the deficit ladder ranks
    // candidates — this is what actually decides build ORDER (which resource
    // wins a tie), separate from `consumption`'s bias MAGNITUDE (which decides
    // how hard the AI works to sustain a resource once it's already the
    // priority). Not difficulty-scaled: order is a design choice, not a
    // difficulty lever. A resource with no `priority` line defaults to the
    // ceiling (full weight, no penalty) — additive/opt-in, so forgetting to
    // tune something never silently deprioritizes it.
    std::map<ResourceType, int> priorityWeight;
    // Minimum completed production buildings before the Defense need starts
    // wanting a garrison at all (UtilityAIModel's DesiredTowerCount gate).
    // Towers aren't a resource so they don't fit the `priority` table above —
    // this is the equivalent build-order lever for "start defense in the
    // meantime, once the economy has SOME footing" (user design 2026-07-19).
    int towerReadinessBuildings{4};
    // Seconds between AI decision cycles (one concrete action attempt per
    // cycle) — the pace lever (user request 2026-07-19: "da się przyspieszyć
    // decyzje AI?"). Identical for every AI in both lockstep worlds (static
    // config), so lowering it is determinism-safe. Default matches the old
    // hardcoded cadence when the line is absent.
    double decisionIntervalSeconds{1.5};

    // Bias for one resource at a difficulty level, floor-rounded; 0 for
    // resources without an entry.
    int ScaledConsumption(ResourceType type, int difficulty) const;
    // The whole per-resource map scaled for a difficulty level (entries that
    // scale to 0 are dropped).
    std::map<ResourceType, int> ScaledMap(int difficulty) const;
    // Normalized [0,1] priority weight for one resource; PriorityCeiling (1.0)
    // for anything without an explicit `priority` line.
    double NormalizedPriority(ResourceType type) const;
    // The whole priority table normalized to [0,1] (only resources with an
    // explicit line — DiagnoseResourceNeed treats an absent entry as 1.0).
    std::map<ResourceType, double> NormalizedPriorityMap() const;
};

// Cached catalog accessor — loads assets/data/ai.rtsdata once. A missing or
// empty file yields a zero bias (the AI just plays without amortization).
const AIEconomyBias& GetAIEconomyBias();

// Test seam / hot-reload: parse a specific file.
AIEconomyBias LoadAIEconomyBiasFromFile(const std::string& path);

#endif
