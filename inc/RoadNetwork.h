#ifndef ROAD_NETWORK_H
#define ROAD_NETWORK_H

#include "Building.h"

class TileMap;

struct NavigationNode
{
    Building* node{nullptr};
    // Returns true when this navigation node contains a road building.
    bool IsRoad() 
    { 
        if( node == nullptr) return false;
        return node->buildingType == BuildingType::Road ? true : false;
    }
};

// Linear navigation grid mirroring the tile map.
class NavigationMap
{
    public:

        std::vector<NavigationNode> map;
    int sizeX{0};
    int sizeY{0};
};

// Calculates road paths and starts resource transports between buildings.
class RoadNetwork
{
    public:

    RoadNetwork() = delete;
    RoadNetwork(TileMap&);

    // Advances road network state.
    void Update(double);
    // Starts a resource transport if a valid path exists.
    bool BeginTransport(Building* src, Building* dest, Transportable* res);
    // Registers a building or road in the navigation map.
    void UpdateNavMap(int id, Building* bld);
    // Calculates a tile-id path between two building footprints.
    std::vector<int> CalculatePath(Building* src, Building* dest);

    const std::string tag{"[Road Network]"};

    std::unique_ptr<NavigationMap> navMap;
    TileMap* tilemap{nullptr};

    private: 

        // Estimates transport duration between two buildings.
        double CalculateTransportTime(Building* src, Building* dest);
        // Returns true when destination buffer and every road on the path have free capacity.
        bool CanReserveTransportPath(Building* dest, Transportable* res, const std::vector<int>& path) const;
        // Counts transports that already occupy or reserve a road tile.
        int CountReservedRoadCapacity(int roadTileId) const;
        // Counts transports already heading to a destination buffer.
        int CountIncomingToDestination(Building* dest, ResourceType type) const;
        // Returns true when a tile id is already present in the current path.
        bool CheckIfPathWasTaken(int id, std::vector<int>& path);
};

#endif
