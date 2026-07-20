#include "ai/AIModel.h"
#include "ai/AIEconomyBias.h"
#include "core/GameWorld.h"
#include "warfare/UnitDefinition.h"

#include <algorithm>
#include <array>

namespace
{
    // Sense cadence (seconds). Cheap aggregate reads.
    constexpr double SenseInterval = 1.0;
    // Decision cadence (one concrete action attempt per cycle) comes from
    // ai.rtsdata's `decision_interval` (AIEconomyBias) — the pace lever the
    // user tunes; 1.5 s was the old hardcoded value and remains the default
    // when the line is absent.
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
    // (manpower) chain, a wood base, AND the iron chain before telemetry has
    // any consumption signal to react to. Each step carries the terrain its
    // extractor needs (GRASS for plain buildings) and, for the terrain-specific
    // mines, the RESOURCE that gates it — because a Mine's type alone can't
    // tell a coal mine from an iron-ore or stone one (2026-07-20 fix: the old
    // type-count gate let the deficit ladder's STONE mine satisfy the COAL
    // step, so the iron chain never got its ore).
    struct OpeningStep
    {
        BuildingType type;
        int target;              // building-type count target (ignored when gateResource is set)
        TileType preferredTile;  // terrain the extractor needs
        ResourceType gateResource;  // Null => gate by building-type count; else by a producer of this resource
    };
    constexpr std::array<OpeningStep, 15> OpeningPlan{{
        // Basic materials, then the IRON CHAIN, then the (expensive) food chain.
        // Rationale:
        //  - BOTH woodcutters before the LumberMill: one Woodcutter (30 WOOD/min)
        //    exactly feeds one LumberMill (30 WOOD/min) with ZERO surplus for
        //    construction, so a single one starves the rest for WOOD and the plan
        //    wedges (2026-07-20). Two give a real surplus.
        //  - Iron chain (coal + iron-ore mines + Foundry) BEFORE the food chain:
        //    it's cheap (mines cost no iron; a first Foundry's 10 IRON is covered
        //    by the HQ's starting 30) and it's the user's actual goal ("AI buduje
        //    produkcję żelaza/węgla"), so stand it up FAST off the opening stock
        //    rather than after grinding out the whole PLANKS/STONE-heavy food
        //    chain (WheatFarm/Windmill/Bakery/Inn ~ 150 PLANKS), which is a
        //    slower, manpower-oriented investment.
        {BuildingType::Woodcutter,      1, TileType::WOOD,     ResourceType::Null},
        {BuildingType::Woodcutter,      2, TileType::WOOD,     ResourceType::Null},
        {BuildingType::LumberMill,      1, TileType::GRASS,    ResourceType::Null},
        {BuildingType::Mine,            1, TileType::STONE,    ResourceType::STONE},
        {BuildingType::Mine,            1, TileType::COAL,     ResourceType::COAL},
        {BuildingType::Mine,            1, TileType::IRON_ORE, ResourceType::IRON_ORE},
        {BuildingType::Foundry,         1, TileType::GRASS,    ResourceType::Null},
        {BuildingType::WheatFarm,       1, TileType::GRASS,    ResourceType::Null},
        {BuildingType::Windmill,        1, TileType::GRASS,    ResourceType::Null},
        {BuildingType::Bakery,          1, TileType::GRASS,    ResourceType::Null},
        {BuildingType::Well,            1, TileType::GRASS,    ResourceType::Null},
        // HuntersHut makes MEAT on WOOD terrain — it was gated GRASS before and
        // so could NEVER be placed (silently breaking the MEAT->Inn food link).
        {BuildingType::HuntersHut,      1, TileType::WOOD,     ResourceType::Null},
        {BuildingType::Inn,             1, TileType::GRASS,    ResourceType::Null},
        {BuildingType::Village,         2, TileType::GRASS,    ResourceType::Null},
        {BuildingType::StorageBuilding, 1, TileType::GRASS,    ResourceType::Null},
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
    // How often RecruitDeploy may spend a decision cycle building toward the
    // top pick's missing cost instead of recruiting (2026-07-20) — bounds a
    // deep, currently-unaffordable chain (steel sword -> iron -> ore) to an
    // occasional nudge rather than a permanent recruit-fallback lockout.
    constexpr double RecruitEconomyBuildInterval = 8.0;

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
    // economy exists; then a small garrison that grows under pressure. The
    // readiness threshold is a build-order lever like `priority`, but towers
    // aren't a resource — it's ai.rtsdata's `tower_readiness_buildings`
    // instead (user design 2026-07-19: defense should start "in the
    // meantime" once the economy has SOME footing, tunable from the same file).
    int DesiredTowerCount(const AISituation& s)
    {
        if (s.productionBuildingCount < GetAIEconomyBias().towerReadinessBuildings)
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

    // Difficulty comes straight from map params (UI -> MapParameters ->
    // save); noise is seeded once from (map seed, player id) — identical
    // across same-seed worlds, so lockstep holds with noise ACTIVE.
    difficulty = std::clamp(world.GetTileMap().params.aiDifficulty, 0, 3);
    if (!noiseSeeded)
    {
        noiseRng.seed(static_cast<unsigned int>(world.GetTileMap().params.seed) ^
                      (0x9E3779B9u * static_cast<unsigned int>(playerId + 1)));
        // The amortized-cost bias is static config scaled by difficulty —
        // computed once alongside the seed (difficulty never changes
        // mid-game; it comes from map params).
        consumptionBias = GetAIEconomyBias().ScaledMap(difficulty);
        // Build-order priority — not difficulty-scaled (see AIModel.h).
        priorityWeights = GetAIEconomyBias().NormalizedPriorityMap();

        // Personality bias (user design 2026-07-20): drawn HERE, in a fixed
        // number of calls right after the seed, so it's identical between two
        // same-seed worlds regardless of anything that happens later (map
        // events, other players' actions) — same reasoning as the rest of
        // this block. Active at every difficulty, including Hard, unlike the
        // NoiseAmplitude/SkipChance noise below.
        //
        // Amplitude bounded by the TIGHTEST hard-won score gap in ScoreNeed,
        // not chosen freely: EconomySustain's food-dead critical (0.75) must
        // stay below RecruitDeploy's quiet-lane value (0.8) — an
        // independent +x/-x swing on both must not invert that, i.e.
        // (1+x)*0.75 < (1-x)*0.8 => x < 0.0323. 0.025 leaves real margin
        // (worst case 0.76875 vs 0.78) while still being a visible, permanent
        // per-AI skew — do not widen this without re-checking every floor
        // pairing documented in ScoreNeed (EconomySustain's routine cap 0.6 vs
        // LogisticsRepair's 0.65 floor is the other tight one, gap 0.65/0.6).
        std::uniform_real_distribution<double> needSkew(-0.025, 0.025);
        for (double& bias : personalityNeedBias)
            bias = needSkew(noiseRng);
        std::uniform_int_distribution<int> waveSkew(-1, 2);
        personalityWaveBias = waveSkew(noiseRng);

        noiseSeeded = true;
    }

    senseTimer -= dt;
    decisionTimer -= dt;
    roadTimer -= dt;
    recruitEconomyBuildTimer -= dt;
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
    decisionTimer = GetAIEconomyBias().decisionIntervalSeconds;

    // Difficulty noise (etap 4): a worse player sometimes just doesn't act
    // this cycle, and mis-weighs what matters. Hard plays the pure model.
    static constexpr std::array<double, 4> NoiseAmplitude{0.30, 0.20, 0.10, 0.0};
    static constexpr std::array<double, 4> SkipChance{0.15, 0.10, 0.05, 0.0};
    if (SkipChance[difficulty] > 0.0)
    {
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        if (unit(noiseRng) < SkipChance[difficulty])
            return;
    }

    // Score every need, then try them in (score desc, enum asc) order until
    // one produces a real command — mirrors the old unified pool's "best
    // executable candidate wins" without the scoring soup.
    std::array<int, static_cast<int>(AINeed::Count)> order{};
    std::array<double, static_cast<int>(AINeed::Count)> scores{};
    for (int i = 0; i < static_cast<int>(AINeed::Count); i++)
    {
        order[i] = i;
        scores[i] = ScoreNeed(static_cast<AINeed>(i), situation) * (1.0 + personalityNeedBias[i]);
    }
    if (NoiseAmplitude[difficulty] > 0.0)
    {
        std::uniform_real_distribution<double> swing(-1.0, 1.0);
        for (double& score : scores)
            score *= 1.0 + NoiseAmplitude[difficulty] * swing(noiseRng);
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

    // Rate alone false-positives "dead" once storage tops up: a healthy Inn
    // with a full output buffer legitimately reads 0/min production (nothing
    // to push out) even though the chain works fine (playtest 2026-07-19: AI
    // stopped developing — no more Mines, no University — after its early
    // buildout filled every buffer, since Research's University trigger also
    // gates on foodProductionAlive). A raw "stored > 0" check was tried and
    // reverted (harness catch): the Hard-difficulty starting grant seeds 200
    // FOOD_PROVISIONS at HQ, so that reads "alive" for a very long time
    // regardless of whether the chain works at all, which changed Economy's
    // behavior enough to spam rejected commands on the stress-test seed.
    // Checking producer HEALTH instead of a stock snapshot sidesteps that
    // confound entirely: a completed, connected, manned Inn is alive even
    // mid-topped-up-buffer; one with any real problem flag isn't, regardless
    // of leftover starting stock.
    AIActions::AIResourceDiagnosis foodDiagnosis = AIActions::DiagnoseResourceNeed(
        player, ResourceType::FOOD_PROVISIONS, 0, &consumptionBias, &priorityWeights);
    s.foodProductionAlive =
        AIActions::GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute,
                                   ResourceType::FOOD_PROVISIONS) > 0 ||
        (foodDiagnosis.hasProducerBuilding && !foodDiagnosis.logisticsProblem &&
         !foodDiagnosis.storageProblem && !foodDiagnosis.manpowerProblem);
    s.populationCap = player->GetPopulationCap();
    s.totalPopulation = player->GetTotalPopulation();

    s.universityCount = AIActions::CountOwnedBuildings(player, BuildingType::University);
    s.hasIdleUniversity = AIActions::FindUniversity(player) != nullptr;
    s.focusActive = !player->focuses.GetActiveFocusId().empty();

    // Tier-2 priority handoff (2026-07-20): ai.rtsdata's `priority` discount
    // exists to win the OPENING build order (tier-1 wood/stone/food ties
    // against tier-2 iron/tools/swords), not to suppress tier-2 forever. But
    // the tier-1 amortized consumption bias is permanent — construction keeps
    // draining stock below the "low reserve" threshold indefinitely — so
    // without an explicit handoff tier-2 never naturally got a turn.
    //
    // Threshold is `tower_readiness_buildings` (default 4, ai.rtsdata) — the
    // SAME "economy has some footing" gate Defense already uses — not the
    // full 12-step OpeningPlan. Harness catch (2026-07-20): requiring the
    // entire opening plan complete meant this handoff (and the RecruitDeploy
    // cost-chain builder in ExecuteRecruitDeploy, gated on the same flag)
    // never engaged within a realistic playtest/test window at all — Foundry
    // never got built despite everything else in Tasks 1-3 being correct,
    // simply because the gate never opened. A handful of standing producers
    // is enough evidence the opening bootstrap is past its most fragile
    // stretch (see ExecuteRecruitDeploy's comment for what that fragility was).
    s.economyEstablished = s.foodProductionAlive &&
        s.productionBuildingCount >= GetAIEconomyBias().towerReadinessBuildings;
    const std::map<ResourceType, double>* effectiveWeights = s.economyEstablished ? nullptr : &priorityWeights;

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
    // Every amortized-cost resource is always a candidate — the bias exists
    // precisely so these are provisioned even with zero real consumption yet.
    for (const auto& [type, amount] : consumptionBias)
        pushCandidate(type);
    for (const auto& [type, rate] : player->economyTelemetry.current.consumptionRatesPerMinute)
        if (rate > 0)
            pushCandidate(type);

    for (ResourceType type : candidates)
    {
        AIActions::AIResourceDiagnosis diagnosis =
            AIActions::DiagnoseResourceNeed(player, type, 0, &consumptionBias, effectiveWeights);
        if (diagnosis.urgency > 0.2)
            s.deficits.push_back({type, diagnosis.urgency});
    }

    // Provision toward the composition's preferred pick before telemetry has
    // any consumption signal for it: a cost resource with (almost) no stock
    // and no production gets a fixed mid-urgency deficit, driving
    // EconomySustain to raise its chain (e.g. Smith for swords) while the
    // recruiter falls back to whatever IS affordable in the meantime. Also
    // priority-weighted (2026-07-19) so a low-priority preferred cost (a
    // sword tier, weight ~0.4) doesn't jump the queue ahead of the actual
    // tier-1 economy just because it's the composition's top unit pick —
    // unless the economy is already established, in which case the full-weight
    // handoff above applies here too.
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
            {
                double weight = 1.0;
                if (effectiveWeights != nullptr)
                {
                    auto it = effectiveWeights->find(cost.type);
                    weight = it != effectiveWeights->end() ? it->second : 1.0;
                }
                s.deficits.push_back({cost.type, std::clamp(0.5 * weight, 0.0, 1.0)});
            }
        }
    }

    std::stable_sort(s.deficits.begin(), s.deficits.end(),
                     [](const AISituation::Deficit& a, const AISituation::Deficit& b)
                     {
                         if (a.urgency != b.urgency)
                             return a.urgency > b.urgency;
                         return static_cast<int>(a.resource) < static_cast<int>(b.resource);
                     });
    // Widened 4 -> 6 (2026-07-20): with tier-1's bias permanently live, tier-2
    // entries used to fall off the end of the list even in cycles where
    // tier-1 only transiently spiked, never getting a turn against the
    // deficit ladder at all.
    if (s.deficits.size() > 6)
        s.deficits.resize(6);

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
            // Manpower-reserve emergency (user design 2026-07-19) deliberately
            // bypasses the 0.75 hard cap below: recruitment is already blocked
            // without manpower (DiagnoseRecruitmentBlock refuses it), so
            // pushing this above RecruitDeploy's floor doesn't starve
            // militaries — it unblocks their one missing resource.
            if (ManpowerEmergency(s, GetAIEconomyBias().manpowerReserve))
                return 0.9;

            // Critical: the manpower lifeline is dead. Deliberately the only
            // path allowed to score above LogisticsRepair's floor (0.65) —
            // see the routine cap below for why.
            double criticalScore = 0.0;
            if (!s.foodProductionAlive)
                criticalScore = 0.75;

            double routineScore = 0.0;
            if (s.productionBuildingCount < static_cast<int>(OpeningPlan.size()))
                routineScore = 0.6;  // opening: build out the base before anything subtler
            if (!s.deficits.empty())
                routineScore = std::max(routineScore, std::clamp(s.deficits.front().urgency, 0.0, 1.0));
            // Routine score capped BELOW LogisticsRepair's floor (0.65), not
            // just RecruitDeploy's (0.8). Harness catch 2026-07-19: chasing a
            // brand-new resource chain (e.g. IRON, now reachable via the
            // COAL/IRON_ORE starting patches) can drive deficit urgency up to
            // the old 0.75 cap, exactly tying a 2-building LogisticsRepair
            // score — and the stable-sort tie-break (enum order) always
            // favored Economy, so it kept greenlighting the new chain forever
            // and Logistics never got a turn: a permanent bridge-
            // affordability deadlock (starting stock burned on the new
            // producer instead of the one connection still unfinished). Only
            // the true food-dead emergency above is allowed to preempt
            // logistics now.
            routineScore = std::min(routineScore, 0.6);

            return std::max(criticalScore, routineScore);
        }
        case AINeed::LogisticsRepair:
        {
            // Deadlock fix (playtest 2026-07-19, user report: "AI can't build
            // bridges when a road cuts buildings off"): a lone disconnected
            // building used to score only 0.4 — BELOW EconomySustain's
            // routine "opening: build out the base" floor (0.6) — so the AI
            // kept greenlighting new producers (each with a real, larger
            // build cost) before ever finishing the connection on the one it
            // just placed. On a map where that connection needs a Bridge
            // (PLANKS+STONE, cheap per tile but still real), the starting
            // stock got fully spent on 5-6 new buildings within 15 seconds,
            // and once WOOD/PLANKS production itself depends on the very
            // building stuck across the track, the deadlock is permanent: no
            // stock -> can't afford the bridge -> can't connect -> that
            // building's own output stays stranded -> still no stock.
            // Any disconnected building now outranks Economy's routine
            // opening score, so the AI finishes what it built before piling
            // on more — Economy's CRITICAL escalations (dead food chain,
            // high real deficit, both capped at 0.75) still win when they
            // should.
            if (s.unconnectedPositionIds.empty())
                return 0.0;
            return std::min(1.0, 0.65 + 0.1 * static_cast<double>(s.unconnectedPositionIds.size() - 1));
        }
        case AINeed::Research:
        {
            if (s.Threat() > 0.5)
                return 0.0;  // guns before books
            double score = 0.0;
            if (s.hasIdleUniversity)
                score = 0.35;  // a standing idle University is sunk cost — use it
            if (!s.focusActive)
                score = std::max(score, 0.25);  // focuses cost nothing to start
            // Building a University is an investment — only once the economy
            // demonstrably carries itself.
            if (s.universityCount == 0 && s.foodProductionAlive && s.productionBuildingCount >= 6)
                score = std::max(score, 0.3);
            return score;
        }
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
            return ExecuteResearch(world, player, s);
        case AINeed::Count:
            break;
    }
    return false;
}

