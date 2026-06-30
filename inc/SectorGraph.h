#ifndef SECTOR_GRAPH_H
#define SECTOR_GRAPH_H

#include "Utils.h"

#include <vector>

class TileMap;
class Player;

// ─── SectorGraph ──────────────────────────────────────────────────────────────
// Adjacency + occupancy over the fixed 2x2 quadrant grid (see DivisionSector.h).
// Quadrants behave like provinces: each holds at most one division, and movement
// is between adjacent, walk-connected cells. This is the substrate the future
// territory/terrain-combat layer will build on.

// The four cardinally-adjacent cell coordinates (no bounds check).
std::vector<Vec2i> SectorCardinalNeighbors(Vec2i cell);

// True when two cells touch (8-neighbourhood, Chebyshev distance 1).
bool SectorsAdjacent(Vec2i a, Vec2i b);

// True when two cardinally-adjacent cells are walk-connected: each has at least
// one walkable tile and a walkable tile of `a` is tile-adjacent to a walkable tile
// of `b` across their shared edge (so a division can actually cross between them).
bool AreSectorsConnected(const TileMap& map, Vec2i a, Vec2i b);

// Returns the id of a division of `player` currently occupying / heading to `cell`
// (by its sectorCell), ignoring `excludingDivisionId`; -1 when the cell is free.
int DivisionOccupyingSector(const Player& player, Vec2i cell, int excludingDivisionId);

// Convenience: true when no other division of `player` holds `cell`.
bool IsSectorCellFree(const Player& player, Vec2i cell, int excludingDivisionId);

// ─── per-tile occupancy (one division per tile) ───────────────────────────────

// Returns the id of a division of `player` standing on / heading to `tile`
// (by its occupiedTile), ignoring `excludingDivisionId`; -1 when the tile is free.
int DivisionOnTile(const Player& player, Vec2i tile, int excludingDivisionId);

// Convenience: true when no other division of `player` holds `tile`.
bool IsTileFree(const Player& player, Vec2i tile, int excludingDivisionId);

// True when `tile` is a frontier of `player`'s territory: owned + walkable and
// adjacent (4-neighbour) to an in-bounds tile not owned by `player`. These are the
// tiles a defensive line should hold to seal the border.
bool IsFrontierTile(const TileMap& map, const Player& player, Vec2i tile);

#endif
