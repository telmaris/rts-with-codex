#include "ai/Controller.h"
#include "core/GameWorld.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <functional>
#include <limits>
#include <random>

namespace
{
    // Initializes TileDistance.
    int TileDistance(TileMap& tilemap, const Building* a, const Building* b)
    {
        if (a == nullptr || b == nullptr)
            return std::numeric_limits<int>::max();

        Vec2i apos = tilemap.GetCoordsFromId(a->positionId);
        Vec2i bpos = tilemap.GetCoordsFromId(b->positionId);
        return std::abs(apos.x - bpos.x) + std::abs(apos.y - bpos.y);
    }

    AIPersonality GeneratePersonality(int playerId)
    {
        std::mt19937 rng(static_cast<unsigned int>(playerId * 7919 + 17));
        std::uniform_real_distribution<float> variance(-0.18f, 0.18f);
        auto trait = [&](float base)
        {
            return std::clamp(base + variance(rng), 0.05f, 0.95f);
        };

        AIPersonality personality;
        personality.aggression = trait(0.32f);
        personality.planning = trait(0.55f);
        personality.riskTolerance = trait(0.40f);
        personality.expansionism = trait(0.45f);
        personality.economicFocus = trait(0.55f);
        personality.militarism = trait(0.35f);
        personality.defensiveBias = trait(0.50f);
        personality.logisticsAwareness = trait(0.55f);
        personality.adaptability = trait(0.45f);
        personality.opportunism = trait(0.35f);
        personality.persistence = trait(0.50f);
        personality.governmentPreference = static_cast<AIGovernmentPreference>(playerId % 4);
        return personality;
    }

    double AxisAnalysisInterval(AIStrategyAxis axis)
    {
        switch (axis)
        {
            case AIStrategyAxis::Resources: return 6.0;
            case AIStrategyAxis::Logistics: return 8.0;
            case AIStrategyAxis::Military: return 10.0;
            case AIStrategyAxis::Risk: return 12.0;
            case AIStrategyAxis::InternalDevelopment: return 15.0;
            case AIStrategyAxis::Technology: return 20.0;
            case AIStrategyAxis::Expansion: return 30.0;
            case AIStrategyAxis::Diplomacy: return 45.0;
        }
        return 10.0;
    }

    std::vector<AIStrategyAxisCache> MakeStrategyAxisCache()
    {
        std::vector<AIStrategyAxisCache> cache;
        for (AIStrategyAxis axis : {
            AIStrategyAxis::Resources,
            AIStrategyAxis::Logistics,
            AIStrategyAxis::Military,
            AIStrategyAxis::Expansion,
            AIStrategyAxis::InternalDevelopment,
            AIStrategyAxis::Technology,
            AIStrategyAxis::Diplomacy,
            AIStrategyAxis::Risk})
        {
            cache.push_back({axis, AxisAnalysisInterval(axis), 0.0, {}});
        }
        return cache;
    }

    int CountCompletedBuildings(Player* player, BuildingType type)
    {
        return player != nullptr ? player->GetTrackedBuildingCount(type, true) : 0;
    }

    double ResourceDevelopmentValue(ResourceType type)
    {
        double value = 0.0;
        for (const auto& definition : GetBuildingDefinitions())
        {
            for (const auto& cost : definition.buildCosts)
                if (cost.type == type)
                    value += 0.35 + cost.amount * 0.012;

            auto inspectProduction = [&](const ProductionDefinition& production)
            {
                for (const auto& input : production.inputs)
                    if (input.type == type)
                        value += 0.85 + input.amount * 0.20;
            };
            inspectProduction(definition.production);
            for (const auto& terrainProduction : definition.terrainProductions)
                inspectProduction(terrainProduction.production);
            for (const auto& recipe : definition.recipes)
                inspectProduction(recipe.production);
        }
        return value;
    }

    double BasicResourcePriority(ResourceType type)
    {
        switch (type)
        {
            case ResourceType::WOOD: return 1.35;
            case ResourceType::STONE: return 1.25;
            case ResourceType::PLANKS: return 1.18;
            case ResourceType::IRON_ORE: return 1.05;
            case ResourceType::COAL: return 1.00;
            case ResourceType::IRON: return 0.92;
            case ResourceType::WHEAT: return 0.90;
            case ResourceType::FOOD_PROVISIONS: return 0.88;
            case ResourceType::WATER: return 0.55;
            default: return 0.68;
        }
    }

    bool IsPrimaryDevelopmentResource(ResourceType type)
    {
        switch (type)
        {
            case ResourceType::WOOD:
            case ResourceType::STONE:
            case ResourceType::PLANKS:
            case ResourceType::IRON_ORE:
            case ResourceType::COAL:
            case ResourceType::IRON:
            case ResourceType::WHEAT:
            case ResourceType::FOOD_PROVISIONS:
                return true;
            default:
                return false;
        }
    }

    bool HasTag(const TechnologyDefinition& definition, const std::string& tag)
    {
        return std::find(definition.tags.begin(), definition.tags.end(), tag) != definition.tags.end();
    }

    bool ProducesResource(const AIProducerOption& option, ResourceType resource)
    {
        return option.buildingType != BuildingType::Building && resource != ResourceType::Null;
    }

    int OpeningTargetCount(BuildingType type)
    {
        switch (type)
        {
            case BuildingType::Woodcutter: return 1;
            case BuildingType::Mine: return 1;
            case BuildingType::LumberMill: return 1;
            case BuildingType::Village: return 1;
            default: return 0;
        }
    }

    int StrategicSoftCap(BuildingType type)
    {
        switch (type)
        {
            case BuildingType::Woodcutter: return 2;
            case BuildingType::Mine: return 3;
            case BuildingType::LumberMill: return 2;
            case BuildingType::Well: return 1;
            case BuildingType::WheatFarm: return 2;
            case BuildingType::StorageBuilding: return 2;
            default: return 99;
        }
    }

    Building* FindOwnedHeadquarters(Player* player)
    {
        if (player == nullptr)
            return nullptr;
        for (auto* building : player->GetTrackedBuildings())
            if (building != nullptr && building->owner == player && building->buildingType == BuildingType::Headquarters)
                return building;
        return nullptr;
    }

