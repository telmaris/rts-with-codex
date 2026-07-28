#include "ai/AIActions.h"
#include "core/GameWorld.h"
#include "economy/StockpileIndex.h"
#include "simulation/PathingService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <set>

// Bodies extracted 1:1 from the removed PrimitiveAIModel (src/ai/Controller.cpp
// before the AI rework czystka, 2026-07-16) — see AIActions.h. Comments about
// perf and determinism fixes travel with the code they protect.

namespace
{
    // Bridges ARE resource roads for every AI purpose (adjacency, BFS
    // passability, connected-road targets) — they only differ in what tile
    // they may stand on (the military track, where plain Road is refused).
    // Delegates to the game-wide IsRoadLike(BuildingType) single source of
    // truth (economy/Building.h, B6). Playtest 2026-07-16: the AI treating
    // only BuildingType::Road as road-like wedged it at the track edge
    // forever.
    bool IsRoadLikeBuilding(const Building* building)
    {
        return building != nullptr && IsRoadLike(building->buildingType);
    }

    int TileDistance(TileMap& tilemap, const Building* a, const Building* b)
    {
        if (a == nullptr || b == nullptr)
            return std::numeric_limits<int>::max();

        Vec2i apos = tilemap.GetCoordsFromId(a->positionId);
        Vec2i bpos = tilemap.GetCoordsFromId(b->positionId);
        return std::abs(apos.x - bpos.x) + std::abs(apos.y - bpos.y);
    }

    int DistanceToNearestInfrastructure(TileMap& tilemap, Player* player, Vec2i pos)
    {
        int best = std::numeric_limits<int>::max() / 4;
        if (player == nullptr)
            return best;

        // Perf fix (2026-07-12): this iterated the WHOLE tilemap (~90k tiles on
        // the default map) — and it runs once per candidate tile inside
        // FindBuildAnchor's scan, making anchor search O(map²) (billions of
        // tile visits, measured 7.7 s per AI build decision). The tracked-
        // buildings registry is a few dozen entries with identical results.
        // Iteration order of the std::set doesn't matter: min() is
        // order-independent, so determinism is preserved.
        for (const Building* building : player->GetTrackedBuildings())
        {
            if (building == nullptr || building->owner != player || building->IsUnderConstruction())
                continue;
            if (!IsRoadLike(building->buildingType) && !AIActions::IsStorageHub(building))
                continue;

            Vec2i buildingPos = tilemap.GetCoordsFromId(building->positionId);
            best = std::min(best, std::abs(pos.x - buildingPos.x) + std::abs(pos.y - buildingPos.y));
        }
        return best;
    }

    int FootprintGap(TileMap& tilemap, Vec2i anchor, Vec2i footprint, const Building* other)
    {
        if (other == nullptr)
            return std::numeric_limits<int>::max() / 4;
        Vec2i otherAnchor = tilemap.GetCoordsFromId(other->positionId);
        Vec2i otherFootprint = other->GetFootprint();
        int left = anchor.x;
        int right = anchor.x + footprint.x - 1;
        int top = anchor.y;
        int bottom = anchor.y + footprint.y - 1;
        int otherLeft = otherAnchor.x;
        int otherRight = otherAnchor.x + otherFootprint.x - 1;
        int otherTop = otherAnchor.y;
        int otherBottom = otherAnchor.y + otherFootprint.y - 1;
        int dx = std::max({0, otherLeft - right - 1, left - otherRight - 1});
        int dy = std::max({0, otherTop - bottom - 1, top - otherBottom - 1});
        return dx + dy;
    }

    std::vector<int> RelevantTrackTiles(GameWorld& world, Player* player)
    {
        std::set<int> unique;
        if (player == nullptr)
            return {};
        for (const MilitaryRoute& route : world.GetMilitaryRoads().GetRoutes())
        {
            if (route.OtherPlayer(player->id) < 0)
                continue;
            unique.insert(route.tiles.begin(), route.tiles.end());
        }
        return {unique.begin(), unique.end()};
    }

    int TrackCoverageAt(GameWorld& world, const std::vector<int>& trackTiles,
                        Vec2i anchor, Vec2i footprint, double range,
                        const std::set<int>* alreadyCovered = nullptr)
    {
        double centerX = anchor.x + footprint.x * 0.5;
        double centerY = anchor.y + footprint.y * 0.5;
        double rangeSq = range * range;
        int covered = 0;
        for (int tileId : trackTiles)
        {
            if (alreadyCovered != nullptr && alreadyCovered->contains(tileId))
                continue;
            Vec2i tile = world.GetTileMap().GetCoordsFromId(tileId);
            double dx = centerX - (tile.x + 0.5);
            double dy = centerY - (tile.y + 0.5);
            if (dx * dx + dy * dy <= rangeSq)
                covered++;
        }
        return covered;
    }
}

