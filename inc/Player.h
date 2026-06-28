#ifndef PLAYER_H
#define PLAYER_H

#include "Utils.h"
#include "InputHandler.h"
#include "BuildingFactory.h"
#include "BuildingConfig.h"
#include "BalanceModifiers.h"
#include "GameCommand.h"
#include "RoadNetwork.h"
#include "StrategicResource.h"
#include "StateDevelopment.h"
#include "Technology.h"
#include "raylib.h"

#include <cmath>
#include <deque>
#include <optional>
#include <set>

class TileMap;
class Player;

enum class PlayerControllerType
{
    LocalHuman,
    AI,
    Remote
};

// Aggregated army state used by HUD, stats and future army overview panels.
struct ArmyRegistry
{
    int militia{0};
    int swordsmen{0};
    int archers{0};
    int queuedMilitia{0};
    int queuedSwordsmen{0};
    int queuedArchers{0};
    int garrisonCapacity{0};
    int supply{0};
    int supplyCapacity{0};
    int supplyConsumption{0};
    int strength{0};

    int TotalTroops() const { return militia + swordsmen + archers; }
    int TotalQueued() const { return queuedMilitia + queuedSwordsmen + queuedArchers; }
};

// Player-local data index fed by simulation events instead of tilemap scans.
struct PlayerDataTracker
{
    std::set<Building*> buildings;
    std::map<BuildingType, std::set<Building*>> buildingsByType;
    std::map<GameCommandType, int> processedCommands;

    // Adds a building to all player-local indexes.
    void RegisterBuilding(Building* building)
    {
        if (building == nullptr)
            return;

        buildings.insert(building);
        buildingsByType[building->buildingType].insert(building);
        IndexComponent<RoadComponent>(building);
        IndexComponent<ProductionComponent>(building);
        IndexComponent<LogisticsComponent>(building);
        IndexComponent<WorkerComponent>(building);
        IndexComponent<RecipeComponent>(building);
        IndexComponent<ResearchComponent>(building);
        IndexComponent<StorageComponent>(building);
        IndexComponent<TerritoryComponent>(building);
        IndexComponent<GarrisonComponent>(building);
        IndexComponent<SupplyBufferComponent>(building);
        IndexComponent<RecruitmentComponent>(building);
        IndexComponent<PopulationComponent>(building);
    }

    // Removes a building from all player-local indexes.
    void UnregisterBuilding(Building* building)
    {
        if (building == nullptr)
            return;

        buildings.erase(building);
        auto byType = buildingsByType.find(building->buildingType);
        if (byType != buildingsByType.end())
        {
            byType->second.erase(building);
            if (byType->second.empty())
                buildingsByType.erase(byType);
        }
        UnindexComponent<RoadComponent>(building);
        UnindexComponent<ProductionComponent>(building);
        UnindexComponent<LogisticsComponent>(building);
        UnindexComponent<WorkerComponent>(building);
        UnindexComponent<RecipeComponent>(building);
        UnindexComponent<ResearchComponent>(building);
        UnindexComponent<StorageComponent>(building);
        UnindexComponent<TerritoryComponent>(building);
        UnindexComponent<GarrisonComponent>(building);
        UnindexComponent<SupplyBufferComponent>(building);
        UnindexComponent<RecruitmentComponent>(building);
        UnindexComponent<PopulationComponent>(building);
    }

    // Records a command accepted by the simulation for later player analytics.
    void TrackCommand(GameCommandType type)
    {
        processedCommands[type]++;
    }

    // Returns whether at least one building of this type exists.
    bool HasBuilding(BuildingType type, bool completedOnly = false) const
    {
        auto it = buildingsByType.find(type);
        if (it == buildingsByType.end())
            return false;
        if (!completedOnly)
            return !it->second.empty();

        for (const auto* building : it->second)
            if (building != nullptr && !building->IsUnderConstruction())
                return true;
        return false;
    }

    // Returns the number of tracked buildings of a type.
    int CountBuildings(BuildingType type, bool completedOnly = false) const
    {
        auto it = buildingsByType.find(type);
        if (it == buildingsByType.end())
            return 0;
        if (!completedOnly)
            return static_cast<int>(it->second.size());

        int count = 0;
        for (const auto* building : it->second)
            if (building != nullptr && !building->IsUnderConstruction())
                count++;
        return count;
    }

