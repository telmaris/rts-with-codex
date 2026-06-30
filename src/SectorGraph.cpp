#include "../inc/SectorGraph.h"
#include "../inc/DivisionSector.h"
#include "../inc/MapGenerator.h"
#include "../inc/Building.h"
#include "../inc/Player.h"

#include <cstdlib>

std::vector<Vec2i> SectorCardinalNeighbors(Vec2i cell)
{
    return {
        {cell.x + 1, cell.y},
        {cell.x - 1, cell.y},
        {cell.x, cell.y + 1},
        {cell.x, cell.y - 1}};
}

bool SectorsAdjacent(Vec2i a, Vec2i b)
{
    int dx = std::abs(a.x - b.x);
    int dy = std::abs(a.y - b.y);
    return (dx | dy) != 0 && dx <= 1 && dy <= 1;
}

bool AreSectorsConnected(const TileMap& map, Vec2i a, Vec2i b)
{
    // Only cardinally adjacent cells can share a crossable edge.
    int dx = std::abs(a.x - b.x);
    int dy = std::abs(a.y - b.y);
    if (dx + dy != 1)
        return false;

    DivisionSector sa = ResolveDivisionSector(map, {a.x * 2, a.y * 2});
    DivisionSector sb = ResolveDivisionSector(map, {b.x * 2, b.y * 2});
    if (!sa.IsValid() || !sb.IsValid())
        return false;

    // Connected when a walkable tile of `a` is tile-adjacent to a walkable tile of
    // `b` across the shared border.
    for (int ia = 0; ia < 4; ia++)
    {
        if ((sa.mask & (1u << ia)) == 0)
            continue;
        Vec2i ta{sa.anchor.x + (ia % 2), sa.anchor.y + (ia / 2)};
        for (int ib = 0; ib < 4; ib++)
        {
            if ((sb.mask & (1u << ib)) == 0)
                continue;
            Vec2i tb{sb.anchor.x + (ib % 2), sb.anchor.y + (ib / 2)};
            if (std::abs(ta.x - tb.x) + std::abs(ta.y - tb.y) == 1)
                return true;
        }
    }
    return false;
}

int DivisionOccupyingSector(const Player& player, Vec2i cell, int excludingDivisionId)
{
    if (cell.x < 0 || cell.y < 0)
        return -1;

    for (Building* building : player.GetTrackedBuildingsWithComponent<GarrisonComponent>())
    {
        if (building == nullptr)
            continue;
        const auto* garrison = building->GetComponent<GarrisonComponent>();
        if (garrison == nullptr)
            continue;
        for (const auto& division : garrison->divisions)
        {
            if (division.id == excludingDivisionId)
                continue;
            if (division.sectorCell.x == cell.x && division.sectorCell.y == cell.y)
                return division.id;
        }
    }
    return -1;
}

bool IsSectorCellFree(const Player& player, Vec2i cell, int excludingDivisionId)
{
    return DivisionOccupyingSector(player, cell, excludingDivisionId) < 0;
}

int DivisionOnTile(const Player& player, Vec2i tile, int excludingDivisionId)
{
    if (tile.x < 0 || tile.y < 0)
        return -1;

    for (Building* building : player.GetTrackedBuildingsWithComponent<GarrisonComponent>())
    {
        if (building == nullptr)
            continue;
        const auto* garrison = building->GetComponent<GarrisonComponent>();
        if (garrison == nullptr)
            continue;
        for (const auto& division : garrison->divisions)
        {
            if (division.id == excludingDivisionId)
                continue;
            if (division.occupiedTile.x == tile.x && division.occupiedTile.y == tile.y)
                return division.id;
        }
    }
    return -1;
}

bool IsTileFree(const Player& player, Vec2i tile, int excludingDivisionId)
{
    return DivisionOnTile(player, tile, excludingDivisionId) < 0;
}

bool IsFrontierTile(const TileMap& map, const Player& player, Vec2i tile)
{
    if (!IsTileWalkableForDivision(map, tile))
        return false;
    if (map.tilemap[map.GetIdFromCoords(tile)].owner != &player)
        return false;

    const Vec2i neighbours[] = {{tile.x + 1, tile.y}, {tile.x - 1, tile.y},
                                {tile.x, tile.y + 1}, {tile.x, tile.y - 1}};
    for (Vec2i n : neighbours)
    {
        if (!map.IsInside(n))
            continue;
        if (map.tilemap[map.GetIdFromCoords(n)].owner != &player)
            return true;  // borders foreign / neutral ground
    }
    return false;
}
