#ifndef BUILDING_COMPONENTS_H
#define BUILDING_COMPONENTS_H

#include "data/Resource.h"
#include "core/Stat.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class Building;

enum class TileType : int;
struct ResourceBufferView;
struct BuildingConnectionView;

struct ProductionRecipeRuntime
{
    std::string name;
    double cycleTime{0.0};
    std::map<ResourceType, int> inputs;
    std::map<ResourceType, int> outputs;
    std::map<ResourceType, int> inputBufferCapacities;
    std::map<ResourceType, int> outputBufferCapacities;
    int workerCapacity{5};
};

enum class BuildingCapability : std::size_t
{
    Production = 0,
    Logistics,
    Workers,
    Recipes,
    Research,
    Storage,
    Population,
    Road,
    Recruitment,
    Hq,
    TowerCombat,
    Upgrade,
    Count
};

constexpr std::size_t BuildingCapabilityCount = static_cast<std::size_t>(BuildingCapability::Count);
using BuildingCapabilitySet = std::bitset<BuildingCapabilityCount>;

// Base interface for all building capability components.
// Components own a distinct slice of building state and behaviour.
class IBuildingComponent
{
public:
    virtual ~IBuildingComponent() = default;
    virtual BuildingCapability GetCapability() const { return BuildingCapability::Count; }
    virtual void Update(Building& self, double dt) {}
    virtual void OnAttached(Building& self) {}
};

struct RoadComponent : IBuildingComponent
{
    int upgradeLevel{1};
    Stat<int> maxCapacity{BalanceStat::RoadCapacity, 5};
    Stat<double> speedModifier{BalanceStat::RoadSpeed, 1.0};

    BuildingCapability GetCapability() const override { return BuildingCapability::Road; }
    int GetModifiedMaxCapacity(const Building& self) const;
    double GetModifiedSpeedModifier(const Building& self) const;
};

// --- UpgradeComponent ---
// Generic, player-triggered per-instance upgrade progression — introduced
// for Road but deliberately not road-specific (any future BuildingType can
// register one). `level` starts at 1 (baseline, not itself upgradeable-to)
// and climbs toward `maxLevel`; per-level cost/duration/effect data lives in
// BuildingUpgradeLevelDefinition (BuildingConfig.h), looked up by level from
// the owning building's BuildingDefinition::upgradeLevels — this component
// only holds the live per-instance state, not the data.
struct UpgradeComponent : IBuildingComponent
{
    int level{1};
    int maxLevel{1};
    bool isUpgrading{false};
    // Mirrors Building::constructionActive but kept separate: an upgrading
    // building must NOT report IsUnderConstruction() (that would block
    // transport, SetReceiver, etc. — see GameWorld.Commands.cpp), so it
    // can't share constructionRemaining/constructionActive. ConstructionQueue
    // sets this the same way it sets constructionActive, sharing the same
    // builder-count pool.
    bool upgradeActive{true};
    double upgradeRemaining{0.0};

    BuildingCapability GetCapability() const override { return BuildingCapability::Upgrade; }
    void Update(Building& self, double dt) override;
};

// --- ProductionComponent ---
// Owns the time-based production cycle: input buffers, cycle timer, output generation,
// and terrain-richness consumption.
struct ProductionComponent : IBuildingComponent
{
    TileType terrainType;                          // terrain tile type this producer targets
    std::map<ResourceType, int> ingredients;       // recipe inputs (type -> amount per cycle)
    std::map<ResourceType, int> products;          // recipe outputs (type -> amount per cycle)
    Stat<double> cycleTime{BalanceStat::ProductionCycleTime, 0.0};
    double elapsed{0.0};
    bool started{false};
    std::map<ResourceType, ResourceBuffer> inputBuffers;
    std::map<ResourceType, ResourceBuffer> outputBuffers;
    bool consumesTerrain{true};
    int totalProduced{0};

    ProductionComponent();

    BuildingCapability GetCapability() const override { return BuildingCapability::Production; }
    void Update(Building& self, double dt) override;
    void Produce(Building& self, double dt);
    // Fraction of the CURRENT cycle elapsed, against the same (tech/focus-
    // modified) cycle time Produce() actually completes the cycle against —
    // using the unmodified base here would desync the reported percentage
    // from the real completion point (see GetProgress's definition).
    float GetProgress(const Building& self) const;
    double GetModifiedCycleTime(const Building& self) const;
    // Cycle time adjusted for current worker efficiency; infinity when idle.
    double GetEffectiveCycleTime(const Building& self) const;
    int GetModifiedOutputAmount(const Building& self, ResourceType type, int base) const;
    bool HasTerrainRichness(const Building& self) const;
    bool ConsumeTerrainRichness(Building& self);

