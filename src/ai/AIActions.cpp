#include "ai/AIActions.h"
#include "core/GameWorld.h"
#include "simulation/PathingService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>

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
            if (building->buildingType != BuildingType::Road && !building->IsStorageLike() && building->buildingType != BuildingType::Headquarters)
                continue;

            Vec2i buildingPos = tilemap.GetCoordsFromId(building->positionId);
            best = std::min(best, std::abs(pos.x - buildingPos.x) + std::abs(pos.y - buildingPos.y));
        }
        return best;
    }
}

namespace AIActions
{

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
    decay(recentBuildOrders);
    decay(expensiveAnchorSearchCooldown);
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
    int amount = 0;
    if (player == nullptr)
        return amount;

    for (const auto* building : player->GetTrackedBuildingsWithComponent<StorageComponent>())
    {
        const auto* storage = building->GetComponent<StorageComponent>();
        if (storage == nullptr || building->owner != player)
            continue;

        auto it = storage->buffers.find(type);
        if (it != storage->buffers.end())
            amount += static_cast<int>(it->second.buffer.size());
    }
    return amount;
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

bool HasAdjacentRoad(GameWorld& world, const Building* building)
{
    if (building == nullptr)
        return false;

    for (int tileId : world.GetTileMap().GetAdjacentTileIds(building))
    {
        Building* neighbour = world.GetTileMap().GetBuilding(tileId);
        if (IsRoadLikeBuilding(neighbour))
            return true;
    }
    return false;
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
        return building->buildingType == BuildingType::Road ||
               building->IsStorageLike() ||
               building->buildingType == BuildingType::Headquarters;
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
            return !building->IsStorageLike() && building->CanAcceptResource(output.type);
        };

        Building* bestConsumer = pather->FindNearestBuilding(sourcePos, consumer_predicate, Domain::Global());
        if (bestConsumer != nullptr)
            return bestConsumer;
    }

    // Stage 4: Fallback - find any Road or Storage infrastructure
    auto fallback_predicate = [&](const Building* building) -> bool {
        if (building == nullptr || building == source || building->owner != player || building->IsUnderConstruction())
            return false;
        return building->buildingType == BuildingType::Road || building->IsStorageLike();
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
        if (building->IsStorageLike() || building->buildingType == BuildingType::Headquarters)
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
        TileType baseTerrain = definition.type == BuildingType::Woodcutter ? TileType::WOOD : TileType::GRASS;
        inspectProduction(definition.type, baseTerrain, definition.production);
        for (const auto& terrainProduction : definition.terrainProductions)
            inspectProduction(definition.type, terrainProduction.tileType, terrainProduction.production);
        for (const auto& recipe : definition.recipes)
            inspectProduction(definition.type, TileType::GRASS, recipe.production);
    }

    return options;
}