    template<typename T>
    const std::set<Building*>& BuildingsWithComponent() const
    {
        static const std::set<Building*> empty;
        constexpr BuildingCapability capability = GetBuildingComponentCapability<T>();
        if constexpr (capability == BuildingCapability::Count)
            return empty;
        else
        {
            auto it = buildingsByComponent.find(capability);
            return it != buildingsByComponent.end() ? it->second : empty;
        }
    }

private:
    std::map<BuildingCapability, std::set<Building*>> buildingsByComponent;

    template<typename T>
    void IndexComponent(Building* building)
    {
        constexpr BuildingCapability capability = GetBuildingComponentCapability<T>();
        if constexpr (capability != BuildingCapability::Count)
            if (building->HasComponent<T>())
                buildingsByComponent[capability].insert(building);
    }

    template<typename T>
    void UnindexComponent(Building* building)
    {
        constexpr BuildingCapability capability = GetBuildingComponentCapability<T>();
        if constexpr (capability != BuildingCapability::Count)
        {
            auto it = buildingsByComponent.find(capability);
            if (it == buildingsByComponent.end())
                return;
            it->second.erase(building);
            if (it->second.empty())
                buildingsByComponent.erase(it);
        }
    }
};

struct ResourceFlowSnapshot
{
    double time{0.0};
    std::map<ResourceType, int> productionRatesPerMinute;
    std::map<ResourceType, int> consumptionRatesPerMinute;
};

struct PlayerEconomyTelemetry
{
    ResourceFlowSnapshot current;
    std::deque<ResourceFlowSnapshot> history;
    double elapsedTime{0.0};
    double sampleTimer{0.0};

    void Update(Player& player, double dt);
    static ResourceFlowSnapshot BuildSnapshot(Player& player, double time);
};

// Player-owned state: buildings, logistics network and strategic resources.
class Player
{
public:
    Player() = default;
    Player(int i, TileMap& tmap) : tilemap(tmap), id(i), build(this, tilemap, id){ roadNetwork = std::make_unique<RoadNetwork>(tilemap);}

    // Updates player input when an input source is attached.
    void Update(double dt)
    {
        if (input == nullptr)
            return;

        input->Update(dt);
        input->GetInputs();
    }

    void UpdateFocus(double dt)
    {
        if (focuses.UpdateActiveFocus(dt))
            RefreshTechnologyModifiers();
    }

    void UpdateResearch(double dt)
    {
        for (auto* building : GetTrackedBuildingsWithComponent<ResearchComponent>())
        {
            auto* research = building != nullptr ? building->GetComponent<ResearchComponent>() : nullptr;
            if (research == nullptr || building->owner != this ||
                building->buildingType != BuildingType::University)
                continue;

            std::string completedTechnology = research->technologyId;
            if (completedTechnology.empty() || !research->Tick(dt))
                continue;

            research->technologyId.clear();
            research->remaining = 0.0;
            research->total = 0.0;
            if (technologies.UnlockTechnology(completedTechnology))
                RefreshTechnologyModifiers();
        }
    }

    void UpdateEconomyTelemetry(double dt)
    {
        economyTelemetry.Update(*this, dt);
    }

    bool IsTechnologyInProgress(const std::string& id) const
    {
        for (auto* building : GetTrackedBuildingsWithComponent<ResearchComponent>())
        {
            const auto* research = building != nullptr ? building->GetComponent<ResearchComponent>() : nullptr;
            if (research != nullptr && building->owner == this &&
                building->buildingType == BuildingType::University &&
                research->technologyId == id)
                return true;
        }
        return false;
    }

    // Registers a newly placed building in player data indexes.
    void RegisterBuilding(Building* building)
    {
        dataTracker.RegisterBuilding(building);
    }

    // Removes a building from player data indexes before it is destroyed.
    void UnregisterBuilding(Building* building)
    {
        dataTracker.UnregisterBuilding(building);
    }

    // Records a gameplay command accepted for this player.
    void TrackAcceptedCommand(GameCommandType type)
    {
        dataTracker.TrackCommand(type);
    }

    // Returns tracked player buildings without scanning the map.
    const std::set<Building*>& GetTrackedBuildings() const
    {
        return dataTracker.buildings;
    }

