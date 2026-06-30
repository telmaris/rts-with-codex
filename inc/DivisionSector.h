#ifndef DIVISION_SECTOR_H
#define DIVISION_SECTOR_H

#include "Utils.h"

#include <cstdint>
#include <vector>

class TileMap;
class Player;

// ─── DivisionSector ───────────────────────────────────────────────────────────
// The map is conceptually partitioned into a FIXED grid of 2x2 quadrants (cells).
// A division occupies one cell, which bounds its maximum span to 2x2. The
// territory border / obstacles then clip a cell into whatever shape survives:
// full 2x2, a 2x1 / 1x2 strip, an L-shape ("kolanko"), or a single 1x1 tile.
//
// The occupiable shape is stored as a 4-bit mask over the cell's local tiles:
//   bit 0 = (0,0)  bit 1 = (1,0)
//   bit 2 = (0,1)  bit 3 = (1,1)   (local = anchor + these offsets)
struct DivisionSector
{
    Vec2i cell{-1, -1};      // quadrant grid coordinate (tile / 2)
    Vec2i anchor{-1, -1};    // top-left tile of the cell = cell * 2
    std::uint8_t mask{0};    // which of the 4 cell tiles are occupiable

    bool IsValid() const { return mask != 0; }
    bool IsFull() const { return mask == 0b1111; }
    int TileCount() const;

    // Bounding-box span of the occupiable tiles (≤ {2,2}); {0,0} when empty.
    Vec2i Span() const;
    // Centroid of the occupiable tiles, in tile-space.
    Vec2f CenterTile() const;
    // Tile a division paths toward / stands on: occupiable tile nearest the
    // centroid (deterministic, lowest local index breaks ties).
    Vec2i RepresentativeTileCoord() const;
    // Linear tile ids of the occupiable tiles.
    std::vector<int> TileIds(const TileMap& map) const;
};

// The fixed 2x2 grid cell coordinate that contains a tile.
Vec2i SectorCellOf(Vec2i tile);

// True when a division can stand on a single tile: in bounds and not blocked by a
// non-road building. Roads are walkable (and later grant movement bonuses).
bool IsTileWalkableForDivision(const TileMap& map, Vec2i pos);

// Resolves the fixed 2x2 quadrant containing `target`, clipped to map bounds and
// walkable tiles. When `owner` is given the cell is additionally AND-ed with that
// player's territory (only owned tiles count occupiable), so the border shape
// bounds the army's footprint on the quadrant. The surviving shape may be a full
// 2x2, a 2x1/1x2 strip, an L ("kolanko"), or 1x1; invalid (mask 0) when nothing
// in the cell qualifies.
DivisionSector ResolveDivisionSector(const TileMap& map, Vec2i target,
                                     const Player* owner = nullptr);

#endif
