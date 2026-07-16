#include "ai/AIModel.h"
#include "core/GameWorld.h"
#include "warfare/UnitDefinition.h"

#include <algorithm>
#include <array>

namespace
{
    // Sense cadence (seconds). Cheap aggregate reads.
    constexpr double SenseInterval = 1.0;
    // Decision cadence — one concrete action attempt per cycle.
    constexpr double DecisionInterval = 1.5;
    // Road-network maintenance cadence (AIActions::TryBuildRoads), kept on
    // its own timer like the old model so the network stays healthy
    // independently of what the decision core is busy with.
    constexpr double RoadMaintenanceInterval = 2.0;
    // Connectivity audit cadence — IsConnectedToRoadNetwork pays a road-BFS
    // per storage per building, so it runs slower than the rest of Sense.
    constexpr double ConnectivityInterval = 3.0;

    // Needs are tried in (score desc, enum asc) order — the enum order IS the
    // fixed priority tie-break: Defense > RecruitDeploy > EconomySustain >
    // LogisticsRepair > Research.
    constexpr double MinActionableScore = 0.05;

    // Deterministic opening build order that bootstraps the FOOD_PROVISIONS
    // (manpower) chain and a wood/ore base before telemetry has any
    // consumption signal to react to. Entries are (type, owned target);
    // completed-or-queued counts gate each step.
    struct OpeningStep
    {
        BuildingType type;
        int target;
    };
    constexpr std::array<OpeningStep, 12> OpeningPlan{{
        {BuildingType::Woodcutter, 1},
        {BuildingType::WheatFarm, 1},
        {BuildingType::Windmill, 1},
        {BuildingType::Bakery, 1},
        {BuildingType::Well, 1},
        {BuildingType::HuntersHut, 1},
        {BuildingType::Inn, 1},
        {BuildingType::LumberMill, 1},
        {BuildingType::Woodcutter, 2},
        {BuildingType::Mine, 1},
        {BuildingType::Village, 2},
        {BuildingType::StorageBuilding, 1},
    }};

    // Rough per-unit combat weight for track-pressure comparisons: damage
    // output plus a slice of staying power. Effective stats so tech buffs
    // count.
    double UnitStrength(const BattleUnit& unit, const Player& owner)
    {
        return unit.GetEffectiveRoadAttack(owner) + unit.currentHp * 0.1;
    }

    // Deploy the whole roster once it reaches this headcount (etap 4 may
    // scale it by difficulty).
    constexpr int WaveSize = 6;
    // Threat above this = the lane is being lost — deploy whatever exists.
    constexpr double EmergencyThreat = 1.0;

    // A unit is a siege specialist when breaching beats lane-fighting.
    bool IsSiegeUnit(const UnitDefinition& def)
    {
        return def.siegeAttack > def.roadAttack;
    }

    int TotalResourceCost(const UnitDefinition& def)
    {
        int total = 0;
        for (const auto& cost : def.cost)
            total += cost.amount;
        return total;
    }

    // Whether a unit's full resource cost already sits in the building's own
    // storage buffer (the only stock a roadless Barracks can ever consume).
    bool CostLocallyBuffered(const Building& barracks, const UnitDefinition& def)
    {
        const auto* storage = barracks.GetComponent<StorageComponent>();
        if (storage == nullptr)
            return false;
        for (const auto& cost : def.cost)
        {
            auto it = storage->buffers.find(cost.type);
            int have = it != storage->buffers.end() ? static_cast<int>(it->second.buffer.size()) : 0;
            if (have < cost.amount)
                return false;
        }
        return true;
    }

    // How many towers this AI wants standing right now. None until a minimal
    // economy exists; then a small garrison that grows under pressure.
    int DesiredTowerCount(const AISituation& s)
    {
        if (s.productionBuildingCount < 4)
            return 0;
        int desired = 2;
        if (s.Threat() > 0.5)
            desired++;
        if (s.hqHpRatio < 0.7)
            desired++;
        return std::min(desired, 4);
    }
}

UtilityAIModel::UtilityAIModel(int controlledPlayerId)
    : playerId(controlledPlayerId)
{
}

