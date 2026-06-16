#ifndef ROAD_NETWORK_H
#define ROAD_NETWORK_H

#include "Building.h"

class TileMap;

struct NavigationNode
{
    Building* node; // it can be a building like production, or a road
    bool IsRoad() 
    { 
        if( node == nullptr) return false;
        // std::cout << "node at " << node->placement << " of type: " << (int)node->buildingType << "\n";
        return node->buildingType == BuildingType::Road ? true : false;
    }
};

class NavigationMap
{
    public:

    std::vector<NavigationNode> map;
    int sizeX, sizeY;
};

class RoadNetwork
{
    public:

    RoadNetwork() = delete;
    RoadNetwork(TileMap&);

    void Update(double);
    void BeginTransport(Building* src, Building* dest, Transportable* res);
    void UpdateNavMap(int id, Building* bld);
    std::vector<int> CalculatePath(Building* src, Building* dest);

    const std::string tag{"[Road Network]"};
    // std::vector<Resource> resources;

    std::unique_ptr<NavigationMap> navMap;
    TileMap* tilemap;

    private: 

        double CalculateTransportTime(Building* src, Building* dest);
        bool CheckIfPathWasTaken(int id, std::vector<int>& path);
};

#endif