    template<typename T>
    const std::set<Building*>& GetTrackedBuildingsWithComponent() const
    {
        return dataTracker.BuildingsWithComponent<T>();
    }

    // Returns whether the player has a tracked building of this type.
    bool HasTrackedBuilding(BuildingType type, bool completedOnly = false) const
    {
        return dataTracker.HasBuilding(type, completedOnly);
    }

    // Returns the number of tracked buildings of this type.
    int GetTrackedBuildingCount(BuildingType type, bool completedOnly = false) const
    {
        return dataTracker.CountBuildings(type, completedOnly);
    }

    // Returns how many accepted commands of a given type were processed.
    int GetAcceptedCommandCount(GameCommandType type) const
    {
        auto it = dataTracker.processedCommands.find(type);
        return it != dataTracker.processedCommands.end() ? it->second : 0;
    }

    // Builds a building type on a tile id and registers it in logistics.
    template <typename T>
    Building* Build(int tilePos, bool chargeCost = true)
    {
        static_assert(std::is_base_of<Building, T>::value);
        T preview{0};
        Vec2i anchor = tilemap.GetCoordsFromId(tilePos);
        if (!tilemap.CanBuildFootprint(anchor, preview.GetFootprint(), this))
            return nullptr;

        const auto& definition = GetBuildingDefinition(preview.buildingType);
        if (chargeCost && !TryPayBuildCost(definition.buildCosts))
        {
            Log::Msg("[Player]", "Not enough resources to build ", definition.name);
            return nullptr;
        }

        build.Build<T>(tilePos);
        auto bld = tilemap.GetBuilding(tilePos);
        if(bld != nullptr)
        {
            double buildTime = ModifyBalanceAt(BalanceStat::BuildTime, definition.buildTime, preview.buildingType, anchor);
            bld->buildTime = buildTime;
            bld->constructionRemaining = chargeCost ? buildTime : 0.0;
            if (!bld->IsUnderConstruction())
            {
                for (int occupiedTileId : tilemap.GetBuildingTileIds(bld))
                    roadNetwork->UpdateNavMap(occupiedTileId, bld);
                tilemap.AutoConnectBuilding(bld);
            }
        }
        return bld;
    }

    // Builds a building type on map coordinates.
    template <typename T>
    Building* Build(Vec2i pos, bool chargeCost = true)
    {
        return Build<T>(tilemap.GetIdFromCoords(pos), chargeCost);
    }

    // Returns true when owned storage buffers can cover all build costs.
    bool HasBuildResources(const std::vector<ResourceAmountDefinition>& costs) const
    {
        for (const auto& cost : costs)
        {
            int available = 0;
            for (const auto* building : GetTrackedBuildingsWithComponent<StorageComponent>())
            {
                const auto* storage = building != nullptr ? building->GetComponent<StorageComponent>() : nullptr;
                if (storage == nullptr || building->owner != this)
                    continue;

                auto it = storage->buffers.find(cost.type);
                if (it != storage->buffers.end())
                    available += static_cast<int>(it->second.buffer.size());
            }

            if (available < cost.amount)
                return false;
        }
        return true;
    }

    // Returns why a building cannot currently be unlocked or paid for.
    std::vector<std::string> GetBuildRequirementFailures(const BuildingDefinition& definition, bool ignoreDebugFreeBuild = true) const
    {
        std::vector<std::string> failures;
        for (const auto& technology : definition.requiredTechnologies)
            if (!technologies.HasTechnology(technology))
                failures.push_back("Requires tech: " + technology);
        for (const auto& focus : definition.requiredFocuses)
            if (!focuses.HasFocus(focus))
                failures.push_back("Requires focus: " + focus);
        if ((!tilemap.params.debugMode || !ignoreDebugFreeBuild) && !HasBuildResources(definition.buildCosts))
            failures.push_back("Not enough resources");
        return failures;
    }

    // Returns true when all build unlock requirements and costs are currently satisfied.
    bool CanBuildDefinition(const BuildingDefinition& definition) const
    {
        return GetBuildRequirementFailures(definition).empty();
    }

    int GetPopulationCap() const
    {
        int cap = 0;
        for (const auto* building : GetTrackedBuildingsWithComponent<PopulationComponent>())
        {
            const auto* population = building != nullptr ? building->GetComponent<PopulationComponent>() : nullptr;
            if (population != nullptr && building->owner == this && !building->IsUnderConstruction())
                cap += ResolveStat(population->populationCap, building);
        }
        return cap;
    }

