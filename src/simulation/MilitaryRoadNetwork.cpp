#include "simulation/MilitaryRoadNetwork.h"
#include "simulation/MapGenerator.h"
#include "core/Utils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace
{
    Vec2i FootprintCenter(Vec2i anchor, Vec2i footprint)
    {
        return {anchor.x + footprint.x / 2, anchor.y + footprint.y / 2};
    }

    // Axis-aligned starting-base rectangle around a player's HQ (matches
    // MapGenerator::PrepareStartingArea's cleared area).
    struct BaseRect
    {
        int playerId{-1};
        int minX{0}, minY{0}, maxX{0}, maxY{0};

        bool Contains(Vec2i pos) const
        {
            return pos.x >= minX && pos.x <= maxX && pos.y >= minY && pos.y <= maxY;
        }
    };

    BaseRect MakeBaseRect(int playerId, Vec2i anchor, Vec2i footprint, int areaSize)
    {
        Vec2i center = FootprintCenter(anchor, footprint);
        int half = areaSize / 2;
        return BaseRect{playerId, center.x - half, center.y - half, center.x + half, center.y + half};
    }

    // Picks the tile just outside a player's HQ footprint, on the side facing
    // `towards`. This is the sole tile of the player's base rectangle a route
    // is ever allowed to touch.
    Vec2i PickGateTile(Vec2i anchor, Vec2i footprint, Vec2i towards)
    {
        Vec2i center = FootprintCenter(anchor, footprint);
        int dx = towards.x - center.x;
        int dy = towards.y - center.y;

        if (std::abs(dx) >= std::abs(dy))
            return {dx >= 0 ? anchor.x + footprint.x : anchor.x - 1, center.y};
        return {center.x, dy >= 0 ? anchor.y + footprint.y : anchor.y - 1};
    }

    // Deterministic 4-directional Dijkstra over the tilemap. Resource-rich
    // tiles are heavily (but not infinitely) penalized; tiles inside a
    // blocked base rectangle are impassable unless explicitly exempted (a
    // route's own two gate tiles). Ties broken by tile id for lockstep-safe
    // reproducibility.
    std::vector<int> FindRouteTiles(const TileMap& tilemap, int fromTile, int toTile,
                                     const std::vector<BaseRect>& blockedRects,
                                     const std::vector<int>& exemptTiles)
    {
        const int sizeX = tilemap.params.sizeX;
        const int sizeY = tilemap.params.sizeY;
        const int total = sizeX * sizeY;
        if (fromTile < 0 || fromTile >= total || toTile < 0 || toTile >= total)
            return {};

        auto isExempt = [&](int tileId)
        {
            return std::find(exemptTiles.begin(), exemptTiles.end(), tileId) != exemptTiles.end();
        };
        auto isBlocked = [&](int tileId)
        {
            if (isExempt(tileId))
                return false;
            Vec2i pos = tilemap.GetCoordsFromId(tileId);
            for (const auto& rect : blockedRects)
                if (rect.Contains(pos))
                    return true;
            return false;
        };

        std::vector<double> dist(total, std::numeric_limits<double>::infinity());
        std::vector<int> parent(total, -1);
        std::vector<bool> visited(total, false);

        using QueueEntry = std::pair<double, int>;
        std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>> queue;

        dist[fromTile] = 0.0;
        queue.push({0.0, fromTile});

        const int deltas[4] = {-sizeX, sizeX, -1, 1};

        while (!queue.empty())
        {
            auto [d, u] = queue.top();
            queue.pop();
            if (visited[u])
                continue;
            visited[u] = true;
            if (u == toTile)
                break;

            int col = u % sizeX;
            int row = u / sizeX;

            for (int dir = 0; dir < 4; dir++)
            {
                int v = u + deltas[dir];
                if (v < 0 || v >= total || visited[v])
                    continue;

                int vCol = v % sizeX;
                int vRow = v / sizeX;
                if (std::abs(vCol - col) + std::abs(vRow - row) != 1)
                    continue; // wrap-around guard on left/right edges

                if (isBlocked(v))
                    continue;

                const Tile& tile = tilemap.tilemap[v];
                double cost = 1.0;
                if (tile.resourceRichness > 0)
                    cost += 200.0;

                double next = d + cost;
                if (next < dist[v])
                {
                    dist[v] = next;
                    parent[v] = u;
                    queue.push({next, v});
                }
            }
        }

        if (!visited[toTile])
            return {};

        std::vector<int> path;
        for (int at = toTile; at != -1; at = parent[at])
            path.push_back(at);
        std::reverse(path.begin(), path.end());
        return path;
    }
}