    std::vector<ResourceBufferView> GetInputBufferViews(const std::map<ResourceType,int>& recipe) const;
    std::vector<ResourceBufferView> GetOutputBufferViews(const Building& self) const;
};

// --- LogisticsComponent ---
// Manages supplier/receiver connections and inbound transport-request bookkeeping.
struct LogisticsComponent : IBuildingComponent
{
    std::map<ResourceType, std::vector<Building*>> suppliers;
    std::map<ResourceType, Building*>              receivers;
    std::map<ResourceType, Building*>              altReceivers;
    std::map<ResourceType, int>                    pendingRequests;
    bool requestBlocked{false};

    BuildingCapability GetCapability() const override { return BuildingCapability::Logistics; }
    bool HasSupplier(ResourceType type) const;
    bool HasReceiver(ResourceType type) const;
    bool AcceptsSupplierFor(ResourceType type, const Building* supplier) const;

    void SetSupplier(ResourceType type, Building* supplier, Building& self);
    void SetReceiver(ResourceType type, Building* receiver, Building& self,
                     ProductionComponent& prod);
    void SetAltReceiver(ResourceType type, Building* receiver, Building& self);
    void RemoveSupplier(ResourceType type, Building* supplier);
    void RemoveReceiver(ResourceType type, Building* receiver, Building& self,
                        ProductionComponent& prod);

    int RequestResource(ResourceType type, int amount, Building& self);
    void MaintainRequests(Building& self, ProductionComponent& prod);
    void DispatchOutputs(Building& self, ProductionComponent& prod);
    int HandleTransportFrom(ResourceType type, int amount, Building* receiver,
                            Building& self, ProductionComponent& prod);
    // AI rework (TODO #2): whether this building has a physical road path to
    // ANY of its owner's storage-like buildings (HQ included) — the "is it
    // wired into the logistics network" check the AI keeps every building
    // honest with. Order-independent (boolean OR over Player::storages), so
    // lockstep-safe. Ignores construction state: it inspects roads only.
    // Not free (one road-network BFS per storage until a hit) — callers on a
    // per-tick path should throttle/cache.
    bool IsConnectedToRoadNetwork(Building& self) const;

    std::vector<BuildingConnectionView> GetSupplierViews(const ProductionComponent& prod) const;
    std::vector<BuildingConnectionView> GetReceiverViews(const ProductionComponent& prod) const;
};

// --- WorkerComponent ---
// Tracks assigned-worker count and worker-slot capacity for production buildings.
struct WorkerComponent : IBuildingComponent
{
    Stat<int> capacity{BalanceStat::WorkerCapacity, 5};
    int assigned{0};

    BuildingCapability GetCapability() const override { return BuildingCapability::Workers; }
    float GetRatio() const;
    int GetModifiedCapacity(const Building& self) const;
};

struct RecipeComponent : IBuildingComponent
{
    std::vector<ProductionRecipeRuntime> recipes;
    int activeRecipeIndex{0};

    BuildingCapability GetCapability() const override { return BuildingCapability::Recipes; }
    bool HasSelectableRecipes() const;
    std::string GetActiveRecipeName() const;
    void SetRecipes(std::vector<ProductionRecipeRuntime> newRecipes,
                    Building& self,
                    ProductionComponent& production,
                    LogisticsComponent& logistics,
                    WorkerComponent& workers);
    bool SetActiveRecipe(int index,
                         Building& self,
                         ProductionComponent& production,
                         LogisticsComponent& logistics,
                         WorkerComponent& workers);
    bool CycleRecipe(Building& self,
                     ProductionComponent& production,
                     LogisticsComponent& logistics,
                     WorkerComponent& workers);
};

// --- ResearchComponent ---
// Tracks active technology-research progress for the University building.
struct ResearchComponent : IBuildingComponent
{
    std::string technologyId;
    double remaining{0.0};
    double total{0.0};

    BuildingCapability GetCapability() const override { return BuildingCapability::Research; }
    bool Start(const std::string& id, double time);
    // Returns true when research just completed (remaining reached 0).
    bool Tick(double dt);
    double GetProgress() const;
};