namespace AIActions
{

bool IsStorageHub(const Building* building)
{
    return building != nullptr &&
           (building->buildingType == BuildingType::Headquarters ||
            building->buildingType == BuildingType::StorageBuilding);
}

void AIActionState::Decay(double dt)
{
    auto decay = [dt](auto& container)
    {
        for (auto it = container.begin(); it != container.end();)
        {
            it->second -= dt;
            if (it->second <= 0.0)
                it = container.erase(it);
            else
                ++it;
        }
    };
    decay(reservedRoadTiles);
    decay(blockedRoadTiles);
    decay(recentBuildOrders);
    decay(expensiveAnchorSearchCooldown);
    decay(deficitBackoff);
}

int CountOwnedBuildings(Player* player, BuildingType type)
{
    if (player == nullptr)
        return 0;
    return player->GetTrackedBuildingCount(type);
}

int CountCompletedOrQueuedBuildings(GameWorld& world, Player* player, BuildingType type)
{
    int count = 0;
    for (auto& tile : world.GetTileMap().tilemap)
    {
        Building* building = tile.building.get();
        if (building != nullptr && building->owner == player && building->buildingType == type)
            count++;
    }
    return count;
}

int CountStoredResource(Player* player, ResourceType type)
{
    return player != nullptr ? StockpileIndex::GetTotal(*player, type) : 0;
}

int CountProducersOfResource(Player* player, ResourceType resource)
{
    int count = 0;
    if (player == nullptr || resource == ResourceType::Null)
        return count;

    // Duplicate-producer guard fix (2026-07-20): counts buildings that
    // actually produce THIS resource on the terrain they're standing on
    // (ProductionComponent::products, same source DiagnoseResourceNeed reads),
    // not every building of a candidate BuildingType. A Mine's products come
    // from the terrain it was placed on (BuildingConfig.cpp), so a Mine on a
    // COAL tile does not count toward IRON_ORE's producer count and vice
    // versa — counting by BuildingType alone conflated the two and let a
    // stalled/unhealthy COAL mine block (or a healthy one wrongly "diversify
    // away from") IRON_ORE decisions that have nothing to do with it.
    for (const auto* building : player->GetTrackedBuildingsWithComponent<ProductionComponent>())
    {
        const auto* production = building != nullptr ? building->GetComponent<ProductionComponent>() : nullptr;
        if (production == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;
        if (production->products.contains(resource))
            count++;
    }
    return count;
}

bool HasProducerOrPendingForResource(Player* player, ResourceType resource)
{
    return CountProducersOrPendingForResource(player, resource) > 0;
}

int CountProducersOrPendingForResource(Player* player, ResourceType resource)
{
    if (player == nullptr || resource == ResourceType::Null)
        return 0;
    // Same terrain-resolved match as CountProducersOfResource, but a producer
    // still under construction counts too — its ProductionComponent::products
    // is fixed the moment it's placed (terrain baked in), so the opening plan
    // must not re-order a coal mine that's already on its way up.
    int count = 0;
    for (const auto* building : player->GetTrackedBuildingsWithComponent<ProductionComponent>())
    {
        const auto* production = building != nullptr ? building->GetComponent<ProductionComponent>() : nullptr;
        if (production == nullptr || building->owner != player)
            continue;
        if (production->products.contains(resource))
            count++;
    }
    return count;
}

bool TrySwitchRecipeFor(GameWorld& world, Player* player, ResourceType resource)
{
    if (player == nullptr || resource == ResourceType::Null)
        return false;
    // Something already actively smelts/forges this — no switch needed.
    if (CountProducersOfResource(player, resource) > 0)
        return false;

    // Simulation-visible (which building changes recipe, and its cleared
    // buffers) — sort by id, same determinism rule as every other actuator.
    const auto& tracked = player->GetTrackedBuildingsWithComponent<ProductionComponent>();
    std::vector<Building*> candidates(tracked.begin(), tracked.end());
    std::sort(candidates.begin(), candidates.end(), [](Building* a, Building* b) { return a->id < b->id; });
    for (Building* building : candidates)
    {
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;
        const auto* recipes = building->GetComponent<RecipeComponent>();
        if (recipes == nullptr || !recipes->HasSelectableRecipes())
            continue;
        for (int i = 0; i < static_cast<int>(recipes->recipes.size()); i++)
        {
            if (i == recipes->activeRecipeIndex)
                continue;
            auto it = recipes->recipes[i].outputs.find(resource);
            if (it == recipes->recipes[i].outputs.end() || it->second <= 0)
                continue;
            world.SubmitCommand(GameCommand::SetRecipe(player->id, building->positionId, i));
            return true;
        }
    }
    return false;
}

int GetResourceRate(const std::map<ResourceType, int>& rates, ResourceType type)
{
    auto it = rates.find(type);
    return it != rates.end() ? it->second : 0;
}

Building* FindOwnedHeadquarters(Player* player)
{
    if (player == nullptr)
        return nullptr;
    // First-match over the unsorted set is safe here ONLY because a player
    // structurally owns at most one HQ, ever (GameWorld::EliminatePlayer
    // transfers ProductionComponent buildings only — verified in the
    // pre-Block-C determinism audit, see docs/tech_debt.md).
    for (auto* building : player->GetTrackedBuildings())
        if (building != nullptr && building->owner == player && building->buildingType == BuildingType::Headquarters)
            return building;
    return nullptr;
}

Building* FindUniversity(Player* player)
{
    if (player == nullptr)
        return nullptr;

    // Determinism audit (docs/work_plan_2026-07-13.md, pre-Block-C): unlike
    // FindOwnedHeadquarters, there is NO "at most one" invariant here — a
    // player can own multiple Universities (no build-count cap), so which
    // idle one gets picked to start research must not depend on Building*
    // heap addresses (it becomes per-building ResearchComponent state, which
    // is simulation-visible).
    std::vector<Building*> sortedUniversities(player->GetTrackedBuildings().begin(), player->GetTrackedBuildings().end());
    std::sort(sortedUniversities.begin(), sortedUniversities.end(), [](Building* a, Building* b) { return a->id < b->id; });

    for (auto* building : sortedUniversities)
    {
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;
        if (building->buildingType != BuildingType::University)
            continue;
        const auto* research = building->GetComponent<ResearchComponent>();
        if (research != nullptr && research->remaining <= 0.0)  // not currently researching
            return building;
    }
    return nullptr;
}

// C1 (docs/work_plan_2026-07-13.md): first non-defeated player reachable
// through the military-road ring (direct edge, or via a chain of conquered
// HQs — PathingService::AreHqsConnected already covers both). Iterates
// playerHandler.players in id order (std::map, not a heap-address-keyed
// container), so the result is deterministic across independently
// constructed GameWorld instances for the same seed/command history.
int FindAttackTargetPlayer(GameWorld& world, Player* player)
{
    if (player == nullptr)
        return -1;

    auto isEliminated = [&](int playerId)
    {
        return world.IsPlayerDefeated(playerId);
    };

    for (const auto& [otherId, otherPlayer] : world.GetPlayerHandler().players)
    {
        if (otherPlayer == nullptr || otherId == player->id || otherPlayer->defeated)
            continue;
        if (PathingService::AreHqsConnected(world.GetMilitaryRoads(), player->id, otherId, isEliminated))
            return otherId;
    }
    return -1;
}

bool HasRoadConnection(Player* player, const Building* source, const Building* target)
{
    if (player == nullptr || source == nullptr || target == nullptr || player->roadNetwork == nullptr)
        return false;

    if (source->IsUnderConstruction() || target->IsUnderConstruction())
        return false;

    auto path = player->roadNetwork->CalculatePath(const_cast<Building*>(source), const_cast<Building*>(target));
    return !path.empty();
}

Building* FindNearestRoadTarget(GameWorld& world, Player* player, const Building* source)
{
    PathingService* pather = world.GetPathingService();
    if (pather == nullptr || source == nullptr)
        return nullptr;

    auto sourcePos = world.GetTileMap().GetCoordsFromId(source->positionId);

    // Stage 1: Find nearest Road/Storage/HQ infrastructure
    auto infrastructure_predicate = [&](const Building* building) -> bool {
        if (building == nullptr || building == source || building->owner != player || building->IsUnderConstruction())
            return false;
        return IsRoadLike(building->buildingType) || IsStorageHub(building);
    };

    Building* bestInfrastructure = pather->FindNearestBuilding(sourcePos, infrastructure_predicate, Domain::Global());
    if (bestInfrastructure != nullptr)
        return bestInfrastructure;

    Building* bestReceiver = nullptr;
    int bestReceiverDistance = std::numeric_limits<int>::max();
    for (const auto& receiver : source->GetReceiverViews())
    {
        Building* building = receiver.building;
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;

        int distance = TileDistance(world.GetTileMap(), source, building);
        if (distance < bestReceiverDistance)
        {
            bestReceiverDistance = distance;
            bestReceiver = building;
        }
    }
    if (bestReceiver != nullptr)
        return bestReceiver;

    // Stage 3: Find nearest consumer building for any output resource
    for (const auto& output : source->GetOutputBufferViews())
    {
        auto consumer_predicate = [&](const Building* building) -> bool {
            if (building == nullptr || building == source || building->owner != player || building->IsUnderConstruction())
                return false;
            return !IsStorageHub(building) && building->CanAcceptResource(output.type);
        };

        Building* bestConsumer = pather->FindNearestBuilding(sourcePos, consumer_predicate, Domain::Global());
        if (bestConsumer != nullptr)
            return bestConsumer;
    }

    // Stage 4: Fallback - find any Road or Storage infrastructure
    auto fallback_predicate = [&](const Building* building) -> bool {
        if (building == nullptr || building == source || building->owner != player || building->IsUnderConstruction())
            return false;
        return IsRoadLike(building->buildingType) || IsStorageHub(building);
    };

    return pather->FindNearestBuilding(sourcePos, fallback_predicate, Domain::Global());
}

Building* FindNearestStorageConnectedRoad(GameWorld& world, Player* player, const Building* source)
{
    if (player == nullptr || source == nullptr)
        return nullptr;

    std::vector<Building*> storageNodes;
    for (auto* building : player->GetTrackedBuildings())
    {
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;
        if (IsStorageHub(building))
            storageNodes.push_back(building);
    }
    if (storageNodes.empty())
        return nullptr;

    Vec2i sourcePos = world.GetTileMap().GetCoordsFromId(source->positionId);
    Building* bestRoad = nullptr;
    int bestDistance = std::numeric_limits<int>::max();
    for (auto& tile : world.GetTileMap().tilemap)
    {
        Building* road = tile.building.get();
        if (road == nullptr || road->owner != player || road->IsUnderConstruction() || !IsRoadLikeBuilding(road))
            continue;

        bool connectedToStorage = false;
        for (Building* storage : storageNodes)
        {
            if (HasRoadConnection(player, storage, road))
            {
                connectedToStorage = true;
                break;
            }
        }
        if (!connectedToStorage)
            continue;

        Vec2i roadPos = world.GetTileMap().GetCoordsFromId(road->positionId);
        int distance = std::abs(sourcePos.x - roadPos.x) + std::abs(sourcePos.y - roadPos.y);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestRoad = road;
        }
    }

    return bestRoad;
}

std::vector<AIProducerOption> FindProducerOptions(ResourceType resource)
{
    std::vector<AIProducerOption> options;
    const auto& buildableTypes = GetBuildableBuildingTypes();
    auto inspectProduction = [&](BuildingType buildingType, TileType terrain, const ProductionDefinition& production)
    {
        if (production.cycleTime <= 0.0)
            return;
        for (const auto& output : production.outputs)
        {
            if (output.type != resource || output.amount <= 0)
                continue;
            AIProducerOption option;
            option.buildingType = buildingType;
            option.terrain = terrain;
            option.outputPerMinute = output.amount * 60.0 / production.cycleTime;
            option.inputs = production.inputs;
            options.push_back(std::move(option));
        }
    };

    for (const auto& definition : GetBuildingDefinitions())
    {
        if (std::find(buildableTypes.begin(), buildableTypes.end(), definition.type) ==
            buildableTypes.end())
            continue;

        TileType baseTerrain = definition.type == BuildingType::Woodcutter ? TileType::WOOD : TileType::GRASS;
        inspectProduction(definition.type, baseTerrain, definition.production);
        for (const auto& terrainProduction : definition.terrainProductions)
            inspectProduction(definition.type, terrainProduction.tileType, terrainProduction.production);
        for (const auto& recipe : definition.recipes)
            inspectProduction(definition.type, TileType::GRASS, recipe.production);
    }

    return options;
}

AIResourceDiagnosis DiagnoseResourceNeed(Player* player, ResourceType resource, int depth,
                                         const std::map<ResourceType, int>* consumptionBias,
                                         const std::map<ResourceType, double>* priorityWeights)
{
    AIResourceDiagnosis diagnosis;
    diagnosis.resource = resource;
    if (player == nullptr || resource == ResourceType::Null)
        return diagnosis;

    auto biasFor = [&](ResourceType type)
    {
        if (consumptionBias == nullptr)
            return 0;
        auto it = consumptionBias->find(type);
        return it != consumptionBias->end() ? it->second : 0;
    };

    int produced = GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute, resource);
    int consumed = GetResourceRate(player->economyTelemetry.current.consumptionRatesPerMinute, resource) +
                   biasFor(resource);
    int stored = CountStoredResource(player, resource);
    bool hasProducerBuilding = false;
    bool stalledProducer = false;
    bool fullOutput = false;
    bool undermannedProducer = false;
    // AI economy tuning plan (2026-07-18, Task 3): a producer already ordered
    // and under construction credits its future output by capping urgency
    // below (see after the urgency accumulation) - without this, the deficit
    // ladder kept re-picking the SAME top deficit every cycle even after
    // ordering a producer for it, since an in-progress build never lowers
    // urgency on its own (playtest 2026-07-18: a big WOOD bias produced a
    // forest of Woodcutters and nothing else, never rotating to the next
    // problem).
    bool producerUnderConstruction = false;