    double GetTotalPopulation() const
    {
        return strategicResources.Get(StrategicResourceType::Manpower) +
               strategicResources.Get(StrategicResourceType::Workers) +
               strategicResources.Get(StrategicResourceType::Soldiers);
    }

    // Aggregates soldiers, queues, supply and combat strength from owned military buildings.
    ArmyRegistry GetArmyRegistry() const
    {
        ArmyRegistry registry;
        for (const auto* building : GetTrackedBuildingsWithComponent<GarrisonComponent>())
        {
            const auto* garrison = building != nullptr ? building->GetComponent<GarrisonComponent>() : nullptr;
            const auto* supply = building != nullptr ? building->GetComponent<SupplyBufferComponent>() : nullptr;
            if (garrison == nullptr || building->owner != this || building->IsUnderConstruction())
                continue;

            registry.militia += garrison->militia;
            registry.swordsmen += garrison->swordsmen;
            registry.archers += garrison->archers;
            registry.garrisonCapacity += garrison->GetTotalTroops() + garrison->GetFreeGarrisonSpace(*building);
            if (supply != nullptr)
            {
                registry.supply += supply->stored;
                registry.supplyCapacity += supply->GetModifiedCapacity(*building);
                registry.supplyConsumption += supply->GetSupplyConsumption(*building, *garrison);
            }
            registry.strength += garrison->GetEffectiveStrength(*building);

            if (const auto* recruitment = building->GetComponent<RecruitmentComponent>())
            {
                for (const auto& job : recruitment->queue)
                {
                    switch (job.type)
                    {
                        case MilitaryUnitType::Swordsman: registry.queuedSwordsmen++; break;
                        case MilitaryUnitType::Archer: registry.queuedArchers++; break;
                        case MilitaryUnitType::Militia:
                        default: registry.queuedMilitia++; break;
                    }
                }
            }
        }
        return registry;
    }

    double GetFoodProductivity() const
    {
        int villageCount = 0;
        double productivity = 0.0;
        for (const auto* building : GetTrackedBuildingsWithComponent<PopulationComponent>())
        {
            const auto* population = building != nullptr ? building->GetComponent<PopulationComponent>() : nullptr;
            if (population == nullptr || building->owner != this || building->IsUnderConstruction())
                continue;

            villageCount++;
            productivity += population->GetWorkerProductivity();
        }

        return villageCount > 0 ? productivity / villageCount : 1.0;
    }

    double ModifyBalance(BalanceStat stat, double base, BuildingType buildingType = BuildingType::Building,
                         ResourceType resourceType = ResourceType::Null,
                         std::optional<MilitaryUnitType> unitType = std::nullopt) const
    {
        return balanceModifiers.ModifyDouble(base, MakeBalanceContext(stat, buildingType, resourceType, unitType));
    }

    int ModifyBalanceInt(BalanceStat stat, int base, BuildingType buildingType = BuildingType::Building,
                         ResourceType resourceType = ResourceType::Null,
                         std::optional<MilitaryUnitType> unitType = std::nullopt,
                         int minimum = 0) const
    {
        return balanceModifiers.ModifyInt(base, MakeBalanceContext(stat, buildingType, resourceType, unitType), minimum);
    }

    double ModifyBalanceAt(BalanceStat stat, double base, BuildingType buildingType, Vec2i position,
                           ResourceType resourceType = ResourceType::Null,
                           std::optional<MilitaryUnitType> unitType = std::nullopt) const
    {
        return balanceModifiers.ModifyDouble(base, MakeBalanceContext(stat, buildingType, resourceType, unitType, position));
    }

    int ModifyBalanceIntAt(BalanceStat stat, int base, BuildingType buildingType, Vec2i position,
                           ResourceType resourceType = ResourceType::Null,
                           std::optional<MilitaryUnitType> unitType = std::nullopt,
                           int minimum = 0) const
    {
        return balanceModifiers.ModifyInt(base, MakeBalanceContext(stat, buildingType, resourceType, unitType, position), minimum);
    }

    double ModifyBalanceForBuilding(BalanceStat stat, double base, const Building* building,
                                    ResourceType resourceType = ResourceType::Null,
                                    std::optional<MilitaryUnitType> unitType = std::nullopt) const
    {
        return balanceModifiers.ModifyDouble(base, MakeBalanceContext(stat, building, resourceType, unitType));
    }