// --- StorageComponent ---
// Generic multi-resource storage hub (Headquarters, StorageBuilding).
struct StorageComponent : IBuildingComponent
{
    std::map<ResourceType, ResourceBuffer> buffers;

    BuildingCapability GetCapability() const override { return BuildingCapability::Storage; }
    bool CanAccept(ResourceType type) const;
    bool CanReceive(ResourceType type) const;
    void AddResource(Resource* res, Building& self);
    void ReturnOutgoingResource(Resource* res);
    Resource GetResource(ResourceType type);
    int HandleTransport(ResourceType type, int amount, Building* receiver, Building& self);
    void Update(Building& self, double dt) override;

    std::vector<ResourceBufferView> GetBufferViews() const;
};

// --- PopulationComponent ---
// Manpower generation and food-supply tracking for Village buildings.
struct PopulationComponent : IBuildingComponent
{
    Stat<double> manpowerRate{BalanceStat::ManpowerRate, 5.0};
    Stat<int> populationCap{BalanceStat::PopulationCap, 1000};
    double upkeepTimer{0.0};
    double upkeepInterval{10.0};
    double foodPackageUpkeep{1.0};
    bool hasFood{true};
    double foodSupplyLevel{1.0};
    double foodSupplyDropPerMissedUpkeep{0.25};
    ResourceBuffer foodBuffer{ResourceType::FOOD_PROVISIONS, 1};

    BuildingCapability GetCapability() const override { return BuildingCapability::Population; }
    void Update(Building& self, double dt) override;
    double GetFoodSupplyRatio() const;
    double GetManpowerProductivity() const;
    double GetWorkerProductivity() const;
    int RequestFoodSupply(Building& self);
    int GetFoodDemand() const;
};

// --- RecruitmentComponent ---
// One in-progress recruit order: which unit definition, and how much of its
// recruitTime remains. Manpower is deducted up front when the order is
// queued (QueueRecruitment). Resources may not be — resourcesReady is false
// while this entry is still waiting on a RequestResource delivery; `total`/
// `remaining` only start counting down once resourcesReady flips true (see
// RecruitmentComponent::Update).
struct RecruitmentQueueEntry
{
    std::string unitDefId;
    double total{0.0};
    double remaining{0.0};
    bool resourcesReady{true};
};

// Recruitment queue for a unit-producing building (Barracks; future
// Stables/MageTower/Workshop are only new UnitDefinition::recruitBuilding
// values, no new component). User request (docs/work_plan_2026-07-13.md,
// 2026-07-15 + TODO #1 2026-07-16): an order joins the queue immediately on
// click (as long as manpower allows), tagged "waiting for resources" if its
// cost isn't already sitting in this building's own StorageComponent buffer.
// Update() then works the queue in strict FIFO order: entries flip
// resourcesReady (consuming their cost) as deliveries land — even behind a
// training front entry — while only the FIRST waiting entry requests its
// shortfall from the road network, net of what's already in flight, so the
// building orders exactly one unit's cost at a time and never stockpiles.
// The timed build runs only for the front entry once it's resourcesReady.
// On completion the finished unit is added to the owning player's
// UnitRoster in state InRoster.
struct RecruitmentComponent : IBuildingComponent
{
    std::deque<RecruitmentQueueEntry> queue;

    BuildingCapability GetCapability() const override { return BuildingCapability::Recruitment; }
    void Update(Building& self, double dt) override;
    // Queues one unit order now: validates the unit exists and the owner has
    // enough manpower (deducted immediately — the only hard gate), then
    // pushes a queue entry. If no earlier entry is still waiting and this
    // building's own buffer already covers the resource cost, it's consumed
    // now and the entry starts counting down right away; otherwise the entry
    // waits (see RecruitmentQueueEntry::resourcesReady) and Update() requests
    // the shortfall in-flight-aware, one unit at a time, strict FIFO.
    // Returns false only when manpower alone is insufficient — resource
    // unavailability never blocks queueing.
    bool QueueRecruitment(Building& self, const std::string& unitDefId);
    // Non-mutating read, for GUI/AI feasibility — returns an empty string
    // when the unit's resource cost exists SOMEWHERE in the player's global
    // storage network and manpower is sufficient, otherwise a short
    // human-readable reason. Deliberately global even though QueueRecruitment
    // itself only checks this building's local buffer: this is what lets the
    // GUI button / AI candidate generation offer recruiting before anything
    // has physically arrived — if this also checked only the local buffer
    // (always empty for a fresh order), nothing would ever attempt
    // QueueRecruitment, so the first RequestResource would never fire.
    std::string DiagnoseRecruitmentBlock(const Building& self, const std::string& unitDefId) const;
};

