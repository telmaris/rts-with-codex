#include "simulation/PathingService.h"
#include "simulation/MapGenerator.h"
#include "simulation/RoadNetwork.h"
#include "economy/Building.h"
#include "core/Utils.h"
#include <cmath>
#include <queue>
#include <vector>
#include <algorithm>

PathingService::PathingService(TileMap& tilemap, RoadNetwork& roadNetwork)
    : tilemap(tilemap), roadNetwork(roadNetwork)
{
}

RoadPath PathingService::FindRoadPath(Vec2i from, Vec2i to, const PathOptions& options)
{
    // Convert tile coordinates to tile IDs
    int fromTile = from.y * tilemap.params.sizeX + from.x;
    int toTile = to.y * tilemap.params.sizeX + to.x;

    return DijkstraRoad(fromTile, toTile, options);
}

FieldPath PathingService::FindFieldPath(Vec2i from, Vec2i to, const std::vector<int>& blockedTiles)
{
    // TODO: Implement A* with terrain cost (uses MovementPlanner logic)
    // For now: return empty path
    return FieldPath();
}

double PathingService::Distance(Vec2f a, Vec2f b) const
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

int PathingService::TileDistance(Vec2i a, Vec2i b) const
{
    int dx = a.x - b.x;
    int dy = a.y - b.y;
    return std::abs(dx) + std::abs(dy);  // Manhattan distance
}

int PathingService::FindNearest(Vec2i from, const TilePredicate& predicate, const Domain& domain)
{
    // BFS from starting position
    int maxIndex = tilemap.params.sizeX * tilemap.params.sizeY;
    int startTile = from.y * tilemap.params.sizeX + from.x;

    if (startTile < 0 || startTile >= maxIndex)
        return -1;

    std::vector<bool> visited(maxIndex, false);
    std::queue<int> q;
    q.push(startTile);
    visited[startTile] = true;

    const std::vector<int> directions{
        -tilemap.params.sizeX,  // up
        tilemap.params.sizeX,   // down
        -1,  // left
        1    // right
    };

    while (!q.empty())
    {
        int current = q.front();
        q.pop();

        // Check if current tile matches predicate and domain
        if (predicate(current))
        {
            // TODO: Check domain filtering
            return current;
        }

        int col = current % tilemap.params.sizeX;
        int row = current / tilemap.params.sizeX;

        for (int dir = 0; dir < 4; dir++)
        {
            int next = current + directions[dir];

            if (next < 0 || next >= maxIndex)
                continue;

            int nextCol = next % tilemap.params.sizeX;
            int nextRow = next / tilemap.params.sizeX;

            if (std::abs(nextCol - col) + std::abs(nextRow - row) != 1)
                continue;

            if (visited[next])
                continue;

            visited[next] = true;
            q.push(next);
        }
    }

    return -1;  // Not found
}

Building* PathingService::FindNearestBuilding(Vec2i from, const std::function<bool(const Building*)>& predicate, const Domain& domain)
{
    // TODO: Implement building search with domain filtering
    // Similar to FindNearest but looking at buildings on tiles
    return nullptr;
}

RoadPath PathingService::DijkstraRoad(int fromTile, int toTile, const PathOptions& options)
{
    int maxIndex = tilemap.params.sizeX * tilemap.params.sizeY;

    if (fromTile < 0 || fromTile >= maxIndex || toTile < 0 || toTile >= maxIndex)
        return RoadPath();

    if (fromTile == toTile)
    {
        return RoadPath{std::vector<int>{fromTile}, 1, 0.0, true};
    }

    std::vector<double> dist(maxIndex, 1e9);
    std::vector<int> parent(maxIndex, -1);
    std::vector<bool> visited(maxIndex, false);

    dist[fromTile] = 0.0;

    for (int iter = 0; iter < maxIndex; iter++)
    {
        // Find unvisited node with minimum distance
        int u = -1;
        double minDist = 1e9;
        for (int i = 0; i < maxIndex; i++)
        {
            if (!visited[i] && dist[i] < minDist)
            {
                minDist = dist[i];
                u = i;
            }
        }

        if (u == -1 || dist[u] > options.maxCost)
            break;  // No more reachable nodes

        visited[u] = true;

        if (u == toTile)
            break;  // Found destination

        int col = u % tilemap.params.sizeX;
        int row = u / tilemap.params.sizeX;

        const std::vector<int> directions{
            -tilemap.params.sizeX,  // up
            tilemap.params.sizeX,   // down
            -1,  // left
            1    // right
        };

        for (int dir = 0; dir < 4; dir++)
        {
            int v = u + directions[dir];

            if (v < 0 || v >= maxIndex || visited[v])
                continue;

            int nextCol = v % tilemap.params.sizeX;
            int nextRow = v / tilemap.params.sizeX;

            if (std::abs(nextCol - col) + std::abs(nextRow - row) != 1)
                continue;

            double cost = 1.0;  // Unit cost per tile
            if (dist[u] + cost < dist[v])
            {
                dist[v] = dist[u] + cost;
                parent[v] = u;
            }
        }
    }

    // Reconstruct path
    if (!visited[toTile])
        return RoadPath();  // No path found

    std::vector<int> path;
    double totalCost = 0.0;
    for (int at = toTile; at != -1; at = parent[at])
    {
        path.push_back(at);
        if (parent[at] != -1)
            totalCost += 1.0;
    }

    std::reverse(path.begin(), path.end());

    return RoadPath{path, static_cast<int>(path.size()), totalCost, true};
}

bool PathingService::IsBetter(int candidateTile, int currentBestTile) const
{
    // Tie-breaking: prefer lower tile ID for determinism (lockstep)
    return candidateTile < currentBestTile;
}
