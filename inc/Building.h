#ifndef BUILDING_H
#define BUILDING_H

#include <algorithm>

#include "Utils.h"
#include "Resource.h"
#include "Transport.h"
#include "Stat.h"

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

// Readable UI/debug label for a military unit type.
const char* MilitaryUnitLabel(MilitaryUnitType type);

// Base recruitment duration before player modifiers.
double GetBaseRecruitmentTime(MilitaryUnitType type);

// Base manpower cost before player modifiers.
int GetBaseRecruitmentManpowerCost(MilitaryUnitType type);

// Placeholder material recruitment costs paid from storage-like buildings.
std::vector<std::pair<ResourceType, int>> GetBaseRecruitmentResourceCosts(MilitaryUnitType type);

// Individual division equipment loadout.
struct DivisionEquipment
{
    ResourceType weapon{ResourceType::Null};
    ResourceType armor{ResourceType::Null};
    ResourceType rangedWeapon{ResourceType::Null};
    ResourceType ammo{ResourceType::Null};
};

// A single controllable battlefield formation represented as a domain object.
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

// Creates a placeholder division template for a trained unit type.
SoldierDivision CreateMilitaryDivision(MilitaryUnitType type, int id);

// Lightweight UI-facing snapshot of one resource buffer.
struct ResourceBufferView
{
    ResourceType type{ResourceType::Null};
    int amount{0};
    int capacity{0};
    int recipeAmount{0};
};

// Lightweight UI-facing snapshot of one logistics connection.
struct BuildingConnectionView
{
    ResourceType type{ResourceType::Null};
    Building* building{nullptr};
};

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

// Capability interface for buildings that define territory and can be attacked.
class IMilitaryBuilding
{
public:
    virtual ~IMilitaryBuilding() = default;
    virtual bool IsAttackable() const = 0;
    virtual int GetTerritoryRadius() const = 0;
    virtual int GetHitPoints() const = 0;
    virtual int GetMaxHitPoints() const = 0;
    virtual int GetEffectiveStrength() const = 0;
    virtual void ReceiveDamage(int damage) = 0;
};

// Base gameplay object placed on one or more map tiles.
class Building
{
    public:
        Building() = default;
        Building(int i) : id(i) {}
        virtual ~Building() = default;

        virtual void Update(double) = 0;
        virtual void InitBuilding(TileType) = 0;

        virtual void AddResource(Resource*) = 0;
        virtual Resource GetResource(ResourceType) = 0;
        virtual void ReturnOutgoingResource(Resource*) {}
        virtual void CancelRequestedResource(ResourceType) {}

        virtual void SetSupplier(ResourceType, Building*) = 0;
        virtual void SetReceiver(ResourceType, Building*) = 0;
        virtual void RemoveSupplier(ResourceType, Building*) {}
        virtual void RemoveReceiver(ResourceType, Building*) {}
        virtual int HandleTransport(ResourceType res, int x, Building* building) = 0;

        virtual std::vector<ResourceBufferView> GetInputBufferViews() const { return {}; }
        virtual std::vector<ResourceBufferView> GetOutputBufferViews() const { return {}; }
        virtual bool IsStorageLike() const { return false; }
        virtual std::vector<BuildingConnectionView> GetSupplierViews() const { return {}; }
        virtual std::vector<BuildingConnectionView> GetReceiverViews() const { return {}; }
        virtual bool HasSupplier(ResourceType) const { return false; }
        virtual bool HasReceiver(ResourceType) const { return false; }
        virtual bool CanAcceptResource(ResourceType) const { return false; }
        virtual bool CanReceiveResource(ResourceType) const { return false; }
        virtual float GetProductionProgress() const { return 0.0f; }
        virtual float GetWorkerRatio() const { return 0.0f; }
        virtual int GetAssignedWorkers() const { return 0; }
        virtual int GetWorkerCapacity() const { return 0; }
        virtual bool IsProductionStalled() const { return false; }
        virtual bool CanBlockProduction() const { return false; }
        virtual bool IsProductionBlocked() const { return productionBlocked; }
        virtual void SetProductionBlocked(bool blocked) { productionBlocked = blocked; }
        virtual bool CanBeManuallyDestroyed() const { return true; }
        virtual Vec2i GetFootprint() const { return footprint; }
        virtual int GetTextureId() const { return textureId; }
        virtual int GetTotalProduced() const { return totalProduced; }
        virtual float GetEfficiency() const;
        virtual double GetLifetime() const { return lifetime; }
        virtual double GetActiveTime() const { return activeTime; }
        virtual bool IsUnderConstruction() const { return constructionRemaining > 0.0; }
        virtual float GetConstructionProgress() const;
        double GetModifiedTransportTime() const;

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
};

