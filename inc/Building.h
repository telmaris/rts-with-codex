#ifndef BUILDING_H
#define BUILDING_H

#include <algorithm>
#include <array>

#include "Utils.h"
#include "Resource.h"
#include "Transport.h"
#include "Stat.h"
#include "BuildingComponents.h"

class Player;
class Tile;

enum class BuildingType : int
{
    Building = 0,
    ProductionBuilding = 1,
    StorageBuilding = 2,
    MilitaryBuilding = 3,
    Road = 4,
    Headquarters = 5,

    Woodcutter = 11,
    LumberMill = 12,
    Mine = 13,
    Foundry = 14,
    Village = 15,
    HuntersHut = 16,
    Windmill = 17,
    Bakery = 18,
    Inn = 19,
    Paperworks = 20,

    GuardTower = 21,
    Fortress = 22,
    Castle = 23,
    Barracks = 24,

    Smith = 31,
    University = 32,
    Well = 33,
    WheatFarm = 34
};

enum class TileType : int
{
    GRASS = 0,
    WOOD = 1,
    COAL = 2,
    IRON_ORE = 3,
    STONE = 4
};

enum class MilitaryOrderType : int
{
    None = 0,
    Attack = 1,
    Support = 2,
    Defend = 3
};

enum class MilitaryUnitType : int
{
    Militia = 0,
    Swordsman = 1,
    Archer = 2
};

const char* MilitaryUnitLabel(MilitaryUnitType type);
double GetBaseRecruitmentTime(MilitaryUnitType type);
int GetBaseRecruitmentManpowerCost(MilitaryUnitType type);
std::vector<std::pair<ResourceType, int>> GetBaseRecruitmentResourceCosts(MilitaryUnitType type);

struct DivisionEquipment
{
    ResourceType weapon{ResourceType::Null};
    ResourceType armor{ResourceType::Null};
    ResourceType rangedWeapon{ResourceType::Null};
    ResourceType ammo{ResourceType::Null};
};

class SoldierDivision
{
public:
    SoldierDivision() = default;

    int id{0};
    MilitaryUnitType type{MilitaryUnitType::Militia};
    int manpowerScale{10};
    int maxHealth{100};
    int health{100};
    int endurance{50};
    int strength{10};
    int morale{60};
    int experience{0};
    int foodSupply{0};
    int foodSupplyCapacity{0};
    int weaponSupply{0};
    int weaponSupplyCapacity{0};
    double speedTilesPerMinute{12.0};
    MilitaryOrderType currentOrder{MilitaryOrderType::None};
    int orderTargetPositionId{-1};
    double orderCooldown{0.0};
    DivisionEquipment equipment;

    float HealthRatio() const
    {
        return maxHealth > 0 ? std::clamp(health / static_cast<float>(maxHealth), 0.0f, 1.0f) : 0.0f;
    }
};

using MilitaryDivision = SoldierDivision;

SoldierDivision CreateMilitaryDivision(MilitaryUnitType type, int id);

struct ResourceBufferView
{
    ResourceType type{ResourceType::Null};
    int amount{0};
    int capacity{0};
    int recipeAmount{0};
};

struct BuildingConnectionView
{
    ResourceType type{ResourceType::Null};
    Building* building{nullptr};
    bool alternative{false};
};

// Base gameplay object placed on one or more map tiles.
// A building is little more than an id, a lifecycle, and a set of capability
// components (IBuildingComponent). All resource-flow and capability queries are
// routed to the relevant component via GetComponent<T>(); concrete building
// classes only assemble the components they need in their constructor.
class Building
{
public:
    Building() = default;
    Building(int i) : id(i) {}
    virtual ~Building() = default;

    // Default tick: advances construction/lifetime, runs every component's
    // Update in registration order, then advances in-flight transportables.
    // Concrete buildings only need to register the right components.
    virtual void Update(double dt);
    // Default: seed the producer's terrain type when present. Buildings with no
    // production component ignore this; terrain-specialised producers override it.
    virtual void InitBuilding(TileType t);
    virtual bool CanBeManuallyDestroyed() const { return true; }