    int DistanceToNearestInfrastructure(TileMap& tilemap, Player* player, Vec2i pos)
    {
        int best = std::numeric_limits<int>::max() / 4;
        for (const auto& tile : tilemap.tilemap)
        {
            Building* building = tile.building.get();
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

// Advances this object's state for one frame.
void LocalController::Update(GameWorld& world, double dt)
{
}

// Advances this object's state for one frame.
void RemoteController::Update(GameWorld& world, double dt)
{
}

// Initializes AIController::AIController.
AIController::AIController(int controlledPlayerId)
    // Initializes IController.
    : IController(controlledPlayerId)
{
    settings.personality = GeneratePersonality(controlledPlayerId);
    model = CreateModel(difficulty);
}

// Advances this object's state for one frame.
void AIController::Update(GameWorld& world, double dt)
{
    auto playerIt = world.playerHandler.players.find(playerId);
    if (playerIt == world.playerHandler.players.end())
        return;

    if (model != nullptr)
        model->Update(world, playerIt->second.get(), dt, settings);
}

// Updates the requested state value.
void AIController::SetDifficulty(AIDifficulty newDifficulty)
{
    difficulty = newDifficulty;
    model = CreateModel(difficulty);
}

// Creates and registers the requested runtime object.
std::unique_ptr<AIModel> AIController::CreateModel(AIDifficulty selectedDifficulty) const
{
    switch (selectedDifficulty)
    {
        case AIDifficulty::Primitive:
        case AIDifficulty::Easy:
        case AIDifficulty::Normal:
        case AIDifficulty::Hard:
        default:
            return std::make_unique<PrimitiveAIModel>();
    }
}

// Advances this object's state for one frame.
void PrimitiveAIModel::Update(GameWorld& world, Player* player, double dt, const AIModelSettings& settings)
{
    if (player == nullptr)
        return;

    AIStrategySnapshot strategy = UpdateStrategyPipeline(world, player, dt, settings);
    UpdateGoalState(world, player, strategy, settings, dt);

    roadTimer -= dt;
    attackTimer -= dt;
    decisionTimer -= dt;
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
    decay(recentResearchOrders);

    // Logistics maintenance runs on its own cadence — keeps the network healthy
    // independently of the strategic decision core below.
    if (roadTimer <= 0.0)
    {
        roadTimer = strategy.GetAxisScore(AIStrategyAxis::Logistics) < -0.2f ? 1.25 : 2.0;
        if (TryBuildRoads(world, player))
            return;
    }

    // Unified decision core (Tier 3): all build/research/focus/attack/recruit
    // actions compete in one scored pool, biased by the active goal + milestone.
    if (decisionTimer <= 0.0)
    {
        float worstAxis = 1.0f;
        for (AIStrategyAxis axis : {AIStrategyAxis::Resources, AIStrategyAxis::Logistics, AIStrategyAxis::Military, AIStrategyAxis::Risk})
            worstAxis = std::min(worstAxis, strategy.GetAxisScore(axis));
        double baseInterval = 1.5 + (1.0 - settings.personality.adaptability) * 1.5;
        decisionTimer = worstAxis < -0.3f ? baseInterval * 0.6 : baseInterval;
        RunUnifiedDecision(world, player, strategy, settings);
    }
}

// Initializes PrimitiveAIModel::TryBuildEconomy.
bool PrimitiveAIModel::TryBuildEconomy(GameWorld& world, Player* player, const AIModelSettings& settings)
{
    struct BuildCandidate
    {
        BuildingType type{BuildingType::Building};
        TileType terrain{TileType::GRASS};
        double score{0.0};
    };

    std::vector<BuildCandidate> candidates;
    auto stored = [&](ResourceType type) { return CountStoredResource(world, player, type); };
    auto add = [&](BuildingType type, TileType terrain, double score)
    {
        if (score <= 0.0 || recentBuildOrders.contains(type))
            return;
        const auto& definition = GetBuildingDefinition(type);
        if (!player->CanBuildDefinition(definition))
            return;
        candidates.push_back({type, terrain, score});
    };

    auto addOpening = [&](BuildingType type, TileType terrain, int targetCount, double score)
    {
        if (CountOwnedBuildings(world, player, type) >= targetCount)
            return;
        add(type, terrain, score);
    };

    int completedBuildings = 0;
    int fullOutputBuildings = 0;
    int stalledBuildings = 0;
    int totalStored = 0;
    int totalStorageCapacity = 0;
    for (const auto* building : player->GetTrackedBuildings())
    {
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;
        completedBuildings++;
        if (building->IsProductionStalled())
            stalledBuildings++;

        if (const auto* production = building->GetComponent<ProductionComponent>())
        {
            bool outputFull = false;
            for (const auto& [type, buffer] : production->outputBuffers)
                outputFull = outputFull || buffer.bufferSize > 0 && static_cast<int>(buffer.buffer.size()) >= buffer.bufferSize;
            if (outputFull)
                fullOutputBuildings++;
        }

        if (const auto* storage = building->GetComponent<StorageComponent>())
        {
            for (const auto& [type, buffer] : storage->buffers)
            {
                totalStored += static_cast<int>(buffer.buffer.size());
                totalStorageCapacity += buffer.bufferSize;
            }
        }
    }

    int villages = CountOwnedBuildings(world, player, BuildingType::Village);
    double populationRatio = player->GetPopulationCap() > 0 ? player->GetTotalPopulation() / player->GetPopulationCap() : 1.0;
    double storageFill = totalStorageCapacity > 0 ? totalStored / static_cast<double>(totalStorageCapacity) : 0.0;
    bool storagePressure = (storageFill > 0.82 && totalStored > 120) || fullOutputBuildings > 1;

    addOpening(BuildingType::Woodcutter, TileType::WOOD, 1, 240.0);
    addOpening(BuildingType::Mine, TileType::STONE, 1, 220.0);
    if (CountOwnedBuildings(world, player, BuildingType::Woodcutter) >= 1 &&
        CountOwnedBuildings(world, player, BuildingType::Mine) >= 1)
        addOpening(BuildingType::LumberMill, TileType::GRASS, 1, 260.0);

    add(BuildingType::StorageBuilding, TileType::GRASS,
        (storagePressure ? 45.0 + settings.personality.logisticsAwareness * 25.0 : 0.0) +
        (activePlan == AIStrategicPlan::FixLogistics && (stalledBuildings > 0 || storagePressure) ? 35.0 : 0.0));

    add(BuildingType::Village, TileType::GRASS,
        (populationRatio > 0.82 ? 75.0 : 0.0) +
        (villages < 1 ? 35.0 : 0.0) +
        (activePlan == AIStrategicPlan::DevelopPopulation ? 35.0 : 0.0));

    std::function<void(ResourceType, int, double)> collectResourceCandidates = [&](ResourceType resource, int depth, double weight)
    {
        if (depth > 3 || resource == ResourceType::Null)
            return;
        AIResourceDiagnosis diagnosis = DiagnoseResourceNeed(world, player, resource, depth);
        if (diagnosis.urgency <= 0.05)
            return;

        bool bottleneckOnly = (diagnosis.storageProblem || diagnosis.logisticsProblem) && diagnosis.missingInputs.empty();
        if (!bottleneckOnly)
        {
            for (const auto& option : FindProducerOptions(diagnosis.resource))
                add(option.buildingType, option.terrain, ScoreProducerOption(world, player, diagnosis, option, settings) * weight);
        }

        for (ResourceType input : diagnosis.missingInputs)
            collectResourceCandidates(input, depth + 1, weight * 0.78);
    };

    for (ResourceType resource : resourceTypes)
        collectResourceCandidates(resource, 0, 1.0);

    double spendPressure = std::clamp(totalStored / 450.0, 0.15, 1.0);
    for (ResourceType resource : resourceTypes)
    {
        double developmentValue = ResourceDevelopmentValue(resource);
        if (developmentValue <= 0.0)
            continue;

        int produced = GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute, resource);
        int consumed = GetResourceRate(player->economyTelemetry.current.consumptionRatesPerMinute, resource);
        int resourceStored = stored(resource);
        for (const auto& option : FindProducerOptions(resource))
        {
            if (recentBuildOrders.contains(option.buildingType))
                continue;

            const auto& definition = GetBuildingDefinition(option.buildingType);
            if (!player->CanBuildDefinition(definition))
                continue;

            int existing = CountOwnedBuildings(world, player, option.buildingType);
            int openingTarget = OpeningTargetCount(option.buildingType);
            if (openingTarget > 0 && existing >= openingTarget && consumed <= produced && resourceStored > std::max(40, consumed * 2))
                continue;
            if (existing >= StrategicSoftCap(option.buildingType) && consumed <= produced)
                continue;
            bool hasProducerForResource = existing > 0;
            bool primaryResource = IsPrimaryDevelopmentResource(resource);
            bool feedsExistingDemand = consumed > 0 || (primaryResource && developmentValue >= 2.0);
            bool inputReady = true;
            for (const auto& input : option.inputs)
            {
                int inputProduced = GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute, input.type);
                inputReady = inputReady && inputProduced > 0;
            }
            if (!option.inputs.empty() && !inputReady)
                continue;

            double diversityNeed = hasProducerForResource ? 0.16 / (1.0 + existing * 0.35) : (primaryResource ? 0.70 : 0.12);
            double throughputNeed = consumed > 0
                ? std::clamp((consumed * 1.35 - produced) / static_cast<double>(std::max(1, consumed * 2)), 0.0, 0.65)
                : 0.0;
            double strategicNeed = std::clamp(developmentValue / 18.0, 0.10, 0.58) * BasicResourcePriority(resource);
            double reserveSoftener = resourceStored > std::max(80, consumed * 4) ? 0.78 : 1.0;
            double need = std::max({diversityNeed, throughputNeed, feedsExistingDemand ? strategicNeed : 0.0}) * reserveSoftener;
            if (need <= 0.06)
                continue;

            double costCoverage = 1.0;
            for (const auto& cost : definition.buildCosts)
                costCoverage *= std::clamp(stored(cost.type) / static_cast<double>(std::max(1, cost.amount)), 0.35, 1.45);
            costCoverage = std::clamp(costCoverage, 0.30, 1.35);

            double personality = 0.85 + settings.personality.economicFocus * 0.35 + settings.personality.expansionism * 0.18;
            double inputModifier = inputReady ? 1.0 : 0.58 + settings.personality.planning * 0.22;
            double existingPenalty = 1.0 / (1.0 + existing * 0.24);
            double noiseSeed = std::sin(static_cast<double>(player->id * 113 + static_cast<int>(option.buildingType) * 37 + static_cast<int>(resource) * 19 + completedBuildings));
            double noise = 0.94 + (noiseSeed + 1.0) * 0.055 * (1.0 - settings.personality.planning);

            AIActionUtility utility;
            utility.baseValue = 10.0 + option.outputPerMinute * 4.0 + developmentValue * 3.0;
            utility.need = need;
            utility.personalityModifier = personality * inputModifier * noise;
            utility.feasibility = costCoverage;
            utility.urgency = 1.0 + spendPressure;
            utility.planModifier = activePlan == AIStrategicPlan::RecoverEconomy || activePlan == AIStrategicPlan::ExpandForResources ? 1.10 : 1.0;
            add(option.buildingType, option.terrain, utility.Score() * existingPenalty);
        }
    }

    if (candidates.empty())
        return false;

    std::sort(candidates.begin(), candidates.end(), [](const BuildCandidate& a, const BuildCandidate& b)
    {
        return a.score > b.score;
    });

    for (const auto& candidate : candidates)
    {
        Vec2i anchor = FindBuildAnchor(world, player, candidate.type, candidate.terrain, nullptr);
        if (anchor.x >= 0 && TrySubmitBuild(world, player, candidate.type, anchor))
            return true;
    }
    return false;
}

// Initializes PrimitiveAIModel::TryBuildRoads.
bool PrimitiveAIModel::TryBuildRoads(GameWorld& world, Player* player)
{
    if (player == nullptr)
        return false;

    int roads = CountOwnedBuildings(world, player, BuildingType::Road);
    int buildings = 0;
    for (const auto* building : player->GetTrackedBuildings())
        if (building != nullptr && building->owner == player && !building->IsUnderConstruction() && building->buildingType != BuildingType::Road)
            buildings++;
    if (roads > std::max(6, buildings * 3))
        return false;

    for (auto* building : player->GetTrackedBuildings())
    {
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;
        if (!building->IsStorageLike() && building->buildingType != BuildingType::Headquarters)
            continue;
        if (HasAdjacentRoad(world, building))
            continue;

        for (int tileId : world.tilemap.GetAdjacentTileIds(building))
        {
            if (tileId < 0 || tileId >= static_cast<int>(world.tilemap.tilemap.size()))
                continue;
            Tile& tile = world.tilemap[tileId];
            if (tile.owner != player || tile.HasBuilding() || reservedRoadTiles.contains(tileId))
                continue;
            Vec2i pos = world.tilemap.GetCoordsFromId(tileId);
            const auto& roadDefinition = GetBuildingDefinition(BuildingType::Road);
            if (!world.tilemap.CanPlaceBuilding(BuildingType::Road, pos, roadDefinition.footprint, player))
                continue;
            world.SubmitCommand(GameCommand::BuildBuilding(player->id, BuildingType::Road, pos));
            reservedRoadTiles[tileId] = 6.0;
            recentBuildOrders[BuildingType::Road] = 1.0;
            return true;
        }
    }

    for (auto& tile : world.tilemap.tilemap)
    {
        Building* building = tile.building.get();
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;
        if (building->buildingType == BuildingType::Road || building->IsStorageLike())
            continue;
        if (HasAdjacentRoad(world, building))
            continue;

        Building* targetRoad = FindNearestStorageConnectedRoad(world, player, building);
        if (targetRoad != nullptr && SubmitRoadPath(world, player, building, targetRoad))
            return true;
    }

    return false;
}

AIMapAssessment PrimitiveAIModel::AssessMap(GameWorld& world, Player* player) const
{
    AIMapAssessment assessment;
    if (player == nullptr)
        return assessment;

    Building* headquarters = FindOwnedHeadquarters(player);
    Vec2i origin{world.tilemap.params.sizeX / 2, world.tilemap.params.sizeY / 2};
    if (headquarters != nullptr)
        origin = world.tilemap.GetCoordsFromId(headquarters->positionId);

    auto isStrategicResource = [](TileType type)
    {
        return type == TileType::IRON_ORE || type == TileType::COAL || type == TileType::STONE || type == TileType::WOOD;
    };

    int nearestUnownedStrategic = 9999;
    for (const auto& tile : world.tilemap.tilemap)
    {
        Vec2i pos = world.tilemap.GetCoordsFromId(tile.id);
        int distance = std::abs(pos.x - origin.x) + std::abs(pos.y - origin.y);
        if (tile.owner != nullptr && tile.owner != player)
        {
            assessment.nearestEnemyDistance = std::min(assessment.nearestEnemyDistance, distance);
            if (isStrategicResource(tile.tileType))
                assessment.enemyStrategicResourceTiles++;
        }

        if (!isStrategicResource(tile.tileType) || tile.resourceRichness <= 0)
            continue;

        if (tile.owner == player)
            assessment.ownedStrategicResourceTiles++;
        else
        {
            nearestUnownedStrategic = std::min(nearestUnownedStrategic, distance);
            if (distance <= 18)
                assessment.nearbyUnownedStrategicResourceTiles++;
        }

        if (tile.tileType == TileType::IRON_ORE && tile.owner != player)
            assessment.nearestIronDistance = std::min(assessment.nearestIronDistance, distance);
        if (tile.tileType == TileType::COAL && tile.owner != player)
            assessment.nearestCoalDistance = std::min(assessment.nearestCoalDistance, distance);
    }

    bool lacksIronOrCoalAccess = assessment.nearestIronDistance < 9999 || assessment.nearestCoalDistance < 9999;
    if (assessment.ownedStrategicResourceTiles < 8 && nearestUnownedStrategic < 9999)
        assessment.expansionPressure = std::max(assessment.expansionPressure, std::clamp((22.0 - nearestUnownedStrategic) / 22.0, 0.15, 1.0));
    if (lacksIronOrCoalAccess && (assessment.nearestIronDistance <= 22 || assessment.nearestCoalDistance <= 22))
        assessment.expansionPressure = std::max(assessment.expansionPressure, 0.62);
    if (assessment.enemyStrategicResourceTiles > 0)
        assessment.militaryOpportunity = std::clamp(assessment.enemyStrategicResourceTiles / 12.0, 0.0, 0.85);
    if (assessment.nearestEnemyDistance <= 18)
        assessment.militaryOpportunity = std::max(assessment.militaryOpportunity, std::clamp((20.0 - assessment.nearestEnemyDistance) / 20.0, 0.25, 0.95));
    assessment.logisticsNeed = nearestUnownedStrategic < 9999 ? std::clamp(nearestUnownedStrategic / 28.0, 0.0, 0.85) : 0.0;
    return assessment;
}

AIStrategySnapshot PrimitiveAIModel::UpdateStrategyPipeline(GameWorld& world, Player* player, double dt, const AIModelSettings& settings)
{
    AIStrategySnapshot snapshot;
    if (player == nullptr)
        return snapshot;

    if (strategyAxisCache.empty())
        strategyAxisCache = MakeStrategyAxisCache();

    for (auto& axisCache : strategyAxisCache)
    {
        axisCache.timeUntilRefresh -= dt;
        if (axisCache.timeUntilRefresh <= 0.0)
        {
            axisCache.signals = AnalyzeAxis(world, player, axisCache.axis, settings);
            axisCache.score = EvaluateAxis(world, player, axisCache.axis, settings);
            axisCache.timeUntilRefresh = axisCache.interval;
        }
        snapshot.signals.insert(snapshot.signals.end(), axisCache.signals.begin(), axisCache.signals.end());
        snapshot.axisScores[static_cast<int>(axisCache.axis)] = axisCache.score;
    }

    snapshot.selectedPlan = SelectStrategicPlan(snapshot, settings);
    activePlan = snapshot.selectedPlan;
    return snapshot;
}

std::vector<AIStrategySignal> PrimitiveAIModel::AnalyzeAxis(GameWorld& world, Player* player, AIStrategyAxis axis, const AIModelSettings& settings) const
{
    std::vector<AIStrategySignal> signals;
    if (player == nullptr)
        return signals;

    auto push = [&](AIStrategyAxis axis, float urgency, ResourceType resource, std::string reason)
    {
        signals.push_back({axis, std::clamp(urgency, 0.0f, 1.0f), resource, std::move(reason)});
    };

    if (axis == AIStrategyAxis::Resources)
    {
        for (ResourceType type : resourceTypes)
        {
            int produced = 0;
            int consumed = 0;
            int stored = CountStoredResource(world, player, type);
            auto producedIt = player->economyTelemetry.current.productionRatesPerMinute.find(type);
            if (producedIt != player->economyTelemetry.current.productionRatesPerMinute.end())
                produced = producedIt->second;
            auto consumedIt = player->economyTelemetry.current.consumptionRatesPerMinute.find(type);
            if (consumedIt != player->economyTelemetry.current.consumptionRatesPerMinute.end())
                consumed = consumedIt->second;

            if (consumed > produced)
            {
                float deficitRatio = static_cast<float>(consumed - produced) / static_cast<float>(std::max(1, consumed));
                std::string reason = produced <= 0 ? "missing producer or input chain" : "resource deficit";
                if (stored <= consumed)
                    reason = "low reserve and negative resource flow";
                push(axis, 0.35f + deficitRatio * 0.55f * settings.personality.economicFocus, type, reason);
            }
            else if (consumed > 0 && stored < consumed * 2)
            {
                push(axis, 0.28f + settings.personality.planning * 0.22f, type, "low reserve");
            }

            if (produced > 0 && consumed > 0 && player->economyTelemetry.history.size() >= 2)
            {
                int oldProduced = GetResourceRate(player->economyTelemetry.history.front().productionRatesPerMinute, type);
                if (oldProduced > produced + 1)
                {
                    float trend = static_cast<float>(oldProduced - produced) / static_cast<float>(std::max(1, oldProduced));
                    push(axis, trend * 0.30f * settings.personality.planning, type, "declining production trend");
                }
            }
        }
        return signals;
    }

    int completedBuildings = 0;
    int stalledBuildings = 0;
    int disconnectedBuildings = 0;
    int storageBuildings = 0;
    int ownedTiles = 0;
    int borderTiles = 0;
    int enemyBorderTiles = 0;
    for (const auto* building : player->GetTrackedBuildings())
    {
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;
        completedBuildings++;
        if (building->IsProductionStalled())
            stalledBuildings++;
        if (building->IsStorageLike())
            storageBuildings++;
        if (building->buildingType != BuildingType::Road && !building->IsStorageLike())
        {
            Building* target = FindNearestRoadTarget(world, player, building);
            if (target != nullptr && !HasRoadConnection(world, player, building, target))
                disconnectedBuildings++;
        }
    }

    if (axis == AIStrategyAxis::Logistics)
    {
        if (stalledBuildings > 0)
            push(axis, std::min(1.0f, 0.35f + stalledBuildings * 0.16f * settings.personality.logisticsAwareness), ResourceType::Null, "stalled production");
        if (disconnectedBuildings > 0)
            push(axis, std::min(1.0f, 0.25f + disconnectedBuildings * 0.12f * settings.personality.logisticsAwareness), ResourceType::Null, "disconnected production chain");
        if (storageBuildings <= 1 && completedBuildings >= 6)
            push(axis, 0.35f + settings.personality.logisticsAwareness * 0.20f, ResourceType::Null, "thin storage network");
        return signals;
    }

    if (axis == AIStrategyAxis::Military)
    {
        ArmyRegistry army = player->GetArmyRegistry();
        double population = std::max(1.0, player->GetTotalPopulation());
        double militaryShare = army.TotalTroops() / population;
        double targetShare = 0.08 + settings.personality.militarism * 0.18;
        if (militaryShare < targetShare)
            push(axis, static_cast<float>((targetShare - militaryShare) / targetShare), ResourceType::Null, "army below target share");
        if (army.strength < 80)
            push(axis, (80 - army.strength) / 80.0f * (0.45f + settings.personality.militarism), ResourceType::Null, "low army strength");
        if (army.supplyCapacity > 0 && army.supply < army.supplyCapacity * 0.35)
            push(axis, 0.45f + settings.personality.logisticsAwareness * 0.25f, ResourceType::FOOD_PROVISIONS, "low army supply readiness");
        if (army.garrisonCapacity < completedBuildings / 3)
            push(axis, 0.30f + settings.personality.defensiveBias * 0.35f, ResourceType::Null, "thin garrisons");
        return signals;
    }

    if (axis == AIStrategyAxis::Expansion || axis == AIStrategyAxis::Risk)
    {
        for (const auto& tile : world.tilemap.tilemap)
        {
            if (tile.owner != player)
                continue;
            ownedTiles++;
            Vec2i pos = world.tilemap.GetCoordsFromId(tile.id);
            const std::array<Vec2i, 4> neighbours{
                Vec2i{pos.x + 1, pos.y},
                Vec2i{pos.x - 1, pos.y},
                Vec2i{pos.x, pos.y + 1},
                Vec2i{pos.x, pos.y - 1}
            };
            bool isBorder = false;
            for (Vec2i neighbour : neighbours)
            {
                if (!world.tilemap.IsInside(neighbour))
                    continue;
                const Tile& other = world.tilemap.tilemap[world.tilemap.GetIdFromCoords(neighbour)];
                if (other.owner != player)
                {
                    isBorder = true;
                    if (other.owner != nullptr)
                        enemyBorderTiles++;
                }
            }
            if (isBorder)
                borderTiles++;
        }
    }

    if (axis == AIStrategyAxis::Expansion)
    {
        int terrainResources = 0;
        for (const auto& tile : world.tilemap.tilemap)
            if (tile.owner == player && (tile.tileType == TileType::WOOD || tile.tileType == TileType::STONE || tile.tileType == TileType::IRON_ORE || tile.tileType == TileType::COAL))
                terrainResources++;
        if (completedBuildings < 8)
            push(axis, (8 - completedBuildings) / 8.0f * settings.personality.expansionism, ResourceType::Null, "small settlement footprint");
        if (terrainResources < 8)
            push(axis, 0.35f + settings.personality.expansionism * 0.35f, ResourceType::Null, "limited owned resource terrain");
        if (ownedTiles > 0 && borderTiles > ownedTiles * 0.45)
            push(axis, 0.30f + settings.personality.expansionism * 0.25f, ResourceType::Null, "thin irregular territory");
        return signals;
    }

    if (axis == AIStrategyAxis::InternalDevelopment)
    {
        if (player->GetFoodProductivity() < 0.85)
            push(axis, static_cast<float>((0.85 - player->GetFoodProductivity()) / 0.85), ResourceType::FOOD_PROVISIONS, "low food productivity");
        if (player->GetTotalPopulation() >= player->GetPopulationCap() * 0.85)
            push(axis, 0.45f + settings.personality.economicFocus * 0.30f, ResourceType::Null, "population close to cap");
        if (CountCompletedBuildings(player, BuildingType::University) <= 0 && completedBuildings >= 10)
            push(axis, 0.35f + settings.personality.planning * 0.25f, ResourceType::Null, "no university");
        if (CountCompletedBuildings(player, BuildingType::StorageBuilding) <= 0 && completedBuildings >= 5)
            push(axis, 0.35f + settings.personality.logisticsAwareness * 0.20f, ResourceType::Null, "no dedicated storage");
        return signals;
    }

    if (axis == AIStrategyAxis::Technology)
    {
        int universities = CountCompletedBuildings(player, BuildingType::University);
        if (universities <= 0 && completedBuildings >= 8)
            push(axis, 0.30f + settings.personality.planning * 0.25f, ResourceType::Null, "research capacity missing");
        else if (universities > 0)
            push(axis, 0.25f + settings.personality.planning * 0.20f, ResourceType::Null, "research specialization available");
        return signals;
    }

    if (axis == AIStrategyAxis::Diplomacy)
    {
        int enemyMilitaryBuildings = 0;
        for (const auto& tile : world.tilemap.tilemap)
        {
            const Building* building = tile.GetBuilding();
            if (building == nullptr || building->owner == nullptr || building->owner == player || building->IsUnderConstruction())
                continue;
            if (building->GetHitPoints() > 0)
                enemyMilitaryBuildings++;
        }
        if (enemyMilitaryBuildings > 0)
            push(axis, std::min(1.0f, 0.20f + enemyMilitaryBuildings * 0.08f * (settings.personality.aggression + settings.personality.opportunism)), ResourceType::Null, "known armed rivals");
        return signals;
    }

    if (axis == AIStrategyAxis::Risk)
    {
        if (player->economyTelemetry.current.consumptionRatesPerMinute[ResourceType::FOOD_PROVISIONS] >
            player->economyTelemetry.current.productionRatesPerMinute[ResourceType::FOOD_PROVISIONS])
            push(axis, 0.45f + (1.0f - settings.personality.riskTolerance) * 0.35f, ResourceType::FOOD_PROVISIONS, "supply reserve risk");
        if (enemyBorderTiles > 0)
            push(axis, std::min(1.0f, 0.25f + enemyBorderTiles * 0.03f * (1.0f - settings.personality.riskTolerance + settings.personality.defensiveBias)), ResourceType::Null, "hostile border exposure");
        if (storageBuildings <= 1 && completedBuildings >= 8)
            push(axis, 0.30f + settings.personality.planning * 0.25f, ResourceType::Null, "low rebuild reserve capacity");
        return signals;
    }

    return signals;
}

float PrimitiveAIModel::EvaluateAxis(GameWorld& world, Player* player, AIStrategyAxis axis, const AIModelSettings& settings) const
{
    if (player == nullptr)
        return 0.0f;

    switch (axis)
    {
    case AIStrategyAxis::Resources:
    {
        const std::array<ResourceType, 6> keyResources{
            ResourceType::WOOD, ResourceType::STONE, ResourceType::PLANKS,
            ResourceType::IRON_ORE, ResourceType::COAL, ResourceType::WHEAT
        };
        float totalWeight = 0.0f;
        float totalScore = 0.0f;
        float worstScore = 1.0f;
        for (ResourceType type : keyResources)
        {
            int produced = GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute, type);
            int consumed = GetResourceRate(player->economyTelemetry.current.consumptionRatesPerMinute, type);
            int stored = CountStoredResource(world, player, type);
            float w = static_cast<float>(BasicResourcePriority(type));
            float score;
            if (consumed == 0 && produced == 0)
                score = 0.0f;
            else if (consumed == 0)
                score = 0.35f;
            else
            {
                float surplus = static_cast<float>(produced - consumed) / static_cast<float>(consumed);
                float reserve = static_cast<float>(stored) / static_cast<float>(std::max(consumed * 3, 3));
                score = std::tanh(surplus * 2.0f) * 0.65f + std::tanh(reserve - 0.5f) * 0.35f;
            }
            totalScore += score * w;
            totalWeight += w;
            worstScore = std::min(worstScore, score);
        }
        float avg = totalWeight > 0.0f ? totalScore / totalWeight : 0.0f;
        return std::clamp(avg * 0.70f + worstScore * 0.30f, -1.0f, 1.0f);
    }

    case AIStrategyAxis::Logistics:
    {
        int completedBuildings = 0;
        int stalledBuildings = 0;
        int disconnectedBuildings = 0;
        int fullOutputBuildings = 0;
        int totalStored = 0;
        int totalCapacity = 0;
        for (const auto* building : player->GetTrackedBuildings())
        {
            if (building == nullptr || building->owner != player || building->IsUnderConstruction())
                continue;
            completedBuildings++;
            if (building->IsProductionStalled())
                stalledBuildings++;
            if (const auto* prod = building->GetComponent<ProductionComponent>())
            {
                for (const auto& [type, buf] : prod->outputBuffers)
                {
                    if (buf.bufferSize > 0 && static_cast<int>(buf.buffer.size()) >= buf.bufferSize)
                    {
                        fullOutputBuildings++;
                        break;
                    }
                }
            }
            if (const auto* stor = building->GetComponent<StorageComponent>())
            {
                for (const auto& [type, buf] : stor->buffers)
                {
                    totalStored += static_cast<int>(buf.buffer.size());
                    totalCapacity += buf.bufferSize;
                }
            }
            if (building->buildingType != BuildingType::Road && !building->IsStorageLike())
            {
                Building* target = FindNearestRoadTarget(world, player, building);
                if (target != nullptr && !HasRoadConnection(world, player, building, target))
                    disconnectedBuildings++;
            }
        }
        if (completedBuildings == 0)
            return 0.0f;
        float stallRate = static_cast<float>(stalledBuildings) / completedBuildings;
        float disconnectRate = static_cast<float>(disconnectedBuildings) / completedBuildings;
        float saturationRate = static_cast<float>(fullOutputBuildings) / completedBuildings;
        float storageFill = totalCapacity > 0 ? static_cast<float>(totalStored) / totalCapacity : 0.5f;
        float penalty = stallRate * 0.40f + disconnectRate * 0.30f + saturationRate * 0.15f
                      + std::max(0.0f, storageFill - 0.80f) * 0.75f;
        return std::clamp(std::tanh(-penalty * 3.5f + 0.4f), -1.0f, 1.0f);
    }

    case AIStrategyAxis::Military:
    {
        AIMapAssessment map = AssessMap(world, player);
        ArmyRegistry army = player->GetArmyRegistry();
        float strengthScore = std::tanh(army.strength / 60.0f - 1.0f);
        float supplyScore = army.supplyCapacity > 0
            ? std::tanh(static_cast<float>(army.supply) / army.supplyCapacity * 3.0f - 1.5f)
            : 0.0f;
        float threatScore = 0.0f;
        if (map.nearestEnemyDistance < 9999)
        {
            float proximity = std::clamp(1.0f - map.nearestEnemyDistance / 25.0f, 0.0f, 1.0f);
            float ownStrength = std::clamp(army.strength / 80.0f, 0.0f, 1.5f);
            threatScore = -proximity * std::max(0.0f, 1.5f - ownStrength);
        }
        return std::clamp(strengthScore * 0.50f + supplyScore * 0.20f + threatScore * 0.30f, -1.0f, 1.0f);
    }

    case AIStrategyAxis::Expansion:
    {
        AIMapAssessment map = AssessMap(world, player);
        float richnessScore = std::tanh(map.ownedStrategicResourceTiles / 8.0f - 1.0f);
        float pressureScore = -static_cast<float>(map.expansionPressure);
        return std::clamp(richnessScore * 0.65f + pressureScore * 0.35f, -1.0f, 1.0f);
    }

    case AIStrategyAxis::InternalDevelopment:
    {
        double popRatio = player->GetPopulationCap() > 0
            ? player->GetTotalPopulation() / player->GetPopulationCap()
            : 0.0;
        float popScore = static_cast<float>(1.0 - 2.0 * popRatio);
        float foodScore = static_cast<float>(player->GetFoodProductivity() * 2.0 - 1.0);
        int completedBuildings = 0;
        bool hasUniversity = false;
        bool hasStorage = false;
        for (const auto* b : player->GetTrackedBuildings())
        {
            if (b == nullptr || b->owner != player || b->IsUnderConstruction()) continue;
            completedBuildings++;
            if (b->buildingType == BuildingType::University) hasUniversity = true;
            if (b->IsStorageLike()) hasStorage = true;
        }
        float infraScore = 0.0f;
        if (!hasUniversity && completedBuildings >= 10) infraScore -= 0.5f;
        if (!hasStorage && completedBuildings >= 5) infraScore -= 0.25f;
        return std::clamp(popScore * 0.30f + foodScore * 0.50f + infraScore, -1.0f, 1.0f);
    }

    case AIStrategyAxis::Technology:
    {
        int universities = player->GetTrackedBuildingCount(BuildingType::University, true);
        int completedBuildings = 0;
        for (const auto* b : player->GetTrackedBuildings())
            if (b != nullptr && b->owner == player && !b->IsUnderConstruction())
                completedBuildings++;
        if (universities == 0)
            return std::clamp(-0.15f - (completedBuildings >= 8 ? 0.45f : 0.0f), -1.0f, 1.0f);

        int unlockedCount = static_cast<int>(player->technologies.GetUnlocked().size());
        int totalCount = 0;
        bool anyResearching = false;
        for (const auto& def : GetTechnologyDefinitions())
        {
            totalCount++;
            if (player->IsTechnologyInProgress(def.id)) anyResearching = true;
        }
        float coverage = totalCount > 0 ? static_cast<float>(unlockedCount) / totalCount : 0.0f;
        float score = std::tanh(coverage * 4.0f - 0.5f) * 0.8f + (anyResearching ? 0.2f : -0.1f);
        return std::clamp(score, -1.0f, 1.0f);
    }

    case AIStrategyAxis::Diplomacy:
    {
        int enemyMilitary = 0;
        int ownMilitary = CountOwnedBuildings(world, player, BuildingType::GuardTower)
                        + CountOwnedBuildings(world, player, BuildingType::Fortress)
                        + CountOwnedBuildings(world, player, BuildingType::Castle);
        for (const auto& tile : world.tilemap.tilemap)
        {
            const Building* b = tile.GetBuilding();
            if (b == nullptr || b->owner == nullptr || b->owner == player || b->IsUnderConstruction())
                continue;
            if (b->GetHitPoints() > 0)
                enemyMilitary++;
        }
        if (enemyMilitary == 0)
            return 0.7f;
        float ratio = static_cast<float>(ownMilitary) / std::max(1, enemyMilitary);
        return std::clamp(std::tanh(ratio * 1.5f - 1.0f), -1.0f, 1.0f);
    }

    case AIStrategyAxis::Risk:
    {
        float acc = 0.3f;

        int foodProd = GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute, ResourceType::FOOD_PROVISIONS);
        int foodCons = GetResourceRate(player->economyTelemetry.current.consumptionRatesPerMinute, ResourceType::FOOD_PROVISIONS);
        if (foodCons > 0 && foodProd < foodCons)
            acc -= 0.5f * (1.0f - static_cast<float>(foodProd) / foodCons);

        ArmyRegistry army = player->GetArmyRegistry();
        if (army.supplyCapacity > 0 && army.supply < army.supplyCapacity * 0.3)
            acc -= 0.3f;

        AIMapAssessment map = AssessMap(world, player);
        if (map.nearestEnemyDistance < 18)
            acc -= 0.4f * (1.0f - map.nearestEnemyDistance / 18.0f);

        int totalStored = 0;
        int totalCapacity = 0;
        for (const auto* b : player->GetTrackedBuildingsWithComponent<StorageComponent>())
        {
            const auto* stor = b != nullptr ? b->GetComponent<StorageComponent>() : nullptr;
            if (stor == nullptr || b->owner != player) continue;
            for (const auto& [type, buf] : stor->buffers)
            {
                totalStored += static_cast<int>(buf.buffer.size());
                totalCapacity += buf.bufferSize;
            }
        }
        if (totalCapacity > 0)
            acc += static_cast<float>(totalStored) / totalCapacity * 0.25f;

        return std::clamp(acc, -1.0f, 1.0f);
    }
    }
    return 0.0f;
}