// Road tile that can carry resource transportables between buildings.
class Road : public Building
{
    public:
        Road() = default;
        Road(int i);

        void Update(double);
        void InitBuilding(TileType t ) override {}

        void AddResource(Resource*) {}
        Resource GetResource(ResourceType) { return Resource{}; }
        int HandleTransport(ResourceType res, int x, Building* building) { return 0; }

        void SetSupplier(ResourceType, Building*) {}
        void SetReceiver(ResourceType, Building*) {}

        void ReceptTransport(Transportable*);

        int upgradeLevel{1};
        Stat<int> maxCapacity{BalanceStat::RoadCapacity, 5};
        Stat<double> speedModifier{BalanceStat::RoadSpeed, 1.0};
        int GetModifiedMaxCapacity() const;
        double GetModifiedSpeedModifier() const;
};

// Building that consumes input buffers and periodically creates output resources.
class ProductionBuilding : public Building
{
    public:
    ProductionBuilding() = default;
    ProductionBuilding(int);
    
        virtual ~ProductionBuilding() = default;

        void Update(double) override;
        virtual void InitBuilding(TileType t) override { type = t;}
        
        void AddResource(Resource*) override;
        void ReturnOutgoingResource(Resource*) override;
        void CancelRequestedResource(ResourceType) override;
        Resource GetResource(ResourceType) override;

        virtual void SetSupplier(ResourceType, Building*);
        virtual void SetReceiver(ResourceType, Building*);
        void RemoveSupplier(ResourceType, Building*) override;
        void RemoveReceiver(ResourceType, Building*) override;
        
        // Produces outputs when all input requirements and timers are satisfied.
        virtual void Produce(double);
        // Dispatches available output resources to configured receivers.
        void HandleTransport();
        // Accepts incoming resources into input buffers.
        int HandleTransport(ResourceType res, int x, Building* building) override;
        // Requests a resource transfer from the configured supplier.
        int RequestResource(ResourceType ResType, int amount);
        // Keeps input buffers filled enough for smooth production.
        void MaintainInputRequests();
        // Returns whether this terrain-based producer still has local resource richness.
        bool HasTerrainRichness() const;
        // Consumes one point of local terrain richness after a terrain output cycle.
        bool ConsumeTerrainRichness();
        // Allows terrain producers such as hunters to require a biome without deleting it.
        virtual bool ShouldConsumeTerrainRichness() const { return true; }

        // Queues a transportable delivered to this production building.
        void ReceptTransport(Transportable*);
        std::vector<ResourceBufferView> GetInputBufferViews() const override;
        std::vector<ResourceBufferView> GetOutputBufferViews() const override;

        std::vector<BuildingConnectionView> GetSupplierViews() const override;
        std::vector<BuildingConnectionView> GetReceiverViews() const override;
        bool HasSupplier(ResourceType) const override;
        bool HasReceiver(ResourceType) const override;
        bool CanAcceptResource(ResourceType) const override;
        bool CanReceiveResource(ResourceType) const override;
        float GetProductionProgress() const override;
        float GetWorkerRatio() const override;
        int GetAssignedWorkers() const override { return assignedWorkers; }
        int GetWorkerCapacity() const override;
        bool IsProductionStalled() const override;
        bool CanBlockProduction() const override { return true; }
        bool HasSelectableRecipes() const { return recipes.size() > 1; }
        const std::vector<ProductionRecipeRuntime>& GetRecipes() const { return recipes; }
        int GetActiveRecipeIndex() const { return activeRecipeIndex; }
        std::string GetActiveRecipeName() const;
        void SetRecipes(std::vector<ProductionRecipeRuntime> newRecipes);
        bool SetActiveRecipe(int index);
        bool CycleRecipe();
        bool StartTechnologyResearch(const std::string& id, double totalTime);
        bool UpdateTechnologyResearch(double dt);
        const std::string& GetActiveTechnologyId() const { return activeTechnologyId; }
        double GetActiveTechnologyRemaining() const { return activeTechnologyRemaining; }
        double GetActiveTechnologyTotal() const { return activeTechnologyTotal; }
        double GetActiveTechnologyProgress() const;
        double GetModifiedProductionTime() const;
        double GetEffectiveProductionCycleTime() const;
        int GetModifiedOutputAmount(ResourceType type, int baseAmount) const;

