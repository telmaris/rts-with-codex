#ifndef BUILDING_FACTORY_H
#define BUILDING_FACTORY_H

#include "MapGenerator.h"
#include "ProductionBuildings.h"

// Small helper that creates player-owned buildings on a tile map.
struct BFactory
{
    BFactory() = default;
    BFactory(Player* p, TileMap& map, int id) : player(p), tilemap(&map), playerId(id) {}

    // Constructs a building type and places it on the requested tile.
    template <typename T> void Build(int tilePos)
    {
        auto bld = std::make_unique<T>(playerId * 100000 + buildingId++);
        tilemap->BuildOnTile(tilePos, player, std::move(bld));
    }

    Player* player{nullptr};
    TileMap* tilemap{nullptr};
    int playerId = 0;
    int buildingId = 0;
};

#endif
