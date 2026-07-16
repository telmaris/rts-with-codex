#include "ai/AIModel.h"
#include "core/GameWorld.h"

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
    s.towerCount = AIActions::CountOwnedBuildings(player, BuildingType::DefenseTower);
    s.barracksCount = AIActions::CountOwnedBuildings(player, BuildingType::Barracks);
    s.villageCount = AIActions::CountOwnedBuildings(player, BuildingType::Village);
    s.manpower = player->strategicResources.Get(StrategicResourceType::Manpower);

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
        case AINeed::RecruitDeploy:
            return 0.0;  // etap 3 — military layer lands next
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
        case AINeed::EconomySustain:
            return ExecuteEconomy(world, player, s);
        case AINeed::LogisticsRepair:
            return ExecuteLogistics(world, player, s);
        case AINeed::Defense:
        case AINeed::RecruitDeploy:
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