void MilitaryRoadNetwork::Generate(TileMap& tilemap, const std::map<int, Vec2i>& hqAnchors, Vec2i hqFootprint,
                                    int startingAreaSize, unsigned int seed)
{
    (void)seed; // Generation is already fully deterministic from HQ anchors + player ids.
    routes.clear();

    std::vector<int> ring;
    ring.reserve(hqAnchors.size());
    for (const auto& [playerId, anchor] : hqAnchors)
        ring.push_back(playerId);

    if (ring.size() < 2)
        return;

    Vec2i mapCenter{tilemap.params.sizeX / 2, tilemap.params.sizeY / 2};
    std::sort(ring.begin(), ring.end(), [&](int a, int b)
    {
        Vec2i ca = FootprintCenter(hqAnchors.at(a), hqFootprint);
        Vec2i cb = FootprintCenter(hqAnchors.at(b), hqFootprint);
        double angleA = std::atan2(static_cast<double>(ca.y - mapCenter.y), static_cast<double>(ca.x - mapCenter.x));
        double angleB = std::atan2(static_cast<double>(cb.y - mapCenter.y), static_cast<double>(cb.x - mapCenter.x));
        if (angleA != angleB)
            return angleA < angleB;
        return a < b;
    });

    std::vector<std::pair<int, int>> edges;
    if (ring.size() == 2)
    {
        edges.push_back({ring[0], ring[1]});
    }
    else
    {
        for (size_t i = 0; i < ring.size(); i++)
            edges.push_back({ring[i], ring[(i + 1) % ring.size()]});
    }

    std::vector<BaseRect> allBaseRects;
    for (const auto& [playerId, anchor] : hqAnchors)
        allBaseRects.push_back(MakeBaseRect(playerId, anchor, hqFootprint, startingAreaSize));

    for (const auto& [playerA, playerB] : edges)
    {
        Vec2i anchorA = hqAnchors.at(playerA);
        Vec2i anchorB = hqAnchors.at(playerB);
        Vec2i centerA = FootprintCenter(anchorA, hqFootprint);
        Vec2i centerB = FootprintCenter(anchorB, hqFootprint);
        Vec2i gateA = PickGateTile(anchorA, hqFootprint, centerB);
        Vec2i gateB = PickGateTile(anchorB, hqFootprint, centerA);

        if (!tilemap.IsInside(gateA) || !tilemap.IsInside(gateB))
            continue;

        int fromTile = tilemap.GetIdFromCoords(gateA);
        int toTile = tilemap.GetIdFromCoords(gateB);
        std::vector<int> exempt{fromTile, toTile};

        // First attempt: every player's starting-base rectangle is impassable
        // except this edge's own two gate tiles. Falls back to allowing
        // passage through other bases (deterministic retry) if that leaves
        // no path — guarantees every ring edge ends up connected.
        std::vector<int> path = FindRouteTiles(tilemap, fromTile, toTile, allBaseRects, exempt);
        if (path.empty())
            path = FindRouteTiles(tilemap, fromTile, toTile, {}, exempt);
        if (path.empty())
            continue;

        for (int tileId : path)
        {
            Tile& tile = tilemap.tilemap[tileId];
            tile.isMilitaryRoad = true;
            tile.resourceRichness = 0;
        }
        tilemap.terrainDirty = true;

        routes.push_back(MilitaryRoute{playerA, playerB, std::move(path)});
    }
}

const MilitaryRoute* MilitaryRoadNetwork::FindRoute(int playerA, int playerB) const
{
    for (const auto& route : routes)
        if (route.Connects(playerA, playerB))
            return &route;
    return nullptr;
}

bool MilitaryRoadNetwork::AreConnected(int playerA, int playerB) const
{
    return FindRoute(playerA, playerB) != nullptr;
}

std::vector<int> MilitaryRoadNetwork::GetNeighbors(int playerId) const
{
    std::vector<int> neighbors;
    for (const auto& route : routes)
    {
        int other = route.OtherPlayer(playerId);
        if (other != -1)
            neighbors.push_back(other);
    }
    std::sort(neighbors.begin(), neighbors.end());
    return neighbors;
}