    for (const auto* building : player->GetTrackedBuildingsWithComponent<ProductionComponent>())
    {
        const auto* production = building != nullptr ? building->GetComponent<ProductionComponent>() : nullptr;
        if (production == nullptr || building->owner != player)
            continue;
        if (!production->products.contains(resource))
            continue;
        if (building->IsUnderConstruction())
        {
            producerUnderConstruction = true;
            continue;
        }
        hasProducerBuilding = true;
        stalledProducer = stalledProducer || building->IsProductionStalled();
        const auto* workers = building->GetComponent<WorkerComponent>();
        undermannedProducer = undermannedProducer ||
            (workers != nullptr && workers->assigned < workers->GetModifiedCapacity(*building));
        auto bufferIt = production->outputBuffers.find(resource);
        if (bufferIt != production->outputBuffers.end() && bufferIt->second.buffer.size() >= bufferIt->second.bufferSize)
            fullOutput = true;
    }
    diagnosis.hasProducerBuilding = hasProducerBuilding;

    if (consumed > produced)
    {
        double deficit = static_cast<double>(consumed - produced) / std::max(1, consumed);
        diagnosis.urgency = std::max(diagnosis.urgency, 0.35 + deficit * 0.55);
        diagnosis.reason = hasProducerBuilding ? "negative production balance" : "missing producer";
    }
    if (consumed > 0 && stored < consumed * (depth == 0 ? 2 : 1))
    {
        diagnosis.urgency = std::max(diagnosis.urgency, 0.28 + (1.0 - std::min(1.0, stored / static_cast<double>(std::max(1, consumed * 2)))) * 0.35);
        if (diagnosis.reason.empty())
            diagnosis.reason = "low reserve";
    }
    if (produced == 0 && consumed > 0)
    {
        diagnosis.urgency = std::max(diagnosis.urgency, 0.62);
        diagnosis.reason = hasProducerBuilding ? "producer inactive" : "missing producer";
    }
    if (stalledProducer)
    {
        diagnosis.logisticsProblem = true;
        diagnosis.urgency = std::max(diagnosis.urgency, 0.55);
        diagnosis.reason = "producer stalled";
    }
    if (fullOutput)
    {
        diagnosis.storageProblem = true;
        diagnosis.urgency = std::max(diagnosis.urgency, 0.45);
        diagnosis.reason = "output storage or transport bottleneck";
    }
    if (undermannedProducer && player->strategicResources.Get(StrategicResourceType::Manpower) < 2.0)
    {
        diagnosis.manpowerProblem = true;
        diagnosis.urgency = std::max(diagnosis.urgency, 0.42);
        diagnosis.reason = "not enough manpower";
    }