// --- HqComponent ---
// HP/defense state for a player's Headquarters (TD etap-6). Losing all HP
// eliminates the owner (see GameWorld::EliminatePlayer) — a real war-system
// requirement, unlike the pre-rework Headquarters which was indestructible.
struct HqComponent : IBuildingComponent
{
    Stat<double> maxHp{BalanceStat::HqMaxHp, 500.0};
    double currentHp{500.0};
    Stat<double> hardDefense{BalanceStat::HqDefense, 0.0};
    Stat<double> thornsDamage{BalanceStat::HqThorns, 0.0};
    // Not BalanceStat-wrapped (like UnitDefinition::attackRange) — a fixed
    // data-driven cadence, not something tech/focus buffs are expected to
    // touch in v1.
    double thornsInterval{3.0};
    double thornsTimer{0.0};
    // Conquest spoils (TD etap-6.3), read once at elimination time.
    double captureStockFraction{0.4};
    double conquestRampDuration{600.0};

    // Render-only "under attack" indicator (ticked in HqCombatSystem::Update,
    // set whenever siege damage lands) — deliberately NOT persisted/saved,
    // resetting to 0 after a load is an acceptable trade-off for a transient
    // HUD cue.
    double recentDamageTimer{0.0};

    BuildingCapability GetCapability() const override { return BuildingCapability::Hq; }
    double GetModifiedMaxHp(const Building& self) const;
    double GetModifiedHardDefense(const Building& self) const;
    double GetModifiedThornsDamage(const Building& self) const;
};

// --- TowerCombatComponent ---
// Combat stats + attack cooldown for a DefenseTower (TD etap-7). Ammo itself
// lives in the building's own StorageComponent buffer (one entry, keyed by
// `ammoResource`) — reusing the same road-network delivery path as any
// production building's inputs, not a separate ammo-tracking mechanism.
// The chosen priority is stored; the concrete target is still resolved fresh
// for every shot, so dead or out-of-range units are never retained.
enum class TowerTargetMode
{
    NearestToHq,
    StrongestUnit
};

struct TowerCombatComponent : IBuildingComponent
{
    Stat<double> damage{BalanceStat::TowerDamage, 3.5};
    Stat<double> range{BalanceStat::TowerRange, 6.0};
    Stat<double> attackSpeed{BalanceStat::TowerAttackSpeed, 1.0};
    double attackTimer{0.0};
    ResourceType ammoResource{ResourceType::ARROWS};
    // Ammo consumed per shot, reduced (floored at 0 — a strong enough bonus
    // makes shots free) by BalanceStat::TowerAmmoEfficiency.
    Stat<int> ammoPerShot{BalanceStat::TowerAmmoEfficiency, 1};
    TowerTargetMode targetMode{TowerTargetMode::NearestToHq};

    BuildingCapability GetCapability() const override { return BuildingCapability::TowerCombat; }
    // Tops up the ammo buffer via the road network every tick (the
    // MaintainInputRequests pattern production buildings use, minus the
    // ProductionComponent coupling that pattern normally requires — a tower
    // has no recipe, just one buffer to keep full).
    void Update(Building& self, double dt) override;
    double GetModifiedDamage(const Building& self) const;
    double GetModifiedRange(const Building& self) const;
    double GetModifiedAttackSpeed(const Building& self) const;
    int GetModifiedAmmoPerShot(const Building& self) const;
};

template<typename T>
constexpr BuildingCapability GetBuildingComponentCapability()
{
    return BuildingCapability::Count;
}

template<> constexpr BuildingCapability GetBuildingComponentCapability<ProductionComponent>() { return BuildingCapability::Production; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<LogisticsComponent>() { return BuildingCapability::Logistics; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<WorkerComponent>() { return BuildingCapability::Workers; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<RecipeComponent>() { return BuildingCapability::Recipes; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<ResearchComponent>() { return BuildingCapability::Research; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<StorageComponent>() { return BuildingCapability::Storage; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<PopulationComponent>() { return BuildingCapability::Population; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<RoadComponent>() { return BuildingCapability::Road; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<RecruitmentComponent>() { return BuildingCapability::Recruitment; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<HqComponent>() { return BuildingCapability::Hq; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<TowerCombatComponent>() { return BuildingCapability::TowerCombat; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<UpgradeComponent>() { return BuildingCapability::Upgrade; }

#endif
