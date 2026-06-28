#include "../inc/Controller.h"
#include "../inc/GameWorld.h"

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
    TryStartStrategicFocus(world, player, strategy, settings);
    roadTimer -= dt;
    economyTimer -= dt;
    militaryTimer -= dt;
    attackTimer -= dt;
    for (auto it = reservedRoadTiles.begin(); it != reservedRoadTiles.end();)
    {
        it->second -= dt;
        if (it->second <= 0.0)
            it = reservedRoadTiles.erase(it);
        else
            ++it;
    }
    for (auto it = recentBuildOrders.begin(); it != recentBuildOrders.end();)
    {
        it->second -= dt;
        if (it->second <= 0.0)
            it = recentBuildOrders.erase(it);
        else
            ++it;
    }

    if (roadTimer <= 0.0)
    {
        roadTimer = strategy.GetUrgency(AIStrategyAxis::Logistics) > 0.55f ? 1.25 : 2.0;
        if (TryBuildRoads(world, player))
            return;
    }

    if (economyTimer <= 0.0)
    {
        economyTimer = 2.0;
        if (TryBuildStrategicStep(world, player, settings) || TryBuildEconomy(world, player, settings))
            return;
    }

    if (militaryTimer <= 0.0)
    {
        militaryTimer = strategy.GetUrgency(AIStrategyAxis::Military) > 0.55f ? 3.0 : 5.0;
        TryBuildMilitary(world, player, settings);
        return;
    }
}

