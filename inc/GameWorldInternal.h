#ifndef GAMEWORLD_INTERNAL_H
#define GAMEWORLD_INTERNAL_H

#include "GameWorld.h"

#include <cmath>

namespace GameWorldInternal
{
    // Creates a concrete building instance while loading save data.
    inline std::unique_ptr<Building> CreateBuildingFromType(BuildingType type, int id)
    {
        switch (type)
        {
            case BuildingType::Headquarters: return std::make_unique<Headquarters>(id);
            case BuildingType::Village: return std::make_unique<Village>(id);
            case BuildingType::StorageBuilding: return std::make_unique<StorageBuilding>(id);
            case BuildingType::Woodcutter: return std::make_unique<Woodcutter>(id);
            case BuildingType::HuntersHut: return std::make_unique<HuntersHut>(id);
            case BuildingType::LumberMill: return std::make_unique<LumberMill>(id);
            case BuildingType::Mine: return std::make_unique<Mine>(id);
            case BuildingType::Foundry: return std::make_unique<Foundry>(id);
            case BuildingType::Well: return std::make_unique<Well>(id);
            case BuildingType::WheatFarm: return std::make_unique<WheatFarm>(id);
            case BuildingType::Windmill: return std::make_unique<Windmill>(id);
            case BuildingType::Bakery: return std::make_unique<Bakery>(id);
            case BuildingType::Inn: return std::make_unique<Inn>(id);
            case BuildingType::Paperworks: return std::make_unique<Paperworks>(id);
            case BuildingType::Smith: return std::make_unique<Smith>(id);
            case BuildingType::University: return std::make_unique<University>(id);
            case BuildingType::GuardTower: return std::make_unique<GuardTower>(id);
            case BuildingType::Fortress: return std::make_unique<Fortress>(id);
            case BuildingType::Castle: return std::make_unique<Castle>(id);
            case BuildingType::Barracks: return std::make_unique<Barracks>(id);
            case BuildingType::SupplyHub: return std::make_unique<SupplyHub>(id);
            case BuildingType::Road: return std::make_unique<Road>(id);
            default: return nullptr;
        }
    }

    // Writes one resource buffer snapshot to a save stream.
    inline void SaveResourceBuffer(std::ostream& out, const char* tag, const ResourceBuffer& buffer)
    {
        out << tag << ' ' << static_cast<int>(buffer.type) << ' '
            << buffer.bufferSize << ' ' << buffer.buffer.size() << '\n';
    }

    // Restores one resource buffer from saved capacity and amount values.
    inline void LoadResourceBuffer(ResourceBuffer& buffer, ResourceType type, int capacity, int amount)
    {
        buffer.type = type;
        buffer.bufferSize = capacity;
        buffer.SetStoredAmount(amount);
    }

    // Deferred connection restored after all saved buildings are placed.
    struct PendingConnection
    {
        int sourcePosition{-1};
        ResourceType resource{ResourceType::Null};
        int targetPosition{-1};
        bool receiver{false};
        bool alternative{false};
    };

    // Returns the tile-space center of a footprint.
    inline Vec2i FootprintCenter(Vec2i anchor, Vec2i footprint)
    {
        return Vec2i{
            anchor.x + footprint.x / 2,
            anchor.y + footprint.y / 2};
    }

    inline Vec2i ClampAnchor(Vec2i anchor, Vec2i footprint, const MapParameters& params)
    {
        return Vec2i{
            std::clamp(anchor.x, 1, std::max(1, params.sizeX - footprint.x - 2)),
            std::clamp(anchor.y, 1, std::max(1, params.sizeY - footprint.y - 2))};
    }

    inline int AnchorDistance(Vec2i a, Vec2i b)
    {
        return std::abs(a.x - b.x) + std::abs(a.y - b.y);
    }