// ─── Tier 1/2/3 strategic decision system ───────────────────────────────────

const char* AIStrategicGoalLabel(AIStrategicGoal goal)
{
    switch (goal)
    {
        case AIStrategicGoal::StabilizeEconomy:      return "StabilizeEconomy";
        case AIStrategicGoal::ExpandTerritory:       return "ExpandTerritory";
        case AIStrategicGoal::DevelopInfrastructure: return "DevelopInfrastructure";
        case AIStrategicGoal::BuildMilitary:         return "BuildMilitary";
        case AIStrategicGoal::LaunchOffensive:       return "LaunchOffensive";
        case AIStrategicGoal::Fortify:               return "Fortify";
    }
    return "Unknown";
}

namespace
{
    // Which axes a building improves, indexed by static_cast<int>(AIStrategyAxis).
    std::array<float, 8> GetBuildingAxisAffinity(BuildingType type)
    {
        std::array<float, 8> a{};
        auto set = [&](AIStrategyAxis ax, float v) { a[static_cast<int>(ax)] = v; };
        switch (type)
        {
            case BuildingType::Woodcutter:
            case BuildingType::Mine:
            case BuildingType::HuntersHut:
            case BuildingType::Well:
                set(AIStrategyAxis::Resources, 0.95f); break;
            case BuildingType::LumberMill:
            case BuildingType::Foundry:
            case BuildingType::Windmill:
            case BuildingType::Bakery:
            case BuildingType::Inn:
            case BuildingType::Paperworks:
            case BuildingType::Smith:
                set(AIStrategyAxis::Resources, 0.85f);
                set(AIStrategyAxis::InternalDevelopment, 0.15f); break;
            case BuildingType::WheatFarm:
                set(AIStrategyAxis::Resources, 0.70f);
                set(AIStrategyAxis::InternalDevelopment, 0.40f); break;
            case BuildingType::StorageBuilding:
                set(AIStrategyAxis::Logistics, 0.90f);
                set(AIStrategyAxis::Risk, 0.40f); break;
            case BuildingType::Road:
                set(AIStrategyAxis::Logistics, 1.00f); break;
            case BuildingType::Village:
                set(AIStrategyAxis::InternalDevelopment, 1.00f);
                set(AIStrategyAxis::Resources, 0.20f); break;
            case BuildingType::University:
                set(AIStrategyAxis::Technology, 1.00f);
                set(AIStrategyAxis::InternalDevelopment, 0.40f); break;
            case BuildingType::Mint:
                set(AIStrategyAxis::Resources, 0.50f);
                set(AIStrategyAxis::Diplomacy, 0.30f); break;
            case BuildingType::Glassworks:
                set(AIStrategyAxis::Resources, 0.70f);
                set(AIStrategyAxis::InternalDevelopment, 0.20f); break;
            case BuildingType::Powderworks:
                set(AIStrategyAxis::Resources, 0.55f);
                set(AIStrategyAxis::Military, 0.45f); break;
            case BuildingType::GuardTower:
                set(AIStrategyAxis::Military, 0.70f);
                set(AIStrategyAxis::Risk, 0.60f);
                set(AIStrategyAxis::Expansion, 0.35f); break;
            case BuildingType::Fortress:
                set(AIStrategyAxis::Military, 0.90f);
                set(AIStrategyAxis::Risk, 0.70f); break;
            case BuildingType::Castle:
                set(AIStrategyAxis::Military, 1.00f);
                set(AIStrategyAxis::Risk, 0.60f); break;
            case BuildingType::Barracks:
                set(AIStrategyAxis::Military, 1.00f); break;
            default:
                set(AIStrategyAxis::Resources, 0.40f); break;
        }
        return a;
    }

