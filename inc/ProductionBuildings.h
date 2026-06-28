#ifndef PROD_BUILDING_H
#define PROD_BUILDING_H

#include "Building.h"

// Concrete production buildings. Each inherits Building directly and is composed
// of the production component set (production cycle + logistics + workers +
// recipes + research). Behaviour lives entirely in those components; the classes
// exist only to assemble the right components and load their data definition.
//
// The shared component block is declared per class on purpose — there is no
// behavioural base type. Terrain-specialised producers override InitBuilding to
// pick a terrain-specific recipe when placed.

// Components shared by every production building. Research is NOT here on
// purpose — only the University researches technologies, so it adds a
// ResearchComponent of its own.
#define RTS_PRODUCTION_COMPONENTS         \
    ProductionComponent production;       \
    LogisticsComponent  logistics;        \
    WorkerComponent     workers;          \
    RecipeComponent     recipes

// Terrain-dependent wood producer.
class Woodcutter : public Building
{
    public:
        Woodcutter(int);
        RTS_PRODUCTION_COMPONENTS;
};
// Forest-based meat producer that preserves the forest tile.
class HuntersHut : public Building
{
    public:
        HuntersHut(int);
        void InitBuilding(TileType) override;
        RTS_PRODUCTION_COMPONENTS;
};
// Converts wood into planks.
class LumberMill : public Building
{
    public:
        LumberMill(int);
        RTS_PRODUCTION_COMPONENTS;
};
// Terrain-dependent ore or stone extractor.
class Mine : public Building
{
    public:
        Mine(int);
        void InitBuilding(TileType) override;
        RTS_PRODUCTION_COMPONENTS;
};
// Converts ore and fuel into metal products.
class Foundry : public Building
{
    public:
        Foundry(int);
        RTS_PRODUCTION_COMPONENTS;
};
// Extracts unlimited water.
class Well : public Building
{
    public:
        Well(int);
        RTS_PRODUCTION_COMPONENTS;
};
// Converts water into grain.
class WheatFarm : public Building
{
    public:
        WheatFarm(int);
        RTS_PRODUCTION_COMPONENTS;
};
// Mills wheat into flour.
class Windmill : public Building
{
    public:
        Windmill(int);
        RTS_PRODUCTION_COMPONENTS;
};
// Bakes bread from flour and water.
class Bakery : public Building
{
    public:
        Bakery(int);
        RTS_PRODUCTION_COMPONENTS;
};
// Converts food ingredients into provisions.
class Inn : public Building
{
    public:
        Inn(int);
        RTS_PRODUCTION_COMPONENTS;
};
// Makes paper for research systems.
class Paperworks : public Building
{
    public:
        Paperworks(int);
        RTS_PRODUCTION_COMPONENTS;
};
// Produces metal equipment; recipe selection sits on the recipe component.
class Smith : public Building
{
    public:
        Smith(int);
        RTS_PRODUCTION_COMPONENTS;
};
// Research building that turns paper into unlocked technologies.
class University : public Building
{
    public:
        University(int);
        RTS_PRODUCTION_COMPONENTS;
        ResearchComponent research;
};

#endif