    // A producer for this resource is already ordered and building - its
    // future output is credited by capping the urgency, so the deficit
    // ladder rotates to the NEXT problem instead of stacking another copy
    // every cycle (see producerUnderConstruction above).
    if (producerUnderConstruction)
        diagnosis.urgency = std::min(diagnosis.urgency, 0.3);

    // Build-order priority (2026-07-19): scales the final urgency so ties
    // between resources with equally "zero production yet" raw deficits
    // (WOOD/STONE/PLANKS/food chain vs IRON/TOOLS/swords, all capped at the
    // same 0.62-0.9 band above) resolve by DESIGN instead of by whichever
    // ResourceType enum value happens to be numerically lowest — that
    // accident used to make IRON (enum 3) win every opening economy decision
    // and build a Foundry before a single Woodcutter, regardless of the
    // consumption bias magnitude (see docs/tech_debt.md).
    if (priorityWeights != nullptr)
    {
        auto it = priorityWeights->find(resource);
        double weight = it != priorityWeights->end() ? it->second : 1.0;
        diagnosis.urgency = std::clamp(diagnosis.urgency * weight, 0.0, 1.0);
    }

    if (diagnosis.urgency <= 0.05)
        return diagnosis;

    for (const auto& option : FindProducerOptions(resource))
    {
        for (const auto& input : option.inputs)
        {
            int inputProduced = GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute, input.type);
            int inputStored = CountStoredResource(player, input.type);
            // AI economy tuning plan (2026-07-18, Task 2): descend only when
            // the input genuinely doesn't exist in this economy yet - no real
            // production AND no meaningful stock. The consumption bias is
            // deliberately NOT applied here - it amortizes the FINAL
            // resource's demand and already drives that resource's own
            // deficit; counting it again against chain inputs made every
            // chain walk collapse to its raw material (playtest 2026-07-18:
            // PLANKS forever redirected to WOOD, no LumberMill ever built).
            if (inputProduced == 0 && inputStored < input.amount * 2)
            {
                if (std::find(diagnosis.missingInputs.begin(), diagnosis.missingInputs.end(), input.type) == diagnosis.missingInputs.end())
                    diagnosis.missingInputs.push_back(input.type);
            }
        }
    }

    return diagnosis;
}

double ForecastSecondsToAfford(Player* player, const std::vector<ResourceAmountDefinition>& costs)
{
    if (player == nullptr)
        return 1e9;

    double worst = 0.0;
    for (const auto& cost : costs)
    {
        int stored = CountStoredResource(player, cost.type);
        if (stored >= cost.amount)
            continue;
        int ratePerMinute = GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute, cost.type);
        if (ratePerMinute <= 0)
            return 1e9;  // no path to afford with current economy
        double seconds = (cost.amount - stored) / (ratePerMinute / 60.0);
        worst = std::max(worst, seconds);
    }
    return worst;
}

int CountTowerTrackCoverage(GameWorld& world, Player* player, const Building* tower)
{
    if (player == nullptr || tower == nullptr || tower->owner != player)
        return 0;
    const auto* combat = tower->GetComponent<TowerCombatComponent>();
    if (combat == nullptr)
        return 0;
    Vec2i anchor = world.GetTileMap().GetCoordsFromId(tower->positionId);
    return TrackCoverageAt(world, RelevantTrackTiles(world, player), anchor, tower->GetFootprint(),
                           combat->GetModifiedRange(*tower));
}

