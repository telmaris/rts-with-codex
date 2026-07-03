#include "../inc/MapGenerator.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    // Smoothstep easing for noise interpolation.
    float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }

    // Deterministic value-noise field in [0,1], one sample per tile. A coarse
    // random lattice (controlled by 'scale') is bilinearly interpolated to full
    // resolution — lower scale yields larger, smoother regions.
    std::vector<float> MakeNoiseField(int w, int h, float scale, std::mt19937& rng)
    {
        scale = std::clamp(scale, 0.005f, 0.5f);
        int gw = std::max(2, static_cast<int>(std::ceil(w * scale)) + 2);
        int gh = std::max(2, static_cast<int>(std::ceil(h * scale)) + 2);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        std::vector<float> lattice(static_cast<size_t>(gw) * gh);
        for (auto& v : lattice)
            v = dist(rng);

        std::vector<float> field(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                float gx = x * scale;
                float gy = y * scale;
                int x0 = static_cast<int>(gx);
                int y0 = static_cast<int>(gy);
                int x1 = std::min(x0 + 1, gw - 1);
                int y1 = std::min(y0 + 1, gh - 1);
                float tx = Smooth(gx - x0);
                float ty = Smooth(gy - y0);
                float a = lattice[x0 + y0 * gw];
                float b = lattice[x1 + y0 * gw];
                float c = lattice[x0 + y1 * gw];
                float d = lattice[x1 + y1 * gw];
                float top = a + (b - a) * tx;
                float bot = c + (d - c) * tx;
                field[x + y * w] = top + (bot - top) * ty;
            }
        }
        return field;
    }

    BiomeType ClassifyBiome(float elevation, float moisture, const BiomeParameters& p)
    {
        if (elevation >= p.mountainElevation)
            return BiomeType::MOUNTAINS;
        if (elevation >= p.hillElevation)
            return BiomeType::HILLS;
        // Lowland: split by moisture.
        if (moisture <= p.desertMoisture)
            return BiomeType::DESERT;
        if (moisture >= p.wetlandMoisture)
            return BiomeType::WETLAND;
        if (moisture >= p.forestMoisture)
            return BiomeType::FOREST;
        return BiomeType::PLAINS;
    }
}

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

    GenerateBiomes(tilemap, params, rng);
    GenerateResourcePatches(tilemap, params, rng);
}

// Assigns a biome to every tile from elevation + moisture noise. Biomes gate where
// resource patches can spawn so the map stays geographically coherent.
void MapGenerator::GenerateBiomes(TileMap& tilemap, const MapParameters& params, std::mt19937& rng)
{
    int w = params.sizeX;
    int h = params.sizeY;
    const BiomeParameters& bp = params.biome;

    // Two octaves of elevation for more organic mountain/coast shapes; one moisture band.
    std::vector<float> elevLow  = MakeNoiseField(w, h, bp.noiseScale, rng);
    std::vector<float> elevHigh = MakeNoiseField(w, h, bp.noiseScale * 2.3f, rng);
    std::vector<float> moisture = MakeNoiseField(w, h, bp.noiseScale * 1.4f, rng);

    for (int i = 0; i < w * h; i++)
    {
        float elevation = elevLow[i] * 0.65f + elevHigh[i] * 0.35f;
        tilemap.tilemap[i].biome = ClassifyBiome(elevation, moisture[i], bp);
    }
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
                // Biome gate: keep deposits geographically plausible.
                if (!patch.allowedBiomes.empty() &&
                    std::find(patch.allowedBiomes.begin(), patch.allowedBiomes.end(), tile.biome) == patch.allowedBiomes.end())
                    continue;

                tile.tileType = patch.type;
                tile.terrainTextureId = tilemap.PickTerrainTexture(patch.type, rng);
                tile.resourceRichness = std::max(1, static_cast<int>(std::round(tilemap.params.resourceRichness * patch.richnessScale)));
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
            tile.biome = BiomeType::PLAINS;
            tile.terrainTextureId = tilemap.PickTerrainTexture(TileType::GRASS, rng);
            tile.resourceRichness = 0;
        }
    }

    tilemap.terrainDirty = true;
}
