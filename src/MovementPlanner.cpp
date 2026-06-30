#include "../inc/MovementPlanner.h"
#include "../inc/MapGenerator.h"
#include "../inc/Building.h"
#include "../inc/DivisionSector.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>

namespace
{
    // Diagonal moves cost √2 ≈ 1.41 of a cardinal move (integer-scaled).
    constexpr std::int64_t DiagonalNumerator = 141;
    constexpr std::int64_t DiagonalDenominator = 100;

    bool IsRoadTile(const TileMap& map, Vec2i pos)
    {
        if (!map.IsInside(pos))
            return false;
        const Tile& tile = map.tilemap[map.GetIdFromCoords(pos)];
        if (!tile.HasBuilding())
            return false;
        const Building* building = tile.GetBuilding();
        return building != nullptr && building->buildingType == BuildingType::Road;
    }

    struct OpenNode
    {
        std::int64_t f{0};
        int tile{-1};
    };

    // Lowest f first; ties broken by tile id so expansion order is deterministic.
    struct OpenNodeGreater
    {
        bool operator()(const OpenNode& a, const OpenNode& b) const
        {
            if (a.f != b.f)
                return a.f > b.f;
            return a.tile > b.tile;
        }
    };
}

std::vector<int> PlanDivisionPath(const TileMap& map, Vec2i start, Vec2i goal,
                                  const MovementCost& cost)
{
    if (!map.IsInside(start) || !map.IsInside(goal))
        return {};
    if (!IsTileWalkableForDivision(map, goal))
        return {};

    const int startId = map.GetIdFromCoords(start);
    const int goalId = map.GetIdFromCoords(goal);
    if (startId == goalId)
        return {startId};

    const int width = map.params.sizeX;
    const int height = map.params.sizeY;
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    constexpr std::int64_t Infinity = std::numeric_limits<std::int64_t>::max();
    std::vector<std::int64_t> gScore(count, Infinity);
    std::vector<int> cameFrom(count, -1);

    const std::int64_t minStepCost = std::min(cost.road, cost.offRoad);
    auto heuristic = [&](int tileId) -> std::int64_t
    {
        Vec2i p = map.GetCoordsFromId(tileId);
        int dx = std::abs(p.x - goal.x);
        int dy = std::abs(p.y - goal.y);
        return minStepCost * std::max(dx, dy);   // octile-admissible (Chebyshev)
    };

    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenNodeGreater> open;
    gScore[startId] = 0;
    open.push({heuristic(startId), startId});

    const int dirs[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

    while (!open.empty())
    {
        OpenNode current = open.top();
        open.pop();

        const int currentId = current.tile;
        if (currentId == goalId)
            break;

        Vec2i cp = map.GetCoordsFromId(currentId);
        const std::int64_t currentG = gScore[currentId];

        for (int d = 0; d < (cost.allowDiagonal ? 8 : 4); d++)
        {
            const bool diagonal = d >= 4;
            Vec2i np{cp.x + dirs[d][0], cp.y + dirs[d][1]};
            if (!map.IsInside(np) || !IsTileWalkableForDivision(map, np))
                continue;

            // No diagonal corner-cutting through blocked tiles or building corners.
            if (diagonal &&
                (!IsTileWalkableForDivision(map, {cp.x + dirs[d][0], cp.y}) ||
                 !IsTileWalkableForDivision(map, {cp.x, cp.y + dirs[d][1]})))
                continue;

            std::int64_t enter = IsRoadTile(map, np) ? cost.road : cost.offRoad;
            if (diagonal)
                enter = enter * DiagonalNumerator / DiagonalDenominator;

            const int neighborId = map.GetIdFromCoords(np);
            const std::int64_t tentative = currentG + enter;
            if (tentative < gScore[neighborId])
            {
                gScore[neighborId] = tentative;
                cameFrom[neighborId] = currentId;
                open.push({tentative + heuristic(neighborId), neighborId});
            }
        }
    }

    if (gScore[goalId] == Infinity)
        return {};

    std::vector<int> path;
    for (int tile = goalId; tile != -1; tile = cameFrom[tile])
    {
        path.push_back(tile);
        if (tile == startId)
            break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}