        TileType type;
        std::map<ResourceType, int> ingredients;
        std::map<ResourceType, int> products;

        Stat<double> productionTime{BalanceStat::ProductionCycleTime, 0.0};
        double elapsedTime = 0.0;
        bool productionStarted = false;
        
        std::map<ResourceType, ResourceBuffer> inputBuffers;
        std::map<ResourceType, ResourceBuffer> outputBuffers;
        
        std::map<ResourceType, std::vector<Building*>> suppliersMap;
        std::map<ResourceType, Building*> receiversMap;
        std::map<ResourceType, int> pendingInputRequests;
        bool inputRequestBlocked{false};
        Stat<int> workerCapacity{BalanceStat::WorkerCapacity, 5};
        int assignedWorkers{0};
        std::vector<ProductionRecipeRuntime> recipes;
        int activeRecipeIndex{0};
        std::string activeTechnologyId;
        double activeTechnologyRemaining{0.0};
        double activeTechnologyTotal{0.0};
};

// Building that stores resources and serves as a logistics hub.
class StorageBuilding : public Building
{
public:
    StorageBuilding() = default;
    StorageBuilding(int);
    
        virtual ~StorageBuilding() = default;

        void Update(double) override;        
        void AddResource(Resource*) override;
        void ReturnOutgoingResource(Resource*) override;
        Resource GetResource(ResourceType) override;
        void InitBuilding(TileType tajl) override;

        virtual void SetSupplier(ResourceType, Building*);
        virtual void SetReceiver(ResourceType, Building*);

        void ReceptTransport(Transportable*);

        bool IsStorageLike() const override { return true; }
        
        int HandleTransport(ResourceType res, int x, Building* building) override;
        std::vector<ResourceBufferView> GetOutputBufferViews() const override;
        bool CanAcceptResource(ResourceType) const override;
        bool CanReceiveResource(ResourceType) const override;

        std::map<ResourceType, ResourceBuffer> resourceBuffers;
};

// Player's starting building: storage plus military territory source.
class Headquarters : public StorageBuilding
                   , public IMilitaryBuilding
{
public:
    Headquarters() = default;
    Headquarters(int);

    bool IsAttackable() const override { return true; }
    int GetTerritoryRadius() const override;
    int GetHitPoints() const override { return hitPoints; }
    int GetMaxHitPoints() const override;
    int GetEffectiveStrength() const override { return 20; }
    void ReceiveDamage(int damage) override;

    bool IsStorageLike() const override { return true; }
    bool CanBeManuallyDestroyed() const override { return false; }

    Stat<int> territoryRadius{BalanceStat::TerritoryRadius, 10};
    int hitPoints{1000};
    Stat<int> maxHitPoints{BalanceStat::HitPoints, 1000};
};

// Settlement that generates manpower and consumes strategic upkeep over time.
class Village : public Building
{
public:
    Village() = default;
    Village(int);

    void Update(double dt) override;
    void InitBuilding(TileType) override {}

    Resource GetResource(ResourceType) override;
    void SetSupplier(ResourceType, Building*) override {}
    void SetReceiver(ResourceType, Building*) override {}
    int HandleTransport(ResourceType, int, Building*) override { return 0; }
    void AddResource(Resource*) override;
    void ReturnOutgoingResource(Resource*) override;
    bool CanAcceptResource(ResourceType) const override;
    bool CanReceiveResource(ResourceType) const override;
    std::vector<ResourceBufferView> GetInputBufferViews() const override;
    double GetFoodSupplyRatio() const;
    double GetManpowerProductivity() const;
    double GetWorkerProductivity() const;
    int RequestFoodSupply();

