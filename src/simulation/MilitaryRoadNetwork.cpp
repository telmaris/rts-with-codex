#include "simulation/MilitaryRoadNetwork.h"
#include "simulation/MapGenerator.h"
#include "core/Utils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <set>

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

    // B7 (docs/work_plan_2026-07-13.md): which of the 4 footprint faces a gate
    // sits on. A ring vertex has (n>=3) exactly two edges, and PickGateTile
    // aims each one at its own neighbor's HQ center — for a convex ring those
    // two directions are frequently similar enough to land on the SAME face,
    // producing the two corridors bunched together right outside the base
    // (user report 2026-07-14, with screenshot). Forcing the second gate onto
    // the opposite face spreads them to different sides of the HQ.
    enum class GateSide { East, West, North, South };

    GateSide SideOfGate(Vec2i anchor, Vec2i footprint, Vec2i gate)
    {
        if (gate.x == anchor.x + footprint.x) return GateSide::East;
        if (gate.x == anchor.x - 1) return GateSide::West;
        if (gate.y == anchor.y + footprint.y) return GateSide::South;
        return GateSide::North; // gate.y == anchor.y - 1
    }

    GateSide OppositeSide(GateSide side)
    {
        switch (side)
        {
            case GateSide::East: return GateSide::West;
            case GateSide::West: return GateSide::East;
            case GateSide::North: return GateSide::South;
            case GateSide::South: return GateSide::North;
        }
        return GateSide::East;
    }

    Vec2i PickGateTileOnSide(Vec2i anchor, Vec2i footprint, GateSide side)
    {
        Vec2i center = FootprintCenter(anchor, footprint);
        switch (side)
        {
            case GateSide::East: return {anchor.x + footprint.x, center.y};
            case GateSide::West: return {anchor.x - 1, center.y};
            case GateSide::South: return {center.x, anchor.y + footprint.y};
            case GateSide::North: return {center.x, anchor.y - 1};
        }
        return center;
    }

    // Deterministic smooth "wiggle" bias so the road isn't a ruler-straight
    // line between two HQs (user report, 2026-07-13) — two long-wavelength
    // sine waves with a seed-derived phase, summed and shifted to stay
    // non-negative (Dijkstra requires positive edge weights). Pure function
    // of tile position + seed, so the route stays perfectly deterministic
    // (lockstep-safe) without needing any RNG state threaded through the
    // search. Amplitude is deliberately small next to the resource-avoidance
    // penalty (200) below, so it steers the path into gentle serpentine
    // curves without ever overriding resource avoidance or connectivity.
    double SerpentineBias(int x, int y, unsigned int seed)
    {
        constexpr double kTwoPi = 6.283185307179586;
        double phaseA = (seed % 6151u) * (kTwoPi / 6151.0);
        double phaseB = ((seed / 6151u) % 7919u) * (kTwoPi / 7919.0);
        double wave1 = std::sin(x * 0.05 + y * 0.035 + phaseA);
        double wave2 = std::sin(x * 0.022 - y * 0.041 + phaseB);
        return wave1 + wave2; // range [-2, 2]
    }

    // B7 (docs/work_plan_2026-07-13.md): Chebyshev distance from every tile to
    // the nearest already-carved route tile, via multi-source 8-directional
    // ("king move") BFS — each BFS layer is exactly one Chebyshev step, so
    // this is the standard cheap way to get that distance field in O(map)
    // without a per-tile distance computation. Recomputed once per ring edge
    // (usedRouteTiles only grows between edges, never within one), reused
    // across that edge's strict-tier retries.
    std::vector<int> ComputeChebyshevDistanceToUsedTiles(const TileMap& tilemap, const std::set<int>& usedRouteTiles)
    {
        const int sizeX = tilemap.params.sizeX;
        const int sizeY = tilemap.params.sizeY;
        const int total = sizeX * sizeY;
        std::vector<int> dist(total, std::numeric_limits<int>::max());
        std::queue<int> queue;
        for (int tileId : usedRouteTiles)
        {
            dist[tileId] = 0;
            queue.push(tileId);
        }

        while (!queue.empty())
        {
            int u = queue.front();
            queue.pop();
            int col = u % sizeX;
            int row = u / sizeX;
            for (int dRow = -1; dRow <= 1; dRow++)
            {
                for (int dCol = -1; dCol <= 1; dCol++)
                {
                    if (dRow == 0 && dCol == 0)
                        continue;
                    int vCol = col + dCol;
                    int vRow = row + dRow;
                    if (vCol < 0 || vCol >= sizeX || vRow < 0 || vRow >= sizeY)
                        continue;
                    int v = vRow * sizeX + vCol;
                    if (dist[u] + 1 < dist[v])
                    {
                        dist[v] = dist[u] + 1;
                        queue.push(v);
                    }
                }
            }
        }
        return dist;
    }

    // Decaying cost for running near (but not on) an already-carved corridor —
    // pulls new routes away from existing ones without the hard block used
    // for the corridor tiles themselves (see isBlocked below). Zero beyond 3
    // tiles: close enough to not fight connectivity on cramped maps.
    double CorridorSeparationPenalty(int chebyshevDistance)
    {
        switch (chebyshevDistance)
        {
            case 1: return 60.0;
            case 2: return 30.0;
            case 3: return 12.0;
            default: return 0.0;
        }
    }

    // One endpoint of a route for the near-gate straightening below: where
    // the gate sits and along which axis its corridor leaves the HQ
    // (East/West faces exit along X, North/South along Y).
    struct GateExit
    {
        Vec2i gate{};
        bool exitAlongX{true};
    };

    // User report (2026-07-16, with screenshot): the serpentine wiggle used
    // to apply right up to the HQ gates, producing messy jogs around the
    // base. The track should leave each HQ STRAIGHT and only undulate
    // mid-route. Two mechanisms:
    //  1. A GUARANTEED straight stub of kGateStub tiles carved outward from
    //     each gate before pathfinding even runs (soft costs alone lose to
    //     hard detours — base rects, corridor separation — on crowded rings).
    //  2. Within kStraightZone of either gate the wiggle is off and off-axis
    //     tiles pay a penalty (keeps the corridor straightish beyond the
    //     stub), then the wiggle fades back in over kWiggleRamp tiles.
    constexpr int kGateStub = 6;
    constexpr int kStraightZone = 14;
    constexpr double kWiggleRamp = 8.0;
    constexpr double kOffAxisPenalty = 2.5;

    int ChebyshevDistance(Vec2i a, Vec2i b)
    {
        return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
    }

    // Deterministic 4-directional Dijkstra over the tilemap. Resource-rich
    // tiles are heavily (but not infinitely) penalized; tiles inside a
    // blocked base rectangle are impassable unless explicitly exempted (a
    // route's own two gate tiles). A smooth positional noise bias
    // (SerpentineBias) nudges the shortest path away from a straight line —
    // but only mid-route: near either gate the corridor is straightened
    // instead (see GateExit above). Ties broken by tile id for
    // lockstep-safe reproducibility.
    std::vector<int> FindRouteTiles(const TileMap& tilemap, int fromTile, int toTile,
                                     const std::vector<BaseRect>& blockedRects,
                                     const std::vector<int>& exemptTiles, unsigned int seed,
                                     const GateExit& exitA, const GateExit& exitB,
                                     const std::set<int>* usedRouteTiles = nullptr,
                                     bool blockResourceTiles = false,
                                     const std::vector<int>* corridorDistance = nullptr)
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
            // B2 (docs/work_plan_2026-07-13.md): tiles already claimed by an
            // earlier-carved edge of this same ring — avoided so two ring
            // routes never run alongside or cross each other. A shared HQ's
            // gate tile for THIS edge is still exempt above even if an
            // earlier edge also touched it.
            if (usedRouteTiles != nullptr && usedRouteTiles->count(tileId) > 0)
                return true;
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
                // Strict tiers refuse resource deposits outright (user report
                // 2026-07-14: the track still sliced through wood/stone
                // fields — a +200 penalty loses to a long enough detour).
                // Relaxed fallback tiers keep the soft penalty so the ring is
                // still guaranteed to connect on cramped maps.
                if (blockResourceTiles && tile.resourceRichness > 0 && !isExempt(v))
                    continue;

                double cost = 1.0;
                if (tile.resourceRichness > 0)
                    cost += 200.0;

                // Constant chosen so the wiggle meaningfully competes with
                // the base 1.0/tile cost over long routes (creating real
                // S-curves) while staying well under the 200 resource
                // penalty above.
                constexpr double kSerpentineStrength = 3.0;
                Vec2i vPos{vCol, vRow};
                int nearestGate = std::min(ChebyshevDistance(vPos, exitA.gate),
                                           ChebyshevDistance(vPos, exitB.gate));
                // 0 inside the straight zone, fading to full wiggle over the
                // ramp — the sinus lives mid-route only.
                double wiggleScale = std::clamp((nearestGate - kStraightZone) / kWiggleRamp, 0.0, 1.0);
                cost += (SerpentineBias(vCol, vRow, seed) + 2.0) * kSerpentineStrength * wiggleScale;

                // Inside a gate's straight zone, drifting off the gate's exit
                // axis costs per tile — the corridor leaves the HQ as a
                // straight line and only starts bending once clear of the
                // base.
                for (const GateExit& exit : {exitA, exitB})
                {
                    if (ChebyshevDistance(vPos, exit.gate) > kStraightZone)
                        continue;
                    int offAxis = exit.exitAlongX ? std::abs(vRow - exit.gate.y)
                                                  : std::abs(vCol - exit.gate.x);
                    cost += offAxis * kOffAxisPenalty;
                }

                // B7 (docs/work_plan_2026-07-13.md): nudge away from tiles
                // near an already-carved corridor (the corridor's own tiles
                // are already hard-blocked above via isBlocked when
                // usedRouteTiles is set) — exempt tiles (this edge's own
                // gates) are excluded so a legitimately shared gate is never
                // penalized for being distance 0 from itself.
                if (corridorDistance != nullptr && !isExempt(v))
                    cost += CorridorSeparationPenalty((*corridorDistance)[v]);

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
    // Narrow, ALWAYS-active floor beneath allBaseRects: just the exact HQ
    // footprint (not the wider startingAreaSize buffer). B5 (docs/
    // work_plan_2026-07-13.md) generates this ring before any Headquarters is
    // actually built, so — unlike the pre-B5 order, where an already-built HQ
    // simply occupied its tiles — nothing here stops a route from landing
    // exactly on a future HQ's footprint unless it's explicitly kept out.
    // Every fallback tier below relaxes allBaseRects eventually, but NEVER
    // this: CreateStartingHq's later Build<Headquarters> call must always
    // succeed. Gate tiles are, by construction, one step outside a footprint
    // (PickGateTile), so a route's own two gates never conflict with this.
    std::vector<BaseRect> allFootprintRects;
    for (const auto& [playerId, anchor] : hqAnchors)
    {
        allBaseRects.push_back(MakeBaseRect(playerId, anchor, hqFootprint, startingAreaSize));
        allFootprintRects.push_back(BaseRect{playerId, anchor.x, anchor.y,
                                              anchor.x + hqFootprint.x - 1, anchor.y + hqFootprint.y - 1});
    }

    // B2 (docs/work_plan_2026-07-13.md): tiles already claimed by an
    // earlier-carved edge, in the same fixed ring order edges are generated
    // below — deterministic and reproducible for the same seed. Later edges
    // avoid these so ring routes never run alongside or cross one another.
    std::set<int> usedRouteTiles;

    // B7 (docs/work_plan_2026-07-13.md): the face of each HQ's FIRST-assigned
    // gate (in ring order, which is deterministic) — a player's second edge
    // checks against this to avoid landing on the same face (see the
    // resolution below). A ring vertex has at most 2 edges, so "already
    // assigned" vs "not yet" is all that's needed; no per-side counting.
    std::map<int, GateSide> firstGateSide;

    // Deterministic texture-variant source for carve-time terrain resets
    // (route forced through a deposit by a relaxed fallback tier).
    std::mt19937 carveRng(seed ^ 0x27D4EB2Fu);

    for (const auto& [playerA, playerB] : edges)
    {
        Vec2i anchorA = hqAnchors.at(playerA);
        Vec2i anchorB = hqAnchors.at(playerB);
        Vec2i centerA = FootprintCenter(anchorA, hqFootprint);
        Vec2i centerB = FootprintCenter(anchorB, hqFootprint);
        Vec2i gateA = PickGateTile(anchorA, hqFootprint, centerB);
        Vec2i gateB = PickGateTile(anchorB, hqFootprint, centerA);

        // B7: if this HQ already has a gate assigned from its other ring
        // edge and the natural pick for THIS edge lands on the same face,
        // push it to the opposite face instead — the two corridors then
        // leave the base from different sides rather than bunching together
        // (the Dijkstra below is free to route however it needs to reach
        // that face, including looping around the base; the user explicitly
        // welcomes that over a cramped shared exit).
        auto resolveGateSide = [&](int playerId, Vec2i anchor, Vec2i naturalGate) -> Vec2i
        {
            GateSide naturalSide = SideOfGate(anchor, hqFootprint, naturalGate);
            auto [it, inserted] = firstGateSide.try_emplace(playerId, naturalSide);
            if (inserted)
                return naturalGate;
            if (it->second == naturalSide)
                return PickGateTileOnSide(anchor, hqFootprint, OppositeSide(naturalSide));
            return naturalGate;
        };
        gateA = resolveGateSide(playerA, anchorA, gateA);
        gateB = resolveGateSide(playerB, anchorB, gateB);

        if (!tilemap.IsInside(gateA) || !tilemap.IsInside(gateB))
            continue;

        // Near-gate straightening (user report 2026-07-16): each corridor
        // must leave its HQ along the gate face's axis — East/West gates
        // exit along X, North/South along Y.
        auto exitAxisAlongX = [&](Vec2i anchor, Vec2i gate)
        {
            GateSide side = SideOfGate(anchor, hqFootprint, gate);
            return side == GateSide::East || side == GateSide::West;
        };
        GateExit exitA{gateA, exitAxisAlongX(anchorA, gateA)};
        GateExit exitB{gateB, exitAxisAlongX(anchorB, gateB)};

        // Guaranteed straight stubs: carve kGateStub tiles straight outward
        // (away from the HQ center) from each gate; Dijkstra then only
        // connects the two stub ENDS. The gate tile itself (i == 0) is always
        // kept even when an earlier edge shares it; deeper stub tiles stop at
        // anything an earlier edge already claimed or the map edge — a
        // truncated stub degrades gracefully toward the old behavior.
        auto outwardDir = [&](Vec2i anchor, Vec2i gate) -> Vec2i
        {
            switch (SideOfGate(anchor, hqFootprint, gate))
            {
                case GateSide::East:  return {1, 0};
                case GateSide::West:  return {-1, 0};
                case GateSide::South: return {0, 1};
                case GateSide::North: return {0, -1};
            }
            return {1, 0};
        };
        auto buildStub = [&](Vec2i gate, Vec2i dir)
        {
            std::vector<int> stub;
            for (int i = 0; i < kGateStub; i++)
            {
                Vec2i pos{gate.x + dir.x * i, gate.y + dir.y * i};
                if (!tilemap.IsInside(pos))
                    break;
                int tileId = tilemap.GetIdFromCoords(pos);
                if (i > 0 && usedRouteTiles.count(tileId) > 0)
                    break;
                stub.push_back(tileId);
            }
            return stub;
        };
        std::vector<int> stubA = buildStub(gateA, outwardDir(anchorA, gateA));
        std::vector<int> stubB = buildStub(gateB, outwardDir(anchorB, gateB));
        if (stubA.empty() || stubB.empty())
            continue;

        // The search only connects the two stub ENDS; stub interiors (and
        // the gates) are hard-blocked so the mid-route can never double back
        // over its own straight exits. Strict tiers additionally avoid
        // earlier edges' corridors as before; the last-resort relaxed tiers
        // still drop that constraint (connectivity first) but keep the stub
        // blocking.
        int fromTile = stubA.back();
        int toTile = stubB.back();
        std::vector<int> exempt{fromTile, toTile};
        std::set<int> stubTiles(stubA.begin(), stubA.end());
        stubTiles.insert(stubB.begin(), stubB.end());
        stubTiles.erase(fromTile);
        stubTiles.erase(toTile);
        std::set<int> usedPlusStubs = usedRouteTiles;
        usedPlusStubs.insert(stubTiles.begin(), stubTiles.end());

        // seed mixed with the edge's own endpoints so each ring edge gets a
        // distinct (but still fully deterministic) wiggle phase, instead of
        // every edge bending in lockstep with the same S-curve.
        unsigned int edgeSeed = seed ^ (static_cast<unsigned int>(playerA) * 0x9E3779B9u) ^
                                 (static_cast<unsigned int>(playerB) * 0x85EBCA6Bu);

        // B7: corridor-separation field for THIS edge's strict-tier attempts
        // (usedRouteTiles only grows between edges, so one BFS per edge
        // covers every strict tier below without recomputing it per tier).
        std::vector<int> corridorDistance = ComputeChebyshevDistanceToUsedTiles(tilemap, usedRouteTiles);

        // Tiered fallback, most-restrictive first. Constraints, relaxed one
        // at a time: resource deposits hard-blocked (2026-07-14 — the +200
        // soft penalty alone still let long routes slice through wood/stone
        // fields) → earlier ring edges' tiles → other players' starting-base
        // rectangles. Guarantees every ring edge ends up connected on
        // cramped maps while making deposit-crossing and route overlap
        // last-resort outcomes rather than routine ones — and NO tier ever
        // relaxes the hard HQ-footprint floor (allFootprintRects, see its
        // declaration above): a future HQ's own placement must always stay
        // possible. The corridor-separation field (B7) rides along with the
        // strict tiers only — relaxed tiers below already accept running
        // through/along an existing corridor as the price of connectivity,
        // so fighting that with a soft penalty there would be counterproductive.
        std::vector<int> path = FindRouteTiles(tilemap, fromTile, toTile, allBaseRects, exempt, edgeSeed, exitA, exitB, &usedPlusStubs, true, &corridorDistance);
        if (path.empty())
            path = FindRouteTiles(tilemap, fromTile, toTile, allFootprintRects, exempt, edgeSeed, exitA, exitB, &usedPlusStubs, true, &corridorDistance);
        if (path.empty())
            path = FindRouteTiles(tilemap, fromTile, toTile, allBaseRects, exempt, edgeSeed, exitA, exitB, &usedPlusStubs, false, &corridorDistance);
        if (path.empty())
            path = FindRouteTiles(tilemap, fromTile, toTile, allFootprintRects, exempt, edgeSeed, exitA, exitB, &usedPlusStubs, false, &corridorDistance);
        if (path.empty())
            path = FindRouteTiles(tilemap, fromTile, toTile, allBaseRects, exempt, edgeSeed, exitA, exitB, &stubTiles);
        if (path.empty())
            path = FindRouteTiles(tilemap, fromTile, toTile, allFootprintRects, exempt, edgeSeed, exitA, exitB, &stubTiles);
        if (path.empty())
            continue;

        // Splice the guaranteed-straight exits back in:
        // gate A → stub A → mid-route → stub B reversed → gate B.
        std::vector<int> fullPath(stubA.begin(), stubA.end() - 1);
        fullPath.insert(fullPath.end(), path.begin(), path.end());
        for (auto it = stubB.rbegin() + 1; it != stubB.rend(); ++it)
            fullPath.push_back(*it);
        path = std::move(fullPath);

        for (int tileId : path)
        {
            Tile& tile = tilemap.tilemap[tileId];
            tile.isMilitaryRoad = true;
            tile.resourceRichness = 0;
            // If a relaxed fallback tier did route through a deposit, also
            // reset the terrain itself — otherwise the tile keeps its
            // WOOD/STONE/... texture and the track visibly sits "on" the
            // resource field (user report 2026-07-14). carveRng is seeded
            // deterministically per Generate call (declared above the loop).
            if (tile.tileType != TileType::GRASS)
            {
                tile.tileType = TileType::GRASS;
                tile.terrainTextureId = tilemap.PickTerrainTexture(TileType::GRASS, carveRng);
            }
            usedRouteTiles.insert(tileId);
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

std::vector<int> MilitaryRoadNetwork::GetDirectedTiles(int fromPlayerId, int toPlayerId) const
{
    const MilitaryRoute* route = FindRoute(fromPlayerId, toPlayerId);
    if (route == nullptr)
        return {};

    if (route->playerA == fromPlayerId)
        return route->tiles;

    std::vector<int> reversed(route->tiles.rbegin(), route->tiles.rend());
    return reversed;
}
