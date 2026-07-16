#ifndef AI_ACTIONS_H
#define AI_ACTIONS_H

#include "economy/Player.h"

#include <map>
#include <vector>

class GameWorld;

// Mechanical AI actuators and read-only world queries, extracted 1:1 from the
// removed PrimitiveAIModel (AI rework 2026-07-16, TODO #2 czystka). These are
// the "already coded actions" any AI model executes decisions through — build
// placement search, road pathing/submission, catalog/economy lookups. They
// contain NO decision policy, and several carry hard-won lockstep-determinism
// fixes (sort-by-id over GetTrackedBuildings(), see
// docs/tech_debt.md) — do not "simplify" those away.
namespace AIActions
{
    // Catalog entry: one building type able to produce a resource (optionally
    // on specific terrain), with its throughput and recipe inputs.
    struct AIProducerOption
    {
        BuildingType buildingType{BuildingType::Building};
        TileType terrain{TileType::GRASS};
        double outputPerMinute{0.0};
        std::vector<ResourceAmountDefinition> inputs;
    };

    // Read-only diagnosis of why a resource is (or isn't) in trouble.
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

    // Mutable actuator bookkeeping a model owns and passes back in: road tiles
    // already ordered this cycle, per-building-type build-order cooldowns, and
    // the expensive-anchor-search backoff (see FindBuildAnchor). Decay() every
    // tick with the simulation dt.
    struct AIActionState
    {
        std::map<int, double> reservedRoadTiles;
        std::map<BuildingType, double> recentBuildOrders;
        std::map<BuildingType, double> expensiveAnchorSearchCooldown;

        void Decay(double dt);
    };

    // ── Read-only queries ────────────────────────────────────────────────────
    int CountOwnedBuildings(Player* player, BuildingType type);
    int CountCompletedOrQueuedBuildings(GameWorld& world, Player* player, BuildingType type);
    int CountStoredResource(Player* player, ResourceType type);
    int GetResourceRate(const std::map<ResourceType, int>& rates, ResourceType type);
    // A player structurally owns at most one HQ (EliminatePlayer only transfers
    // ProductionComponent buildings), so first-match here is deterministic.
    Building* FindOwnedHeadquarters(Player* player);
    // Sorted by building id — a player may own several Universities, and which
    // idle one starts research is simulation-visible state.
    Building* FindUniversity(Player* player);
    // First non-defeated player reachable over the military-road ring (direct
    // edge or through conquered HQs), -1 when none. Iterates players in id
    // order — deterministic.
    int FindAttackTargetPlayer(GameWorld& world, Player* player);
    bool HasAdjacentRoad(GameWorld& world, const Building* building);
    bool HasRoadConnection(Player* player, const Building* source, const Building* target);
    Building* FindNearestRoadTarget(GameWorld& world, Player* player, const Building* source);
    Building* FindNearestStorageConnectedRoad(GameWorld& world, Player* player, const Building* source);
    // Pure function of the static building catalog.
    std::vector<AIProducerOption> FindProducerOptions(ResourceType resource);
    AIResourceDiagnosis DiagnoseResourceNeed(Player* player, ResourceType resource, int depth = 0);
    // Seconds until the player's current production rates cover `costs`
    // (0 = affordable now, 1e9 = no path with the current economy).
    double ForecastSecondsToAfford(Player* player, const std::vector<ResourceAmountDefinition>& costs);

    // ── Actuators (submit real GameCommands) ─────────────────────────────────
    // Expanding-window placement search biased toward target/HQ proximity;
    // backs off per building type once a full-map pass fails (state cooldown).
    Vec2i FindBuildAnchor(GameWorld& world, Player* player, BuildingType type,
                          TileType preferredTile, const Building* target, AIActionState& state);
    bool TrySubmitBuild(GameWorld& world, Player* player, BuildingType type, Vec2i anchor,
                        AIActionState& state);
    // BFS road path between two buildings' adjacency, submitted as Road build
    // commands (capped per call); reserves tiles in `state` against re-orders.
    bool SubmitRoadPath(GameWorld& world, Player* player, const Building* source,
                        const Building* target, AIActionState& state);
    // Road-network maintenance: first ensures storage-like/HQ buildings have an
    // adjacent road stub, then connects the first (lowest-id) unconnected
    // production building to storage-connected road infrastructure.
    bool TryBuildRoads(GameWorld& world, Player* player, AIActionState& state);
}

#endif