Vec2i FindBuildAnchor(GameWorld& world, Player* player, BuildingType type,
                      TileType preferredTile, const Building* target, AIActionState& state)
{
    const auto& definition = GetBuildingDefinition(type);
    BuildingPlacementCategory placementCategory = definition.placementCategory;
    // A Mine is data-driven as part of the metal district by default, but a
    // stone quarry feeds construction rather than smelting. Keep the shared
    // building definition while letting the actual deposit choose its local
    // district context.
    if (type == BuildingType::Mine && preferredTile == TileType::STONE)
        placementCategory = BuildingPlacementCategory::Construction;
    TileMap& map = world.GetTileMap();
    Vec2i targetPos{};
    bool hasTarget = target != nullptr;
    if (hasTarget)
        targetPos = map.GetCoordsFromId(target->positionId);
    Building* headquarters = FindOwnedHeadquarters(player);
    Vec2i headquartersPos{};
    bool hasHeadquarters = headquarters != nullptr;
    if (hasHeadquarters)
        headquartersPos = map.GetCoordsFromId(headquarters->positionId);

    // Thematic district context. Same-category buildings define the local
    // cluster; recipe/storage relationships add a stronger supplier-consumer
    // pull. Both are soft capped, so a cramped district never makes a legal
    // site impossible.
    std::vector<const Building*> sameCategory;
    std::vector<const Building*> relatedProduction;
    std::set<ResourceType> candidateInputs;
    std::set<ResourceType> candidateOutputs;
    auto collectProduction = [&](const ProductionDefinition& production)
    {
        for (const auto& input : production.inputs)
            candidateInputs.insert(input.type);
        for (const auto& output : production.outputs)
            candidateOutputs.insert(output.type);
    };
    collectProduction(definition.production);
    for (const auto& terrain : definition.terrainProductions)
        collectProduction(terrain.production);
    for (const auto& recipe : definition.recipes)
        collectProduction(recipe.production);
    if (placementCategory == BuildingPlacementCategory::Military)
        for (const auto& buffer : definition.storageBuffers)
            candidateInputs.insert(buffer.type);

    for (const Building* building : player->GetTrackedBuildings())
    {
        if (building == nullptr || building->owner != player || IsRoadLike(building->buildingType))
            continue;
        const auto& otherDefinition = GetBuildingDefinition(building->buildingType);
        if (placementCategory != BuildingPlacementCategory::None &&
            otherDefinition.placementCategory == placementCategory)
            sameCategory.push_back(building);

        bool related = false;
        if (const auto* production = building->GetComponent<ProductionComponent>(); production != nullptr)
        {
            for (const auto& [resource, buffer] : production->products)
                if (candidateInputs.contains(resource))
                    related = true;
        }
        for (ResourceType output : candidateOutputs)
            if (building->CanAcceptResource(output))
                related = true;
        if (related)
            relatedProduction.push_back(building);
    }

    const bool towerPlacement = type == BuildingType::DefenseTower;
    const std::vector<int> relevantTrackTiles = towerPlacement ? RelevantTrackTiles(world, player) : std::vector<int>{};
    std::set<int> existingTowerCoverage;
    if (towerPlacement)
    {
        for (const Building* building : player->GetTrackedBuildingsWithComponent<TowerCombatComponent>())
        {
            if (building == nullptr || building->owner != player || building->IsUnderConstruction())
                continue;
            const auto* combat = building->GetComponent<TowerCombatComponent>();
            if (combat == nullptr)
                continue;
            Vec2i towerAnchor = map.GetCoordsFromId(building->positionId);
            for (int tileId : relevantTrackTiles)
            {
                Vec2i tile = map.GetCoordsFromId(tileId);
                double centerX = towerAnchor.x + building->GetFootprint().x * 0.5;
                double centerY = towerAnchor.y + building->GetFootprint().y * 0.5;
                double dx = centerX - (tile.x + 0.5);
                double dy = centerY - (tile.y + 0.5);
                double range = combat->GetModifiedRange(*building);
                if (dx * dx + dy * dy <= range * range)
                    existingTowerCoverage.insert(tileId);
            }
        }
    }

    // Evaluates one tile; updates best score/anchor. Same filter + scoring as
    // the original whole-map scan.
    Vec2i bestAnchor{-1, -1};
    int bestScore = std::numeric_limits<int>::max();
    int bestExtractorRichness = 0;
    auto evaluateTile = [&](Vec2i pos)
    {
        const Tile& tile = map[map.GetIdFromCoords(pos)];
        // TD(etap-1): tile ownership no longer expands (RecalculateTerritory is
        // gone) — placement is now gated solely by CanPlaceBuilding's occupancy +
        // enemy-proximity rule, matching what the human player's build UI allows.
        if (tile.HasBuilding())
            return;

        // Perf fix (2026-07-12, follow-up): cheap single-tile terrain reject
        // BEFORE the expensive CanPlaceBuilding call (which itself scans a
        // 7x7 neighbourhood for IsWithinEnemyProximity on every single
        // candidate). For a terrain-hungry building (Woodcutter/Mine hunting
        // WOOD/STONE) the overwhelming majority of tiles in any window are
        // the wrong terrain — paying the expensive check on all of them
        // before finding out is exactly backwards. CanPlaceBuilding still
        // re-validates terrain internally (with richness, footprint-wide) —
        // this is a pure ordering change, not a relaxed check.
        if (preferredTile != TileType::GRASS && tile.tileType != preferredTile)
            return;

        if (!map.CanPlaceBuilding(type, pos, definition.footprint, player))
            return;

        int nonGrassTiles = 0;
        if (preferredTile == TileType::GRASS)
            for (int y = 0; y < definition.footprint.y; y++)
                for (int x = 0; x < definition.footprint.x; x++)
                    if (map[{pos.x + x, pos.y + y}].tileType != TileType::GRASS)
                        nonGrassTiles++;

        int distanceToTarget = hasTarget ? std::abs(pos.x - targetPos.x) + std::abs(pos.y - targetPos.y) : 0;
        int distanceToHeadquarters = hasHeadquarters ? std::abs(pos.x - headquartersPos.x) + std::abs(pos.y - headquartersPos.y) : 0;
        int infrastructureDistance = DistanceToNearestInfrastructure(map, player, pos);
        int categoryGap = std::numeric_limits<int>::max() / 4;
        for (const Building* building : sameCategory)
            categoryGap = std::min(categoryGap, FootprintGap(map, pos, definition.footprint, building));
        int relatedGap = std::numeric_limits<int>::max() / 4;
        for (const Building* building : relatedProduction)
            relatedGap = std::min(relatedGap, FootprintGap(map, pos, definition.footprint, building));
        int borderPenalty = 0;
        const std::array<Vec2i, 4> neighbours{
            Vec2i{pos.x + 1, pos.y},
            Vec2i{pos.x - 1, pos.y},
            Vec2i{pos.x, pos.y + 1},
            Vec2i{pos.x, pos.y - 1}
        };
        for (Vec2i neighbour : neighbours)
        {
            if (!map.IsInside(neighbour))
            {
                borderPenalty += 10;
                continue;
            }
            // Tile::owner (ground "territory") is a relic of the removed
            // pre-pivot territory system and always nullptr today — the
            // nearest equivalent to "growing from my own territory" is
            // "next to one of my own buildings" (docs/post_pivot_audit_2026-07-12.md T2).
            const Building* neighbourBuilding = map[map.GetIdFromCoords(neighbour)].GetBuilding();
            if (neighbourBuilding == nullptr || neighbourBuilding->owner != player)
                borderPenalty += 6;
        }

        bool terrainExtractor = preferredTile != TileType::GRASS;
        int extractorRichness = 0;
        if (terrainExtractor)
            for (int y = 0; y < definition.footprint.y; y++)
                for (int x = 0; x < definition.footprint.x; x++)
                {
                    const Tile& footprintTile = map[{pos.x + x, pos.y + y}];
                    if (footprintTile.tileType == preferredTile)
                        extractorRichness += std::max(0, footprintTile.resourceRichness);
                }
        int logisticsDistance = terrainExtractor ? infrastructureDistance : std::min(infrastructureDistance, 20);
        int score = hasTarget
            ? distanceToTarget * 9 + distanceToHeadquarters * 2 + std::min(infrastructureDistance, 20) * 5 + borderPenalty * 3
            : distanceToHeadquarters * (terrainExtractor ? 5 : 9) + logisticsDistance * (terrainExtractor ? 12 : 7) + borderPenalty * 4;
        // Extractors are only useful for the richness beneath their whole
        // footprint. This must dominate a short road or a familiar district:
        // otherwise a replacement Woodcutter happily takes a depleted remnant
        // beside the old mill while a healthy forest is one window farther.
        if (terrainExtractor)
        {
            score -= extractorRichness * 20;
        }
        // Plain buildings strongly prefer an all-GRASS footprint, but it is
        // a soft preference. A hard reject here wedged dense/debug maps once
        // no perfect 3x3/4x4 rectangle remained, even though the normal game
        // placement rules still exposed legal sites. This is the placement
        // contract requested by design: cluster when possible, relax rather
        // than stop progressing when packing gets tight.
        score += nonGrassTiles * 90;
        if (!towerPlacement && categoryGap < std::numeric_limits<int>::max() / 8)
        {
            // One empty tile between footprints is ideal: enough room for a
            // shared road without dissolving the district into map-wide sprawl.
            int categoryPenalty = categoryGap == 0 ? 24 : std::min(12, std::abs(categoryGap - 1)) * 7;
            score += categoryPenalty;
        }
        if (!towerPlacement && relatedGap < std::numeric_limits<int>::max() / 8)
        {
            int flowPenalty = relatedGap == 0 ? 18 : std::min(14, std::abs(relatedGap - 1)) * 10;
            score += flowPenalty;
        }
        if (towerPlacement)
        {
            double candidateRange = player != nullptr
                ? player->ModifyBalanceAt(BalanceStat::TowerRange, definition.tower.range, type, pos)
                : definition.tower.range;
            int coverage = TrackCoverageAt(world, relevantTrackTiles, pos, definition.footprint,
                                           candidateRange);
            if (coverage == 0)
                return; // a decorative tower must never satisfy Defense
            int marginalCoverage = TrackCoverageAt(world, relevantTrackTiles, pos, definition.footprint,
                                                   candidateRange, &existingTowerCoverage);
            // Marginal lane coverage is the primary objective. Total coverage
            // remains valuable so some overlapping concentrated fire survives.
            score -= marginalCoverage * 240 + coverage * 100;
        }
        if (score < bestScore)
        {
            bestScore = score;
            bestAnchor = pos;
            bestExtractorRichness = extractorRichness;
        }
    };

    // Perf fix (2026-07-12): the original scanned the ENTIRE tilemap (~90k
    // tiles on the default 301×301 map) — and with the per-tile
    // DistanceToNearestInfrastructure call this froze the sim for seconds per
    // AI build decision. The score strongly favors proximity to the HQ/target
    // anyway (distance terms dominate), so scan an expanding window centered
    // there instead: the practical optimum is always nearby; the full-map
    // pass remains only as the last-resort fallback when nothing valid exists
    // close (e.g. extractors hunting a distant deposit). Deterministic: pure
    // function of world state, same row-major tie-breaking within each window.
    Vec2i center = hasTarget ? targetPos
                 : hasHeadquarters ? headquartersPos
                 : Vec2i{map.params.sizeX / 2, map.params.sizeY / 2};
    int fullMargin = std::max(map.params.sizeX, map.params.sizeY);

    // Perf fix (2026-07-12, follow-up #2): a terrain-hungry candidate can keep
    // failing EVEN in the smallest window — real per-tile work no cheap
    // pre-filter can skip. Once the WHOLE search (every tier) fails for a
    // building type, don't retry ANY tier until the cooldown expires; map
    // state (deposits, nearby construction) doesn't change fast enough to
    // justify re-paying this (measured ~17-190 ms in Debug) every ~1-2 s.
    auto cooldownKey = std::make_pair(type, preferredTile);
    if (state.expensiveAnchorSearchCooldown.count(cooldownKey) > 0)
        return bestAnchor;

    for (int margin : {12, 24, 48, 96, fullMargin})
    {
        int minX = std::max(0, center.x - margin);
        int maxX = std::min(map.params.sizeX - 1, center.x + margin);
        int minY = std::max(0, center.y - margin);
        int maxY = std::min(map.params.sizeY - 1, center.y + margin);

        for (int y = minY; y <= maxY; y++)
            for (int x = minX; x <= maxX; x++)
                evaluateTile({x, y});

        // Plain buildings can accept the first local optimum. Extractors keep
        // expanding when that window contains only depleted scraps; two full
        // fresh tiles are enough to stop, matching the placement minimum.
        const int healthyExtractorRichness = std::max(1, map.params.resourceRichness) * 2;
        if (bestAnchor.x >= 0 &&
            (preferredTile == TileType::GRASS || bestExtractorRichness >= healthyExtractorRichness || margin == fullMargin))
            return bestAnchor;
    }

    state.expensiveAnchorSearchCooldown[cooldownKey] = 120.0;
    return bestAnchor;
}