void UtilityAIModel::Update(GameWorld& world, Player* player, double dt)
{
    if (player == nullptr || player->defeated)
        return;

    senseTimer -= dt;
    decisionTimer -= dt;
    roadTimer -= dt;
    attackTargetCacheTimer -= dt;
    actions.Decay(dt);

    if (senseTimer <= 0.0)
    {
        senseTimer = SenseInterval;
        situation = Sense(world, player);
    }

    if (roadTimer <= 0.0)
    {
        roadTimer = RoadMaintenanceInterval;
        if (AIActions::TryBuildRoads(world, player, actions))
            return;
    }

    if (decisionTimer > 0.0)
        return;
    decisionTimer = DecisionInterval;

    // Score every need, then try them in (score desc, enum asc) order until
    // one produces a real command — mirrors the old unified pool's "best
    // executable candidate wins" without the scoring soup.
    std::array<int, static_cast<int>(AINeed::Count)> order{};
    std::array<double, static_cast<int>(AINeed::Count)> scores{};
    for (int i = 0; i < static_cast<int>(AINeed::Count); i++)
    {
        order[i] = i;
        scores[i] = ScoreNeed(static_cast<AINeed>(i), situation);
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return scores[a] > scores[b]; });

    for (int index : order)
    {
        if (scores[index] < MinActionableScore)
            break;
        if (ExecuteNeed(static_cast<AINeed>(index), world, player, situation))
            return;
    }
}

