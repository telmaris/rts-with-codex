#ifndef BUILDING_H
#define BUILDING_H

#include "Utils.h"
#include "Resource.h"
#include "Transport.h"

class Player;
class Tile;

enum class BuildingType : int   
{
    Building = 0,
    ProductionBuilding = 1,
    StorageBuilding = 2,
    MilitaryBuilding = 3,
    Road = 4,

    Woodcutter = 11,
    LumberMill = 12,
    Mine = 13,
    Foundry = 14
};

enum class TileType : int
{
    GRASS = 0,  // empty square
    WOOD = 1,
    COAL = 2,
    IRON_ORE = 3
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
};

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

        virtual void SetSupplier(ResourceType, Building*) = 0;
        virtual void SetReceiver(ResourceType, Building*) = 0;
        virtual void HandleTransport(ResourceType res, int x, Building* building) = 0;

        virtual std::vector<ResourceBufferView> GetInputBufferViews() const { return {}; }
        virtual std::vector<ResourceBufferView> GetOutputBufferViews() const { return {}; }
        virtual std::vector<BuildingConnectionView> GetSupplierViews() const { return {}; }
        virtual std::vector<BuildingConnectionView> GetReceiverViews() const { return {}; }
        virtual bool HasSupplier(ResourceType) const { return false; }
        virtual bool HasReceiver(ResourceType) const { return false; }
        virtual float GetProductionProgress() const { return 0.0f; }
        virtual bool CanBlockProduction() const { return false; }
        virtual bool IsProductionBlocked() const { return productionBlocked; }
        virtual void SetProductionBlocked(bool blocked) { productionBlocked = blocked; }
        virtual Vec2i GetFootprint() const { return footprint; }
        virtual int GetTextureId() const { return textureId; }
        virtual int GetTotalProduced() const { return totalProduced; }
        virtual float GetEfficiency() const;
        virtual double GetLifetime() const { return lifetime; }
        virtual double GetActiveTime() const { return activeTime; }

        void ReceptTransport(Transportable*);
        void UpdateTransportables(double);

    Player* owner;
    Tile* placement;
    int id;
    int positionId;
    std::string name{"Building - Generic"};
    BuildingType buildingType = BuildingType::Building;
    std::string tag;
    std::vector<Transportable*> transportables;
    double transportTime = 0.0;
    Vec2i footprint{1, 1};
    int textureId{0};
    bool productionBlocked{false};
    double lifetime{0.0};
    double activeTime{0.0};
    int totalProduced{0};
};

class Road : public Building
{
    public:
        Road() = default;
        Road(int i);

        void Update(double);
        void InitBuilding(TileType t ) override {}

        void AddResource(Resource*) {}
        Resource GetResource(ResourceType) { return Resource{}; }
        void HandleTransport(ResourceType res, int x, Building* building) {}

        void SetSupplier(ResourceType, Building*) {}
        void SetReceiver(ResourceType, Building*) {}

        void ReceptTransport(Transportable*);

        int upgradeLevel;
        int maxCapacity = 5;
        double speedModifier = 1.0;
};

// todo: jak zaplanować rozkaz wydawania surowców: priorytet ma producent czy odbiorca?

class ProductionBuilding : public Building
{
    public:
    ProductionBuilding() = default;
    ProductionBuilding(int);
    
        virtual ~ProductionBuilding() = default;

        void Update(double) override;
        virtual void InitBuilding(TileType t) override { type = t;}
        
        void AddResource(Resource*) override;
        Resource GetResource(ResourceType) override;

        virtual void SetSupplier(ResourceType, Building*);
        virtual void SetReceiver(ResourceType, Building*);
        
        //protected:
        virtual void Produce(double);
        void HandleTransport();
        void HandleTransport(ResourceType res, int x, Building* building) override;
        void RequestResource(ResourceType ResType, int amount);

        void ReceptTransport(Transportable*);
        std::vector<ResourceBufferView> GetInputBufferViews() const override;
        std::vector<ResourceBufferView> GetOutputBufferViews() const override;
        std::vector<BuildingConnectionView> GetSupplierViews() const override;
        std::vector<BuildingConnectionView> GetReceiverViews() const override;
        bool HasSupplier(ResourceType) const override;
        bool HasReceiver(ResourceType) const override;
        float GetProductionProgress() const override;
        bool CanBlockProduction() const override { return true; }

        TileType type;
        std::map<ResourceType, int> ingredients;    // to budynek pochłania do produkcji (1 para <resourcetype, int> per składnik)
        std::map<ResourceType, int> products;       // to budynek produkuje (analogicznie do ingredients - 1 para per surowiec)

        double productionTime = 0.0, elapsedTime = 0.0;
        bool productionStarted = false;
        
        // 1 resource buffer per 1 resource type
        std::map<ResourceType, ResourceBuffer> inputBuffers;   // analogicznie do ingredients, para <resourcetype, resourcebuffer> per składnik
        std::map<ResourceType, ResourceBuffer> outputBuffers;
        
        std::map<ResourceType, Building*> suppliersMap;
        std::map<ResourceType, Building*> receiversMap;
};

class StorageBuilding : public Building
{
public:
    StorageBuilding() = default;
    StorageBuilding(int);
    
        virtual ~StorageBuilding() = default;

        void Update(double) override;        
        void AddResource(Resource*) override;
        Resource GetResource(ResourceType) override;
        void InitBuilding(TileType tajl) override;

        virtual void SetSupplier(ResourceType, Building*);
        virtual void SetReceiver(ResourceType, Building*);

        void ReceptTransport(Transportable*);
        
        //protected:
        //void HandleTransport();
        void HandleTransport(ResourceType res, int x, Building* building) override;
        std::vector<ResourceBufferView> GetOutputBufferViews() const override;

        std::map<ResourceType, ResourceBuffer> resourceBuffers;
};

class MilitaryBuilding : public Building    
{
    
};



#endif