bool PrimitiveAIModel::TryBuildStrategicStep(GameWorld& world, Player* player, const AIModelSettings& settings)
{
    if (player == nullptr)
        return false;

    struct Step
    {
        BuildingType type;
        TileType terrain;
        int targetCount;
    };

    std::vector<Step> opening{
        {BuildingType::Woodcutter, TileType::WOOD, 1},
        {BuildingType::Mine, TileType::STONE, 1},
        {BuildingType::LumberMill, TileType::GRASS, 1},
        {BuildingType::WheatFarm, TileType::GRASS, 1},
        {BuildingType::Well, TileType::GRASS, 1},
        {BuildingType::Windmill, TileType::GRASS, 1},
        {BuildingType::HuntersHut, TileType::GRASS, 1}
    };
    bool aggressiveOpening = settings.personality.aggression > 0.62f || settings.personality.militarism > 0.62f;
    AIMapAssessment map = AssessMap(world, player);
    bool strategicAggression = aggressiveOpening || (map.militaryOpportunity > 0.55 && settings.personality.aggression > 0.42f);
    if (aggressiveOpening)
    {
        opening.push_back({BuildingType::Barracks, TileType::GRASS, 1});
        opening.push_back({BuildingType::GuardTower, TileType::GRASS, 1});
    }
    opening.push_back({BuildingType::Bakery, TileType::GRASS, 1});
    opening.push_back({BuildingType::Inn, TileType::GRASS, 1});

    auto tryBuildCostSupport = [&](const BuildingDefinition& blockedDefinition)
    {
        for (const auto& cost : blockedDefinition.buildCosts)
        {
            if (CountStoredResource(world, player, cost.type) >= cost.amount)
                continue;

            BuildingType supportType = BuildingType::Building;
            TileType supportTerrain = TileType::GRASS;
            int supportCap = 1;
            switch (cost.type)
            {
                case ResourceType::WOOD:
                    supportType = BuildingType::Woodcutter;
                    supportTerrain = TileType::WOOD;
                    supportCap = 2;
                    break;
                case ResourceType::STONE:
                    supportType = BuildingType::Mine;
                    supportTerrain = TileType::STONE;
                    supportCap = 2;
                    break;
                case ResourceType::PLANKS:
                    supportType = BuildingType::LumberMill;
                    supportCap = 2;
                    break;
                case ResourceType::WHEAT:
                    supportType = BuildingType::WheatFarm;
                    supportCap = 2;
                    break;
                case ResourceType::WATER:
                    supportType = BuildingType::Well;
                    supportCap = 1;
                    break;
                case ResourceType::FLOUR:
                    supportType = BuildingType::Windmill;
                    supportCap = 1;
                    break;
                case ResourceType::BREAD:
                    supportType = BuildingType::Bakery;
                    supportCap = 1;
                    break;
                default:
                    break;
            }
            if (supportType == BuildingType::Building)
                continue;
            if (supportType == blockedDefinition.type)
                continue;
            if (CountCompletedOrQueuedBuildings(world, player, supportType) >= supportCap)
                continue;

            const auto& supportDefinition = GetBuildingDefinition(supportType);
            if (!player->CanBuildDefinition(supportDefinition))
                continue;
            Vec2i anchor = FindBuildAnchor(world, player, supportType, supportTerrain, nullptr);
            if (anchor.x >= 0 && TrySubmitBuild(world, player, supportType, anchor))
                return true;
        }
        return false;
    };

    for (const auto& step : opening)
    {
        if (CountCompletedOrQueuedBuildings(world, player, step.type) >= step.targetCount)
            continue;
        const auto& definition = GetBuildingDefinition(step.type);
        if (!player->CanBuildDefinition(definition))
        {
            if (!player->HasBuildResources(definition.buildCosts))
            {
                tryBuildCostSupport(definition);
                return true;
            }
            continue;
        }

        Vec2i anchor = FindBuildAnchor(world, player, step.type, step.terrain, nullptr);
        if (anchor.x >= 0 && TrySubmitBuild(world, player, step.type, anchor))
            return true;
        return true;
    }

    int woodcutters = CountCompletedOrQueuedBuildings(world, player, BuildingType::Woodcutter);
    int lumberMills = CountCompletedOrQueuedBuildings(world, player, BuildingType::LumberMill);
    int mines = CountCompletedOrQueuedBuildings(world, player, BuildingType::Mine);
    int foundries = CountCompletedOrQueuedBuildings(world, player, BuildingType::Foundry);

    if (lumberMills < 2 && woodcutters >= 1)
    {
        const auto& definition = GetBuildingDefinition(BuildingType::LumberMill);
        if (player->CanBuildDefinition(definition))
        {
            Vec2i anchor = FindBuildAnchor(world, player, BuildingType::LumberMill, TileType::GRASS, nullptr);
            if (anchor.x >= 0 && TrySubmitBuild(world, player, BuildingType::LumberMill, anchor))
                return true;
        }
    }

    if (mines < 2)
    {
        const auto& definition = GetBuildingDefinition(BuildingType::Mine);
        if (player->CanBuildDefinition(definition))
        {
            Vec2i anchor = FindBuildAnchor(world, player, BuildingType::Mine, TileType::IRON_ORE, nullptr);
            if (anchor.x >= 0 && TrySubmitBuild(world, player, BuildingType::Mine, anchor))
                return true;
        }
    }

    int ironOreProduction = GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute, ResourceType::IRON_ORE);
    int coalProduction = GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute, ResourceType::COAL);
    if (foundries < 1 && ironOreProduction > 0 && coalProduction > 0)
    {
        const auto& definition = GetBuildingDefinition(BuildingType::Foundry);
        if (player->CanBuildDefinition(definition))
        {
            Vec2i anchor = FindBuildAnchor(world, player, BuildingType::Foundry, TileType::GRASS, nullptr);
            if (anchor.x >= 0 && TrySubmitBuild(world, player, BuildingType::Foundry, anchor))
                return true;
        }
    }

    if (strategicAggression)
    {
        if (CountCompletedOrQueuedBuildings(world, player, BuildingType::Barracks) < 1)
        {
            const auto& definition = GetBuildingDefinition(BuildingType::Barracks);
            if (player->CanBuildDefinition(definition))
            {
                Vec2i anchor = FindBuildAnchor(world, player, BuildingType::Barracks, TileType::GRASS, nullptr);
                if (anchor.x >= 0 && TrySubmitBuild(world, player, BuildingType::Barracks, anchor))
                    return true;
            }
        }
        if (CountCompletedOrQueuedBuildings(world, player, BuildingType::StorageBuilding) < 2)
        {
            const auto& definition = GetBuildingDefinition(BuildingType::StorageBuilding);
            if (player->CanBuildDefinition(definition))
            {
                Vec2i anchor = FindBuildAnchor(world, player, BuildingType::StorageBuilding, TileType::GRASS, nullptr);
                if (anchor.x >= 0 && TrySubmitBuild(world, player, BuildingType::StorageBuilding, anchor))
                    return true;
            }
        }
    }

    return false;
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

