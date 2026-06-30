#ifndef PLAYER_H
#define PLAYER_H

#include "Utils.h"
#include "InputHandler.h"
#include "BuildingFactory.h"
#include "BuildingConfig.h"
#include "ArmyGroup.h"
#include "BalanceModifiers.h"
#include "GameCommand.h"
#include "RoadNetwork.h"
#include "StrategicResource.h"
#include "StateDevelopment.h"
#include "Technology.h"
#include "ArmyRegistry.h"
#include "PlayerDataTracker.h"
#include "PlayerEconomy.h"
#include "raylib.h"

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

// Player-owned state: buildings, logistics network and strategic resources.
class Player
{
public:
    Player() = default;
    Player(int i, TileMap& tmap) : tilemap(tmap), id(i), build(this, tilemap, id)
    {
        roadNetwork = std::make_unique<RoadNetwork>(tilemap);
        RefreshTechnologyModifiers();
    }

    // Updates player input when an input source is attached.
    void Update(double dt)
    {
        if (input == nullptr)
            return;
        input->Update(dt);
        input->GetInputs();
    }

    void UpdateFocus(double dt);
    void UpdateResearch(double dt);

    void UpdateEconomyTelemetry(double dt) { economyTelemetry.Update(*this, dt); }

    bool IsTechnologyInProgress(const std::string& id) const;

    // Registers a newly placed building in player data indexes.
    void RegisterBuilding(Building* building) { dataTracker.RegisterBuilding(building); }

    // Removes a building from player data indexes before it is destroyed.
    void UnregisterBuilding(Building* building) { dataTracker.UnregisterBuilding(building); }

    // Records a gameplay command accepted for this player.
    void TrackAcceptedCommand(GameCommandType type) { dataTracker.TrackCommand(type); }

    // Returns tracked player buildings without scanning the map.
    const std::set<Building*>& GetTrackedBuildings() const { return dataTracker.buildings; }

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
        if (bld != nullptr)
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

    bool HasBuildResources(const std::vector<ResourceAmountDefinition>& costs) const;
    std::vector<std::string> GetBuildRequirementFailures(const BuildingDefinition& definition, bool ignoreDebugFreeBuild = true) const;

    // Returns true when all build unlock requirements and costs are currently satisfied.
    bool CanBuildDefinition(const BuildingDefinition& definition) const
    {
        return GetBuildRequirementFailures(definition).empty();
    }

    int GetPopulationCap() const;

    double GetTotalPopulation() const
    {
        return strategicResources.Get(StrategicResourceType::Manpower) +
               strategicResources.Get(StrategicResourceType::Workers) +
               strategicResources.Get(StrategicResourceType::Soldiers);
    }

    ArmyRegistry GetArmyRegistry() const;
    double GetFoodProductivity() const;

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

    bool CanResearchTechnology(const std::string& id) const;

    bool CanUnlockFocus(const std::string& id) const
    {
        const auto* definition = FindFocusDefinition(id);
        return definition != nullptr && focuses.CanStartFocus(id);
    }

    bool UnlockFocus(const std::string& id);

    bool StartFocus(const std::string& id);

    bool UnlockTechnology(const std::string& id);
    bool StartTechnologyResearch(const std::string& id, Building* university);

    // Rebuilds the modifier set entries emitted by unlocked technologies.
    void RefreshTechnologyModifiers();

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

    double AddManpower(double amount);
    int AutoAssignWorkers(Building* building);
    bool TryPayBuildCost(const std::vector<ResourceAmountDefinition>& costs);

    // Starts resource transport through this player's road network.
    bool BeginTransport(Building* src, Building* dest, Resource* res)
    {
        return roadNetwork->BeginTransport(src, dest, res);
    }

    int id;
    std::string name{"Player"};
    Color color{66, 154, 255, 255};
    PlayerControllerType controllerType{PlayerControllerType::LocalHuman};
    bool debugMode{false};

    // Army management
    ArmyGroupRegistry armyGroups;
    // Global army modifiers from tech/focus (keys: "tech:...", "focus:...")
    BalanceModifierSet armyModifierSet;

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

#endif