    // --- Resource-flow facade: routed to the building's resource components ---
    // No-ops when the building owns no component handling that resource.
    void AddResource(Resource* res);
    Resource GetResource(ResourceType type);
    void ReturnOutgoingResource(Resource* res);
    void CancelRequestedResource(ResourceType type);
    void SetSupplier(ResourceType type, Building* supplier);
    void SetReceiver(ResourceType type, Building* receiver);
    void SetAlternativeReceiver(ResourceType type, Building* receiver);
    void RemoveSupplier(ResourceType type, Building* supplier);
    void RemoveReceiver(ResourceType type, Building* receiver);
    int  HandleTransport(ResourceType type, int amount, Building* receiver);
    bool CanAcceptResource(ResourceType type) const;
    bool CanReceiveResource(ResourceType type) const;

    // --- Capability queries: routed to components, empty/zero when absent ---
    std::vector<ResourceBufferView> GetInputBufferViews() const;
    std::vector<ResourceBufferView> GetOutputBufferViews() const;
    std::vector<BuildingConnectionView> GetSupplierViews() const;
    std::vector<BuildingConnectionView> GetReceiverViews() const;
    bool HasSupplier(ResourceType type) const;
    bool HasReceiver(ResourceType type) const;
    bool IsStorageLike() const;
    float GetProductionProgress() const;
    float GetWorkerRatio() const;
    int GetAssignedWorkers() const;
    int GetWorkerCapacity() const;
    bool IsProductionStalled() const;
    bool CanBlockProduction() const;
    int GetHitPoints() const;
    int GetTerritoryRadius() const;

    bool IsProductionBlocked() const { return productionBlocked; }
    void SetProductionBlocked(bool blocked) { productionBlocked = blocked; }
    Vec2i GetFootprint() const { return footprint; }
    int GetTextureId() const { return textureId; }
    int GetTotalProduced() const { return totalProduced; }
    float GetEfficiency() const;
    double GetLifetime() const { return lifetime; }
    double GetActiveTime() const { return activeTime; }
    bool IsUnderConstruction() const { return constructionRemaining > 0.0; }
    float GetConstructionProgress() const;
    double GetModifiedTransportTime() const;

    // Component registry — subclass constructors call RegisterComponent for each owned component.
    template<typename T>
    bool HasComponent() const
    {
        constexpr BuildingCapability capability = GetBuildingComponentCapability<T>();
        if constexpr (capability != BuildingCapability::Count)
            return HasCapabilityFlag(capability);
        else
            return false;
    }

    template<typename T>
    T* GetComponent()
    {
        constexpr BuildingCapability capability = GetBuildingComponentCapability<T>();
        if constexpr (capability != BuildingCapability::Count)
        {
            auto index = static_cast<std::size_t>(capability);
            return HasCapabilityFlag(capability) ? static_cast<T*>(m_componentSlots[index]) : nullptr;
        }
        else
        {
            return nullptr;
        }
    }

    template<typename T>
    const T* GetComponent() const
    {
        constexpr BuildingCapability capability = GetBuildingComponentCapability<T>();
        if constexpr (capability != BuildingCapability::Count)
        {
            auto index = static_cast<std::size_t>(capability);
            return HasCapabilityFlag(capability) ? static_cast<const T*>(m_componentSlots[index]) : nullptr;
        }
        else
        {
            return nullptr;
        }
    }

    void ReceptTransport(Transportable*);
    void UpdateTransportables(double);
    bool UpdateConstruction(double dt);
    double BeginOperationalUpdate(double dt);