AIResourceDiagnosis DiagnoseResourceNeed(Player* player, ResourceType resource, int depth)
{
    AIResourceDiagnosis diagnosis;
    diagnosis.resource = resource;
    if (player == nullptr || resource == ResourceType::Null)
        return diagnosis;

    int produced = GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute, resource);
    int consumed = GetResourceRate(player->economyTelemetry.current.consumptionRatesPerMinute, resource);
    int stored = CountStoredResource(player, resource);
    bool hasProducerBuilding = false;
    bool stalledProducer = false;
    bool fullOutput = false;
    bool undermannedProducer = false;

    for (const auto* building : player->GetTrackedBuildingsWithComponent<ProductionComponent>())
    {
        const auto* production = building != nullptr ? building->GetComponent<ProductionComponent>() : nullptr;
        if (production == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;
        if (!production->products.contains(resource))
            continue;
        hasProducerBuilding = true;
        stalledProducer = stalledProducer || building->IsProductionStalled();
        const auto* workers = building->GetComponent<WorkerComponent>();
        undermannedProducer = undermannedProducer ||
            (workers != nullptr && workers->assigned < workers->GetModifiedCapacity(*building));
        auto bufferIt = production->outputBuffers.find(resource);
        if (bufferIt != production->outputBuffers.end() && bufferIt->second.buffer.size() >= bufferIt->second.bufferSize)
            fullOutput = true;
    }

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

    if (diagnosis.urgency <= 0.05)
        return diagnosis;

    for (const auto& option : FindProducerOptions(resource))
    {
        for (const auto& input : option.inputs)
        {
            int inputProduced = GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute, input.type);
            int inputConsumed = GetResourceRate(player->economyTelemetry.current.consumptionRatesPerMinute, input.type);
            int inputStored = CountStoredResource(player, input.type);
            if (inputStored < input.amount * 2 || inputProduced < inputConsumed)
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

Vec2i FindBuildAnchor(GameWorld& world, Player* player, BuildingType type,
                      TileType preferredTile, const Building* target, AIActionState& state)
{
    const auto& definition = GetBuildingDefinition(type);
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

    // Evaluates one tile; updates best score/anchor. Same filter + scoring as
    // the original whole-map scan.
    Vec2i bestAnchor{-1, -1};
    int bestScore = std::numeric_limits<int>::max();
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

        if (preferredTile == TileType::GRASS)
        {
            bool terrainMatches = true;
            for (int y = 0; y < definition.footprint.y && terrainMatches; y++)
                for (int x = 0; x < definition.footprint.x && terrainMatches; x++)
                    terrainMatches = map[{pos.x + x, pos.y + y}].tileType == TileType::GRASS;

            if (!terrainMatches)
                return;
        }

        int distanceToTarget = hasTarget ? std::abs(pos.x - targetPos.x) + std::abs(pos.y - targetPos.y) : 0;
        int distanceToHeadquarters = hasHeadquarters ? std::abs(pos.x - headquartersPos.x) + std::abs(pos.y - headquartersPos.y) : 0;
        int infrastructureDistance = DistanceToNearestInfrastructure(map, player, pos);
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
        int score = hasTarget
            ? distanceToTarget * 9 + distanceToHeadquarters * 2 + std::min(infrastructureDistance, 20) * 5 + borderPenalty * 3
            : distanceToHeadquarters * (terrainExtractor ? 5 : 9) + std::min(infrastructureDistance, 20) * 7 + borderPenalty * 4;
        if (score < bestScore)
        {
            bestScore = score;
            bestAnchor = pos;
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
    if (state.expensiveAnchorSearchCooldown.count(type) > 0)
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

        if (bestAnchor.x >= 0)
            return bestAnchor;
    }

    state.expensiveAnchorSearchCooldown[type] = 120.0;
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
    if (target->buildingType == BuildingType::Road)
        goalIds.push_back(target->positionId);

    auto canUseRoadPathTile = [&](int tileId)
    {
        if (tileId < 0 || tileId >= static_cast<int>(world.GetTileMap().tilemap.size()))
            return false;
        if (state.reservedRoadTiles.contains(tileId))
            return false;

        Tile& tile = world.GetTileMap()[tileId];
        Building* building = tile.GetBuilding();
        return building == nullptr || IsRoadLikeBuilding(building);
    };

    std::queue<int> frontier;
    std::map<int, int> parent;
    for (int startId : startIds)
    {
        if (!canUseRoadPathTile(startId) || parent.contains(startId))
            continue;

        parent[startId] = -1;
        frontier.push(startId);
    }

    int reachedGoal = -1;
    while (!frontier.empty())
    {
        int current = frontier.front();
        frontier.pop();

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
            if (parent.contains(nextId) || !canUseRoadPathTile(nextId))
                continue;

            parent[nextId] = current;
            frontier.push(nextId);
        }
    }

    if (reachedGoal < 0)
        return false;

    std::vector<int> path;
    for (int cursor = reachedGoal; cursor >= 0; cursor = parent[cursor])
        path.push_back(cursor);
    std::reverse(path.begin(), path.end());

    int newRoadTiles = 0;
    int existingRoadTiles = 0;
    bool needsBridge = false;
    for (int tileId : path)
    {
        Building* building = world.GetTileMap().GetBuilding(tileId);
        if (IsRoadLikeBuilding(building))
            existingRoadTiles++;
        else if (building == nullptr)
        {
            newRoadTiles++;
            if (world.GetTileMap()[tileId].isMilitaryRoad)
                needsBridge = true;
        }
    }
    if (newRoadTiles <= 0)
        return false;
    if (newRoadTiles > 8)
        return false;

    // Crossing the military track means Bridge tiles (PLANKS+STONE, see
    // buildings.rtsdata) — if a bridge isn't affordable right now, wait and
    // retry instead of spamming commands the simulation will reject. No
    // reservations either: the same crossing stays first choice once the
    // planks arrive.
    if (needsBridge && !player->CanBuildDefinition(GetBuildingDefinition(BuildingType::Bridge)))
        return false;

    // Pre-validate every new tile BEFORE submitting anything: on the track
    // only Bridge is legal (and e.g. two bridges may not sit orthogonally
    // adjacent — TileMap::CanBuildFootprint). An invalid tile poisons the
    // whole path; reserving it steers the next BFS (which skips reserved
    // tiles) toward a different crossing instead of retrying this one
    // forever. Playtest 2026-07-16: submitting plain Road onto track tiles
    // wedged the AI at the track edge indefinitely.
    for (int tileId : path)
    {
        Building* building = world.GetTileMap().GetBuilding(tileId);
        if (building != nullptr || state.reservedRoadTiles.contains(tileId))
            continue;
        Tile& tile = world.GetTileMap()[tileId];
        BuildingType type = tile.isMilitaryRoad ? BuildingType::Bridge : BuildingType::Road;
        Vec2i pos = world.GetTileMap().GetCoordsFromId(tileId);
        if (!world.GetTileMap().CanPlaceBuilding(type, pos, GetBuildingDefinition(type).footprint, player))
        {
            state.reservedRoadTiles[tileId] = 6.0;
            return false;
        }
    }

    bool submitted = false;
    int submittedCount = 0;
    constexpr int maxRoadCommandsPerTick = 8;
    for (int tileId : path)
    {
        Building* building = world.GetTileMap().GetBuilding(tileId);
        if (building != nullptr)
            continue;
        if (state.reservedRoadTiles.contains(tileId))
            continue;

        Tile& tile = world.GetTileMap()[tileId];
        BuildingType type = tile.isMilitaryRoad ? BuildingType::Bridge : BuildingType::Road;
        world.SubmitCommand(GameCommand::BuildBuilding(player->id, type, world.GetTileMap().GetCoordsFromId(tileId)));
        state.reservedRoadTiles[tileId] = 6.0;
        submitted = true;
        submittedCount++;
        if (submittedCount >= maxRoadCommandsPerTick)
            break;
    }

    return submitted;
}

bool TryBuildRoads(GameWorld& world, Player* player, AIActionState& state)
{
    if (player == nullptr)
        return false;

    int roads = CountOwnedBuildings(player, BuildingType::Road);
    int buildings = 0;
    for (const auto* building : player->GetTrackedBuildings())
        if (building != nullptr && building->owner == player && !building->IsUnderConstruction() && building->buildingType != BuildingType::Road)
            buildings++;
    if (roads > std::max(6, buildings * 3))
        return false;

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
        if (!building->IsStorageLike() && building->buildingType != BuildingType::Headquarters)
            continue;
        if (HasAdjacentRoad(world, building))
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
        if (building->buildingType == BuildingType::Road || building->IsStorageLike())
            continue;
        if (HasAdjacentRoad(world, building))
            continue;

        Building* targetRoad = FindNearestStorageConnectedRoad(world, player, building);
        if (targetRoad != nullptr && SubmitRoadPath(world, player, building, targetRoad, state))
            return true;
    }

    return false;
}

}
