#ifndef BUILDING_COMPONENTS_H
#define BUILDING_COMPONENTS_H

#include "Resource.h"
#include "Stat.h"

#include <bitset>
#include <cstddef>
#include <deque>
#include <map>
#include <string>
#include <vector>

class Building;

enum class TileType : int;
enum class MilitaryOrderType : int;
enum class MilitaryUnitType : int;
struct SoldierDivision;
using MilitaryDivision = SoldierDivision;
struct ResourceBufferView;
struct BuildingConnectionView;
struct SupplyBufferComponent;

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
    Territory,
    Garrison,
    Supply,
    Recruitment,
    Population,
    Road,
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
    float GetProgress() const;
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

// --- TerritoryComponent ---
// Projects territory and exposes HP / attackability for military & HQ buildings.
struct TerritoryComponent : IBuildingComponent
{
    Stat<int> radius{BalanceStat::TerritoryRadius, 0};
    int hp{0};
    Stat<int> maxHp{BalanceStat::HitPoints, 0};
    int baseStrength{0};

    BuildingCapability GetCapability() const override { return BuildingCapability::Territory; }
    int GetRadius(const Building& self) const;
    int GetMaxHp(const Building& self) const;
    void ReceiveDamage(int damage);
};

// --- GarrisonComponent ---
// Manages stationed divisions and building-level combat orders.
struct GarrisonComponent : IBuildingComponent
{
    Stat<int> cap{BalanceStat::GarrisonCapacity, 0};
    Stat<int> strength{BalanceStat::MilitaryStrength, 0};
    int militia{0};
    int swordsmen{0};
    int archers{0};
    int garrison{0};
    int nextDivisionId{1};
    std::vector<MilitaryDivision> divisions;
    MilitaryOrderType currentOrder;
    int orderTargetId{-1};
    double orderCooldown{0.0};

    GarrisonComponent();

    BuildingCapability GetCapability() const override { return BuildingCapability::Garrison; }
    void Update(Building& self, double dt) override;
    void IssueOrder(MilitaryOrderType order, int targetId);
    bool IssueDivisionOrder(int divisionId, MilitaryOrderType order, int targetId,
                             Building& self);
    void StartAllDivisionsMovement(Building& self, Building& target);
    void ClearOrder();
    bool HasActiveDivisionOrders() const;
    int GetTotalTroops() const;
    int GetFreeGarrisonSpace(const Building& self) const;
    int GetDivisionCap(const Building& self) const;
    int GetFreeDivisionSpace(const Building& self) const;
    int GetAverageMorale() const;
    int GetAverageExperience() const;
    int GetEffectiveStrength(const Building& self) const;
    int GetModifiedAttackDamage(const Building& self) const;
    void Recount();
};

// --- SupplyBufferComponent ---
// Manages the FOOD_PROVISIONS buffer used by military buildings.
struct SupplyBufferComponent : IBuildingComponent
{
    ResourceBuffer buffer{ResourceType::FOOD_PROVISIONS, 0};
    Stat<int> capacity{BalanceStat::SupplyCapacity, 0};
    int stored{0};

    BuildingCapability GetCapability() const override { return BuildingCapability::Supply; }
    bool CanReceive() const;
    void AddResource(Resource* res);
    void ReturnOutgoingResource(Resource* res);
    Resource GetResource();
    int HandleTransport(int amount, Building* receiver, Building& self);
    int GetModifiedCapacity(const Building& self) const;
    int GetSupplyConsumption(const Building& self, const GarrisonComponent& garrison) const;
};

// --- RecruitmentComponent ---
// Manages the unit-training queue for Barracks buildings.
struct RecruitmentComponent : IBuildingComponent
{
    struct Job
    {
        MilitaryUnitType type;
        double remaining{0.0};
        Job();
        Job(MilitaryUnitType t, double r);
    };

    std::deque<Job> queue;

    BuildingCapability GetCapability() const override { return BuildingCapability::Recruitment; }
    void Update(Building& self, double dt) override;
    bool QueueUnit(MilitaryUnitType type, Building& self, GarrisonComponent& garrison);
};

// --- PopulationComponent ---
// Manpower generation and food-supply tracking for Village buildings.
struct PopulationComponent : IBuildingComponent
{
    Stat<double> manpowerRate{BalanceStat::ManpowerRate, 0.2};
    Stat<int> populationCap{BalanceStat::PopulationCap, 80};
    double upkeepTimer{0.0};
    double upkeepInterval{10.0};
    double foodPackageUpkeep{1.0};
    bool hasFood{true};
    double foodSupplyLevel{1.0};
    double foodSupplyDropPerMissedUpkeep{0.25};
    ResourceBuffer foodBuffer{ResourceType::FOOD_PROVISIONS, 3};

    BuildingCapability GetCapability() const override { return BuildingCapability::Population; }
    void Update(Building& self, double dt) override;
    double GetFoodSupplyRatio() const;
    double GetManpowerProductivity() const;
    double GetWorkerProductivity() const;
    int RequestFoodSupply(Building& self);
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
template<> constexpr BuildingCapability GetBuildingComponentCapability<TerritoryComponent>() { return BuildingCapability::Territory; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<GarrisonComponent>() { return BuildingCapability::Garrison; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<SupplyBufferComponent>() { return BuildingCapability::Supply; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<RecruitmentComponent>() { return BuildingCapability::Recruitment; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<PopulationComponent>() { return BuildingCapability::Population; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<RoadComponent>() { return BuildingCapability::Road; }

#endif