AISituation UtilityAIModel::Sense(GameWorld& world, Player* player)
{
    AISituation s;

    // Track state: deployed units, split mine vs. heading-at-me. std::map
    // iteration — deterministic.
    for (const auto& [instanceId, unit] : world.GetDeployedUnits())
    {
        if (unit.state == BattleUnitState::Dying)
            continue;
        auto ownerIt = world.GetPlayerHandler().players.find(unit.ownerPlayerId);
        if (ownerIt == world.GetPlayerHandler().players.end() || ownerIt->second == nullptr)
            continue;

        if (unit.ownerPlayerId == player->id)
        {
            s.myDeployedCount++;
            s.myDeployedStrength += UnitStrength(unit, *ownerIt->second);
        }
        else if (unit.routeToPlayerId == player->id)
        {
            s.enemyIncomingCount++;
            s.enemyIncomingStrength += UnitStrength(unit, *ownerIt->second);
        }
    }

    Building* hq = AIActions::FindOwnedHeadquarters(player);
    if (hq != nullptr)
    {
        const auto* hqComponent = hq->GetComponent<HqComponent>();
        if (hqComponent != nullptr)
        {
            double maxHp = hqComponent->GetModifiedMaxHp(*hq);
            s.hqHpRatio = maxHp > 0.0 ? std::clamp(hqComponent->currentHp / maxHp, 0.0, 1.0) : 1.0;
        }
    }

    s.rosterCount = static_cast<int>(player->roster.units.size());
    for (const auto& [instanceId, unit] : player->roster.units)  // std::map — deterministic
        s.rosterByDef[unit.unitDefId]++;
    s.towerCount = AIActions::CountOwnedBuildings(player, BuildingType::DefenseTower);
    s.barracksCount = AIActions::CountOwnedBuildings(player, BuildingType::Barracks);
    s.villageCount = AIActions::CountOwnedBuildings(player, BuildingType::Village);
    s.manpower = player->strategicResources.Get(StrategicResourceType::Manpower);

    // Static-defense strength + ammo state. Pure aggregation — order-safe.
    for (const auto* building : player->GetTrackedBuildingsWithComponent<TowerCombatComponent>())
    {
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;
        const auto* combat = building->GetComponent<TowerCombatComponent>();
        if (combat != nullptr)
            s.towerStrength += combat->GetModifiedDamage(*building) * combat->GetModifiedAttackSpeed(*building);
    }
    s.arrowsStored = AIActions::CountStoredResource(player, ResourceType::ARROWS);
    s.arrowsRate = AIActions::GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute,
                                              ResourceType::ARROWS);

    for (const auto* building : player->GetTrackedBuildingsWithComponent<ProductionComponent>())
        if (building != nullptr && building->owner == player && !building->IsUnderConstruction())
            s.productionBuildingCount++;  // pure count — order-independent

    s.foodProductionAlive =
        AIActions::GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute,
                                   ResourceType::FOOD_PROVISIONS) > 0;

    // Resource deficits. Candidate list is deterministic: the manpower
    // lifeline first, then every unit-cost resource (catalog is a std::map),
    // tower ammo when towers exist, then everything currently consumed.
    std::vector<ResourceType> candidates;
    auto pushCandidate = [&](ResourceType type)
    {
        if (type == ResourceType::Null)
            return;
        if (std::find(candidates.begin(), candidates.end(), type) == candidates.end())
            candidates.push_back(type);
    };
    pushCandidate(ResourceType::FOOD_PROVISIONS);
    for (const auto& [defId, def] : GetUnitCatalog())
        for (const auto& cost : def.cost)
            pushCandidate(cost.type);
    if (s.towerCount > 0)
        pushCandidate(ResourceType::ARROWS);
    for (const auto& [type, rate] : player->economyTelemetry.current.consumptionRatesPerMinute)
        if (rate > 0)
            pushCandidate(type);

    for (ResourceType type : candidates)
    {
        AIActions::AIResourceDiagnosis diagnosis = AIActions::DiagnoseResourceNeed(player, type);
        if (diagnosis.urgency > 0.2)
            s.deficits.push_back({type, diagnosis.urgency});
    }

    // Provision toward the composition's preferred pick before telemetry has
    // any consumption signal for it: a cost resource with (almost) no stock
    // and no production gets a fixed mid-urgency deficit, driving
    // EconomySustain to raise its chain (e.g. Smith for swords) while the
    // recruiter falls back to whatever IS affordable in the meantime.
    std::vector<const UnitDefinition*> ranked = RankUnitChoices(s);
    if (!ranked.empty() && ranked.front() != nullptr)
    {
        for (const auto& cost : ranked.front()->cost)
        {
            int stored = AIActions::CountStoredResource(player, cost.type);
            int rate = AIActions::GetResourceRate(
                player->economyTelemetry.current.productionRatesPerMinute, cost.type);
            bool alreadyListed = std::any_of(s.deficits.begin(), s.deficits.end(),
                [&](const AISituation::Deficit& d) { return d.resource == cost.type; });
            if (!alreadyListed && stored < cost.amount * 2 && rate == 0)
                s.deficits.push_back({cost.type, 0.5});
        }
    }

    std::stable_sort(s.deficits.begin(), s.deficits.end(),
                     [](const AISituation::Deficit& a, const AISituation::Deficit& b)
                     {
                         if (a.urgency != b.urgency)
                             return a.urgency > b.urgency;
                         return static_cast<int>(a.resource) < static_cast<int>(b.resource);
                     });
    if (s.deficits.size() > 4)
        s.deficits.resize(4);

    // Connectivity audit on its own (slower) cadence — carry the previous
    // answer between audits.
    connectivityTimer -= SenseInterval;
    if (connectivityTimer <= 0.0)
    {
        connectivityTimer = ConnectivityInterval;
        // Sorted by building id — the repair action serves the FIRST entry,
        // so order is simulation-visible (see docs/tech_debt.md).
        std::vector<Building*> candidates2(player->GetTrackedBuildings().begin(),
                                           player->GetTrackedBuildings().end());
        std::sort(candidates2.begin(), candidates2.end(),
                  [](Building* a, Building* b) { return a->id < b->id; });
        lastUnconnectedPositionIds.clear();
        for (Building* building : candidates2)
        {
            if (building == nullptr || building->owner != player)
                continue;
            if (building->buildingType == BuildingType::Road ||
                building->buildingType == BuildingType::Headquarters ||
                building->IsStorageLike())
                continue;
            const auto* logistics = building->GetComponent<LogisticsComponent>();
            if (logistics == nullptr)
                continue;
            if (!logistics->IsConnectedToRoadNetwork(*building))
                lastUnconnectedPositionIds.push_back(building->positionId);
        }
    }
    s.unconnectedPositionIds = lastUnconnectedPositionIds;

    return s;
}

double UtilityAIModel::ScoreNeed(AINeed need, const AISituation& s) const
{
    switch (need)
    {
        case AINeed::Defense:
        {
            // Standing garrison gap + the live-pressure formula from the
            // plan: half lane pressure, half HQ damage taken.
            double score = s.towerCount < DesiredTowerCount(s) ? 0.3 : 0.0;
            if (s.towerCount > 0 && s.arrowsStored == 0 && s.arrowsRate == 0)
                score = std::max(score, 0.45);  // towers without ammo are decoration
            double pressure = std::clamp(0.5 * s.Threat() + 0.5 * (1.0 - s.hqHpRatio), 0.0, 1.0);
            return std::max(score, pressure);
        }
        case AINeed::RecruitDeploy:
        {
            // The prime TD objective — permanently high, higher still when
            // the lane is quiet (push!), and dominant when an emergency
            // deploy is possible.
            double score = 0.55 + 0.25 * (1.0 - std::clamp(s.Threat(), 0.0, 1.0));
            if (s.Threat() > EmergencyThreat && s.rosterCount > 0)
                score = 0.95;
            return score;
        }
        case AINeed::EconomySustain:
        {
            double score = 0.0;
            if (s.productionBuildingCount < static_cast<int>(OpeningPlan.size()))
                score = 0.6;  // opening: build out the base before anything subtler
            if (!s.foodProductionAlive)
                score = std::max(score, 0.75);  // manpower lifeline is dead — critical
            if (!s.deficits.empty())
                score = std::max(score, std::clamp(s.deficits.front().urgency, 0.0, 1.0));
            return score;
        }
        case AINeed::LogisticsRepair:
            return std::min(1.0, 0.4 * static_cast<double>(s.unconnectedPositionIds.size()));
        case AINeed::Research:
            return 0.0;  // etap 5
        case AINeed::Count:
            break;
    }
    return 0.0;
}