    Stat<double> manpowerRate{BalanceStat::ManpowerRate, 0.2};
    Stat<int> populationCap{BalanceStat::PopulationCap, 80};
    double upkeepTimer{0.0};
    double upkeepInterval{10.0};
    double foodPackageUpkeep{1.0};
    bool hasFood{true};
    double foodSupplyLevel{1.0};
    double foodSupplyDropPerMissedUpkeep{0.25};
    ResourceBuffer foodSupplyBuffer{ResourceType::FOOD_PROVISIONS, 3};
};

// Military building that projects territory and can hold a garrison.
class MilitaryBuilding : public Building
                       , public IMilitaryBuilding
{
public:
    MilitaryBuilding() = default;
    MilitaryBuilding(int);

    void Update(double dt) override;
    void InitBuilding(TileType) override {}

    void AddResource(Resource*) override;
    void ReturnOutgoingResource(Resource*) override;
    Resource GetResource(ResourceType) override;
    void SetSupplier(ResourceType, Building*) override {}
    void SetReceiver(ResourceType, Building*) override {}
    int HandleTransport(ResourceType, int, Building*) override;
    bool CanAcceptResource(ResourceType) const override;
    bool CanReceiveResource(ResourceType) const override;

    bool IsAttackable() const override { return true; }
    int GetTerritoryRadius() const override;
    int GetHitPoints() const override { return hitPoints; }
    int GetMaxHitPoints() const override;
    int GetEffectiveStrength() const override;
    void ReceiveDamage(int damage) override;
    void IssueOrder(MilitaryOrderType order, int targetPositionId);
    bool IssueDivisionOrder(int divisionId, MilitaryOrderType order, int targetPositionId);
    void ClearOrder();
    bool HasActiveDivisionOrders() const;
    int GetTotalTroops() const;
    int GetFreeGarrisonSpace() const;
    int GetSupplyCapacity() const;
    int GetSupplyConsumption() const;
    int GetModifiedAttackDamage() const;
    int GetDivisionCapacity() const;
    int GetFreeDivisionSpace() const;
    int GetAverageDivisionMorale() const;
    int GetAverageDivisionExperience() const;

    Stat<int> territoryRadius{BalanceStat::TerritoryRadius, 0};
    int hitPoints{0};
    Stat<int> maxHitPoints{BalanceStat::HitPoints, 0};
    Stat<int> combatStrength{BalanceStat::MilitaryStrength, 0};
    int garrison{0};
    Stat<int> garrisonCapacity{BalanceStat::GarrisonCapacity, 0};
    int supply{0};
    Stat<int> supplyCapacity{BalanceStat::SupplyCapacity, 0};
    ResourceBuffer supplyBuffer{ResourceType::FOOD_PROVISIONS, 0};
    int militia{0};
    int swordsmen{0};
    int archers{0};
    int nextDivisionId{1};
    std::vector<MilitaryDivision> divisions;
    MilitaryOrderType currentOrder{MilitaryOrderType::None};
    int orderTargetPositionId{-1};
    double orderCooldown{0.0};
};

class GuardTower : public MilitaryBuilding
{
public:
    GuardTower() = default;
    GuardTower(int);
};

class Fortress : public MilitaryBuilding
{
public:
    Fortress() = default;
    Fortress(int);
};

class Castle : public MilitaryBuilding
{
public:
    Castle() = default;
    Castle(int);
};

class Barracks : public MilitaryBuilding
{
public:
    Barracks() = default;
    Barracks(int);

    void Update(double dt) override;
    bool QueueRecruitment(MilitaryUnitType type);

    struct RecruitmentJob
    {
        MilitaryUnitType type{MilitaryUnitType::Militia};
        double remaining{0.0};
    };

    std::deque<RecruitmentJob> recruitmentQueue;
};



#endif