bool TrySubmitBuild(GameWorld& world, Player* player, BuildingType type, Vec2i anchor,
                    AIActionState& state)
{
    if (player == nullptr || anchor.x < 0)
        return false;
    if (state.recentBuildOrders.contains(type))
        return false;

    world.SubmitCommand(GameCommand::BuildBuilding(player->id, type, anchor));
    state.recentBuildOrders[type] = type == BuildingType::Road ? 3.0 : 8.0;
    return true;
}

bool SubmitRoadPath(GameWorld& world, Player* player, const Building* source,
                    const Building* target, AIActionState& state)
{
    if (player == nullptr || source == nullptr || target == nullptr)
        return false;

    std::vector<int> startIds = world.GetTileMap().GetAdjacentTileIds(source);
    std::vector<int> goalIds = world.GetTileMap().GetAdjacentTileIds(target);
    if (IsRoadLikeBuilding(target))
    {
        // The target returned by FindNearestStorageConnectedRoad is only an
        // entry point into a whole corridor. Treat every physically connected
        // owned road/bridge as a goal, including segments still under
        // construction. Targeting one specific tile made a second producer
        // ignore a nearer pending branch and carve a parallel road toward the
        // same forest while the first branch was finishing.
        goalIds.clear();
        std::queue<int> corridor;
        std::set<int> visited;
        corridor.push(target->positionId);
        visited.insert(target->positionId);
        while (!corridor.empty())
        {
            int current = corridor.front();
            corridor.pop();
            goalIds.push_back(current);

            Vec2i pos = world.GetTileMap().GetCoordsFromId(current);
            const std::array<Vec2i, 4> neighbours{
                Vec2i{pos.x + 1, pos.y}, Vec2i{pos.x - 1, pos.y},
                Vec2i{pos.x, pos.y + 1}, Vec2i{pos.x, pos.y - 1}
            };
            for (Vec2i neighbour : neighbours)
            {
                if (!world.GetTileMap().IsInside(neighbour))
                    continue;
                int neighbourId = world.GetTileMap().GetIdFromCoords(neighbour);
                Building* road = world.GetTileMap().GetBuilding(neighbourId);
                if (road == nullptr || road->owner != player || !IsRoadLikeBuilding(road) ||
                    visited.contains(neighbourId))
                    continue;
                visited.insert(neighbourId);
                corridor.push(neighbourId);
            }
        }
    }

    auto canUseRoadPathTile = [&](int tileId)
    {
        if (tileId < 0 || tileId >= static_cast<int>(world.GetTileMap().tilemap.size()))
            return false;

        Tile& tile = world.GetTileMap()[tileId];
        Building* building = tile.GetBuilding();
        // AI economy tuning plan (2026-07-18, Task 1): a reservation must
        // only block a tile that's still EMPTY (a command was submitted but
        // hasn't placed a building yet) — once the road/bridge physically
        // stands, it's just a road, and MUST be reusable by the 0-1 BFS
        // below. Blocking it too (as before) made every freshly-placed
        // corridor look like a wall to the planner for ~6s, so the next
        // connection in the same direction dug a full parallel row instead
        // of joining the existing one (playtest 2026-07-17: "carpets" of
        // 2-3 redundant roads side by side).
        if (building == nullptr && state.blockedRoadTiles.contains(tileId))
            return false;
        // An empty track tile next to an existing Bridge can never take a
        // second Bridge (edge-to-edge crossings are rejected by
        // CanBuildFootprint's adjacency rule) — encode that here so the
        // planner reroutes up front instead of discovering it at submit
        // time, reserving the tile, and replanning every cadence.
        if (building == nullptr && tile.isMilitaryRoad)
        {
            for (int neighbourId : world.GetTileMap().GetAdjacentTileIds(world.GetTileMap().GetCoordsFromId(tileId), {1, 1}))
            {
                Building* neighbour = world.GetTileMap().GetBuilding(neighbourId);
                if (neighbour != nullptr && neighbour->buildingType == BuildingType::Bridge)
                    return false;
            }
        }
        return building == nullptr || IsRoadLikeBuilding(building);
    };

    // The military track may only be CROSSED — one track tile at a time,
    // perpendicular — never ridden along. Without this rule the track is the
    // planner's favorite corridor: a long, guaranteed building-free lane at
    // the same step cost as grass, so paths route straight down it; at
    // submit time every track tile becomes a Bridge, the first placement
    // succeeds, the second is orthogonally adjacent and gets refused, the
    // tile is reserved, the plan reroutes one tile off the track and lays a
    // Road there instead — net effect: roads glued alongside the track the
    // whole way (playtest 2026-07-19 screenshot: "AI chyba uznaje tor
    // jednostek za drogę"). Banning track→track edges makes riding
    // impossible while leaving every legal crossing intact — a legal
    // crossing of the 1-tile-wide track only ever occupies one track tile
    // per crossing anyway (that's exactly what the bridge adjacency rule
    // enforces at placement).
    auto isTrackTile = [&](int tileId)
    {
        return world.GetTileMap()[tileId].isMilitaryRoad;
    };

    // Dijkstra: stepping onto an existing (or already-ordered, i.e. placed
    // and under construction) road/bridge tile is FREE, carving a fresh tile
    // costs 1 — the planner strongly prefers reusing corridors it already
    // has over digging a parallel one a tile away. Playtest 2026-07-17: with
    // a plain unweighted BFS, incremental re-planning across maintenance
    // cadences kept picking equal-length parallel lines, carpeting the base
    // with redundant roads.
    auto constructionCost = [&](BuildingType type)
    {
        int total = 0;
        for (const auto& cost : player->GetEffectiveBuildCosts(GetBuildingDefinition(type)))
            total += std::max(0, cost.amount);
        return std::max(1, total);
    };

    const int roadConstructionCost = constructionCost(BuildingType::Road);
    // A bridge consumes a scarce crossing point on the military track. Keep
    // its game-data cost untouched, but make the route planner avoid it unless
    // the alternative is materially longer or unavailable.
    constexpr int bridgeAvoidanceMultiplier = 3;
    const int bridgeConstructionCost = constructionCost(BuildingType::Bridge) * bridgeAvoidanceMultiplier;
    auto stepCost = [&](int tileId)
    {
        if (IsRoadLikeBuilding(world.GetTileMap().GetBuilding(tileId)) ||
            state.reservedRoadTiles.contains(tileId))
            return 0;
        return world.GetTileMap()[tileId].isMilitaryRoad ? bridgeConstructionCost : roadConstructionCost;
    };

    using PathQueueEntry = std::pair<int, int>;
    std::priority_queue<PathQueueEntry, std::vector<PathQueueEntry>, std::greater<PathQueueEntry>> frontier;
    std::map<int, int> parent;
    std::map<int, int> pathCost;
    for (int startId : startIds)
    {
        if (!canUseRoadPathTile(startId) || parent.contains(startId))
            continue;

        parent[startId] = -1;
        pathCost[startId] = stepCost(startId);
        frontier.push({pathCost[startId], startId});
    }

    int reachedGoal = -1;
    while (!frontier.empty())
    {
        const auto [currentCost, current] = frontier.top();
        frontier.pop();
        auto currentKnown = pathCost.find(current);
        if (currentKnown == pathCost.end() || currentKnown->second != currentCost)
            continue;

        if (std::find(goalIds.begin(), goalIds.end(), current) != goalIds.end())
        {
            reachedGoal = current;
            break;
        }

        Vec2i pos = world.GetTileMap().GetCoordsFromId(current);
        const std::array<Vec2i, 4> neighbours{
            Vec2i{pos.x + 1, pos.y},
            Vec2i{pos.x - 1, pos.y},
            Vec2i{pos.x, pos.y + 1},
            Vec2i{pos.x, pos.y - 1}
        };

        for (Vec2i nextPos : neighbours)
        {
            if (!world.GetTileMap().IsInside(nextPos))
                continue;

            int nextId = world.GetTileMap().GetIdFromCoords(nextPos);
            if (!canUseRoadPathTile(nextId))
                continue;
            // Cross the track, never ride it (see isTrackTile above).
            if (isTrackTile(current) && isTrackTile(nextId))
                continue;

            int weight = stepCost(nextId);
            int nextCost = currentCost + weight;
            auto known = pathCost.find(nextId);
            if (known != pathCost.end() && known->second <= nextCost)
                continue;

            pathCost[nextId] = nextCost;
            parent[nextId] = current;
            frontier.push({nextCost, nextId});
        }
    }

    if (reachedGoal < 0)
        return false;

    std::vector<int> path;
    for (int cursor = reachedGoal; cursor >= 0; cursor = parent[cursor])
        path.push_back(cursor);
    std::reverse(path.begin(), path.end());

    int newRoadTiles = 0;
    for (int tileId : path)
    {
        Building* building = world.GetTileMap().GetBuilding(tileId);
        if (building == nullptr)
            newRoadTiles++;
    }
    if (newRoadTiles <= 0)
        return false;
    // Sanity cap only — NOT a per-call submission limit. Playtest 2026-07-17:
    // the old hard "reject any path needing more than 8 new tiles" meant a
    // building farther than 8 tiles from road infrastructure could NEVER be
    // connected — the AI just kept dropping orphan stubs everywhere instead.
    // Long connections are now built incrementally: up to 8 tiles per call,
    // continuing from the (by then existing/pending) road ends on the next
    // maintenance cadence, until the path closes.
    constexpr int kMaxPlannedRoadLength = 48;
    if (newRoadTiles > kMaxPlannedRoadLength)
        return false;

    bool submitted = false;
    int submittedCount = 0;
    constexpr int maxRoadCommandsPerCall = 4;

    // Commands are charged later by ProcessCommands. Keep a local resource
    // ledger while batching this path so every submitted tile is affordable
    // together, not merely against the same pre-command stock snapshot.
    std::map<ResourceType, int> remainingResources;
    for (const auto& [resourceType, totals] : StockpileIndex::Snapshot(*player))
        remainingResources[resourceType] = totals.amount;

    for (int tileId : path)
    {
        Building* building = world.GetTileMap().GetBuilding(tileId);
        if (building != nullptr)
            continue;
        if (state.reservedRoadTiles.contains(tileId))
            continue;

        Tile& tile = world.GetTileMap()[tileId];
        BuildingType type = tile.isMilitaryRoad ? BuildingType::Bridge : BuildingType::Road;

        // Affordability gate for EVERY tile type (Road: STONE, Bridge:
        // PLANKS+STONE) — harness catch 2026-07-17: without it, an AI whose
        // stone ran dry kept submitting road commands the simulation
        // rejected, ~130 refusals per 30 s, forever. Stop here and resume
        // from this exact spot once stocks recover; no reservation, so the
        // plan stays first choice.
        if (!player->CanBuildDefinition(GetBuildingDefinition(type)))
            break;

        const auto effectiveCosts = player->GetEffectiveBuildCosts(GetBuildingDefinition(type));
        bool affordableInBatch = true;
        for (const auto& cost : effectiveCosts)
            if (remainingResources[cost.type] < cost.amount)
            {
                affordableInBatch = false;
                break;
            }
        if (!affordableInBatch)
            break;

        // Validate just before submitting (on the track only Bridge is legal,
        // and e.g. two bridges may not sit orthogonally adjacent). A tile
        // that can't take its type gets reserved — the next BFS skips
        // reserved tiles, so the plan reroutes around it instead of retrying
        // the same spot forever (playtest 2026-07-16: plain Road submitted
        // onto track tiles wedged the AI at the track edge indefinitely).
        Vec2i pos = world.GetTileMap().GetCoordsFromId(tileId);
        if (!world.GetTileMap().CanPlaceBuilding(type, pos, GetBuildingDefinition(type).footprint, player))
        {
            state.blockedRoadTiles[tileId] = 6.0;
            break;
        }

        world.SubmitCommand(GameCommand::BuildBuilding(player->id, type, pos));
        for (const auto& cost : effectiveCosts)
            remainingResources[cost.type] -= cost.amount;
        state.reservedRoadTiles[tileId] = 6.0;
        submitted = true;
        submittedCount++;
        if (submittedCount >= maxRoadCommandsPerCall)
            break;
    }

    return submitted;
}