    Player* owner{nullptr};
    Tile* placement{nullptr};
    int id{0};
    int positionId{-1};
    std::string name{"Building - Generic"};
    BuildingType buildingType = BuildingType::Building;
    std::string tag;
    std::vector<Transportable*> transportables;
    Stat<double> transportTime{BalanceStat::TransportTime, 0.0};
    Vec2i footprint{1, 1};
    int textureId{0};
    bool productionBlocked{false};
    Stat<double> buildTime{BalanceStat::BuildTime, 0.0};
    double constructionRemaining{0.0};
    double lifetime{0.0};
    double activeTime{0.0};
    int totalProduced{0};

protected:
    void RegisterComponent(IBuildingComponent* component)
    {
        if (component == nullptr)
            return;

        m_components.push_back(component);
        BuildingCapability capability = component->GetCapability();
        if (IsValidCapability(capability))
        {
            auto index = static_cast<std::size_t>(capability);
            m_capabilities.set(index);
            m_componentSlots[index] = component;
        }
        component->OnAttached(*this);
    }

private:
    static bool IsValidCapability(BuildingCapability capability)
    {
        return static_cast<std::size_t>(capability) < BuildingCapabilityCount;
    }

    bool HasCapabilityFlag(BuildingCapability capability) const
    {
        return IsValidCapability(capability) &&
               m_capabilities.test(static_cast<std::size_t>(capability));
    }

    std::vector<IBuildingComponent*> m_components; // non-owning; owned by subclass members
    std::array<IBuildingComponent*, BuildingCapabilityCount> m_componentSlots{};
    BuildingCapabilitySet m_capabilities;
};

// Road tile that carries resource transportables between buildings.
class Road : public Building
{
public:
    Road() = default;
    Road(int i);

    RoadComponent road;
    int GetModifiedMaxCapacity() const;
    double GetModifiedSpeedModifier() const;
};

// Building that stores resources and serves as a logistics hub.
class StorageBuilding : public Building
{
public:
    StorageBuilding() = default;
    StorageBuilding(int);
    virtual ~StorageBuilding() = default;

    // --- Component member ---
    StorageComponent storage;
};

// Player's starting building: a storage hub that also projects territory.
class Headquarters : public Building
{
public:
    Headquarters() = default;
    Headquarters(int);

    bool CanBeManuallyDestroyed() const override { return false; }

    int GetMaxHitPoints() const { return territory.GetMaxHp(*this); }
    int GetEffectiveStrength() const { return 20; }
    void ReceiveDamage(int damage) { territory.ReceiveDamage(damage); }

    // --- Component members ---
    StorageComponent   storage;
    TerritoryComponent territory;
};

// Settlement that generates manpower and consumes food upkeep over time.
class Village : public Building
{
public:
    Village() = default;
    Village(int);

    double GetFoodSupplyRatio() const { return population.GetFoodSupplyRatio(); }
    double GetManpowerProductivity() const { return population.GetManpowerProductivity(); }
    double GetWorkerProductivity() const { return population.GetWorkerProductivity(); }
    int RequestFoodSupply() { return population.RequestFoodSupply(*this); }

    // --- Component member ---
    PopulationComponent population;
};

// Military buildings project territory, hold a garrison, and execute combat
// orders. All of that lives in the territory/garrison/supply components; the
// concrete classes only assemble the right set.
class GuardTower : public Building
{
public:
    GuardTower() = default;
    GuardTower(int);

    TerritoryComponent    territory;
    GarrisonComponent     garrison;
    SupplyBufferComponent supplyBuffer;
};

class Fortress : public Building
{
public:
    Fortress() = default;
    Fortress(int);

    TerritoryComponent    territory;
    GarrisonComponent     garrison;
    SupplyBufferComponent supplyBuffer;
};

class Castle : public Building
{
public:
    Castle() = default;
    Castle(int);

    TerritoryComponent    territory;
    GarrisonComponent     garrison;
    SupplyBufferComponent supplyBuffer;
};

class Barracks : public Building
{
public:
    Barracks() = default;
    Barracks(int);

    bool QueueRecruitment(MilitaryUnitType type)
        { return recruitment.QueueUnit(type, *this, garrison); }

    TerritoryComponent    territory;
    GarrisonComponent     garrison;
    SupplyBufferComponent supplyBuffer;
    RecruitmentComponent  recruitment;
};

#endif
