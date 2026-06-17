#include "../inc/MapGenerator.h"

void MapGenerator::GenerateTileMap(TileMap& tilemap, MapParameters& params)
{
    int size = params.sizeX*params.sizeY;
    tilemap.tilemap.reserve(size);
    tilemap.params = params;
    std::mt19937 rng(params.seed);

    for(int i = 0; i < size; i++)
    {
        tilemap.tilemap.emplace_back(i);
        tilemap.tilemap.back().terrainTextureId = tilemap.PickTerrainTexture(tilemap.tilemap.back().tileType, rng);
    }
}