bool UtilityAIModel::ExecuteEconomy(GameWorld& world, Player* player, const AISituation& s)
{
    // Manpower-reserve emergency (user design 2026-07-19): villages already
    // near capacity won't grow manpower further on their own, so another
    // Village — converting food into manpower — is the immediate fix. Same
    // condition ScoreNeed uses to escalate above the 0.75 cap, so the two
    // can never disagree about whether this is live.
    if (ManpowerEmergency(s, GetAIEconomyBias().manpowerReserve) &&
        player->CanBuildDefinition(GetBuildingDefinition(BuildingType::Village)))
    {
        Vec2i anchor = AIActions::FindBuildAnchor(world, player, BuildingType::Village,
                                                  TileType::GRASS, nullptr, actions);
        if (anchor.x >= 0 && AIActions::TrySubmitBuild(world, player, BuildingType::Village, anchor, actions))
            return true;
    }

    // Keep the smelter on IRON regardless of what else is going on (a fresh
    // Foundry defaults to the COPPER recipe — index 0 — and the AI never mines
    // COPPER_ORE, so without this it stalls forever and produces no iron; see
    // AIActions::TrySwitchRecipeFor). Fires once, as soon as a Foundry stands,
    // even while the opening plan is still grinding out the food chain — the
    // recipe switch must NOT wait behind the deficit ladder below.
    if (AIActions::TrySwitchRecipeFor(world, player, ResourceType::IRON))
        return true;

    // Opening bootstrap runs FIRST, before the reactive deficit ladder. The
    // plan front-loads the basic materials and the (cheap) iron chain, so those
    // get built off the opening stock before anything competes for it — the AI
    // reliably stands up real coal/iron production instead of leaning on a big
    // starting grant (user report 2026-07-20: "AI wciąż nie buduje żelaza/
    // węgla"). When the plan places a step it consumes the cycle; when it's
    // saving up (unaffordable next step) or complete it returns false and the
    // deficit ladder below takes over reactive scaling/sustain.
    if (TryOpeningPlan(world, player))
        return true;

    // Deficit backoff (2026-07-19): a resource that just failed (every
    // producer option unaffordable, not merely a missing anchor — see
    // AIActionState::deficitBackoff) is skipped for a few cycles so a
    // persistently-stuck top deficit can't starve every lower-priority one
    // of a turn forever. Without this, a resource whose OWN chain is wedged
    // (e.g. its producer's output never gets hauled away — a pre-existing
    // logistics issue, not something the priority ladder can fix) retries
    // itself every single cycle and the AI never tries anything else.
    for (const auto& deficit : s.deficits)
    {
        if (actions.deficitBackoff.count(deficit.resource) > 0)
            continue;
        if (TryBuildProducerFor(world, player, deficit.resource))
        {
            // Rotation fix (2026-07-20): a SUCCESS used to leave the very top
            // deficit free to win again next cycle too (nothing but the new
            // producer's own construction time slowed it down), so a lower-
            // priority deficit further down the ladder could starve for many
            // cycles in a row even though it would have succeeded on its own.
            // Short cooldown (not the 12s failure one — this resource isn't
            // stuck, it just went) lets the next 2-3 cycles serve other
            // deficits before this one is eligible again.
            actions.deficitBackoff[deficit.resource] = 4.0;
            return true;
        }
        actions.deficitBackoff[deficit.resource] = 12.0;
    }

    return false;
}