// Initializes PrimitiveAIModel::TryBuildMilitary.
bool PrimitiveAIModel::TryBuildMilitary(GameWorld& world, Player* player, const AIModelSettings& settings)
{
    if (!IsEconomyStable(world, player))
        return false;

    Building* nearestEnemy = nullptr;
    Building* bestMilitary = FindBestMilitary(world, player);
    if (bestMilitary != nullptr)
    {
        nearestEnemy = FindNearestEnemyMilitary(world, player, bestMilitary);
        if (nearestEnemy != nullptr && attackTimer <= 0.0 && settings.personality.aggression >= 0.5f)
        {
            attackTimer = 8.0;
            world.SubmitCommand(GameCommand::AttackBuilding(player->id, bestMilitary->positionId, nearestEnemy->positionId));
            return true;
        }
    }

    int desiredTowers = settings.personality.aggression >= 0.5f ? 4 : 2;
    if (CountOwnedBuildings(world, player, BuildingType::GuardTower) >= desiredTowers)
        return false;

    Vec2i anchor = FindBuildAnchor(world, player, BuildingType::GuardTower, TileType::GRASS, nearestEnemy);
    if (anchor.x >= 0)
    {
        world.SubmitCommand(GameCommand::BuildBuilding(player->id, BuildingType::GuardTower, anchor));
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

bool PrimitiveAIModel::TryStartStrategicFocus(GameWorld& world, Player* player, const AIStrategySnapshot& strategy, const AIModelSettings& settings)
{
    if (player == nullptr || !player->focuses.GetActiveFocusId().empty())
        return false;

    AIMapAssessment map = AssessMap(world, player);
    const auto& definitions = GetFocusDefinitions();
    const TechnologyDefinition* best = nullptr;
    double bestScore = 0.0;
    for (const auto& definition : definitions)
    {
        if (!player->CanUnlockFocus(definition.id))
            continue;

        double score = 1.0;
        auto tagScore = [&](const std::string& tag, double value)
        {
            if (HasTag(definition, tag))
                score += value;
        };
        auto laneScore = [&](const std::string& lane, double value)
        {
            if (definition.category == lane || definition.layoutLane == lane)
                score += value;
        };

        double resourceUrgency = strategy.GetUrgency(AIStrategyAxis::Resources);
        double logisticsUrgency = std::max<double>(strategy.GetUrgency(AIStrategyAxis::Logistics), map.logisticsNeed);
        double expansionUrgency = std::max<double>(strategy.GetUrgency(AIStrategyAxis::Expansion), map.expansionPressure);
        double militaryUrgency = std::max<double>(strategy.GetUrgency(AIStrategyAxis::Military), map.militaryOpportunity);
        double politicalUrgency = std::max<double>(strategy.GetUrgency(AIStrategyAxis::InternalDevelopment), map.expansionPressure * 0.65);

        laneScore("PRODUCTION", resourceUrgency * (0.9 + settings.personality.economicFocus));
        laneScore("MILITARY", militaryUrgency * (0.9 + settings.personality.militarism));
        laneScore("WARFARE", militaryUrgency * (0.9 + settings.personality.militarism));
        laneScore("POLITICS", politicalUrgency * (0.8 + settings.personality.planning));
        laneScore("SOCIAL", politicalUrgency * 0.45);

        tagScore("production", resourceUrgency * (1.0 + settings.personality.economicFocus));
        tagScore("wood", resourceUrgency * 0.35);
        tagScore("planks", resourceUrgency * 0.70);
        tagScore("stone", resourceUrgency * 0.35);
        tagScore("iron", std::max(resourceUrgency, map.expansionPressure) * 0.75);
        tagScore("ore", std::max(resourceUrgency, map.expansionPressure) * 0.55);
        tagScore("logistics", logisticsUrgency * (1.0 + settings.personality.logisticsAwareness));
        tagScore("roads", logisticsUrgency * 0.70);
        tagScore("military", militaryUrgency * (0.8 + settings.personality.militarism));
        tagScore("population", politicalUrgency * (0.8 + settings.personality.economicFocus));
        tagScore("government", politicalUrgency * (0.6 + settings.personality.planning));
        tagScore("research", strategy.GetUrgency(AIStrategyAxis::Technology) * (0.7 + settings.personality.planning));

        if (map.expansionPressure > 0.55)
        {
            laneScore("POLITICS", map.expansionPressure * (0.8 + settings.personality.expansionism));
            laneScore("MILITARY", map.expansionPressure * settings.personality.aggression * 0.65);
            tagScore("government", map.expansionPressure * 0.45);
            tagScore("military", map.expansionPressure * settings.personality.aggression * 0.55);
        }
        if (map.militaryOpportunity > 0.45)
        {
            laneScore("MILITARY", map.militaryOpportunity * (0.9 + settings.personality.aggression + settings.personality.opportunism));
            tagScore("military", map.militaryOpportunity * (0.8 + settings.personality.militarism));
            tagScore("logistics", map.militaryOpportunity * settings.personality.logisticsAwareness * 0.35);
        }

        score += std::max(0.0, 260.0 - definition.researchTime) / 900.0;
        double idNoise = 0.0;
        for (char c : definition.id)
            idNoise += static_cast<unsigned char>(c);
        double noiseSeed = std::sin(static_cast<double>(player->id * 131) + idNoise);
        score *= 0.96 + (noiseSeed + 1.0) * 0.04 * (1.0 - settings.personality.planning);

        if (score > bestScore)
        {
            bestScore = score;
            best = &definition;
        }
    }

    if (best == nullptr)
        return false;

    world.SubmitCommand(GameCommand::StartFocus(player->id, best->id));
    return true;
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
            axisCache.timeUntilRefresh = axisCache.interval;
        }
        snapshot.signals.insert(snapshot.signals.end(), axisCache.signals.begin(), axisCache.signals.end());
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

AIStrategicPlan PrimitiveAIModel::SelectStrategicPlan(const AIStrategySnapshot& snapshot, const AIModelSettings& settings) const
{
    struct Candidate
    {
        AIStrategicPlan plan;
        double score;
    };

    std::vector<Candidate> candidates{
        {AIStrategicPlan::RecoverEconomy, snapshot.GetUrgency(AIStrategyAxis::Resources) * (0.9 + settings.personality.economicFocus)},
        {AIStrategicPlan::FixLogistics, snapshot.GetUrgency(AIStrategyAxis::Logistics) * (0.9 + settings.personality.logisticsAwareness)},
        {AIStrategicPlan::BuildArmy, snapshot.GetUrgency(AIStrategyAxis::Military) * (0.7 + settings.personality.militarism)},
        {AIStrategicPlan::DefendBorder, std::max(snapshot.GetUrgency(AIStrategyAxis::Military), snapshot.GetUrgency(AIStrategyAxis::Risk)) * (0.7 + settings.personality.defensiveBias)},
        {AIStrategicPlan::PrepareOffensive, std::max(snapshot.GetUrgency(AIStrategyAxis::Military), snapshot.GetUrgency(AIStrategyAxis::Diplomacy)) * (0.5 + settings.personality.aggression + settings.personality.opportunism * 0.5)},
        {AIStrategicPlan::ExpandForResources, std::max(snapshot.GetUrgency(AIStrategyAxis::Expansion), snapshot.GetUrgency(AIStrategyAxis::Resources)) * (0.6 + settings.personality.expansionism)},
        {AIStrategicPlan::DevelopPopulation, snapshot.GetUrgency(AIStrategyAxis::InternalDevelopment) * (0.7 + settings.personality.economicFocus)},
        {AIStrategicPlan::ResearchSpecialization, snapshot.GetUrgency(AIStrategyAxis::Technology) * (0.8 + settings.personality.planning)},
        {AIStrategicPlan::ConsolidateTerritory, std::max(snapshot.GetUrgency(AIStrategyAxis::Risk), snapshot.GetUrgency(AIStrategyAxis::Expansion)) * (0.6 + settings.personality.planning + settings.personality.defensiveBias * 0.5)}
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

// Returns whether this condition is currently true.
bool PrimitiveAIModel::IsEconomyStable(GameWorld& world, Player* player) const
{
    if (CountOwnedBuildings(world, player, BuildingType::Woodcutter) < 2)
        return false;
    if (CountOwnedBuildings(world, player, BuildingType::Mine) < 1)
        return false;
    if (CountOwnedBuildings(world, player, BuildingType::LumberMill) < 1)
        return false;

    return CountStoredResource(world, player, ResourceType::WOOD) >= 40 &&
           CountStoredResource(world, player, ResourceType::STONE) >= 40 &&
           CountStoredResource(world, player, ResourceType::PLANKS) >= 20;
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
