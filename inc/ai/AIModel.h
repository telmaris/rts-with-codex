#ifndef AI_MODEL_H
#define AI_MODEL_H

#include "ai/AIActions.h"

class GameWorld;

// Utility-based tower-defense AI (AI rework, TODO #2 — replaces the removed
// 3-tier axis/goal/milestone PrimitiveAIModel, whose priority-axis framing
// didn't fit the TD loop). One decision cycle:
//   1. Sense()  — read-only AISituation snapshot (throttled).
//   2. Score each AINeed category to a utility in [0,1].
//   3. Try needs in (score desc, fixed priority) order until one executes a
//      concrete action through AIActions (build / road / recruit / deploy /
//      research / focus — all real GameCommands).
// Deterministic by construction: reads + SubmitCommand only; every
// first-match iteration over tracked buildings is sorted by building id
// (see docs/tech_debt.md); the only randomness is the difficulty-scaled
// decision noise seeded from (map seed, player id) — identical across
// same-seed worlds (etap 4).
enum class AINeed
{
    Defense,        // towers + ammo + emergency deploy (etap 3)
    RecruitDeploy,  // the prime objective: units on the track (etap 3)
    EconomySustain, // production chains for food/manpower and unit costs
    LogisticsRepair,// every building connected to the road network
    Research,       // University / technologies / focuses (etap 5)
    Count
};

// Read-only world snapshot the utility scores work from. Rebuilt on a
// throttle, never mutated by execution.
struct AISituation
{
    // Track state (military consumers land in etap 3; sensed already so the
    // scores have data from day one).
    int myDeployedCount{0};
    double myDeployedStrength{0.0};
    int enemyIncomingCount{0};
    double enemyIncomingStrength{0.0};
    double hqHpRatio{1.0};
    int rosterCount{0};
    int towerCount{0};
    int barracksCount{0};

    double manpower{0.0};
    int villageCount{0};
    int productionBuildingCount{0};
    // FOOD_PROVISIONS is the manpower lifeline — "alive" = positive
    // production rate in current telemetry.
    bool foodProductionAlive{false};

    // Resource shortfalls, worst first (urgency desc, then enum asc) —
    // deterministic ordering.
    struct Deficit
    {
        ResourceType resource{ResourceType::Null};
        double urgency{0.0};
    };
    std::vector<Deficit> deficits;

    // Own buildings with a LogisticsComponent that have no road path to any
    // storage/HQ — anchor positionIds, ordered by building id at sense time.
    // Stored as tile ids (not Building*) so a building destroyed between the
    // sense and the decision can't dangle; execution re-resolves via the
    // tilemap and skips anything gone.
    std::vector<int> unconnectedPositionIds;
};

class UtilityAIModel
{
public:
    explicit UtilityAIModel(int controlledPlayerId);

    void Update(GameWorld& world, Player* player, double dt);

private:
    AISituation Sense(GameWorld& world, Player* player);
    double ScoreNeed(AINeed need, const AISituation& s) const;
    bool ExecuteNeed(AINeed need, GameWorld& world, Player* player, const AISituation& s);

    bool ExecuteEconomy(GameWorld& world, Player* player, const AISituation& s);
    bool ExecuteLogistics(GameWorld& world, Player* player, const AISituation& s);
    // Builds the first affordable producer of `resource` (or of the deepest
    // missing input in its chain). Returns false when nothing can be placed
    // or afforded right now.
    bool TryBuildProducerFor(GameWorld& world, Player* player, ResourceType resource);
    // Fixed, deterministic opening build order that bootstraps the food/
    // manpower chain before telemetry has any consumption signal to react to.
    bool TryOpeningPlan(GameWorld& world, Player* player);

    int playerId{0};
    AIActions::AIActionState actions;
    AISituation situation;
    double senseTimer{0.0};
    double decisionTimer{0.0};
    double roadTimer{0.0};
    // Connectivity audits are the expensive part of Sense (road BFS per
    // storage per building) — they run on their own slower cadence and the
    // last answer is carried between audits.
    double connectivityTimer{0.0};
    std::vector<int> lastUnconnectedPositionIds;
};

#endif