    int ModifyBalanceIntForBuilding(BalanceStat stat, int base, const Building* building,
                                    ResourceType resourceType = ResourceType::Null,
                                    std::optional<MilitaryUnitType> unitType = std::nullopt,
                                    int minimum = 0) const
    {
        return balanceModifiers.ModifyInt(base, MakeBalanceContext(stat, building, resourceType, unitType), minimum);
    }

    // Resolves a floating-point stat for a concrete building context.
    double ResolveStat(const Stat<double>& stat, const Building* building,
                       ResourceType resourceType = ResourceType::Null,
                       std::optional<MilitaryUnitType> unitType = std::nullopt) const
    {
        return ModifyBalanceForBuilding(stat.GetStatId(), stat.GetBase(), building, resourceType, unitType);
    }

    // Resolves an integer stat for a concrete building context.
    int ResolveStat(const Stat<int>& stat, const Building* building,
                    ResourceType resourceType = ResourceType::Null,
                    std::optional<MilitaryUnitType> unitType = std::nullopt,
                    int minimum = 0) const
    {
        return ModifyBalanceIntForBuilding(stat.GetStatId(), stat.GetBase(), building, resourceType, unitType, minimum);
    }

    // Resolves a floating-point stat for a map-position context before a building exists.
    double ResolveStatAt(const Stat<double>& stat, BuildingType buildingType, Vec2i position,
                         ResourceType resourceType = ResourceType::Null,
                         std::optional<MilitaryUnitType> unitType = std::nullopt) const
    {
        return ModifyBalanceAt(stat.GetStatId(), stat.GetBase(), buildingType, position, resourceType, unitType);
    }

    // Resolves an integer stat for a map-position context before a building exists.
    int ResolveStatAt(const Stat<int>& stat, BuildingType buildingType, Vec2i position,
                      ResourceType resourceType = ResourceType::Null,
                      std::optional<MilitaryUnitType> unitType = std::nullopt,
                      int minimum = 0) const
    {
        return ModifyBalanceIntAt(stat.GetStatId(), stat.GetBase(), buildingType, position, resourceType, unitType, minimum);
    }

    // Returns whether the player can currently pay for and unlock a technology.
    bool CanResearchTechnology(const std::string& id) const
    {
        const auto* definition = FindTechnologyDefinition(id);
        return definition != nullptr &&
               technologies.CanUnlock(id) &&
               !IsTechnologyInProgress(id) &&
               (tilemap.params.debugMode || HasBuildResources(definition->costs));
    }

    bool CanUnlockFocus(const std::string& id) const
    {
        const auto* definition = FindFocusDefinition(id);
        return definition != nullptr &&
               focuses.CanStartFocus(id);
    }

    bool UnlockFocus(const std::string& id)
    {
        const auto* definition = FindFocusDefinition(id);
        if (definition == nullptr || !focuses.CanUnlock(id))
            return false;

        if (!focuses.UnlockFocus(id))
            return false;

        RefreshTechnologyModifiers();
        return true;
    }

    bool StartFocus(const std::string& id)
    {
        const auto* definition = FindFocusDefinition(id);
        return definition != nullptr && focuses.StartFocus(id);
    }

    // Pays technology costs, unlocks it and refreshes technology modifiers.
    bool UnlockTechnology(const std::string& id)
    {
        const auto* definition = FindTechnologyDefinition(id);
        if (definition == nullptr || !technologies.CanUnlock(id))
            return false;

        if (!tilemap.params.debugMode && !TryPayBuildCost(definition->costs))
            return false;

        if (!technologies.UnlockTechnology(id))
            return false;

        RefreshTechnologyModifiers();
        return true;
    }

    bool StartTechnologyResearch(const std::string& id, Building* university)
    {
        const auto* definition = FindTechnologyDefinition(id);
        auto* research = university != nullptr ? university->GetComponent<ResearchComponent>() : nullptr;
        if (definition == nullptr || university == nullptr || university->owner != this ||
            university->buildingType != BuildingType::University || research == nullptr ||
            !research->technologyId.empty())
            return false;

        if (!CanResearchTechnology(id))
            return false;

        if (!tilemap.params.debugMode && !TryPayBuildCost(definition->costs))
            return false;

        double researchTime = ModifyBalanceForBuilding(
            BalanceStat::ProductionCycleTime,
            definition->researchTime,
            university,
            ResourceType::Null,
            std::nullopt);
        if (!research->Start(id, researchTime))
            return false;

        return true;
    }