    float PersonalityAxisWeight(const AIPersonality& p, AIStrategyAxis axis)
    {
        switch (axis)
        {
            case AIStrategyAxis::Resources:            return 0.7f + p.economicFocus * 0.6f;
            case AIStrategyAxis::Logistics:            return 0.6f + p.logisticsAwareness * 0.8f;
            case AIStrategyAxis::Military:             return 0.5f + p.militarism * 0.7f + p.aggression * 0.3f;
            case AIStrategyAxis::Expansion:            return 0.5f + p.expansionism * 0.8f;
            case AIStrategyAxis::InternalDevelopment:  return 0.6f + p.economicFocus * 0.4f + p.planning * 0.2f;
            case AIStrategyAxis::Technology:           return 0.5f + p.planning * 0.8f;
            case AIStrategyAxis::Diplomacy:            return 0.5f + p.opportunism * 0.5f;
            case AIStrategyAxis::Risk:                 return 0.5f + p.defensiveBias * 0.6f + (1.0f - p.riskTolerance) * 0.4f;
        }
        return 1.0f;
    }

    AIStrategyAxis GoalPrimaryAxis(AIStrategicGoal goal)
    {
        switch (goal)
        {
            case AIStrategicGoal::StabilizeEconomy:      return AIStrategyAxis::Resources;
            case AIStrategicGoal::ExpandTerritory:       return AIStrategyAxis::Expansion;
            case AIStrategicGoal::DevelopInfrastructure: return AIStrategyAxis::InternalDevelopment;
            case AIStrategicGoal::BuildMilitary:         return AIStrategyAxis::Military;
            case AIStrategicGoal::LaunchOffensive:       return AIStrategyAxis::Military;
            case AIStrategicGoal::Fortify:               return AIStrategyAxis::Risk;
        }
        return AIStrategyAxis::Resources;
    }

