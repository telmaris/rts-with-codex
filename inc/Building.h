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
};

class Road : public Building
{
    public:
        Road() = default;
        Road(int i);

        void Update(double);
        void InitBuilding(TileType t ) override {}

        void AddResource(Resource*) {}
        Resource GetResource(ResourceType) {}
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

        std::map<ResourceType, ResourceBuffer> resourceBuffers;
};

class MilitaryBuilding : public Building    
{
    
};



#endif