    // Rebuilds the modifier set entries emitted by unlocked technologies.
    void RefreshTechnologyModifiers()
    {
        balanceModifiers.ClearSourcePrefix("tech:");
        balanceModifiers.ClearSourcePrefix("focus:");
        balanceModifiers.ClearSourcePrefix("state:");
        std::set<std::string> unlockedGovernmentIds;
        for (const auto& focusId : focuses.GetUnlocked())
        {
            const auto* focus = FindFocusDefinition(focusId);
            if (focus != nullptr && !focus->governmentId.empty())
                unlockedGovernmentIds.insert(focus->governmentId);
        }
        stateDevelopment.RefreshFromGovernmentIds(unlockedGovernmentIds);
        technologies.CollectModifiers(balanceModifiers);
        focuses.CollectModifiers(balanceModifiers);
        stateDevelopment.CollectModifiers(balanceModifiers);
    }

    BalanceModifierContext MakeBalanceContext(BalanceStat stat, BuildingType buildingType,
                                              ResourceType resourceType = ResourceType::Null,
                                              std::optional<MilitaryUnitType> unitType = std::nullopt,
                                              std::optional<Vec2i> position = std::nullopt) const
    {
        BalanceModifierContext context{stat, buildingType, resourceType, unitType};
        context.position = position;
        if (position.has_value() && tilemap.IsInside(position.value()))
        {
            context.positionId = tilemap.GetIdFromCoords(position.value());
            context.ownedTerritory = tilemap.tilemap[context.positionId.value()].owner == this;
        }
        return context;
    }

    BalanceModifierContext MakeBalanceContext(BalanceStat stat, const Building* building,
                                              ResourceType resourceType = ResourceType::Null,
                                              std::optional<MilitaryUnitType> unitType = std::nullopt) const
    {
        BalanceModifierContext context = MakeBalanceContext(
            stat,
            building != nullptr ? building->buildingType : BuildingType::Building,
            resourceType,
            unitType);

        if (building == nullptr)
            return context;

        context.buildingId = building->id;
        context.positionId = building->positionId;
        if (building->positionId >= 0)
        {
            context.position = tilemap.GetCoordsFromId(building->positionId);
            if (tilemap.IsInside(context.position.value()))
                context.ownedTerritory = tilemap.tilemap[building->positionId].owner == this;
        }
        return context;
    }

    double AddManpower(double amount)
    {
        if (amount <= 0.0)
            return 0.0;

        double cap = static_cast<double>(GetPopulationCap());
        double room = std::max(0.0, cap - GetTotalPopulation());
        double added = std::min(amount, room);
        if (added > 0.0)
            strategicResources.Add(StrategicResourceType::Manpower, added);
        return added;
    }

    int AutoAssignWorkers(Building* building)
    {
        if (building == nullptr || building->owner != this)
            return 0;

        auto* workers = building->GetComponent<WorkerComponent>();
        if (workers == nullptr)
            return 0;

        int needed = std::max(0, building->GetWorkerCapacity() - workers->assigned);
        int available = static_cast<int>(std::floor(strategicResources.Get(StrategicResourceType::Manpower)));
        int assigned = std::min(needed, available);
        if (assigned <= 0)
            return 0;

        strategicResources.Consume(StrategicResourceType::Manpower, assigned);
        strategicResources.Add(StrategicResourceType::Workers, assigned);
        workers->assigned += assigned;
        return assigned;
    }

    // Pays build costs from owned storage buffers after a full availability check.
    bool TryPayBuildCost(const std::vector<ResourceAmountDefinition>& costs)
    {
        if (!HasBuildResources(costs))
            return false;

        for (const auto& cost : costs)
        {
            int remaining = cost.amount;
            for (auto* building : GetTrackedBuildingsWithComponent<StorageComponent>())
            {
                auto* storage = building != nullptr ? building->GetComponent<StorageComponent>() : nullptr;
                if (storage == nullptr || building->owner != this)
                    continue;

                auto it = storage->buffers.find(cost.type);
                if (it == storage->buffers.end())
                    continue;

                while (remaining > 0 && !it->second.buffer.empty())
                {
                    it->second.FreeResource();
                    remaining--;
                }

                if (remaining == 0)
                    break;
            }
        }

        return true;
    }