bool UtilityAIModel::TryBuildProducerFor(GameWorld& world, Player* player, ResourceType resource)
{
    // Tier-2 priority handoff (2026-07-20, see Sense's economyEstablished):
    // once the economy has some real footing and food is alive, stop
    // discounting tier-2's urgency here too — otherwise IRON/TOOLS/swords
    // could win a slot on the deficit ladder (Sense) but still lose the
    // chain-walk/duplicate-guard math below to a tier-1 resource that
    // out-weighs them 1.0 vs 0.4.
    const std::map<ResourceType, double>* effectiveWeights =
        situation.economyEstablished ? nullptr : &priorityWeights;

    // Walk down the input chain: if the producer of `resource` is starved of
    // an input, build toward that input instead (bounded depth). missingInputs
    // order comes from the static building catalog — deterministic.
    ResourceType target = resource;
    for (int depth = 0; depth < 3; depth++)
    {
        AIActions::AIResourceDiagnosis diagnosis =
            AIActions::DiagnoseResourceNeed(player, target, depth, &consumptionBias, effectiveWeights);
        if (diagnosis.missingInputs.empty())
            break;
        target = diagnosis.missingInputs.front();
    }

    // Deadlock fix (rdzeń zgłoszenia 2026-07-19: "AI stawia podejrzanie dużo
    // chat drwala, nie rozwija się"): re-diagnose the resolved target and
    // check whether an EXISTING producer is the problem before building
    // another one. Without this, a stalled/bottlenecked/unmanned producer's
    // output reads as 0, urgency stays high, and every cycle answers with
    // "build one more" — the new producer inherits the same disconnection/
    // manpower drought, stalls too, and the ladder just grows a forest of
    // producers instead of ever developing. Fixing an EXISTING producer is
    // LogisticsRepair's (road/storage bottleneck) or the manpower-reserve
    // emergency's (unmanned) job, not this function's.
    //
    // Exactly one redundant producer is still allowed through (harness catch
    // 2026-07-19, AIBehaviorHarnessTests regression from the initial
    // unconditional block): when the lone existing producer is stuck behind a
    // logistics problem LogisticsRepair itself can't clear yet (a Bridge one
    // tile away that's still unaffordable — a real, not-instantly-fixable
    // deadlock), refusing ANY duplicate leaves the AI with no path forward at
    // all while it waits, since the thing that would unblock it (the bridge)
    // needs the very resource that's now stuck. A second producer placed on
    // fresh, already-connected ground routes around the stuck one instead of
    // literally being a copy of it. Once two are unhealthy, that path is
    // exhausted too and the hard block resumes.
    // Cause-D fix (2026-07-20): before building a new producer for `target`,
    // check whether an EXISTING multi-recipe building can produce it by
    // switching recipe. A Foundry defaults to COPPER and a Smith to TOOLS
    // (recipe index 0), so a Foundry the AI built expressly to make IRON sits
    // on the Copper recipe forever, and CountProducersOfResource(IRON) reads 0
    // — which would otherwise make this function build ANOTHER Copper Foundry
    // every cycle. Switching the recipe is the actual fix; it also stops the
    // redundant-Foundry spam.
    if (AIActions::TrySwitchRecipeFor(world, player, target))
        return true;

    std::vector<AIActions::AIProducerOption> options = AIActions::FindProducerOptions(target);
    // Counted per-PRODUCT (2026-07-20 fix), not per-BuildingType: a Mine on a
    // COAL tile and a Mine on an IRON_ORE tile are different producers as far
    // as this guard (and the diversify-sort below) are concerned — see
    // CountProducersOfResource.
    int ownedProducers = AIActions::CountProducersOfResource(player, target);

    AIActions::AIResourceDiagnosis targetDiagnosis =
        AIActions::DiagnoseResourceNeed(player, target, 0, &consumptionBias, effectiveWeights);
    if (targetDiagnosis.hasProducerBuilding && ownedProducers >= 2 &&
        (targetDiagnosis.logisticsProblem || targetDiagnosis.storageProblem || targetDiagnosis.manpowerProblem))
        return false;
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
        // Terrain-specific mines gate on an actual producer of their resource
        // (matched by terrain-resolved product, pending builds included) — a
        // type-count gate can't tell a coal mine from a stone/iron-ore one, so
        // the deficit ladder's STONE mine used to satisfy the COAL step and the
        // iron chain never got its ore (2026-07-20 user report).
        bool satisfied = step.gateResource != ResourceType::Null
            ? AIActions::HasProducerOrPendingForResource(player, step.gateResource)
            : AIActions::CountCompletedOrQueuedBuildings(world, player, step.type) >= step.target;
        if (satisfied)
            continue;

        // The plan is strictly ordered: an unaffordable step means "save up",
        // not "skip ahead and spend on something later" — deterministic
        // patience beats churning the buffer on cheap filler.
        if (!player->CanBuildDefinition(GetBuildingDefinition(step.type)))
            return false;

        Vec2i anchor = AIActions::FindBuildAnchor(world, player, step.type, step.preferredTile, nullptr, actions);
        if (anchor.x < 0)
            continue;  // nowhere to put THIS one (terrain gone / search backoff) — don't block the rest
        if (AIActions::TrySubmitBuild(world, player, step.type, anchor, actions))
            return true;
        // Cooldown after a just-submitted order of this type — move on.
    }
    return false;
}