    // Tech/focus prerequisite gate only — ignores resource affordability so the AI
    // can commit to an as-yet-unaffordable milestone target and save up for it.
    bool MeetsUnlockGate(Player* player, const BuildingDefinition& def)
    {
        for (const auto& tech : def.requiredTechnologies)
            if (!player->technologies.HasTechnology(tech))
                return false;
        for (const auto& focus : def.requiredFocuses)
            if (!player->focuses.HasFocus(focus))
                return false;
        return true;
    }
}

AIStrategicGoal PrimitiveAIModel::SelectStrategicGoal(const AIStrategySnapshot& snapshot, const AIModelSettings& settings) const
{
    const auto& p = settings.personality;
    auto pressure = [&](AIStrategyAxis a) { return static_cast<double>(snapshot.GetPressure(a)); };
    auto score = [&](AIStrategyAxis a) { return static_cast<double>(snapshot.GetAxisScore(a)); };

    struct Candidate { AIStrategicGoal goal; double value; };

    // Offensive only makes sense when we are strong AND outmatch the enemy AND not starving.
    double offensiveReadiness = std::clamp(score(AIStrategyAxis::Military), 0.0, 1.0) * 0.5
                              + std::clamp(score(AIStrategyAxis::Diplomacy), 0.0, 1.0) * 0.5;
    double riskGate = score(AIStrategyAxis::Risk) > -0.3 ? 1.0 : 0.25;

    std::vector<Candidate> candidates{
        {AIStrategicGoal::StabilizeEconomy,      pressure(AIStrategyAxis::Resources) * (0.9 + p.economicFocus) + pressure(AIStrategyAxis::Logistics) * 0.3},
        {AIStrategicGoal::ExpandTerritory,       std::max(pressure(AIStrategyAxis::Expansion), pressure(AIStrategyAxis::Resources) * 0.5) * (0.55 + p.expansionism)},
        {AIStrategicGoal::DevelopInfrastructure, pressure(AIStrategyAxis::InternalDevelopment) * (0.7 + p.economicFocus) + pressure(AIStrategyAxis::Technology) * (0.4 + p.planning)},
        {AIStrategicGoal::BuildMilitary,         pressure(AIStrategyAxis::Military) * (0.6 + p.militarism) + pressure(AIStrategyAxis::Risk) * 0.25},
        {AIStrategicGoal::LaunchOffensive,       offensiveReadiness * (0.4 + p.aggression + p.opportunism * 0.5) * riskGate},
        {AIStrategicGoal::Fortify,               pressure(AIStrategyAxis::Risk) * (0.55 + p.defensiveBias) + pressure(AIStrategyAxis::Military) * 0.3}
    };

    auto best = std::max_element(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.value < b.value; });
    if (best == candidates.end() || best->value <= 0.05)
        return goalState.goal;

    double currentValue = 0.0;
    for (const auto& c : candidates)
        if (c.goal == goalState.goal)
            currentValue = c.value;

    // Hysteresis: persistent AIs need a bigger margin to abandon their current goal.
    double switchThreshold = 0.12 + p.persistence * 0.22 - p.adaptability * 0.12;
    if (goalState.initialized && best->goal != goalState.goal && best->value < currentValue + switchThreshold)
        return goalState.goal;
    return best->goal;
}

std::vector<AIMilestone> PrimitiveAIModel::BuildMilestoneChain(AIStrategicGoal goal, const AIModelSettings& settings) const
{
    auto B = [](BuildingType t, int n, const char* label)
    { return AIMilestone{AIMilestoneKind::BuildingCount, t, ResourceType::Null, "", n, label}; };
    auto Rate = [](ResourceType r, int n, const char* label)
    { return AIMilestone{AIMilestoneKind::ProductionRate, BuildingType::Building, r, "", n, label}; };
    auto Stock = [](ResourceType r, int n, const char* label)
    { return AIMilestone{AIMilestoneKind::ResourceStock, BuildingType::Building, r, "", n, label}; };
    auto Army = [](int n, const char* label)
    { return AIMilestone{AIMilestoneKind::ArmyStrength, BuildingType::Building, ResourceType::Null, "", n, label}; };
    auto Tag = [](const char* tag, int n, const char* label)
    { return AIMilestone{AIMilestoneKind::TechWithTag, BuildingType::Building, ResourceType::Null, tag, n, label}; };

    bool aggressive = settings.personality.aggression > 0.55f || settings.personality.militarism > 0.55f;

    switch (goal)
    {
        case AIStrategicGoal::StabilizeEconomy:
            return {
                B(BuildingType::Woodcutter, 1, "first woodcutter"),
                B(BuildingType::Mine, 1, "first mine"),
                Rate(ResourceType::WOOD, 25, "wood flow"),
                B(BuildingType::LumberMill, 1, "lumber mill"),
                Rate(ResourceType::PLANKS, 8, "planks flow"),
                Stock(ResourceType::PLANKS, 20, "planks reserve")
            };
        case AIStrategicGoal::ExpandTerritory:
            return {
                B(BuildingType::GuardTower, 1, "border tower"),
                B(BuildingType::Woodcutter, 2, "extra extraction"),
                B(BuildingType::StorageBuilding, 1, "forward storage"),
                B(BuildingType::GuardTower, 2, "second tower")
            };
        case AIStrategicGoal::DevelopInfrastructure:
            return {
                B(BuildingType::StorageBuilding, 1, "storage"),
                B(BuildingType::Village, 1, "village"),
                B(BuildingType::University, 1, "university"),
                Tag("production", 1, "production tech"),
                B(BuildingType::Windmill, 1, "windmill"),
                B(BuildingType::Bakery, 1, "bakery")
            };
        case AIStrategicGoal::BuildMilitary:
            return {
                B(BuildingType::Barracks, 1, "barracks"),
                Army(40, "starter army"),
                Rate(ResourceType::FOOD_PROVISIONS, 4, "supply flow"),
                Army(80, "standing army"),
                B(BuildingType::GuardTower, aggressive ? 3 : 2, "defensive towers")
            };
        case AIStrategicGoal::LaunchOffensive:
            return {
                B(BuildingType::Barracks, 1, "barracks"),
                Army(60, "assault force"),
                Stock(ResourceType::FOOD_PROVISIONS, 15, "campaign supplies"),
                AIMilestone{AIMilestoneKind::AttackReady, BuildingType::Building, ResourceType::Null, "", 70, "strike"}
            };
        case AIStrategicGoal::Fortify:
            return {
                B(BuildingType::GuardTower, 2, "perimeter towers"),
                B(BuildingType::StorageBuilding, 2, "deep reserves"),
                B(BuildingType::Fortress, 1, "fortress"),
                Army(50, "garrison force")
            };
    }
    return {};
}

bool PrimitiveAIModel::IsMilestoneComplete(GameWorld& world, Player* player, const AIMilestone& m) const
{
    return MilestoneProgress(world, player, m) >= 1.0;
}

double PrimitiveAIModel::MilestoneProgress(GameWorld& world, Player* player, const AIMilestone& m) const
{
    if (player == nullptr)
        return 1.0;
    auto ratio = [](double have, double need) { return need <= 0.0 ? 1.0 : std::clamp(have / need, 0.0, 1.0); };

    switch (m.kind)
    {
        case AIMilestoneKind::BuildingCount:
            return ratio(CountCompletedOrQueuedBuildings(world, player, m.building), m.threshold);
        case AIMilestoneKind::ProductionRate:
            return ratio(GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute, m.resource), m.threshold);
        case AIMilestoneKind::ResourceStock:
            return ratio(CountStoredResource(world, player, m.resource), m.threshold);
        case AIMilestoneKind::ArmyStrength:
            return ratio(player->GetArmyRegistry().strength, m.threshold);
        case AIMilestoneKind::TechWithTag:
        {
            int count = 0;
            for (const auto& def : GetTechnologyDefinitions())
                if (player->technologies.HasTechnology(def.id) && HasTag(def, m.tag))
                    count++;
            return ratio(count, m.threshold);
        }
        case AIMilestoneKind::AttackReady:
        {
            ArmyRegistry army = player->GetArmyRegistry();
            bool supplied = army.supplyCapacity <= 0 || army.supply >= army.supplyCapacity * 0.4;
            return (army.strength >= m.threshold && supplied) ? 1.0 : ratio(army.strength, m.threshold) * 0.9;
        }
    }
    return 1.0;
}

int PrimitiveAIModel::FindActiveMilestone(GameWorld& world, Player* player, const std::vector<AIMilestone>& chain) const
{
    for (int i = 0; i < static_cast<int>(chain.size()); i++)
        if (!IsMilestoneComplete(world, player, chain[i]))
            return i;
    return static_cast<int>(chain.size());  // all complete
}

void PrimitiveAIModel::UpdateGoalState(GameWorld& world, Player* player, const AIStrategySnapshot& snapshot, const AIModelSettings& settings, double dt)
{
    AIStrategicGoal selected = SelectStrategicGoal(snapshot, settings);
    if (!goalState.initialized || selected != goalState.goal)
    {
        goalState.goal = selected;
        goalState.chain = BuildMilestoneChain(selected, settings);
        goalState.timeInGoal = 0.0;
        goalState.initialized = true;
    }
    else
    {
        goalState.timeInGoal += dt;
    }
    goalState.activeMilestone = FindActiveMilestone(world, player, goalState.chain);
    activePlan = snapshot.selectedPlan;  // keep legacy plan field in sync for any external readers
}

