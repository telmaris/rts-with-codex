#include "../inc/MapGenerator.h"

#include <cmath>

// Initializes MapGenerator::GenerateTileMap.
void MapGenerator::GenerateTileMap(TileMap& tilemap, MapParameters& params)
{
    int presetSize = SizeFromPreset(params.sizePreset);
    if (params.sizeX <= 0 || params.sizeY <= 0)
    {
        params.sizeX = presetSize;
        params.sizeY = presetSize;
    }
    if (params.sizeX % 2 == 0) params.sizeX++;
    if (params.sizeY % 2 == 0) params.sizeY++;

    int size = params.sizeX*params.sizeY;
    tilemap.tilemap.clear();
    tilemap.tilemap.reserve(size);
    tilemap.params = params;
    tilemap.terrainDirty = true;
    tilemap.buildingsDirty = true;
    tilemap.territoryDirty = true;
    std::mt19937 rng(params.seed);

    for(int i = 0; i < size; i++)
    {
        tilemap.tilemap.emplace_back(i);
        tilemap.tilemap.back().terrainTextureId = tilemap.PickTerrainTexture(tilemap.tilemap.back().tileType, rng);
    }

    GenerateResourcePatches(tilemap, params, rng);
}

// Initializes MapGenerator::SizeFromPreset.
int MapGenerator::SizeFromPreset(MapSizePreset preset)
{
    switch (preset)
    {
        case MapSizePreset::S: return 301;
        case MapSizePreset::M: return 501;
        case MapSizePreset::L: return 701;
        case MapSizePreset::XL: return 1001;
        default: return 301;
    }
}

// Picks a map position or generated value.
Vec2i MapGenerator::PickHeadquartersAnchor(const MapParameters& params)
{
    Vec2i footprint = HeadquartersFootprint();
    return {
        params.sizeX / 2 - footprint.x / 2,
        params.sizeY / 2 - footprint.y / 2
    };
}

// Initializes MapGenerator::GenerateResourcePatches.
void MapGenerator::GenerateResourcePatches(TileMap& tilemap, const MapParameters& params, std::mt19937& rng)
{
    float densityScale = 0.5f + std::clamp(params.resourceDensity, 0.0f, 1.0f) * 1.75f;
    float sizeScale = 0.65f + std::clamp(params.resourceFieldSize, 0.0f, 1.0f) * 1.35f;
    for (auto patch : params.resourcePatches)
    {
        patch.patchCount = std::max(1, static_cast<int>(std::round(patch.patchCount * densityScale)));
        patch.minRadius = std::max(1, static_cast<int>(std::round(patch.minRadius * sizeScale)));
        patch.maxRadius = std::max(patch.minRadius, static_cast<int>(std::round(patch.maxRadius * sizeScale)));
        GeneratePatch(tilemap, patch, rng);
    }
}

// Initializes MapGenerator::GeneratePatch.
void MapGenerator::GeneratePatch(TileMap& tilemap, const ResourcePatchParameters& patch, std::mt19937& rng)
{
    if (patch.patchCount <= 0 || patch.maxRadius <= 0)
        return;

    std::uniform_int_distribution<int> radiusDist(std::max(1, patch.minRadius), std::max(patch.minRadius, patch.maxRadius));
    std::uniform_int_distribution<int> xDist(0, tilemap.params.sizeX - 1);
    std::uniform_int_distribution<int> yDist(0, tilemap.params.sizeY - 1);
    std::uniform_real_distribution<float> fillDist(0.0f, 1.0f);

    for (int patchIndex = 0; patchIndex < patch.patchCount; patchIndex++)
    {
        int radius = radiusDist(rng);
        int diameter = radius * 2 + 1;
        std::vector<int> cells(diameter * diameter, 0);
        std::vector<int> next = cells;

        int blobs = std::max(3, radius / 2);
        std::uniform_int_distribution<int> offsetDist(-std::max(1, radius / 2), std::max(1, radius / 2));
        std::uniform_int_distribution<int> blobRadiusDist(std::max(2, radius / 3), radius);

        for (int blob = 0; blob < blobs; blob++)
        {
            int cx = radius + offsetDist(rng);
            int cy = radius + offsetDist(rng);
            int blobRadius = blobRadiusDist(rng);

            for (int y = 0; y < diameter; y++)
            {
                for (int x = 0; x < diameter; x++)
                {
                    int dx = x - cx;
                    int dy = y - cy;
                    if (dx * dx + dy * dy <= blobRadius * blobRadius)
                        cells[x + y * diameter] = 1;
                }
            }
        }

        for (int pass = 0; pass < std::max(1, patch.smoothingPasses); pass++)
        {
            next = cells;
            for (int y = 0; y < diameter; y++)
            {
                for (int x = 0; x < diameter; x++)
                {
                    int neighbours = 0;
                    for (int oy = -1; oy <= 1; oy++)
                    {
                        for (int ox = -1; ox <= 1; ox++)
                        {
                            if (ox == 0 && oy == 0)
                                continue;

                            int nx = x + ox;
                            int ny = y + oy;
                            if (nx < 0 || ny < 0 || nx >= diameter || ny >= diameter)
                            {
                                neighbours++;
                                continue;
                            }
                            neighbours += cells[nx + ny * diameter];
                        }
                    }

                    if (cells[x + y * diameter] == 1)
                        next[x + y * diameter] = neighbours >= 3 ? 1 : 0;
                    else
                        next[x + y * diameter] = neighbours >= 6 ? 1 : 0;
                }
            }
            cells.swap(next);
        }

        Vec2i center{xDist(rng), yDist(rng)};
        for (int y = 0; y < diameter; y++)
        {
            for (int x = 0; x < diameter; x++)
            {
                if (cells[x + y * diameter] == 0)
                    continue;

                Vec2i mapPos{center.x + x - radius, center.y + y - radius};
                if (!tilemap.IsInside(mapPos))
                    continue;

                auto& tile = tilemap[mapPos];
                tile.tileType = patch.type;
                tile.terrainTextureId = tilemap.PickTerrainTexture(patch.type, rng);
                tile.resourceRichness = std::max(1, tilemap.params.resourceRichness);
            }
        }
    }
}

// Initializes MapGenerator::PrepareStartingArea.
void MapGenerator::PrepareStartingArea(TileMap& tilemap, Vec2i hqAnchor, std::mt19937& rng)
{
    Vec2i hqFootprint = HeadquartersFootprint();
    int territorySize = HeadquartersTerritorySize();
    Vec2i center{hqAnchor.x + hqFootprint.x / 2, hqAnchor.y + hqFootprint.y / 2};
    int half = territorySize / 2;

    for (int y = -half; y <= half; y++)
    {
        for (int x = -half; x <= half; x++)
        {
            Vec2i pos{center.x + x, center.y + y};
            if (!tilemap.IsInside(pos))
                continue;

            auto& tile = tilemap[pos];
            tile.tileType = TileType::GRASS;
            tile.terrainTextureId = tilemap.PickTerrainTexture(TileType::GRASS, rng);
            tile.resourceRichness = 0;
        }
    }

    tilemap.terrainDirty = true;
}
