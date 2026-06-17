#ifndef BUILDING_FACTORY_H
#define BUILDING_FACTORY_H

#include "MapGenerator.h"
#include "ProductionBuildings.h"

struct BFactory
{
    BFactory() = default;
    BFactory(Player* p, TileMap& map, int id) : player(p), tilemap(&map), playerId(id) {}

    template <typename T> void Build(int tilePos)
    {
        auto bld = std::make_unique<T>(playerId + buildingId++);
        tilemap->BuildOnTile(tilePos, player, std::move(bld));
    }

    Player* player{nullptr};
    TileMap* tilemap{nullptr};
    int playerId = 0;
    int buildingId = 0;
};

#endif