double PrimitiveAIModel::ForecastSecondsToAfford(GameWorld& world, Player* player, const std::vector<ResourceAmountDefinition>& costs) const
{
    double worst = 0.0;
    for (const auto& cost : costs)
    {
        int stored = CountStoredResource(world, player, cost.type);
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

double PrimitiveAIModel::MilestoneAlignment(const AIActionCandidate& candidate, const AIMilestone* m) const
{
    if (m == nullptr)
        return 1.0;
    switch (candidate.kind)
    {
        case AIActionKind::Build:
            if (m->kind == AIMilestoneKind::BuildingCount && candidate.building == m->building)
                return 2.6;
            if (m->kind == AIMilestoneKind::ProductionRate || m->kind == AIMilestoneKind::ResourceStock)
                for (const auto& opt : FindProducerOptions(m->resource))
                    if (opt.buildingType == candidate.building)
                        return m->kind == AIMilestoneKind::ProductionRate ? 2.4 : 2.0;
            return 0.85;
        case AIActionKind::Research:
            return m->kind == AIMilestoneKind::TechWithTag ? 2.2 : 1.1;
        case AIActionKind::Focus:
            return 1.2;
        case AIActionKind::Attack:
            return m->kind == AIMilestoneKind::AttackReady ? 3.0 : 0.9;
        case AIActionKind::Recruit:
            return (m->kind == AIMilestoneKind::ArmyStrength || m->kind == AIMilestoneKind::AttackReady) ? 2.3 : 1.0;
    }
    return 1.0;
}

Building* PrimitiveAIModel::FindUniversity(GameWorld& world, Player* player) const
{
    for (auto* building : player->GetTrackedBuildings())
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

Building* PrimitiveAIModel::FindRecruitmentBarracks(GameWorld& world, Player* player) const
{
    Building* best = nullptr;
    int fewestQueued = std::numeric_limits<int>::max();
    for (auto* building : player->GetTrackedBuildings())
    {
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;
        if (building->buildingType != BuildingType::Barracks)
            continue;
        const auto* recruitment = building->GetComponent<RecruitmentComponent>();
        if (recruitment == nullptr || building->GetComponent<GarrisonComponent>() == nullptr)
            continue;
        int queued = static_cast<int>(recruitment->queue.size());
        if (queued < fewestQueued)
        {
            fewestQueued = queued;
            best = building;
        }
    }
    return fewestQueued <= 2 ? best : nullptr;  // don't pile up endless recruitment jobs
}

std::string PrimitiveAIModel::SelectResearchTarget(GameWorld& world, Player* player, const AIStrategySnapshot& snapshot, const AIModelSettings& settings) const
{
    const TechnologyDefinition* best = nullptr;
    double bestScore = 0.0;
    for (const auto& def : GetTechnologyDefinitions())
    {
        if (player->technologies.HasTechnology(def.id) || player->IsTechnologyInProgress(def.id))
            continue;
        if (recentResearchOrders.contains(def.id) || !player->CanResearchTechnology(def.id))
            continue;

        double s = 0.4;
        auto tag = [&](const char* t, AIStrategyAxis ax, double w) { if (HasTag(def, t)) s += snapshot.GetPressure(ax) * w; };
        tag("production", AIStrategyAxis::Resources, 1.0);
        tag("wood", AIStrategyAxis::Resources, 0.4);
        tag("iron", AIStrategyAxis::Resources, 0.6);
        tag("logistics", AIStrategyAxis::Logistics, 1.0);
        tag("roads", AIStrategyAxis::Logistics, 0.6);
        tag("military", AIStrategyAxis::Military, 1.0);
        tag("population", AIStrategyAxis::InternalDevelopment, 0.8);
        tag("government", AIStrategyAxis::InternalDevelopment, 0.6);
        tag("research", AIStrategyAxis::Technology, 0.7);
        s += std::max(0.0, 200.0 - def.researchTime) / 600.0;
        if (s > bestScore)
        {
            bestScore = s;
            best = &def;
        }
    }
    return best != nullptr ? best->id : std::string{};
}

std::string PrimitiveAIModel::SelectFocusTarget(GameWorld& world, Player* player, const AIStrategySnapshot& snapshot, const AIModelSettings& settings) const
{
    if (!player->focuses.GetActiveFocusId().empty())
        return std::string{};

    const TechnologyDefinition* best = nullptr;
    double bestScore = 0.0;
    for (const auto& def : GetFocusDefinitions())
    {
        if (!player->CanUnlockFocus(def.id))
            continue;

        double s = 0.5;
        auto tag = [&](const char* t, AIStrategyAxis ax, double w) { if (HasTag(def, t)) s += snapshot.GetPressure(ax) * w; };
        auto lane = [&](const char* l, AIStrategyAxis ax, double w)
        { if (def.category == l || def.layoutLane == l) s += snapshot.GetPressure(ax) * w; };
        lane("PRODUCTION", AIStrategyAxis::Resources, 0.9 + settings.personality.economicFocus);
        lane("MILITARY", AIStrategyAxis::Military, 0.9 + settings.personality.militarism);
        lane("WARFARE", AIStrategyAxis::Military, 0.9 + settings.personality.militarism);
        lane("POLITICS", AIStrategyAxis::InternalDevelopment, 0.8 + settings.personality.planning);
        tag("production", AIStrategyAxis::Resources, 1.0);
        tag("logistics", AIStrategyAxis::Logistics, 1.0);
        tag("military", AIStrategyAxis::Military, 0.9);
        tag("population", AIStrategyAxis::InternalDevelopment, 0.8);
        tag("government", AIStrategyAxis::InternalDevelopment, 0.6);
        s += std::max(0.0, 260.0 - def.researchTime) / 900.0;
        if (s > bestScore)
        {
            bestScore = s;
            best = &def;
        }
    }
    return best != nullptr ? best->id : std::string{};
}

std::vector<AIActionCandidate> PrimitiveAIModel::GatherActionCandidates(GameWorld& world, Player* player, const AIStrategySnapshot& snapshot, const AIModelSettings& settings) const
{
    std::vector<AIActionCandidate> candidates;
    auto terrainFor = [](BuildingType t) -> TileType
    {
        switch (t)
        {
            case BuildingType::Woodcutter: return TileType::WOOD;
            case BuildingType::Mine:       return TileType::STONE;
            default:                       return TileType::GRASS;
        }
    };
    auto addBuild = [&](BuildingType type, bool allowUnaffordable)
    {
        if (type == BuildingType::Building || recentBuildOrders.contains(type))
            return;
        const auto& def = GetBuildingDefinition(type);
        bool affordable = player->CanBuildDefinition(def);
        if (!affordable && !(allowUnaffordable && MeetsUnlockGate(player, def)))
            return;
        AIActionCandidate c;
        c.kind = AIActionKind::Build;
        c.building = type;
        c.terrain = terrainFor(type);
        candidates.push_back(std::move(c));
    };

    const AIMilestone* active = (goalState.activeMilestone < static_cast<int>(goalState.chain.size()))
        ? &goalState.chain[goalState.activeMilestone] : nullptr;

    // (a) Active-milestone target — committed even if not yet affordable (save-up planning).
    if (active != nullptr)
    {
        if (active->kind == AIMilestoneKind::BuildingCount)
            addBuild(active->building, true);
        else if (active->kind == AIMilestoneKind::ProductionRate || active->kind == AIMilestoneKind::ResourceStock)
            for (const auto& opt : FindProducerOptions(active->resource))
                addBuild(opt.buildingType, true);
    }

    // (b) Resource-diagnosis producers — opportunistic, must be affordable now.
    for (ResourceType resource : resourceTypes)
    {
        AIResourceDiagnosis diag = DiagnoseResourceNeed(world, player, resource, 0);
        if (diag.urgency <= 0.15)
            continue;
        bool bottleneckOnly = (diag.storageProblem || diag.logisticsProblem) && diag.missingInputs.empty();
        if (bottleneckOnly)
        {
            addBuild(BuildingType::StorageBuilding, false);
            continue;
        }
        for (const auto& opt : FindProducerOptions(diag.resource))
            addBuild(opt.buildingType, false);
        for (ResourceType input : diag.missingInputs)
            for (const auto& opt : FindProducerOptions(input))
                addBuild(opt.buildingType, false);
    }

    // (c) Standing infrastructure options.
    addBuild(BuildingType::StorageBuilding, false);
    addBuild(BuildingType::Village, false);
    addBuild(BuildingType::University, false);
    addBuild(BuildingType::GuardTower, false);
    if (settings.personality.militarism > 0.4f || goalState.goal == AIStrategicGoal::BuildMilitary
        || goalState.goal == AIStrategicGoal::LaunchOffensive)
        addBuild(BuildingType::Barracks, false);

    // (d) Research — needs a free university.
    if (FindUniversity(world, player) != nullptr)
    {
        std::string techId = SelectResearchTarget(world, player, snapshot, settings);
        if (!techId.empty())
        {
            AIActionCandidate c;
            c.kind = AIActionKind::Research;
            c.researchId = techId;
            candidates.push_back(std::move(c));
        }
    }

    // (e) Focus — one decision-tree slot at a time.
    {
        std::string focusId = SelectFocusTarget(world, player, snapshot, settings);
        if (!focusId.empty())
        {
            AIActionCandidate c;
            c.kind = AIActionKind::Focus;
            c.researchId = focusId;
            candidates.push_back(std::move(c));
        }
    }

    // (f) Recruit — when we have a barracks with spare queue and the manpower.
    if (player->strategicResources.Get(StrategicResourceType::Manpower) >= 1.0)
    {
        Building* barracks = FindRecruitmentBarracks(world, player);
        if (barracks != nullptr)
        {
            AIActionCandidate c;
            c.kind = AIActionKind::Recruit;
            c.sourceTileId = barracks->positionId;
            c.unitType = MilitaryUnitType::Militia;
            candidates.push_back(std::move(c));
        }
    }

    // (g) Attack — when offensive-minded and an enemy is reachable.
    if (attackTimer <= 0.0)
    {
        Building* bestMilitary = FindBestMilitary(world, player);
        Building* enemy = bestMilitary != nullptr ? FindNearestEnemyMilitary(world, player, bestMilitary) : nullptr;
        if (bestMilitary != nullptr && enemy != nullptr)
        {
            AIActionCandidate c;
            c.kind = AIActionKind::Attack;
            c.sourceTileId = bestMilitary->positionId;
            c.targetTileId = enemy->positionId;
            candidates.push_back(std::move(c));
        }
    }

    return candidates;
}

double PrimitiveAIModel::ScoreAction(GameWorld& world, Player* player, const AIActionCandidate& candidate, const AIStrategySnapshot& snapshot, const AIModelSettings& settings) const
{
    const AIMilestone* active = (goalState.activeMilestone < static_cast<int>(goalState.chain.size()))
        ? &goalState.chain[goalState.activeMilestone] : nullptr;
    double alignment = MilestoneAlignment(candidate, active);
    AIStrategyAxis goalAxis = GoalPrimaryAxis(goalState.goal);

    // Deterministic tie-break noise (lockstep-safe — no RNG in the sim path).
    double seed = std::sin(static_cast<double>(player->id * 131 + static_cast<int>(candidate.kind) * 53
                  + static_cast<int>(candidate.building) * 17 + goalState.activeMilestone * 7));
    double noise = 0.95 + (seed + 1.0) * 0.05 * (1.0 - settings.personality.planning);

    switch (candidate.kind)
    {
        case AIActionKind::Build:
        {
            const auto& def = GetBuildingDefinition(candidate.building);
            auto affinity = GetBuildingAxisAffinity(candidate.building);
            double need = 0.30;
            for (int i = 0; i < 8; i++)
            {
                AIStrategyAxis axis = static_cast<AIStrategyAxis>(i);
                need += snapshot.GetPressure(axis) * affinity[i] * PersonalityAxisWeight(settings.personality, axis);
            }
            double seconds = ForecastSecondsToAfford(world, player, player->GetEffectiveBuildCosts(def));
            double feasibility = seconds >= 1e8 ? 0.04 : 1.0 / (1.0 + seconds / 25.0);
            double goalBoost = affinity[static_cast<int>(goalAxis)] > 0.5f ? 1.35 : 1.0;
            int existing = CountOwnedBuildings(world, player, candidate.building);
            double existingPenalty = 1.0 / (1.0 + existing * 0.22);
            double base = 12.0;
            return base * need * feasibility * alignment * goalBoost * existingPenalty * noise;
        }
        case AIActionKind::Research:
        {
            double pressure = snapshot.GetPressure(AIStrategyAxis::Technology);
            double base = 14.0 + pressure * 10.0;
            double goalBoost = goalState.goal == AIStrategicGoal::DevelopInfrastructure ? 1.4 : 1.0;
            return base * (0.6 + settings.personality.planning) * alignment * goalBoost * noise;
        }
        case AIActionKind::Focus:
        {
            double base = 12.0;
            double drive = 0.7 + settings.personality.planning * 0.5;
            return base * drive * alignment * noise;
        }
        case AIActionKind::Recruit:
        {
            ArmyRegistry army = player->GetArmyRegistry();
            double militaryPressure = snapshot.GetPressure(AIStrategyAxis::Military);
            double base = 9.0 + militaryPressure * 16.0;
            double goalBoost = (goalState.goal == AIStrategicGoal::BuildMilitary || goalState.goal == AIStrategicGoal::LaunchOffensive) ? 1.5 : 0.8;
            double supplyCap = army.supplyCapacity > 0 && army.supply < army.supplyCapacity * 0.2 ? 0.4 : 1.0;
            return base * (0.5 + settings.personality.militarism) * alignment * goalBoost * supplyCap * noise;
        }
        case AIActionKind::Attack:
        {
            double readiness = std::clamp(static_cast<double>(snapshot.GetAxisScore(AIStrategyAxis::Military)), 0.0, 1.0) * 0.5
                             + std::clamp(static_cast<double>(snapshot.GetAxisScore(AIStrategyAxis::Diplomacy)), 0.0, 1.0) * 0.5;
            if (snapshot.GetAxisScore(AIStrategyAxis::Risk) < -0.4f)
                readiness *= 0.3;  // don't march out while the home front is collapsing
            double base = 8.0 + readiness * 40.0;
            double goalBoost = goalState.goal == AIStrategicGoal::LaunchOffensive ? 1.6 : 0.7;
            return base * (0.3 + settings.personality.aggression + settings.personality.opportunism * 0.4) * alignment * goalBoost * noise;
        }
    }
    return 0.0;
}

bool PrimitiveAIModel::ExecuteAction(GameWorld& world, Player* player, const AIActionCandidate& candidate)
{
    switch (candidate.kind)
    {
        case AIActionKind::Build:
        {
            Vec2i anchor = FindBuildAnchor(world, player, candidate.building, candidate.terrain, nullptr);
            if (anchor.x < 0)
                return false;
            // Only spend if we can actually afford it right now; otherwise we're still saving up.
            if (!player->CanBuildDefinition(GetBuildingDefinition(candidate.building)))
                return false;
            return TrySubmitBuild(world, player, candidate.building, anchor);
        }
        case AIActionKind::Research:
        {
            Building* university = FindUniversity(world, player);
            if (university == nullptr || candidate.researchId.empty())
                return false;
            world.SubmitCommand(GameCommand::StartTechnologyResearch(player->id, candidate.researchId, university->positionId));
            recentResearchOrders[candidate.researchId] = 30.0;
            return true;
        }
        case AIActionKind::Focus:
        {
            if (candidate.researchId.empty())
                return false;
            world.SubmitCommand(GameCommand::StartFocus(player->id, candidate.researchId));
            return true;
        }
        case AIActionKind::Recruit:
        {
            if (candidate.sourceTileId < 0)
                return false;
            world.SubmitCommand(GameCommand::RecruitUnit(player->id, candidate.sourceTileId, candidate.unitType));
            return true;
        }
        case AIActionKind::Attack:
        {
            if (candidate.sourceTileId < 0 || candidate.targetTileId < 0)
                return false;
            // attackTimer is reset by the caller (RunUnifiedDecision) before dispatch.
            // Combat is division-based: order the source garrison's divisions to
            // march on and siege the target (divisionId < 0 = whole garrison).
            world.SubmitCommand(GameCommand::IssueMilitaryOrder(
                player->id, MilitaryOrderType::Attack,
                candidate.sourceTileId, candidate.targetTileId));
            return true;
        }
    }
    return false;
}

bool PrimitiveAIModel::RunUnifiedDecision(GameWorld& world, Player* player, const AIStrategySnapshot& snapshot, const AIModelSettings& settings)
{
    std::vector<AIActionCandidate> candidates = GatherActionCandidates(world, player, snapshot, settings);
    if (candidates.empty())
        return TryBuildEconomy(world, player, settings);  // safety net: legacy economic engine

    for (auto& c : candidates)
        c.score = ScoreAction(world, player, c, snapshot, settings);
    std::sort(candidates.begin(), candidates.end(),
        [](const AIActionCandidate& a, const AIActionCandidate& b) { return a.score > b.score; });

    for (const auto& c : candidates)
    {
        if (c.score <= 0.0)
            break;
        if (c.kind == AIActionKind::Attack)
            attackTimer = 8.0 + (1.0 - settings.personality.aggression) * 8.0;
        if (ExecuteAction(world, player, c))
            return true;
    }
    return TryBuildEconomy(world, player, settings);
}

AIStrategicPlan PrimitiveAIModel::SelectStrategicPlan(const AIStrategySnapshot& snapshot, const AIModelSettings& settings) const
{
    struct Candidate
    {
        AIStrategicPlan plan;
        double score;
    };

    auto p = [&](AIStrategyAxis axis) -> double { return snapshot.GetPressure(axis); };

    std::vector<Candidate> candidates{
        {AIStrategicPlan::RecoverEconomy,       p(AIStrategyAxis::Resources) * (0.9 + settings.personality.economicFocus)},
        {AIStrategicPlan::FixLogistics,         p(AIStrategyAxis::Logistics) * (0.9 + settings.personality.logisticsAwareness)},
        {AIStrategicPlan::BuildArmy,            p(AIStrategyAxis::Military)  * (0.7 + settings.personality.militarism)},
        {AIStrategicPlan::DefendBorder,         std::max(p(AIStrategyAxis::Military), p(AIStrategyAxis::Risk)) * (0.7 + settings.personality.defensiveBias)},
        {AIStrategicPlan::PrepareOffensive,     std::max(p(AIStrategyAxis::Military), p(AIStrategyAxis::Diplomacy)) * (0.5 + settings.personality.aggression + settings.personality.opportunism * 0.5)},
        {AIStrategicPlan::ExpandForResources,   std::max(p(AIStrategyAxis::Expansion), p(AIStrategyAxis::Resources)) * (0.6 + settings.personality.expansionism)},
        {AIStrategicPlan::DevelopPopulation,    p(AIStrategyAxis::InternalDevelopment) * (0.7 + settings.personality.economicFocus)},
        {AIStrategicPlan::ResearchSpecialization, p(AIStrategyAxis::Technology) * (0.8 + settings.personality.planning)},
        {AIStrategicPlan::ConsolidateTerritory, std::max(p(AIStrategyAxis::Risk), p(AIStrategyAxis::Expansion)) * (0.6 + settings.personality.planning + settings.personality.defensiveBias * 0.5)}
    };

    auto best = std::max_element(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
    {
        return a.score < b.score;
    });
    if (best == candidates.end() || best->score <= 0.05)
        return activePlan;

    double currentScore = 0.0;
    for (const auto& candidate : candidates)
        if (candidate.plan == activePlan)
            currentScore = candidate.score;

    double switchThreshold = 0.08 + settings.personality.persistence * 0.18 - settings.personality.adaptability * 0.10;
    if (best->plan != activePlan && best->score < currentScore + switchThreshold)
        return activePlan;
    return best->plan;
}

// Initializes PrimitiveAIModel::CountOwnedBuildings.
int PrimitiveAIModel::CountOwnedBuildings(GameWorld& world, Player* player, BuildingType type) const
{
    int count = 0;
    for (auto& tile : world.tilemap.tilemap)
    {
        Building* building = tile.building.get();
        if (building != nullptr && building->owner == player && building->buildingType == type)
            count++;
    }
    return count;
}

int PrimitiveAIModel::CountCompletedOrQueuedBuildings(GameWorld& world, Player* player, BuildingType type) const
{
    int count = 0;
    for (auto& tile : world.tilemap.tilemap)
    {
        Building* building = tile.building.get();
        if (building != nullptr && building->owner == player && building->buildingType == type)
            count++;
    }
    return count;
}

// Initializes PrimitiveAIModel::CountStoredResource.
int PrimitiveAIModel::CountStoredResource(GameWorld& world, Player* player, ResourceType type) const
{
    (void)world;
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

int PrimitiveAIModel::GetResourceRate(const std::map<ResourceType, int>& rates, ResourceType type) const
{
    auto it = rates.find(type);
    return it != rates.end() ? it->second : 0;
}

std::vector<AIProducerOption> PrimitiveAIModel::FindProducerOptions(ResourceType resource) const
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

AIResourceDiagnosis PrimitiveAIModel::DiagnoseResourceNeed(GameWorld& world, Player* player, ResourceType resource, int depth) const
{
    AIResourceDiagnosis diagnosis;
    diagnosis.resource = resource;
    if (player == nullptr || resource == ResourceType::Null)
        return diagnosis;

    int produced = GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute, resource);
    int consumed = GetResourceRate(player->economyTelemetry.current.consumptionRatesPerMinute, resource);
    int stored = CountStoredResource(world, player, resource);
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
            int inputStored = CountStoredResource(world, player, input.type);
            if (inputStored < input.amount * 2 || inputProduced < inputConsumed)
            {
                if (std::find(diagnosis.missingInputs.begin(), diagnosis.missingInputs.end(), input.type) == diagnosis.missingInputs.end())
                    diagnosis.missingInputs.push_back(input.type);
            }
        }
    }

    return diagnosis;
}

double PrimitiveAIModel::ScoreProducerOption(GameWorld& world, Player* player, const AIResourceDiagnosis& diagnosis, const AIProducerOption& option, const AIModelSettings& settings) const
{
    if (player == nullptr || diagnosis.urgency <= 0.0)
        return 0.0;
    const auto& definition = GetBuildingDefinition(option.buildingType);
    if (!player->CanBuildDefinition(definition))
        return 0.0;

    double feasibility = 1.0;
    for (const auto& cost : definition.buildCosts)
    {
        int stored = CountStoredResource(world, player, cost.type);
        if (stored < cost.amount)
            feasibility *= std::max(0.15, stored / static_cast<double>(std::max(1, cost.amount)));
    }
    for (const auto& input : option.inputs)
    {
        int inputProduced = GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute, input.type);
        if (inputProduced <= 0)
            return 0.0;
    }

    double personality = 0.85 + settings.personality.economicFocus * 0.35 + settings.personality.planning * 0.15;
    if (diagnosis.logisticsProblem || diagnosis.storageProblem)
        personality *= 0.75 + settings.personality.logisticsAwareness * 0.45;
    if (diagnosis.manpowerProblem)
        personality *= 0.70 + settings.personality.planning * 0.25;

    double existingPenalty = 1.0 / (1.0 + CountOwnedBuildings(world, player, option.buildingType) * 0.18);
    double noiseSeed = std::sin(static_cast<double>(player->id * 97 + static_cast<int>(option.buildingType) * 31 + static_cast<int>(diagnosis.resource) * 13 + recentBuildOrders.size()));
    double noise = 0.92 + (noiseSeed + 1.0) * 0.08 * (1.0 - settings.personality.planning);

    AIActionUtility utility;
    utility.baseValue = std::max(12.0, option.outputPerMinute * 8.0);
    utility.need = diagnosis.urgency;
    utility.personalityModifier = personality * noise;
    utility.feasibility = feasibility;
    utility.urgency = 1.0 + diagnosis.urgency;
    utility.planModifier = activePlan == AIStrategicPlan::RecoverEconomy || activePlan == AIStrategicPlan::ExpandForResources ? 1.15 : 1.0;
    return utility.Score() * existingPenalty;
}

