#ifndef TRANSPORT_H
#define TRANSPORT_H

#include "Utils.h"

class Building;
class TileMap;

struct Transportable
{
    virtual ~Transportable() = default;

    double transportTime = 0.0, elapsedTime = 0.0;

    Building* sourceBuilding = nullptr;
    Building* targetBuilding = nullptr;
    TileMap* map = nullptr;
    std::vector<int> transportPath;
    int currentPathStep = 0;
    
    bool Update(double);
    void BeginTransport(Building*, Building*, TileMap*, const std::vector<int>&);
};

#endif
