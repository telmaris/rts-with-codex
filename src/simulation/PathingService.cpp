#include "simulation/PathingService.h"
#include "simulation/MapGenerator.h"
#include "simulation/RoadNetwork.h"
#include "economy/Building.h"
#include "core/Utils.h"
#include <cmath>
#include <map>
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
    int maxIndex = tilemap.params.sizeX * tilemap.params.sizeY;
    int startTile = from.y * tilemap.params.sizeX + from.x;

    if (startTile < 0 || startTile >= maxIndex)
        return nullptr;

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

        // Check if building on current tile matches predicate and domain
        if (tilemap.tilemap[current].building != nullptr)
        {
            Building* building = tilemap.tilemap[current].building.get();
            if (predicate(building))
            {
                return building;
            }
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

    return nullptr;  // Not found
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

MilitaryPath PathingService::FindMilitaryPath(const MilitaryRoadNetwork& militaryRoads, int fromPlayerId, int toPlayerId,
                                               const std::function<bool(int)>& isEliminated)
{
    MilitaryPath path;
    path.tiles = militaryRoads.GetDirectedTiles(fromPlayerId, toPlayerId);
    if (!path.tiles.empty())
    {
        path.found = true;
        return path;
    }

    if (!isEliminated)
        return path; // no direct edge and no elimination info to route through

    // BFS over the ring graph; may only transit through eliminated
    // intermediate players (TD etap-6.3: route through a conquered HQ).
    std::map<int, int> parent;
    std::queue<int> frontier;
    parent[fromPlayerId] = fromPlayerId;
    frontier.push(fromPlayerId);
    bool foundTarget = false;

    while (!frontier.empty() && !foundTarget)
    {
        int current = frontier.front();
        frontier.pop();
        for (int neighbor : militaryRoads.GetNeighbors(current))
        {
            if (parent.count(neighbor) != 0)
                continue;
            parent[neighbor] = current;
            if (neighbor == toPlayerId)
            {
                foundTarget = true;
                break;
            }
            if (isEliminated(neighbor))
                frontier.push(neighbor);
        }
    }

    if (!foundTarget)
        return path;

    std::vector<int> chain;
    for (int at = toPlayerId;; at = parent.at(at))
    {
        chain.push_back(at);
        if (at == fromPlayerId)
            break;
    }
    std::reverse(chain.begin(), chain.end());

    std::vector<int> tiles;
    for (std::size_t i = 0; i + 1 < chain.size(); i++)
    {
        std::vector<int> hop = militaryRoads.GetDirectedTiles(chain[i], chain[i + 1]);
        if (hop.empty())
            return MilitaryPath(); // topology invariant violated; fail closed

        std::size_t startIndex = tiles.empty() ? 0 : 1; // drop duplicate join tile
        tiles.insert(tiles.end(), hop.begin() + static_cast<std::ptrdiff_t>(startIndex), hop.end());
    }

    path.tiles = std::move(tiles);
    path.found = !path.tiles.empty();
    return path;
}

bool PathingService::AreHqsConnected(const MilitaryRoadNetwork& militaryRoads, int playerA, int playerB,
                                     const std::function<bool(int)>& isEliminated)
{
    if (militaryRoads.AreConnected(playerA, playerB))
        return true;
    return FindMilitaryPath(militaryRoads, playerA, playerB, isEliminated).IsValid();
}
