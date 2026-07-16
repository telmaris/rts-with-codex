#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "economy/Player.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

class GameWorld;

class IController
{
public:
    virtual ~IController() = default;
    explicit IController(int controlledPlayerId) : playerId(controlledPlayerId) {}

    virtual void Update(GameWorld& world, double dt) = 0;

    int playerId{0};
};

class LocalController : public IController
{
public:
    explicit LocalController(int controlledPlayerId) : IController(controlledPlayerId) {}

    void Update(GameWorld& world, double dt) override;
};

class RemoteController : public IController
{
public:
    explicit RemoteController(int controlledPlayerId) : IController(controlledPlayerId) {}

    void Update(GameWorld& world, double dt) override;
};

enum class AIDifficulty
{
    Primitive,
    Easy,
    Normal,
    Hard
};

// C1 (docs/work_plan_2026-07-13.md): Diplomacy and Expansion dropped —
// post-pivot there is no diplomacy system, and "territorial expansion" no
// longer means anything without the removed ground-ownership concept (see
// docs/post_pivot_audit_2026-07-12.md T2); their pressure folded into
// Resources (ExpandTerritory goal now reads Resources, see GoalPrimaryAxis).
// Military is no longer stubbed — see AnalyzeAxis/EvaluateAxis.
enum class AIStrategyAxis
{
    Resources,
    Logistics,
    Military,
    InternalDevelopment,
    Technology,
    Risk
};

enum class AIGovernmentPreference
{
    Tribal,
    Chiefdom,
    Kingdom,
    Aristocracy
};

// TIER 1 — long-horizon intent. One goal is active at a time, chosen from axis scores.
enum class AIStrategicGoal
{
    StabilizeEconomy,
    ExpandTerritory,
    DevelopInfrastructure,
    BuildMilitary,
    LaunchOffensive,
    Fortify
};

const char* AIStrategicGoalLabel(AIStrategicGoal goal);

// TIER 2 — a milestone is one prerequisite step inside a goal's chain.
enum class AIMilestoneKind
{
    BuildingCount,    // own >= threshold buildings of 'building'
    ProductionRate,   // produce >= threshold/min of 'resource'
    ResourceStock,    // store >= threshold of 'resource'
    ArmyStrength,     // army.strength >= threshold
    TechWithTag,      // unlocked >= threshold technologies tagged 'tag'
    AttackReady       // army supplied + strength threshold + enemy reachable
};

struct AIMilestone
{
    AIMilestoneKind kind{AIMilestoneKind::BuildingCount};
    BuildingType building{BuildingType::Building};
    ResourceType resource{ResourceType::Null};
    std::string tag;
    int threshold{1};
    std::string label;
};

// TIER 3 — a concrete, immediately executable action competing in one scoring pool.
enum class AIActionKind
{
    Build,
    Research,
    Focus,
    Attack,
    Recruit
};

struct AIActionCandidate
{
    AIActionKind kind{AIActionKind::Build};
    BuildingType building{BuildingType::Building};
    TileType terrain{TileType::GRASS};
    std::string researchId;
    int sourceTileId{-1};
    int targetTileId{-1};
    double score{0.0};
    std::string debugLabel;
};

struct AIGoalState
{
    AIStrategicGoal goal{AIStrategicGoal::StabilizeEconomy};
    std::vector<AIMilestone> chain;
    int activeMilestone{0};
    double timeInGoal{0.0};
    bool initialized{false};
};

struct AIPersonality
{
    float aggression{0.25f};
    float planning{0.50f};
    float riskTolerance{0.40f};
    float expansionism{0.45f};
    float economicFocus{0.55f};
    float militarism{0.35f};
    float defensiveBias{0.50f};
    float logisticsAwareness{0.55f};
    float adaptability{0.45f};
    float opportunism{0.35f};
    float persistence{0.50f};
    AIGovernmentPreference governmentPreference{AIGovernmentPreference::Chiefdom};
};

struct AIStrategySignal
{
    AIStrategyAxis axis{AIStrategyAxis::Resources};
    float urgency{0.0f};
    ResourceType resource{ResourceType::Null};
    std::string reason;
};

struct AIStrategySnapshot
{
    std::vector<AIStrategySignal> signals;
    std::array<float, 6> axisScores{};  // indexed by static_cast<int>(AIStrategyAxis), range [-1, 1]

    float GetUrgency(AIStrategyAxis axis) const
    {
        float result = 0.0f;
        for (const auto& signal : signals)
            if (signal.axis == axis)
                result = std::max(result, signal.urgency);
        return result;
    }

    // Signed evaluation: -1 = crisis, 0 = neutral, +1 = thriving.
    float GetAxisScore(AIStrategyAxis axis) const
    {
        return axisScores[static_cast<int>(axis)];
    }