    // Starts resource transport through this player's road network.
    inline bool BeginTransport(Building* src, Building* dest, Resource* res)
    {
        return roadNetwork->BeginTransport(src, dest, res);
    }

    int id;
    std::string name{"Player"};
    Color color{66, 154, 255, 255};
    PlayerControllerType controllerType{PlayerControllerType::LocalHuman};

    std::unique_ptr<InputHandler> input;
    std::unique_ptr<RoadNetwork> roadNetwork;
    TileMap& tilemap;
    BFactory build;
    StrategicResourcePool strategicResources;
    TechnologyState technologies;
    FocusState focuses;
    StateDevelopment stateDevelopment;
    BalanceModifierSet balanceModifiers;
    PlayerDataTracker dataTracker;
    PlayerEconomyTelemetry economyTelemetry;
};

// Local human-controlled player type.
class HumanPlayer : public Player
{
public:
    HumanPlayer() = default;
};

inline void PlayerEconomyTelemetry::Update(Player& player, double dt)
{
    elapsedTime += std::max(0.0, dt);
    current = BuildSnapshot(player, elapsedTime);
    sampleTimer += std::max(0.0, dt);
    if (sampleTimer >= 1.0 || history.empty())
    {
        sampleTimer = 0.0;
        history.push_back(current);
        while (!history.empty() && elapsedTime - history.front().time > 300.0)
            history.pop_front();
    }
}

inline ResourceFlowSnapshot PlayerEconomyTelemetry::BuildSnapshot(Player& player, double time)
{
    ResourceFlowSnapshot snapshot;
    snapshot.time = time;

    for (const auto* building : player.GetTrackedBuildings())
    {
        if (building == nullptr || building->owner != &player)
            continue;

        if (building->IsUnderConstruction())
        {
            const auto& definition = GetBuildingDefinition(building->buildingType);
            double buildTime = std::max(1.0, building->buildTime.GetBase());
            for (const auto& cost : definition.buildCosts)
                snapshot.consumptionRatesPerMinute[cost.type] += static_cast<int>(std::ceil(cost.amount * 60.0 / buildTime));
            continue;
        }

        if (const auto* production = building->GetComponent<ProductionComponent>())
        {
            const auto* workers = building->GetComponent<WorkerComponent>();
            double workerRatio = workers != nullptr ? workers->GetRatio() : 1.0;
            double modifiedCycle = production->GetModifiedCycleTime(*building);
            double foodProductivity = player.GetFoodProductivity();
            double workerEfficiency = workerRatio * foodProductivity;
            double cycleTime = workerEfficiency > 0.0 && modifiedCycle > 0.0
                ? modifiedCycle / workerEfficiency
                : 0.0;
            if (cycleTime > 0.0 && workerEfficiency > 0.0 && !building->IsProductionBlocked())
            {
                double cyclesPerMinute = 60.0 * workerEfficiency / cycleTime;
                for (const auto& [type, amount] : production->products)
                    snapshot.productionRatesPerMinute[type] += static_cast<int>(std::round(production->GetModifiedOutputAmount(*building, type, amount) * cyclesPerMinute));
                for (const auto& [type, amount] : production->ingredients)
                    snapshot.consumptionRatesPerMinute[type] += static_cast<int>(std::round(amount * cyclesPerMinute));
            }
        }

        if (const auto* population = building->GetComponent<PopulationComponent>())
        {
            if (population->upkeepInterval > 0.0)
                snapshot.consumptionRatesPerMinute[ResourceType::FOOD_PROVISIONS] += static_cast<int>(std::ceil(population->foodPackageUpkeep * (60.0 / population->upkeepInterval)));
        }

        const auto* garrison = building->GetComponent<GarrisonComponent>();
        const auto* supply = building->GetComponent<SupplyBufferComponent>();
        if (garrison != nullptr && supply != nullptr)
            snapshot.consumptionRatesPerMinute[ResourceType::FOOD_PROVISIONS] += supply->GetSupplyConsumption(*building, *garrison);
    }

    return snapshot;
}

#endif