bool TryBuildRoads(GameWorld& world, Player* player, AIActionState& state)
{
    if (player == nullptr)
        return false;

    // Roads must not consume the last construction reserve. A one-stone road
    // otherwise wins every 2 s and can absorb the quarry's entire trickle,
    // leaving the next Bakery/Inn/Smith permanently at 0 STONE even after the
    // production bias correctly asks for more. Resume automatically as soon
    // as extraction lifts stock above the reserve.
    constexpr int ConstructionStoneReserve = 20;
    if (CountStoredResource(player, ResourceType::STONE) <= ConstructionStoneReserve)
        return false;

    int roads = CountOwnedBuildings(player, BuildingType::Road);
    int buildings = 0;
    for (const auto* building : player->GetTrackedBuildings())
        if (building != nullptr && building->owner == player && !building->IsUnderConstruction() && building->buildingType != BuildingType::Road)
            buildings++;

    // Runaway-spam guard for the CHEAP single-tile stub loop below only
    // (2026-07-19 fix — was gating the whole function, including the bounded
    // SubmitRoadPath loop beneath it). A single legitimate long connection —
    // e.g. crossing the military track, which needs a Bridge tile per track
    // tile crossed — can easily need far more road tiles than 3x the
    // building count without being spam; SubmitRoadPath already bounds
    // itself (48 tiles/call, 8 commands/cycle, 6s reservation cooldown on a
    // tile that can't take its type). Gating THAT loop too meant that once a
    // base had built "enough" roads elsewhere, any building still stranded
    // across the track — needing exactly the long bridge crossing this ratio
    // was suspicious of — could never be tried again: the AI would build 27
    // road tiles finishing every OTHER connection, trip the cap, and leave
    // the one building that actually needed a bridge permanently cut off
    // (playtest 2026-07-19, HardAIMakesSteadyProgressAndAttacks: a Mine far
    // from HQ never connected, "producer stalled" for its whole output ever
    // after).
    if (roads <= std::max(6, buildings * 3))
    {
        // Determinism fix (docs/work_plan_2026-07-13.md, found while verifying B1/
        // B2): this loop returns (submits a command) on the first disconnected
        // storage-like/HQ building, so — same reasoning as the sorted loop below
        // it — iteration order is simulation-visible and must not depend on
        // Building* heap addresses.
        std::vector<Building*> disconnectedCandidates(player->GetTrackedBuildings().begin(), player->GetTrackedBuildings().end());
        std::sort(disconnectedCandidates.begin(), disconnectedCandidates.end(), [](Building* a, Building* b) { return a->id < b->id; });
        for (Building* building : disconnectedCandidates)
        {
            if (building == nullptr || building->owner != player || building->IsUnderConstruction())
                continue;
            if (!IsStorageHub(building))
                continue;
            if (auto* logistics = building->GetComponent<LogisticsComponent>();
                logistics != nullptr && logistics->IsConnectedToRoadNetwork(*building))
                continue;

            for (int tileId : world.GetTileMap().GetAdjacentTileIds(building))
            {
                if (tileId < 0 || tileId >= static_cast<int>(world.GetTileMap().tilemap.size()))
                    continue;
                Tile& tile = world.GetTileMap()[tileId];
                if (tile.HasBuilding() || state.reservedRoadTiles.contains(tileId))
                    continue;
                Vec2i pos = world.GetTileMap().GetCoordsFromId(tileId);
                const auto& roadDefinition = GetBuildingDefinition(BuildingType::Road);
                if (!world.GetTileMap().CanPlaceBuilding(BuildingType::Road, pos, roadDefinition.footprint, player))
                    continue;
                world.SubmitCommand(GameCommand::BuildBuilding(player->id, BuildingType::Road, pos));
                state.reservedRoadTiles[tileId] = 6.0;
                state.recentBuildOrders[BuildingType::Road] = 1.0;
                return true;
            }
        }
    }

    // Perf fix (2026-07-12): full tilemap scan on the ~1.25-2 s road-maintenance
    // cadence — same tracked-buildings substitution as the loop above. Sorted
    // by id rather than iterated in the set's native pointer order: this loop
    // returns (submits a command) on the first match, so iteration order is
    // simulation-visible and must not depend on Building* heap addresses,
    // which differ across separately-constructed GameWorld instances (found
    // via a flaky lockstep-determinism test).
    std::vector<Building*> roadCandidates(player->GetTrackedBuildings().begin(), player->GetTrackedBuildings().end());
    std::sort(roadCandidates.begin(), roadCandidates.end(), [](Building* a, Building* b) { return a->id < b->id; });
    for (Building* building : roadCandidates)
    {
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;
        if (IsRoadLike(building->buildingType) || IsStorageHub(building))
            continue;
        // Adjacency is not connectivity: a building beside an isolated road
        // stub or the military track still cannot transport to HQ/storage.
        // The old HasAdjacentRoad shortcut permanently stranded replacement
        // extractors and made a finite-resource recovery look "complete".
        if (auto* logistics = building->GetComponent<LogisticsComponent>();
            logistics != nullptr && logistics->IsConnectedToRoadNetwork(*building))
            continue;

        Building* targetRoad = FindNearestStorageConnectedRoad(world, player, building);
        if (targetRoad != nullptr && SubmitRoadPath(world, player, building, targetRoad, state))
            return true;
    }

    return false;
}

}