    // Converts signed score to urgency pressure [0,1]: crisis=1, thriving=0.
    float GetPressure(AIStrategyAxis axis) const
    {
        return (1.0f - axisScores[static_cast<int>(axis)]) * 0.5f;
    }
};

struct AIStrategyAxisCache
{
    AIStrategyAxis axis{AIStrategyAxis::Resources};
    double interval{10.0};
    double timeUntilRefresh{0.0};
    std::vector<AIStrategySignal> signals;
    float score{0.0f};
};

struct AIActionUtility
{
    double baseValue{1.0};
    double need{0.0};
    double personalityModifier{1.0};
    double feasibility{1.0};
    double urgency{1.0};
    double planModifier{1.0};

    double Score() const
    {
        return baseValue * need * personalityModifier * feasibility * urgency * planModifier;
    }
};

struct AIResourceDiagnosis
{
    ResourceType resource{ResourceType::Null};
    double urgency{0.0};
    std::string reason;
    std::vector<ResourceType> missingInputs;
    bool logisticsProblem{false};
    bool manpowerProblem{false};
    bool storageProblem{false};
};

struct AIProducerOption
{
    BuildingType buildingType{BuildingType::Building};
    TileType terrain{TileType::GRASS};
    double outputPerMinute{0.0};
    std::vector<ResourceAmountDefinition> inputs;
};

struct AIMapAssessment
{
    int ownedStrategicResourceTiles{0};
    int nearbyUnownedStrategicResourceTiles{0};
    int enemyStrategicResourceTiles{0};
    int nearestIronDistance{9999};
    int nearestCoalDistance{9999};
    int nearestEnemyDistance{9999};
    double expansionPressure{0.0};
    double militaryOpportunity{0.0};
    double logisticsNeed{0.0};
};

struct AIModelSettings
{
    AIPersonality personality;
};

class AIModel
{
public:
    virtual ~AIModel() = default;
    virtual void Update(GameWorld& world, Player* player, double dt, const AIModelSettings& settings) = 0;
};

class PrimitiveAIModel : public AIModel
{
public:
    void Update(GameWorld& world, Player* player, double dt, const AIModelSettings& settings) override;

private:
    bool TryBuildEconomy(GameWorld& world, Player* player, const AIModelSettings& settings);  // legacy economic fallback
    bool TryBuildRoads(GameWorld& world, Player* player);
    int CountCompletedOrQueuedBuildings(GameWorld& world, Player* player, BuildingType type) const;
    AIMapAssessment AssessMap(GameWorld& world, Player* player) const;
    AIStrategySnapshot UpdateStrategyPipeline(GameWorld& world, Player* player, double dt, const AIModelSettings& settings);
    std::vector<AIStrategySignal> AnalyzeAxis(GameWorld& world, Player* player, AIStrategyAxis axis, const AIModelSettings& settings) const;
    int CountOwnedBuildings(GameWorld& world, Player* player, BuildingType type) const;
    int CountStoredResource(GameWorld& world, Player* player, ResourceType type) const;
    int GetResourceRate(const std::map<ResourceType, int>& rates, ResourceType type) const;
    AIResourceDiagnosis DiagnoseResourceNeed(GameWorld& world, Player* player, ResourceType resource, int depth = 0) const;
    std::vector<AIProducerOption> FindProducerOptions(ResourceType resource) const;
    double ScoreProducerOption(GameWorld& world, Player* player, const AIResourceDiagnosis& diagnosis, const AIProducerOption& option, const AIModelSettings& settings) const;
    bool TrySubmitBuild(GameWorld& world, Player* player, BuildingType type, Vec2i anchor);
    Vec2i FindBuildAnchor(GameWorld& world, Player* player, BuildingType type, TileType preferredTile, const Building* target) const;
    Building* FindNearestRoadTarget(GameWorld& world, Player* player, const Building* source) const;
    Building* FindNearestStorageConnectedRoad(GameWorld& world, Player* player, const Building* source) const;
    bool HasAdjacentRoad(GameWorld& world, const Building* building) const;
    bool HasRoadConnection(GameWorld& world, Player* player, const Building* source, const Building* target) const;
    bool SubmitRoadPath(GameWorld& world, Player* player, const Building* source, const Building* target);
    float EvaluateAxis(GameWorld& world, Player* player, AIStrategyAxis axis, const AIModelSettings& settings) const;

    // TIER 1 — goal selection with hysteresis.
    AIStrategicGoal SelectStrategicGoal(GameWorld& world, Player* player, const AIStrategySnapshot& snapshot, const AIModelSettings& settings) const;
    void UpdateGoalState(GameWorld& world, Player* player, const AIStrategySnapshot& snapshot, const AIModelSettings& settings, double dt);