bool UtilityAIModel::ExecuteNeed(AINeed need, GameWorld& world, Player* player, const AISituation& s)
{
    switch (need)
    {
        case AINeed::Defense:
            return ExecuteDefense(world, player, s);
        case AINeed::RecruitDeploy:
            return ExecuteRecruitDeploy(world, player, s);
        case AINeed::EconomySustain:
            return ExecuteEconomy(world, player, s);
        case AINeed::LogisticsRepair:
            return ExecuteLogistics(world, player, s);
        case AINeed::Research:
        case AINeed::Count:
            break;
    }
    return false;
}

bool UtilityAIModel::ExecuteEconomy(GameWorld& world, Player* player, const AISituation& s)
{
    // Manpower starvation with a live food chain → another Village converts
    // that food into manpower.
    if (s.manpower < 5.0 && s.foodProductionAlive &&
        player->CanBuildDefinition(GetBuildingDefinition(BuildingType::Village)))
    {
        Vec2i anchor = AIActions::FindBuildAnchor(world, player, BuildingType::Village,
                                                  TileType::GRASS, nullptr, actions);
        if (anchor.x >= 0 && AIActions::TrySubmitBuild(world, player, BuildingType::Village, anchor, actions))
            return true;
    }

    for (const auto& deficit : s.deficits)
        if (TryBuildProducerFor(world, player, deficit.resource))
            return true;

    return TryOpeningPlan(world, player);
}

bool UtilityAIModel::TryBuildProducerFor(GameWorld& world, Player* player, ResourceType resource)
{
    // Walk down the input chain: if the producer of `resource` is starved of
    // an input, build toward that input instead (bounded depth). missingInputs
    // order comes from the static building catalog — deterministic.
    ResourceType target = resource;
    for (int depth = 0; depth < 3; depth++)
    {
        AIActions::AIResourceDiagnosis diagnosis = AIActions::DiagnoseResourceNeed(player, target, depth);
        if (diagnosis.missingInputs.empty())
            break;
        target = diagnosis.missingInputs.front();
    }

    std::vector<AIActions::AIProducerOption> options = AIActions::FindProducerOptions(target);
    std::stable_sort(options.begin(), options.end(),
                     [&](const AIActions::AIProducerOption& a, const AIActions::AIProducerOption& b)
                     {
                         int ownedA = AIActions::CountOwnedBuildings(player, a.buildingType);
                         int ownedB = AIActions::CountOwnedBuildings(player, b.buildingType);
                         if (ownedA != ownedB)
                             return ownedA < ownedB;  // diversify before duplicating
                         return static_cast<int>(a.buildingType) < static_cast<int>(b.buildingType);
                     });

    for (const auto& option : options)
    {
        if (!player->CanBuildDefinition(GetBuildingDefinition(option.buildingType)))
            continue;
        Vec2i anchor = AIActions::FindBuildAnchor(world, player, option.buildingType,
                                                  option.terrain, nullptr, actions);
        if (anchor.x < 0)
            continue;
        if (AIActions::TrySubmitBuild(world, player, option.buildingType, anchor, actions))
            return true;
    }
    return false;
}