bool UtilityAIModel::ManpowerEmergency(const AISituation& s, double manpowerReserve)
{
    // Villages already near capacity won't grow manpower any further on
    // their own — a low reserve there means "build another Village now",
    // not "wait, it'll recover" (user design 2026-07-19). A dead food chain
    // stays the higher-priority problem: a new Village without food can't
    // produce manpower either, so it's not a fix while foodProductionAlive
    // is false — the existing "food is dead" escalation (0.75) wins instead.
    if (!s.foodProductionAlive)
        return false;
    if (s.manpower >= manpowerReserve)
        return false;
    if (s.populationCap <= 0.0)
        return false;  // no village to be "full" yet — not this emergency
    return s.totalPopulation >= s.populationCap * 0.95;
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
    // Personality bias (2026-07-20): +/- a couple units on the wave threshold
    // (5..8) so two AIs on the same map don't deploy in visually identical
    // lockstep.
    int effectiveWaveSize = WaveSize + personalityWaveBias;
    if (s.rosterCount >= effectiveWaveSize || emergency)
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

    // Dążenie do ataku (2026-07-20, user report: "AI wciąż nie buduje żelaza/
    // węgla/narzędzi broni"): RecruitDeploy is the highest-scored need, but it
    // used to just recruit whatever's affordable and quietly fall back to bare
    // militia forever — nothing ever asked for the sword/tools/iron economy
    // the TOP-ranked unit choice actually needs. Build toward the top pick's
    // missing costs here; a fully-costed unit (e.g. a swordsman) is what
    // finally drives Foundry/Smith/Mine construction from the need that
    // should want them most.
    //
    // GATED on economyEstablished (harness catch 2026-07-20): without this,
    // the very FIRST decision cycle of the game (empty roster -> "knight",
    // needing IRON_SWORD) tried to walk the WHOLE iron chain -- Mine(IRON_ORE)
    // -> Foundry -> Smith -- before TryOpeningPlan ever got a turn, spending
    // the AI's limited starting stock and its shared per-type build cooldown
    // on an out-of-order producer. That one early hijack was enough to wedge
    // the deterministic opening bootstrap for the ENTIRE rest of the game
    // (roster froze, zero deploys, every later build request rejected for
    // lack of resources) -- rate-limiting the ATTEMPT (recruitEconomyBuildTimer)
    // did nothing because the damage was already done on that first,
    // unthrottled call. Waiting for the economy to clear its first-footing
    // threshold (same `economyEstablished` flag Task 2 uses for the priority
    // handoff, ai.rtsdata's `tower_readiness_buildings`) keeps this action a
    // genuine complement to a working economy instead of a competitor for its
    // bootstrap — a handful of standing producers is enough to be past the
    // fragile stretch, without waiting for the entire opening plan (which
    // could take many minutes and left this need permanently dead in
    // practice).
    std::vector<const UnitDefinition*> ranked = RankUnitChoices(s);
    if (s.economyEstablished && recruitEconomyBuildTimer <= 0.0 && !ranked.empty() && ranked.front() != nullptr)
    {
        for (const auto& cost : ranked.front()->cost)
        {
            if (actions.deficitBackoff.count(cost.type) > 0)
                continue;
            int stored = AIActions::CountStoredResource(player, cost.type);
            int rate = AIActions::GetResourceRate(
                player->economyTelemetry.current.productionRatesPerMinute, cost.type);
            if (stored >= cost.amount || rate > 0)
                continue;  // already covered — nothing to build toward
            // Gate consumed on the FIRST real attempt regardless of outcome —
            // otherwise a chain with several missing costs could exhaust its
            // whole per-resource backoff table in one cycle and effectively
            // recreate the lockout the interval exists to prevent.
            recruitEconomyBuildTimer = RecruitEconomyBuildInterval;
            if (TryBuildProducerFor(world, player, cost.type))
            {
                // Short success cooldown (not the 12s failure one) — lets the
                // ladder rotate to the pick's NEXT missing cost across the
                // next couple of gated attempts instead of hammering the same
                // one every time.
                actions.deficitBackoff[cost.type] = 4.0;
                return true;
            }
            actions.deficitBackoff[cost.type] = 12.0;
            break;
        }
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

    for (const UnitDefinition* def : ranked)
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

bool UtilityAIModel::ExecuteResearch(GameWorld& world, Player* player, const AISituation& s)
{
    // 1. Focus first — costs nothing to start, pure strategic modifier.
    //    First unlockable in catalog order (focuses.rtsdata is today a flat
    //    stat cheat-sheet pending its own redesign — a deliberately minimal
    //    heuristic until that lands).
    if (!s.focusActive)
    {
        for (const auto& def : GetFocusDefinitions())
        {
            if (!player->CanUnlockFocus(def.id))
                continue;
            world.SubmitCommand(GameCommand::StartFocus(player->id, def.id));
            return true;
        }
    }

    // 2. No University yet — building one IS the research action.
    if (s.universityCount == 0)
    {
        if (!player->CanBuildDefinition(GetBuildingDefinition(BuildingType::University)))
            return false;
        Vec2i anchor = AIActions::FindBuildAnchor(world, player, BuildingType::University,
                                                  TileType::GRASS, nullptr, actions);
        return anchor.x >= 0 &&
               AIActions::TrySubmitBuild(world, player, BuildingType::University, anchor, actions);
    }

    // 3. Idle University — pick a technology: posture-preferred tag first
    //    (military under pressure, production otherwise), then cheapest.
    //    Deterministic: catalog iteration order breaks ties (strict >).
    Building* university = AIActions::FindUniversity(player);
    if (university == nullptr)
        return false;
    const char* preferredTag = (s.Threat() > 0.0 || s.UnderAttack()) ? "military" : "production";
    const TechnologyDefinition* best = nullptr;
    double bestScore = -1e18;
    for (const auto& def : GetTechnologyDefinitions())
    {
        if (player->technologies.HasTechnology(def.id) || player->IsTechnologyInProgress(def.id))
            continue;
        if (!player->CanResearchTechnology(def.id))
            continue;
        bool tagged = std::find(def.tags.begin(), def.tags.end(), preferredTag) != def.tags.end();
        double score = (tagged ? 1000.0 : 0.0) - def.researchTime;
        if (score > bestScore)
        {
            bestScore = score;
            best = &def;
        }
    }
    if (best == nullptr)
        return false;
    world.SubmitCommand(GameCommand::StartTechnologyResearch(player->id, best->id, university->positionId));
    return true;
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