bool PrimitiveAIModel::TrySubmitBuild(GameWorld& world, Player* player, BuildingType type, Vec2i anchor)
{
    if (player == nullptr || anchor.x < 0)
        return false;
    if (recentBuildOrders.contains(type))
        return false;

    world.SubmitCommand(GameCommand::BuildBuilding(player->id, type, anchor));
    recentBuildOrders[type] = type == BuildingType::Road ? 3.0 : 8.0;
    return true;
}

// Finds the best matching runtime object.
Vec2i PrimitiveAIModel::FindBuildAnchor(GameWorld& world, Player* player, BuildingType type, TileType preferredTile, const Building* target) const
{
    const auto& definition = GetBuildingDefinition(type);
    Vec2i bestAnchor{-1, -1};
    int bestScore = std::numeric_limits<int>::max();
    Vec2i targetPos{};
    bool hasTarget = target != nullptr;
    if (hasTarget)
        targetPos = world.tilemap.GetCoordsFromId(target->positionId);
    Building* headquarters = FindOwnedHeadquarters(player);
    Vec2i headquartersPos{};
    bool hasHeadquarters = headquarters != nullptr;
    if (hasHeadquarters)
        headquartersPos = world.tilemap.GetCoordsFromId(headquarters->positionId);

    for (const auto& tile : world.tilemap.tilemap)
    {
        if (tile.owner != player || tile.HasBuilding())
            continue;

        Vec2i pos = world.tilemap.GetCoordsFromId(tile.id);
        if (!world.tilemap.CanPlaceBuilding(type, pos, definition.footprint, player))
            continue;

        if (preferredTile != TileType::GRASS && world.tilemap[world.tilemap.GetIdFromCoords(pos)].tileType != preferredTile)
            continue;

        if (preferredTile == TileType::GRASS)
        {
            bool terrainMatches = true;
            for (int y = 0; y < definition.footprint.y && terrainMatches; y++)
                for (int x = 0; x < definition.footprint.x && terrainMatches; x++)
                    terrainMatches = world.tilemap[{pos.x + x, pos.y + y}].tileType == TileType::GRASS;

            if (!terrainMatches)
                continue;
        }

        int distanceToTarget = hasTarget ? std::abs(pos.x - targetPos.x) + std::abs(pos.y - targetPos.y) : 0;
        int distanceToHeadquarters = hasHeadquarters ? std::abs(pos.x - headquartersPos.x) + std::abs(pos.y - headquartersPos.y) : 0;
        int infrastructureDistance = DistanceToNearestInfrastructure(world.tilemap, player, pos);
        int borderPenalty = 0;
        const std::array<Vec2i, 4> neighbours{
            Vec2i{pos.x + 1, pos.y},
            Vec2i{pos.x - 1, pos.y},
            Vec2i{pos.x, pos.y + 1},
            Vec2i{pos.x, pos.y - 1}
        };
        for (Vec2i neighbour : neighbours)
        {
            if (!world.tilemap.IsInside(neighbour))
            {
                borderPenalty += 10;
                continue;
            }
            if (world.tilemap[world.tilemap.GetIdFromCoords(neighbour)].owner != player)
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
    }

    return bestAnchor;
}

// Finds the best matching runtime object.
Building* PrimitiveAIModel::FindNearestRoadTarget(GameWorld& world, Player* player, const Building* source) const
{
    Building* bestInfrastructure = nullptr;
    int bestInfrastructureDistance = std::numeric_limits<int>::max();
    for (auto& tile : world.tilemap.tilemap)
    {
        Building* building = tile.building.get();
        if (building == nullptr || building == source || building->owner != player || building->IsUnderConstruction())
            continue;
        if (building->buildingType != BuildingType::Road && !building->IsStorageLike() && building->buildingType != BuildingType::Headquarters)
            continue;

        int distance = TileDistance(world.tilemap, source, building);
        if (distance < bestInfrastructureDistance)
        {
            bestInfrastructureDistance = distance;
            bestInfrastructure = building;
        }
    }
    if (bestInfrastructure != nullptr)
        return bestInfrastructure;

    Building* bestReceiver = nullptr;
    int bestReceiverDistance = std::numeric_limits<int>::max();
    for (const auto& receiver : source->GetReceiverViews())
    {
        Building* building = receiver.building;
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;

        int distance = TileDistance(world.tilemap, source, building);
        if (distance < bestReceiverDistance)
        {
            bestReceiverDistance = distance;
            bestReceiver = building;
        }
    }
    if (bestReceiver != nullptr)
        return bestReceiver;

    Building* bestConsumer = nullptr;
    int bestConsumerDistance = std::numeric_limits<int>::max();
    for (const auto& output : source->GetOutputBufferViews())
    {
        for (auto& tile : world.tilemap.tilemap)
        {
            Building* building = tile.building.get();
            if (building == nullptr || building == source || building->owner != player || building->IsUnderConstruction())
                continue;
            if (building->IsStorageLike() || !building->CanAcceptResource(output.type))
                continue;

            int distance = TileDistance(world.tilemap, source, building);
            if (distance < bestConsumerDistance)
            {
                bestConsumerDistance = distance;
                bestConsumer = building;
            }
        }
    }
    if (bestConsumer != nullptr)
        return bestConsumer;

    Building* best = nullptr;
    int bestDistance = std::numeric_limits<int>::max();
    for (auto& tile : world.tilemap.tilemap)
    {
        Building* building = tile.building.get();
        if (building == nullptr || building == source || building->owner != player || building->IsUnderConstruction())
            continue;
        if (building->buildingType != BuildingType::Road && !building->IsStorageLike())
            continue;

        int distance = TileDistance(world.tilemap, source, building);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = building;
        }
    }
    return best;
}