    inline void SetFootprintTerrain(TileMap& tilemap, Vec2i anchor, Vec2i footprint, TileType type, std::mt19937& rng, int padding = 0)
    {
        for (int y = -padding; y < footprint.y + padding; y++)
        {
            for (int x = -padding; x < footprint.x + padding; x++)
            {
                Vec2i pos{anchor.x + x, anchor.y + y};
                if (!tilemap.IsInside(pos))
                    continue;

                auto& tile = tilemap[pos];
                tile.tileType = type;
                tile.terrainTextureId = tilemap.PickTerrainTexture(type, rng);
                tile.resourceRichness = type == TileType::GRASS ? 0 : std::max(1, tilemap.params.resourceRichness);
            }
        }
        tilemap.terrainDirty = true;
    }

    inline bool FootprintsOverlap(Vec2i a, Vec2i aSize, Vec2i b, Vec2i bSize, int padding = 0)
    {
        return a.x - padding < b.x + bSize.x + padding &&
               a.x + aSize.x + padding > b.x - padding &&
               a.y - padding < b.y + bSize.y + padding &&
               a.y + aSize.y + padding > b.y - padding;
    }

    inline void PlaceStartingResourcePatch(TileMap& tilemap, Vec2i hqAnchor, Vec2i hqFootprint,
                                    Vec2i villageAnchor, Vec2i villageFootprint,
                                    TileType type, std::mt19937& rng)
    {
        Vec2i center{hqAnchor.x + hqFootprint.x / 2, hqAnchor.y + hqFootprint.y / 2};
        int half = MapGenerator::HeadquartersTerritorySize() / 2;
        int radius = 4;
        Vec2i preferredOffset = type == TileType::WOOD ? Vec2i{-half + radius + 1, 0}
                                                       : Vec2i{half - radius - 1, 0};

        Vec2i bestCenter{-1, -1};
        int bestScore = std::numeric_limits<int>::min();
        for (int y = -half + radius; y <= half - radius; y++)
        {
            for (int x = -half + radius; x <= half - radius; x++)
            {
                Vec2i patchCenter{center.x + x, center.y + y};
                int distSq = x * x + y * y;
                if (distSq < 25 || distSq > (half - 1) * (half - 1))
                    continue;

                Vec2i patchAnchor{patchCenter.x - radius, patchCenter.y - radius};
                Vec2i patchSize{radius * 2 + 1, radius * 2 + 1};
                if (!tilemap.IsInsideFootprint(patchAnchor, patchSize))
                    continue;
                if (FootprintsOverlap(patchAnchor, patchSize, hqAnchor, hqFootprint, 1) ||
                    FootprintsOverlap(patchAnchor, patchSize, villageAnchor, villageFootprint, 1))
                    continue;

                int paintableTiles = 0;
                for (int py = -radius; py <= radius; py++)
                {
                    for (int px = -radius; px <= radius; px++)
                    {
                        if (px * px + py * py > radius * radius)
                            continue;

                        Vec2i pos{patchCenter.x + px, patchCenter.y + py};
                        if (!tilemap.IsInside(pos))
                            continue;

                        const Tile& tile = tilemap[pos];
                        if (tile.tileType == TileType::GRASS && !tile.HasBuilding())
                            paintableTiles++;
                    }
                }

                if (paintableTiles <= 0)
                    continue;

                int preferredDistance = std::abs(x - preferredOffset.x) + std::abs(y - preferredOffset.y);
                int score = paintableTiles * 100 - preferredDistance;
                if (score > bestScore)
                {
                    bestScore = score;
                    bestCenter = patchCenter;
                }
            }
        }

        if (bestCenter.x < 0)
        {
            for (int y = -half; y <= half && bestCenter.x < 0; y++)
            {
                for (int x = -half; x <= half && bestCenter.x < 0; x++)
                {
                    Vec2i pos{center.x + x, center.y + y};
                    if (!tilemap.IsInside(pos))
                        continue;

                    Tile& tile = tilemap[pos];
                    if (tile.tileType == TileType::GRASS && !tile.HasBuilding())
                        bestCenter = pos;
                }
            }
        }

        if (bestCenter.x < 0)
            return;

        int painted = 0;
        for (int y = -radius; y <= radius; y++)
        {
            for (int x = -radius; x <= radius; x++)
            {
                if (x * x + y * y > radius * radius)
                    continue;

                Vec2i pos{bestCenter.x + x, bestCenter.y + y};
                if (!tilemap.IsInside(pos))
                    continue;

                auto* building = tilemap.GetBuilding(pos);
                if (building != nullptr)
                    continue;

                Tile& tile = tilemap[pos];
                if (tile.tileType != TileType::GRASS)
                    continue;

                tile.tileType = type;
                tile.terrainTextureId = tilemap.PickTerrainTexture(type, rng);
                tile.resourceRichness = std::max(1, tilemap.params.resourceRichness);
                painted++;
            }
        }
        if (painted == 0)
            Log::Msg("[MapGenerator]", "Starting resource patch failed for tile type ", static_cast<int>(type));
        tilemap.terrainDirty = true;
    }

