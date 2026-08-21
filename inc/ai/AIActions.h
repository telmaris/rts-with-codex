#ifndef AI_ACTIONS_H
#define AI_ACTIONS_H

#include "economy/Player.h"

#include <map>
#include <utility>
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
        // At least one COMPLETED producer of `resource` already exists (2026-
        // 07-19 — see TryBuildProducerFor). Distinguishes "genuinely no
        // producer yet" from "a producer exists but is stalled/bottlenecked/
        // unmanned" — building ANOTHER producer only fixes the former; the
        // latter needs its existing one repaired (logistics/manpower), not a
        // duplicate that will hit the same wall.
        bool hasProducerBuilding{false};
    };

    // Mutable actuator bookkeeping a model owns and passes back in: road tiles
    // already ordered this cycle, per-building-type build-order cooldowns, and
    // the expensive-anchor-search backoff (see FindBuildAnchor). Decay() every
    // tick with the simulation dt.
    struct AIActionState
    {
        std::map<int, double> reservedRoadTiles;
        std::map<int, double> blockedRoadTiles;
        std::map<BuildingType, double> recentBuildOrders;
        // Keyed by (type, preferredTile) — not type alone (2026-07-19 fix):
        // a failed search for one terrain (e.g. Mine hunting COAL) used to
        // block EVERY other terrain the same BuildingType can extract (Mine
        // hunting STONE or IRON_ORE) for the same 120s, even though those are
        // completely independent searches over different tiles.
        std::map<std::pair<BuildingType, TileType>, double> expensiveAnchorSearchCooldown;
        // A resource's deficit went through TryBuildProducerFor and every
        // producer option was unaffordable (2026-07-19) — unlike a failed
        // anchor search, CanBuildDefinition failures have no backoff of
        // their own, so a resource that's simply short on OTHER resources
        // (not a missing anchor) gets retried every decision cycle forever,
        // starving every lower-priority deficit of a turn even when THEY
        // would succeed. Short cooldown (not the anchor search's 120s —
        // affordability can change within a few cycles as production
        // trickles in) so the ladder rotates fairly instead of wedging on
        // the top entry.
        std::map<ResourceType, double> deficitBackoff;

        void Decay(double dt);
    };

    // ── Read-only queries ────────────────────────────────────────────────────
    int CountOwnedBuildings(Player* player, BuildingType type);
    int CountCompletedOrQueuedBuildings(GameWorld& world, Player* player, BuildingType type);
    // True only for network storage hubs. Barracks and towers also own a
    // StorageComponent, but theirs is a local consumer buffer and still needs
    // a complete route to a real hub.
    bool IsStorageHub(const Building* building);
    int CountStoredResource(Player* player, ResourceType type);
    // Completed producers of `resource` specifically (matched via
    // ProductionComponent::products, the terrain-resolved recipe output) —
    // NOT every building of a BuildingType that CAN produce it on some other
    // terrain (e.g. a Mine standing on COAL vs. one standing on IRON_ORE).
    int CountProducersOfResource(Player* player, ResourceType resource);
    // Same terrain/recipe-resolved count, including buildings still under
    // construction. Used by ordered progression targets where a pending
    // second quarry must satisfy the step immediately and prevent duplicates.
    int CountProducersOrPendingForResource(Player* player, ResourceType resource);
    // As above but also counts a producer still UNDER CONSTRUCTION (its
    // terrain-resolved products are already fixed at placement) — the opening
    // plan needs this so a coal mine it just ordered isn't re-ordered as
    // "still missing" while it builds.
    bool HasProducerOrPendingForResource(Player* player, ResourceType resource);
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
    // `consumptionBias` (optional): virtual per-minute consumption added on
    // top of real telemetry — the AI economy bias (ai/AIEconomyBias.h, user
    // design 2026-07-17) amortizing construction/recruitment/ammo costs so
    // the AI sustains production for them.
    // `priorityWeights` (optional): normalized [0,1] build-order weight per
    // resource (ai/AIEconomyBias.h's `priority` table, user design
    // 2026-07-19) — multiplies the final urgency, so a resource without an
    // explicit entry (weight 1.0) is unaffected and low-priority resources
    // consistently lose ties against high-priority ones instead of the
    // outcome depending on ResourceType's raw enum value.
    AIResourceDiagnosis DiagnoseResourceNeed(Player* player, ResourceType resource, int depth = 0,
                                             const std::map<ResourceType, int>* consumptionBias = nullptr,
                                             const std::map<ResourceType, double>* priorityWeights = nullptr);
    // Seconds until the player's current production rates cover `costs`
    // (0 = affordable now, 1e9 = no path with the current economy).
    double ForecastSecondsToAfford(Player* player, const std::vector<ResourceAmountDefinition>& costs);
    // Number of relevant military-route tiles inside this tower's real combat
    // radius. Zero means the tower can never contribute to this player's lane.
    int CountTowerTrackCoverage(GameWorld& world, Player* player, const Building* tower);

    // ── Actuators (submit real GameCommands) ─────────────────────────────────
    // Expanding-window placement search biased toward target/HQ proximity;
    // backs off per building type once a full-map pass fails (state cooldown).
    Vec2i FindBuildAnchor(GameWorld& world, Player* player, BuildingType type,
                          TileType preferredTile, const Building* target, AIActionState& state);
    bool TrySubmitBuild(GameWorld& world, Player* player, BuildingType type, Vec2i anchor,
                        AIActionState& state);
    // Assign an explicit role to the nth completed building of one type.
    // Used by policy code for stable workshop roles (e.g. Smith #0 = tools,
    // Smith #1 = swords). Returns true only when it submits a SetRecipe.
    bool TryAssignRecipe(GameWorld& world, Player* player, BuildingType buildingType,
                         int buildingOrdinal, ResourceType resource);

    // Generic recovery for an inactive recipe. It may repurpose only a
    // redundant production line: the last active producer of its current
    // output is protected. This prevents a lone Smith from oscillating
    // between TOOLS and IRON_SWORD as consecutive deficits are evaluated.
    // Deterministic: candidates sorted by building id.
    bool TrySwitchRecipeFor(GameWorld& world, Player* player, ResourceType resource);
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