Building* PrimitiveAIModel::FindNearestStorageConnectedRoad(GameWorld& world, Player* player, const Building* source) const
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

    Vec2i sourcePos = world.tilemap.GetCoordsFromId(source->positionId);
    Building* bestRoad = nullptr;
    int bestDistance = std::numeric_limits<int>::max();
    for (auto& tile : world.tilemap.tilemap)
    {
        Building* road = tile.building.get();
        if (road == nullptr || road->owner != player || road->IsUnderConstruction() || road->buildingType != BuildingType::Road)
            continue;

        bool connectedToStorage = false;
        for (Building* storage : storageNodes)
        {
            if (HasRoadConnection(world, player, storage, road))
            {
                connectedToStorage = true;
                break;
            }
        }
        if (!connectedToStorage)
            continue;

        Vec2i roadPos = world.tilemap.GetCoordsFromId(road->positionId);
        int distance = std::abs(sourcePos.x - roadPos.x) + std::abs(sourcePos.y - roadPos.y);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestRoad = road;
        }
    }

    return bestRoad;
}

// Returns whether this condition is currently true.
bool PrimitiveAIModel::HasAdjacentRoad(GameWorld& world, const Building* building) const
{
    if (building == nullptr)
        return false;

    for (int tileId : world.tilemap.GetAdjacentTileIds(building))
    {
        Building* neighbour = world.tilemap.GetBuilding(tileId);
        if (neighbour != nullptr && neighbour->buildingType == BuildingType::Road)
            return true;
    }
    return false;
}

// Returns whether this condition is currently true.
bool PrimitiveAIModel::HasRoadConnection(GameWorld& world, Player* player, const Building* source, const Building* target) const
{
    if (player == nullptr || source == nullptr || target == nullptr || player->roadNetwork == nullptr)
        return false;

    if (source->IsUnderConstruction() || target->IsUnderConstruction())
        return false;

    auto path = player->roadNetwork->CalculatePath(const_cast<Building*>(source), const_cast<Building*>(target));
    return !path.empty();
}

// Submits this command to the simulation.
bool PrimitiveAIModel::SubmitRoadPath(GameWorld& world, Player* player, const Building* source, const Building* target)
{
    if (player == nullptr || source == nullptr || target == nullptr)
        return false;

    std::vector<int> startIds = world.tilemap.GetAdjacentTileIds(source);
    std::vector<int> goalIds = world.tilemap.GetAdjacentTileIds(target);
    if (target->buildingType == BuildingType::Road)
        goalIds.push_back(target->positionId);

    auto canUseRoadPathTile = [&](int tileId)
    {
        if (tileId < 0 || tileId >= static_cast<int>(world.tilemap.tilemap.size()))
            return false;
        if (reservedRoadTiles.contains(tileId))
            return false;

        Tile& tile = world.tilemap[tileId];
        if (tile.owner != player)
            return false;

        Building* building = tile.GetBuilding();
        return building == nullptr || building->buildingType == BuildingType::Road;
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

        Vec2i pos = world.tilemap.GetCoordsFromId(current);
        const std::array<Vec2i, 4> neighbours{
            Vec2i{pos.x + 1, pos.y},
            Vec2i{pos.x - 1, pos.y},
            Vec2i{pos.x, pos.y + 1},
            Vec2i{pos.x, pos.y - 1}
        };

        for (Vec2i nextPos : neighbours)
        {
            if (!world.tilemap.IsInside(nextPos))
                continue;

            int nextId = world.tilemap.GetIdFromCoords(nextPos);
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
    for (int tileId : path)
    {
        Building* building = world.tilemap.GetBuilding(tileId);
        if (building != nullptr && building->buildingType == BuildingType::Road)
            existingRoadTiles++;
        else if (building == nullptr)
            newRoadTiles++;
    }
    if (newRoadTiles <= 0)
        return false;
    if (newRoadTiles > 8)
        return false;

    bool submitted = false;
    int submittedCount = 0;
    constexpr int maxRoadCommandsPerTick = 8;
    for (int tileId : path)
    {
        Building* building = world.tilemap.GetBuilding(tileId);
        if (building != nullptr)
            continue;
        if (reservedRoadTiles.contains(tileId))
            continue;

        world.SubmitCommand(GameCommand::BuildBuilding(player->id, BuildingType::Road, world.tilemap.GetCoordsFromId(tileId)));
        reservedRoadTiles[tileId] = 6.0;
        submitted = true;
        submittedCount++;
        if (submittedCount >= maxRoadCommandsPerTick)
            break;
    }

    return submitted;
}

// Finds the best matching runtime object.
Building* PrimitiveAIModel::FindBestMilitary(GameWorld& world, Player* player) const
{
    Building* bestMilitary = nullptr;
    int bestSourceStrength = -1;
    (void)world;
    if (player == nullptr)
        return nullptr;

    for (Building* building : player->GetTrackedBuildingsWithComponent<GarrisonComponent>())
    {
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;

        const auto* territory = building->GetComponent<TerritoryComponent>();
        const auto* garrison = building->GetComponent<GarrisonComponent>();
        if (territory == nullptr || garrison == nullptr || territory->hp <= 0)
            continue;

        int strength = garrison->GetEffectiveStrength(*building);
        if (strength > bestSourceStrength)
        {
            bestSourceStrength = strength;
            bestMilitary = building;
        }
    }
    return bestMilitary;
}

// Finds the best matching runtime object.
Building* PrimitiveAIModel::FindNearestEnemyMilitary(GameWorld& world, Player* player, const Building* source) const
{
    Building* nearestEnemy = nullptr;
    int bestDistance = std::numeric_limits<int>::max();
    for (auto& tile : world.tilemap.tilemap)
    {
        Building* building = tile.building.get();
        if (building == nullptr || building->owner == player || building->IsUnderConstruction())
            continue;

        // Only military targets (defensive works + HQ) are attack destinations —
        // civil buildings like the Barracks factory are not.
        if (!IsMilitaryAttackTarget(*building))
            continue;
        const auto* territory = building->GetComponent<TerritoryComponent>();
        if (territory == nullptr || territory->hp <= 0)
            continue;

        int distance = TileDistance(world.tilemap, source, building);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            nearestEnemy = building;
        }
    }
    return nearestEnemy;
}