bool UtilityAIModel::TryOpeningPlan(GameWorld& world, Player* player)
{
    for (const OpeningStep& step : OpeningPlan)
    {
        if (AIActions::CountCompletedOrQueuedBuildings(world, player, step.type) >= step.target)
            continue;

        // The plan is strictly ordered: an unaffordable step means "save up",
        // not "skip ahead and spend on something later" — deterministic
        // patience beats churning the buffer on cheap filler.
        if (!player->CanBuildDefinition(GetBuildingDefinition(step.type)))
            return false;

        TileType preferredTile = step.type == BuildingType::Woodcutter ? TileType::WOOD
                               : step.type == BuildingType::Mine       ? TileType::COAL
                                                                       : TileType::GRASS;
        Vec2i anchor = AIActions::FindBuildAnchor(world, player, step.type, preferredTile, nullptr, actions);
        if (anchor.x < 0)
            continue;  // nowhere to put THIS one (terrain gone / search backoff) — don't block the rest
        if (AIActions::TrySubmitBuild(world, player, step.type, anchor, actions))
            return true;
        // Cooldown after a just-submitted order of this type — move on.
    }
    return false;
}

std::vector<const UnitDefinition*> UtilityAIModel::RankUnitChoices(const AISituation& s)
{
    // Offensive mix bookkeeping: how much siege the roster already holds.
    int siegeCount = 0;
    for (const auto& [defId, count] : s.rosterByDef)
    {
        const UnitDefinition* def = FindUnitDefinition(defId);
        if (def != nullptr && IsSiegeUnit(*def))
            siegeCount += count;
    }
    // Keep roughly one siege unit per two lane-fighters. Strict `<` against
    // rosterCount alone makes an EMPTY roster start with fighters — siege
    // units are escort-dependent (see units.rtsdata's ram note) and must
    // never be the first recruit.
    bool wantSiege = siegeCount * 3 < s.rosterCount;
    bool defensive = s.UnderAttack();

    std::vector<std::pair<const UnitDefinition*, double>> scored;
    for (const auto& [defId, def] : GetUnitCatalog())  // std::map — deterministic id order
    {
        double score = 0.0;
        if (defensive)
        {
            // Hold the lane: staying power + lane damage per cost.
            score = (def.roadAttack + def.armor + 0.5 * def.maxHp) /
                    std::max(1.0, def.manpowerCost + TotalResourceCost(def));
        }
        else
        {
            // Push: fill whichever class the 2:1 mix is short on; rank
            // within the class by what that class is for.
            bool preferredClass = IsSiegeUnit(def) == wantSiege;
            double classValue = IsSiegeUnit(def) ? def.siegeAttack : def.moveSpeed * def.roadAttack;
            score = (preferredClass ? 1000.0 : 0.0) + classValue;
        }
        scored.emplace_back(&def, score);
    }

    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<const UnitDefinition*> ranked;
    ranked.reserve(scored.size());
    for (const auto& [def, score] : scored)
        ranked.push_back(def);
    return ranked;
}

bool UtilityAIModel::ExecuteDefense(GameWorld& world, Player* player, const AISituation& s)
{
    // 1. Missing towers — anchor near the HQ (the lane's endpoint).
    if (s.towerCount < DesiredTowerCount(s) &&
        player->CanBuildDefinition(GetBuildingDefinition(BuildingType::DefenseTower)))
    {
        Building* hq = AIActions::FindOwnedHeadquarters(player);
        Vec2i anchor = AIActions::FindBuildAnchor(world, player, BuildingType::DefenseTower,
                                                  TileType::GRASS, hq, actions);
        if (anchor.x >= 0 && AIActions::TrySubmitBuild(world, player, BuildingType::DefenseTower, anchor, actions))
            return true;
    }

    // 2. Towers standing but nothing feeds them — build toward the ARROWS
    //    chain (Smith + inputs).
    if (s.towerCount > 0 && s.arrowsStored == 0 && s.arrowsRate == 0)
        return TryBuildProducerFor(world, player, ResourceType::ARROWS);

    return false;
}

