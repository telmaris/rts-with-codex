#ifndef BUILDING_FACTORY_H
#define BUILDING_FACTORY_H

#include "Building.h"
#include "ProductionBuildings.h"

class TileMap;

struct BFactory
{
    BFactory() = default;
    BFactory(Player* p, TileMap& map) : player(p), tilemap(map) {}

    template <typename T> void Build(int tilePos)
    {
        auto bld = std::make_unique<T>(player->id + buildingId++);
        tilemap.BuildOnTile(tilePos, player, std::move(bld));
    }

    Player* player;
    TileMap& tilemap;
    int buildingId = 0;
};

#endif