    // TIER 2 — milestone chains.
    std::vector<AIMilestone> BuildMilestoneChain(AIStrategicGoal goal, const AIModelSettings& settings) const;
    bool IsMilestoneComplete(GameWorld& world, Player* player, const AIMilestone& milestone) const;
    double MilestoneProgress(GameWorld& world, Player* player, const AIMilestone& milestone) const;
    int FindActiveMilestone(GameWorld& world, Player* player, const std::vector<AIMilestone>& chain) const;

    // TIER 3 — unified action scoring.
    bool RunUnifiedDecision(GameWorld& world, Player* player, const AIStrategySnapshot& snapshot, const AIModelSettings& settings);
    std::vector<AIActionCandidate> GatherActionCandidates(GameWorld& world, Player* player, const AIStrategySnapshot& snapshot, const AIModelSettings& settings) const;
    double ScoreAction(GameWorld& world, Player* player, const AIActionCandidate& candidate, const AIStrategySnapshot& snapshot, const AIModelSettings& settings) const;
    bool ExecuteAction(GameWorld& world, Player* player, const AIActionCandidate& candidate);

    // Planning helpers.
    double ForecastSecondsToAfford(GameWorld& world, Player* player, const std::vector<ResourceAmountDefinition>& costs) const;
    double MilestoneAlignment(const AIActionCandidate& candidate, const AIMilestone* activeMilestone) const;
    Building* FindUniversity(GameWorld& world, Player* player) const;
    std::string SelectResearchTarget(GameWorld& world, Player* player, const AIStrategySnapshot& snapshot, const AIModelSettings& settings) const;
    std::string SelectFocusTarget(GameWorld& world, Player* player, const AIStrategySnapshot& snapshot, const AIModelSettings& settings) const;

    // C1 (docs/work_plan_2026-07-13.md): Military axis + Recruit/Attack action
    // helpers. Returns the id of a connected, non-defeated enemy to deploy
    // against (ring-adjacent or reachable through eliminated players' conquered
    // HQs — see PathingService::AreHqsConnected), or -1 if none is reachable
    // yet (e.g. still mid-ring, or every neighbor already eliminated).
    int FindAttackTargetPlayer(GameWorld& world, Player* player) const;
    // Perf fix (found verifying C1's own acceptance test, docs/work_plan_2026-07-13.md):
    // SelectStrategicGoal and MilestoneProgress's AttackReady case both call
    // FindAttackTargetPlayer, and both run EVERY tick (UpdateGoalState is
    // unconditional, unlike RunUnifiedDecision's decisionTimer-gated block) —
    // calling a real PathingService::AreHqsConnected lookup 100x/sim-second
    // measured at ~32 ms/tick average (vs. the <1 ms/tick baseline) once a
    // Barracks/roster made the Military axis actually engage. Cached on its
    // own timer, decremented alongside the other timers in Update().
    int GetCachedAttackTargetPlayer(GameWorld& world, Player* player) const;
    mutable double attackTargetCacheTimer{0.0};
    mutable int cachedAttackTargetPlayer{-1};

    double roadTimer{0.0};
    double economyTimer{0.0};
    double militaryTimer{4.0};
    double attackTimer{30.0};
    double decisionTimer{0.0};
    std::vector<AIStrategyAxisCache> strategyAxisCache;
    AIGoalState goalState;
    std::map<int, double> reservedRoadTiles;
    std::map<BuildingType, double> recentBuildOrders;
    std::map<std::string, double> recentResearchOrders;
    // Perf fix (docs/post_pivot_audit_2026-07-12.md follow-up #3, 2026-07-12):
    // FindBuildAnchor's last-resort tier scans the ENTIRE tilemap (~90k tiles
    // on the default map) — cheap windowed tiers cover the common case, but a
    // terrain-specific extractor (Woodcutter/Mine) with no deposit left
    // nearby keeps falling through to that full scan EVERY decision cycle,
    // repeatedly, since the negative result doesn't change from one cycle to
    // the next. Measured: ~190 ms/decision once triggered, recurring on the
    // ~1.2-2 s AI decision cadence — the residual "freeze every couple
    // seconds" after the AssessMap/DistanceToNearestInfrastructure fix.
    // Throttle: once the full-map tier comes up empty for a building type,
    // don't pay for it again for a while (map state — deposits, buildings —
    // doesn't change fast enough to need retrying every cycle). Mutable
    // because FindBuildAnchor is const; decayed alongside the other timers.
    mutable std::map<BuildingType, double> expensiveAnchorSearchCooldown;
};

class AIController : public IController
{
public:
    explicit AIController(int controlledPlayerId);

    void Update(GameWorld& world, double dt) override;
    void SetDifficulty(AIDifficulty newDifficulty);

private:
    std::unique_ptr<AIModel> CreateModel(AIDifficulty selectedDifficulty) const;

    AIDifficulty difficulty{AIDifficulty::Primitive};
    AIModelSettings settings;
    std::unique_ptr<AIModel> model;
};

#endif