bool UtilityAIModel::ExecuteRecruitDeploy(GameWorld& world, Player* player, const AISituation& s)
{
    // No recruiting without a Barracks — that IS the recruit-deploy action
    // until one stands.
    if (s.barracksCount == 0)
    {
        if (!player->CanBuildDefinition(GetBuildingDefinition(BuildingType::Barracks)))
            return false;
        Vec2i anchor = AIActions::FindBuildAnchor(world, player, BuildingType::Barracks,
                                                  TileType::GRASS, nullptr, actions);
        return anchor.x >= 0 &&
               AIActions::TrySubmitBuild(world, player, BuildingType::Barracks, anchor, actions);
    }

    // Wave ready (or the lane is being lost and anything helps) — deploy the
    // whole roster at the reachable enemy. Ids in instanceId order
    // (std::map) — deterministic.
    bool emergency = s.Threat() > EmergencyThreat && s.rosterCount > 0;
    if (s.rosterCount >= WaveSize || emergency)
    {
        int target = GetCachedAttackTargetPlayer(world, player);
        if (target >= 0)
        {
            std::vector<int> orderedIds;
            orderedIds.reserve(player->roster.units.size());
            for (const auto& [instanceId, unit] : player->roster.units)
                orderedIds.push_back(instanceId);
            if (!orderedIds.empty())
            {
                world.SubmitCommand(GameCommand::DeployUnits(player->id, target, std::move(orderedIds)));
                return true;
            }
        }
        if (!emergency)
            return false;  // full wave but no reachable enemy — don't recruit past the cap
    }

    // Otherwise recruit toward the wave. First completed Barracks by id —
    // which one trains is simulation-visible state, so the pick must not
    // depend on heap order (see docs/tech_debt.md).
    const auto& recruitCapable = player->GetTrackedBuildingsWithComponent<RecruitmentComponent>();
    std::vector<Building*> sortedBarracks(recruitCapable.begin(), recruitCapable.end());
    std::sort(sortedBarracks.begin(), sortedBarracks.end(),
              [](Building* a, Building* b) { return a->id < b->id; });
    Building* barracks = nullptr;
    for (Building* building : sortedBarracks)
    {
        if (building != nullptr && building->owner == player && !building->IsUnderConstruction())
        {
            barracks = building;
            break;
        }
    }
    if (barracks == nullptr)
        return false;
    auto* recruitment = barracks->GetComponent<RecruitmentComponent>();
    if (recruitment == nullptr)
        return false;
    // Progress gates: never stack orders behind one that is still waiting on
    // deliveries (strict-FIFO queue — everything behind it waits too), and
    // keep the queue short so orders track the situation, not a backlog.
    if (!recruitment->queue.empty() && !recruitment->queue.front().resourcesReady)
        return false;
    if (recruitment->queue.size() >= 2)
        return false;

    // DiagnoseRecruitmentBlock is a GLOBAL stock scan — resources counted
    // there still have to physically reach this Barracks over roads. Without
    // a road connection only locally-buffered costs can ever be consumed, so
    // gate on that (learned the hard way: a globally-affordable siege unit
    // queued into a roadless Barracks starves the whole queue forever).
    bool connected = std::find(s.unconnectedPositionIds.begin(), s.unconnectedPositionIds.end(),
                               barracks->positionId) == s.unconnectedPositionIds.end();

    for (const UnitDefinition* def : RankUnitChoices(s))
    {
        if (def == nullptr)
            continue;
        if (!connected && !CostLocallyBuffered(*barracks, *def))
            continue;
        if (!recruitment->DiagnoseRecruitmentBlock(*barracks, def->id).empty())
            continue;  // not affordable/manpowered right now — next-best pick
        world.SubmitCommand(GameCommand::RecruitUnit(player->id, barracks->positionId, def->id));
        return true;
    }
    return false;
}

int UtilityAIModel::GetCachedAttackTargetPlayer(GameWorld& world, Player* player)
{
    if (attackTargetCacheTimer <= 0.0)
    {
        cachedAttackTargetPlayer = AIActions::FindAttackTargetPlayer(world, player);
        attackTargetCacheTimer = 3.0;
    }
    return cachedAttackTargetPlayer;
}

bool UtilityAIModel::ExecuteLogistics(GameWorld& world, Player* player, const AISituation& s)
{
    // General maintenance first (storage/HQ road stubs, first roadless
    // producer) — it already submits at most one order.
    if (AIActions::TryBuildRoads(world, player, actions))
        return true;

    // Then path-level repairs TryBuildRoads can't see: a building that HAS an
    // adjacent road but no route to any storage (orphaned stub).
    for (int positionId : s.unconnectedPositionIds)
    {
        Building* building = world.GetTileMap().GetBuilding(positionId);
        if (building == nullptr || building->owner != player)
            continue;  // destroyed/replaced since the audit

        Building* targetRoad = AIActions::FindNearestStorageConnectedRoad(world, player, building);
        if (targetRoad != nullptr && AIActions::SubmitRoadPath(world, player, building, targetRoad, actions))
            return true;

        Building* storage = world.GetTileMap().FindNearestStorage(building, player);
        if (storage != nullptr && AIActions::SubmitRoadPath(world, player, building, storage, actions))
            return true;
    }
    return false;
}
