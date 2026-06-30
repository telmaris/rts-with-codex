#include "../inc/DivisionSector.h"
#include "../inc/MapGenerator.h"
#include "../inc/Building.h"

#include <algorithm>
#include <bit>

namespace
{
    // Local offsets of the four tiles within a 2x2 cell, indexed by mask bit.
    const Vec2i kLocalOffsets[4] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
}

int DivisionSector::TileCount() const
{
    return std::popcount(mask);
}

Vec2i DivisionSector::Span() const
{
    if (mask == 0)
        return {0, 0};

    int minX = 2, minY = 2, maxX = -1, maxY = -1;
    for (int i = 0; i < 4; i++)
    {
        if ((mask & (1u << i)) == 0)
            continue;
        minX = std::min(minX, kLocalOffsets[i].x);
        minY = std::min(minY, kLocalOffsets[i].y);
        maxX = std::max(maxX, kLocalOffsets[i].x);
        maxY = std::max(maxY, kLocalOffsets[i].y);
    }
    return {maxX - minX + 1, maxY - minY + 1};
}

Vec2f DivisionSector::CenterTile() const
{
    float sumX = 0.0f, sumY = 0.0f;
    int count = 0;
    for (int i = 0; i < 4; i++)
    {
        if ((mask & (1u << i)) == 0)
            continue;
        sumX += anchor.x + kLocalOffsets[i].x + 0.5f;
        sumY += anchor.y + kLocalOffsets[i].y + 0.5f;
        count++;
    }
    if (count == 0)
        return {anchor.x + 1.0f, anchor.y + 1.0f};
    return {sumX / count, sumY / count};
}

Vec2i DivisionSector::RepresentativeTileCoord() const
{
    Vec2f center = CenterTile();
    Vec2i best{anchor};
    float bestDist = -1.0f;
    for (int i = 0; i < 4; i++)
    {
        if ((mask & (1u << i)) == 0)
            continue;
        Vec2i pos{anchor.x + kLocalOffsets[i].x, anchor.y + kLocalOffsets[i].y};
        float dx = pos.x + 0.5f - center.x;
        float dy = pos.y + 0.5f - center.y;
        float dist = dx * dx + dy * dy;
        if (bestDist < 0.0f || dist < bestDist)  // first hit (lowest i) breaks ties
        {
            bestDist = dist;
            best = pos;
        }
    }
    return best;
}

std::vector<int> DivisionSector::TileIds(const TileMap& map) const
{
    std::vector<int> ids;
    for (int i = 0; i < 4; i++)
    {
        if ((mask & (1u << i)) == 0)
            continue;
        Vec2i pos{anchor.x + kLocalOffsets[i].x, anchor.y + kLocalOffsets[i].y};
        if (map.IsInside(pos))
            ids.push_back(map.GetIdFromCoords(pos));
    }
    return ids;
}

Vec2i SectorCellOf(Vec2i tile)
{
    return {tile.x / 2, tile.y / 2};
}

bool IsTileWalkableForDivision(const TileMap& map, Vec2i pos)
{
    if (!map.IsInside(pos))
        return false;

    const Tile& tile = map.tilemap[map.GetIdFromCoords(pos)];
    if (!tile.HasBuilding())
        return true;

    // Roads are walkable; any other building blocks the tile.
    const Building* building = tile.GetBuilding();
    return building != nullptr && building->buildingType == BuildingType::Road;
}

DivisionSector ResolveDivisionSector(const TileMap& map, Vec2i target, const Player* owner)
{
    if (!map.IsInside(target))
        return {};

    DivisionSector sector;
    sector.cell = SectorCellOf(target);
    sector.anchor = {sector.cell.x * 2, sector.cell.y * 2};

    for (int i = 0; i < 4; i++)
    {
        Vec2i pos{sector.anchor.x + kLocalOffsets[i].x, sector.anchor.y + kLocalOffsets[i].y};
        if (!IsTileWalkableForDivision(map, pos))
            continue;
        // AND with territory: only the owner's tiles count toward the footprint.
        if (owner != nullptr && map.tilemap[map.GetIdFromCoords(pos)].owner != owner)
            continue;
        sector.mask |= static_cast<std::uint8_t>(1u << i);
    }
    return sector;
}
