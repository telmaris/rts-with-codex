#ifndef PROD_BUILDING_H
#define PROD_BUILDING_H

#include "Building.h"

class Woodcutter : public ProductionBuilding
{
    public:
        Woodcutter(int);
};
class LumberMill : public ProductionBuilding
{
    public:
        LumberMill(int);
};
class Mine : public ProductionBuilding
{
    public:
        Mine(int);
        void InitBuilding(TileType) override;
};
class Foundry : public ProductionBuilding
{
    public:
        Foundry(int);
        void SetSupplier(ResourceType, Building*) override;
        void SetReceiver(ResourceType, Building*) override;
};

#endif