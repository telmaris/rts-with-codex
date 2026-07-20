#ifndef BUILDING_H
#define BUILDING_H

#include <algorithm>
#include <array>
#include <memory>

#include "core/Utils.h"
#include "data/Resource.h"
#include "simulation/Transport.h"
#include "core/Stat.h"
#include "economy/BuildingComponents.h"

class Player;
class Tile;

enum class BuildingType : int
{
    Building = 0,
    ProductionBuilding = 1,
    StorageBuilding = 2,
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

    Barracks = 24,

    Smith = 31,
    University = 32,
    Well = 33,
    WheatFarm = 34,
    Mint = 35,
    Glassworks = 36,
    Powderworks = 37,

    // TD(etap-7): one class handles every tower tier (data-driven, like
    // BattleUnit) rather than a Woodcutter/Mine-style class per tier — so
    // this is the only tower BuildingType value needed for now.
    DefenseTower = 40,

    // B6 (docs/work_plan_2026-07-13.md): resource-road crossing over an
    // isMilitaryRoad tile — the only building type whose placement rule
    // REQUIRES that ground instead of refusing it (see TileMap::
    // CanBuildFootprint). Appended at the end so old save files (which
    // serialize this enum as a plain int) keep loading unchanged.
    Bridge = 41
};

// True for every building type the resource-road network (RoadNetwork/
// NavigationMap) treats as a traversable road node — Road itself plus Bridge
// (B6), which is functionally a Road that happens to sit on a military-road
// tile. Single source of truth so a future road-like type only needs to be
// added here, not at every "is this tile a road" call site.
inline bool IsRoadLike(BuildingType type)
{
    return type == BuildingType::Road || type == BuildingType::Bridge;
}

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
    // True when `supplier` is one of this building's explicitly wired
    // suppliers for `type`, or when no supplier is wired at all yet (nothing
    // to conflict with). False when a *different* supplier is already wired
    // — lets ambient storage-to-storage redistribution (StorageComponent::
    // Update) respect an explicit supplier reassignment instead of feeding a
    // resource through the old link anyway.
    bool AcceptsSupplierFor(ResourceType type, const Building* supplier) const;
    bool IsStorageLike() const;
    float GetProductionProgress() const;
    float GetWorkerRatio() const;
    int GetAssignedWorkers() const;
    int GetWorkerCapacity() const;
    bool IsProductionStalled() const;
    bool CanBlockProduction() const;

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
    UpgradeComponent upgrade;
    int GetModifiedMaxCapacity() const;
    double GetModifiedSpeedModifier() const;
};

// Resource-road crossing over the immutable military road (B6, docs/
// work_plan_2026-07-13.md) — a ring/edge of the unit track can otherwise cut
// off part of the map from the resource-road network (nothing may be built
// on isMilitaryRoad tiles), stranding whatever is on the far side. Bridge is
// a Road in every way that matters to the logistics network (same
// RoadComponent, same IsRoadLike() treatment in RoadNetwork) — the only
// difference is TileMap::CanBuildFootprint's placement rule, which REQUIRES
// isMilitaryRoad ground instead of refusing it. Marching units are
// unaffected: UnitMarchSystem walks the ring's own precomputed tile list, not
// building occupancy, so a bridge sitting on a track tile doesn't block or
// alter marching. Bridges have no HP (consistent with towers being the only
// combat-capable structure today) — they can only be removed by their owner.
class Bridge : public Building
{
public:
    Bridge() = default;
    Bridge(int i);

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

// Player's starting building: a storage hub with HP/defense (HqComponent,
// TD etap-6). Never manually destroyed by its own owner — falls only to
// siege damage (UnitCombatSystem/HqCombatSystem), which triggers elimination.
class Headquarters : public Building
{
public:
    Headquarters() = default;
    Headquarters(int);

    bool CanBeManuallyDestroyed() const override { return false; }

    // --- Component members ---
    StorageComponent storage;
    HqComponent hq;
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

// Recruitment factory. Holds a StorageComponent buffer for delivered unit
// costs, a LogisticsComponent that actively requests those costs over the
// road network (T3 fix, docs/post_pivot_audit_2026-07-12.md — mirrors
// DefenseTower's ammo pull, RecruitmentComponent::Update drives it every
// tick), and a RecruitmentComponent queue that spends those resources plus
// player manpower to produce BattleUnit instances into the owner's roster.
class Barracks : public Building
{
public:
    Barracks() = default;
    Barracks(int);

    // --- Component members ---
    StorageComponent storage;
    LogisticsComponent logistics;
    RecruitmentComponent recruitment;
};

// Defensive tower (TD etap-7). One class handles every tower tier — combat
// stats are data-driven (TowerCombatComponent), same as Barracks handles
// every recruitable unit type via UnitDefinition rather than a class per
// unit. Ammo is an ordinary StorageComponent buffer fed by the existing road
// network (LogisticsComponent); crew is an ordinary WorkerComponent, so
// manpower auto-returns on destruction for free via the existing generic path.
class DefenseTower : public Building
{
public:
    DefenseTower() = default;
    DefenseTower(int);

    // --- Component members ---
    StorageComponent storage;
    LogisticsComponent logistics;
    WorkerComponent workers;
    TowerCombatComponent combat;
};

#endif
