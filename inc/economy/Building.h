#ifndef BUILDING_H
#define BUILDING_H

#include <algorithm>
#include <array>
#include <memory>

#include "core/Utils.h"
#include "data/Resource.h"
#include "simulation/Transport.h"
#include "core/Stat.h"
#include "warfare/UnitStats.h"
#include "warfare/Division.h"
#include "economy/BuildingComponents.h"

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
    SupplyHub = 25,

    Smith = 31,
    University = 32,
    Well = 33,
    WheatFarm = 34,
    Mint = 35,
    Glassworks = 36,
    Powderworks = 37
};

// What deposit (if any) sits on a tile — read by Mine/Woodcutter terrain_production.
enum class TileType : int
{
    GRASS = 0,
    WOOD = 1,
    COAL = 2,
    IRON_ORE = 3,
    STONE = 4,
    COPPER_ORE = 5,
    TIN_ORE = 6,
    SILVER_ORE = 7,
    GOLD_ORE = 8,
    SAND = 9,
    SULFUR = 10,
    SALTPETER = 11
};

// Coarse terrain region driving resource placement (and, later, ground visuals).
// See docs/resource_world_design.md.
enum class BiomeType : int
{
    PLAINS = 0,
    FOREST = 1,
    HILLS = 2,
    MOUNTAINS = 3,
    DESERT = 4,
    WETLAND = 5
};

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
    // False while the building waits in the build queue with no free builder
    // assigned. Set every tick by the owner's ConstructionQueue::Refresh.
    bool constructionActive{true};
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
    // Fallback home for field divisions whose building was captured/destroyed, so
    // deployed troops are not deleted along with the lost building.
    GarrisonComponent     garrison;
    // BUG 3c: HQ is a full supply depot — it receives weapon/food/materiel
    // packages just like any military building, and deployed divisions in
    // range can draw from its stockpile.
    SupplyBufferComponent supplyBuffer;
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

// True for defensive works whose garrison physically mans the building. The
// Barracks (a training factory) and the Headquarters (the capital, and the
// fallback home for field divisions) deliberately do NOT count: they can be
// attacked, but divisions never station inside or defend them from within, and
// the GUI must not present them as garrison buildings.
inline bool IsDefensiveGarrisonBuilding(const Building& building)
{
    return building.buildingType == BuildingType::GuardTower ||
           building.buildingType == BuildingType::Fortress ||
           building.buildingType == BuildingType::Castle;
}

// Buildings an army may besiege and capture: defensive works and the enemy
// capital (taking the HQ is the conquest path). Civil buildings — the Barracks
// factory included — are NOT military targets: divisions neither siege them nor
// do they count as attack destinations for the AI.
inline bool IsMilitaryAttackTarget(const Building& building)
{
    return IsDefensiveGarrisonBuilding(building) ||
           building.buildingType == BuildingType::Headquarters;
}

// Logistics building (pilot): draws finished gear + rations from the player's
// storage network on demand, converts them into weapon-supply packages and ships
// them to the front. It owns no storage of its own — it never stockpiles
// equipment. All behaviour lives in the dedicated SupplyPackageComponent.
class SupplyHub : public Building
{
public:
    SupplyHub() = default;
    SupplyHub(int);

    SupplyPackageComponent packaging;
};

#endif
