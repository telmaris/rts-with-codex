#ifndef PROD_BUILDING_H
#define PROD_BUILDING_H

#include "Building.h"

// Terrain-dependent wood producer.
class Woodcutter : public ProductionBuilding
{
    public:
        Woodcutter(int);
};
// Forest-based meat producer that preserves the forest tile.
class HuntersHut : public ProductionBuilding
{
    public:
        HuntersHut(int);
        void InitBuilding(TileType) override;
        bool ShouldConsumeTerrainRichness() const override { return false; }
};
// Converts wood into planks.
class LumberMill : public ProductionBuilding
{
    public:
        LumberMill(int);
};
// Terrain-dependent ore or stone extractor.
class Mine : public ProductionBuilding
{
    public:
        Mine(int);
        void InitBuilding(TileType) override;
};
// Converts ore and fuel into metal products.
class Foundry : public ProductionBuilding
{
    public:
        Foundry(int);
        void SetSupplier(ResourceType, Building*) override;
        void SetReceiver(ResourceType, Building*) override;
};
// Extracts unlimited water.
class Well : public ProductionBuilding
{
    public:
        Well(int);
};
// Converts water into grain.
class WheatFarm : public ProductionBuilding
{
    public:
        WheatFarm(int);
};
// Mills wheat into flour.
class Windmill : public ProductionBuilding
{
    public:
        Windmill(int);
};
// Bakes bread from flour and water.
class Bakery : public ProductionBuilding
{
    public:
        Bakery(int);
};
// Converts food ingredients into provisions.
class Inn : public ProductionBuilding
{
    public:
        Inn(int);
};
// Makes paper for research systems.
class Paperworks : public ProductionBuilding
{
    public:
        Paperworks(int);
};
// Produces metal equipment; recipe selection will sit on top of this class later.
class Smith : public ProductionBuilding
{
    public:
        Smith(int);
};
// Placeholder research building that currently consumes paper into abstract output.
class University : public ProductionBuilding
{
    public:
        University(int);
};

#endif