    inline Vec2i PickEnemyHeadquartersAnchor(const std::vector<Vec2i>& occupiedAnchors, Vec2i footprint, const MapParameters& params, unsigned int seed)
    {
        int margin = std::max(14, MapGenerator::HeadquartersTerritorySize() / 2 + 4);
        int maxX = std::max(margin, params.sizeX - footprint.x - margin);
        int maxY = std::max(margin, params.sizeY - footprint.y - margin);
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> xDist(margin, maxX);
        std::uniform_int_distribution<int> yDist(margin, maxY);

        int minSafeDistance = std::max(70, std::min(params.sizeX, params.sizeY) / 3);
        Vec2i firstAnchor = occupiedAnchors.empty() ? MapGenerator::PickHeadquartersAnchor(params) : occupiedAnchors.front();
        Vec2i best = ClampAnchor({params.sizeX - firstAnchor.x - footprint.x - 1,
                                  params.sizeY - firstAnchor.y - footprint.y - 1}, footprint, params);
        int bestDistance = -1;
        for (int attempt = 0; attempt < 128; attempt++)
        {
            Vec2i candidate = ClampAnchor({xDist(rng), yDist(rng)}, footprint, params);
            int distance = std::numeric_limits<int>::max();
            for (Vec2i occupied : occupiedAnchors)
                distance = std::min(distance, AnchorDistance(candidate, occupied));
            if (distance > bestDistance)
            {
                bestDistance = distance;
                best = candidate;
            }
            if (distance >= minSafeDistance)
                return candidate;
        }

        return best;
    }

    // Builds a simple orthogonal road between two starting buildings.
    inline void BuildStartRoad(Player* player, Vec2i fromAnchor, Vec2i fromFootprint, Vec2i toAnchor, Vec2i toFootprint)
    {
        if (player == nullptr)
            return;

        Vec2i fromCenter = FootprintCenter(fromAnchor, fromFootprint);
        Vec2i toCenter = FootprintCenter(toAnchor, toFootprint);
        Vec2i start = fromCenter;
        Vec2i end = toCenter;

        int fromRight = fromAnchor.x + fromFootprint.x - 1;
        int toRight = toAnchor.x + toFootprint.x - 1;
        int fromBottom = fromAnchor.y + fromFootprint.y - 1;
        int toBottom = toAnchor.y + toFootprint.y - 1;

        if (fromRight < toAnchor.x)
        {
            start = {fromRight + 1, fromCenter.y};
            end = {toAnchor.x - 1, toCenter.y};
        }
        else if (toRight < fromAnchor.x)
        {
            start = {fromAnchor.x - 1, fromCenter.y};
            end = {toRight + 1, toCenter.y};
        }
        else if (fromBottom < toAnchor.y)
        {
            start = {fromCenter.x, fromBottom + 1};
            end = {toCenter.x, toAnchor.y - 1};
        }
        else if (toBottom < fromAnchor.y)
        {
            start = {fromCenter.x, fromAnchor.y - 1};
            end = {toCenter.x, toBottom + 1};
        }

        Vec2i cursor = start;
        int stepX = cursor.x <= end.x ? 1 : -1;
        while (cursor.x != end.x)
        {
            player->Build<Road>(cursor, false);
            cursor.x += stepX;
        }

        int stepY = cursor.y <= end.y ? 1 : -1;
        while (cursor.y != end.y)
        {
            player->Build<Road>(cursor, false);
            cursor.y += stepY;
        }

        player->Build<Road>(cursor, false);
    }
}


#endif
