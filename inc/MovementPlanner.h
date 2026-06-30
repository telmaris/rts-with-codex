#ifndef MOVEMENT_PLANNER_H
#define MOVEMENT_PLANNER_H

#include "Utils.h"

#include <vector>

class TileMap;

// ─── MovementPlanner ──────────────────────────────────────────────────────────
// Deterministic, road-aware pathfinding for division movement. Each step costs
// less on a road than off-road, so the minimum-cost path naturally follows a road
// where it helps and peels off onto open ground whenever a shortcut is faster
// overall — exactly the "walk the road, then cut across" behaviour we want.
//
// Costs are integers and the open set breaks ties by tile id, so the result is
// identical on every machine (required for lockstep / server-authoritative sim).

struct MovementCost
{
    int offRoad{100};       // cost to enter an open tile (cardinal)
    int road{40};           // cost to enter a road tile (cardinal) — ~2.5x faster
    bool allowDiagonal{true};
};

// Minimum-cost walkable path of tile ids (start..goal inclusive). The start tile
// need not be walkable (a division leaves its building); every other tile on the
// path must be. Returns empty when the goal is unreachable. start == goal yields
// a single-element path.
std::vector<int> PlanDivisionPath(const TileMap& map, Vec2i start, Vec2i goal,
                                  const MovementCost& cost = {});

#endif
