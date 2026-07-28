#include "simulation/MilitaryRoadNetwork.h"
#include "simulation/MapGenerator.h"
#include "core/Types.h"

#include <algorithm>
#include <cmath>
#include <functional>
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
    //  1. A GUARANTEED straight stub of kStubSeedLength tiles carved outward
    //     from each gate before pathfinding even runs (soft costs alone lose
    //     to hard detours — base rects, corridor separation — on crowded
    //     rings). User request (2026-07-20): every ring edge's stub is now
    //     laid down for ALL HQs in a pre-pass before any pathfinding runs at
    //     all (see the two-pass split in Generate below), fixing tracks that
    //     used to visually merge near a base.
    //  2. Within kStraightZone of either gate the wiggle is off and off-axis
    //     tiles pay a penalty (keeps the corridor straightish beyond the
    //     stub), then the wiggle fades back in over kWiggleRamp tiles.
    constexpr int kStubSeedLength = 5;
    constexpr int kStraightZone = 14;
    constexpr double kWiggleRamp = 8.0;
    constexpr double kOffAxisPenalty = 2.5;

    int ChebyshevDistance(Vec2i a, Vec2i b)
    {
        return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
    }

    // Every 90° direction change pays this on top of the tile cost —
    // playtest 2026-07-17 #3 ("kolanka"): without it the serpentine bias
    // dissolved into per-tile staircase jitter and random sharp elbows.
    // With it the route bends in few, deliberate places, which RoundCorners
    // below then opens into staircase arcs. Comparable to one serpentine
    // swing (±2 × strength 3) so the wiggle still shapes the route.
    constexpr double kTurnPenalty = 6.0;

    // Deterministic 4-directional Dijkstra over the tilemap, with the
    // search state extended to (tile, incoming direction) so turns can be
    // penalized (kTurnPenalty). Resource-rich tiles are heavily (but not
    // infinitely) penalized; tiles inside a blocked base rectangle are
    // impassable unless explicitly exempted (a route's own two gate tiles).
    // A smooth positional noise bias (SerpentineBias) nudges the shortest
    // path away from a straight line — but only mid-route: near either gate
    // the corridor is straightened instead (see GateExit above). Ties broken
    // by state id for lockstep-safe reproducibility.
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

        // State layout: tile * 4 + incoming direction.
        const int stateCount = total * 4;
        std::vector<double> dist(stateCount, std::numeric_limits<double>::infinity());
        std::vector<int> parentState(stateCount, -1);
        std::vector<bool> visited(stateCount, false);

        using QueueEntry = std::pair<double, int>;
        std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>> queue;

        // The start tile is entered "from nowhere" — seed all four incoming
        // directions at zero so the first real step never pays a turn.
        for (int d = 0; d < 4; d++)
        {
            dist[fromTile * 4 + d] = 0.0;
            queue.push({0.0, fromTile * 4 + d});
        }

        const int deltas[4] = {-sizeX, sizeX, -1, 1};

        int goalState = -1;
        while (!queue.empty())
        {
            auto [d, s] = queue.top();
            queue.pop();
            if (visited[s])
                continue;
            visited[s] = true;

            int u = s / 4;
            int incomingDir = s % 4;
            if (u == toTile)
            {
                goalState = s;
                break;
            }

            int col = u % sizeX;
            int row = u / sizeX;

            for (int dir = 0; dir < 4; dir++)
            {
                int v = u + deltas[dir];
                if (v < 0 || v >= total)
                    continue;

                int vCol = v % sizeX;
                int vRow = v / sizeX;
                if (std::abs(vCol - col) + std::abs(vRow - row) != 1)
                    continue; // wrap-around guard on left/right edges

                int vState = v * 4 + dir;
                if (visited[vState])
                    continue;

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

                // Inside a gate's straight zone the corridor must stay near
                // the gate's exit axis — but as a WIDENING CONE, not a hard
                // corridor: the allowed off-axis drift grows one tile per two
                // tiles of distance past the guaranteed-straight stub, so the
                // route opens into a gentle arc instead of running straight
                // and snapping 90° at the zone edge (user report 2026-07-17:
                // "kwadraty przy HQ" — bigger turn radii wanted).
                for (const GateExit& exit : {exitA, exitB})
                {
                    int gateDistance = ChebyshevDistance(vPos, exit.gate);
                    if (gateDistance > kStraightZone)
                        continue;
                    int offAxis = exit.exitAlongX ? std::abs(vRow - exit.gate.y)
                                                  : std::abs(vCol - exit.gate.x);
                    int allowedDrift = std::max(0, (gateDistance - kStubSeedLength) / 2);
                    cost += std::max(0, offAxis - allowedDrift) * kOffAxisPenalty;
                }

                // B7 (docs/work_plan_2026-07-13.md): nudge away from tiles
                // near an already-carved corridor (the corridor's own tiles
                // are already hard-blocked above via isBlocked when
                // usedRouteTiles is set) — exempt tiles (this edge's own
                // gates) are excluded so a legitimately shared gate is never
                // penalized for being distance 0 from itself.
                if (corridorDistance != nullptr && !isExempt(v))
                    cost += CorridorSeparationPenalty((*corridorDistance)[v]);

                if (dir != incomingDir)
                    cost += kTurnPenalty;

                double next = d + cost;
                if (next < dist[vState])
                {
                    dist[vState] = next;
                    parentState[vState] = s;
                    queue.push({next, vState});
                }
            }
        }

        if (goalState < 0)
            return {};

        std::vector<int> path;
        for (int at = goalState; at != -1; at = parentState[at])
            path.push_back(at / 4);
        std::reverse(path.begin(), path.end());
        return path;
    }

    // Opens a 90° elbow with straight legs into an alternating-step
    // staircase "arc" (playtest 2026-07-17 #3: "gładkie zakręty, większe
    // promienie, nie kolanka"). The staircase has the same Manhattan length
    // as the L it replaces, so it splices in place; every replacement tile
    // must pass `tileOk`, otherwise the next variant is tried: full radius
    // first, cutting INSIDE the corner (d1-first) or bulging OUTSIDE it
    // (d2-first — the inside is often exactly the resource field the route
    // was bending around), then the same pair at a smaller radius. An elbow
    // with no valid variant stays as carved — connectivity always wins.
    constexpr int kCornerRadius = 3;

    bool TryRoundCornerAt(const TileMap& tilemap, std::vector<int>& path, size_t i, int r,
                          bool insideFirst, const std::function<bool(int)>& tileOk)
    {
        auto stepDir = [&](int fromId, int toId) -> Vec2i
        {
            Vec2i a = tilemap.GetCoordsFromId(fromId);
            Vec2i b = tilemap.GetCoordsFromId(toId);
            return {b.x - a.x, b.y - a.y};
        };

        Vec2i d1 = stepDir(path[i - 1], path[i]);
        Vec2i d2 = stepDir(path[i], path[i + 1]);

        // Both legs must run straight for the whole radius.
        for (int k = 1; k < r; k++)
        {
            Vec2i back = stepDir(path[i - k - 1], path[i - k]);
            Vec2i ahead = stepDir(path[i + k], path[i + k + 1]);
            if (back.x != d1.x || back.y != d1.y || ahead.x != d2.x || ahead.y != d2.y)
                return false;
        }

        // Alternate the two leg directions from the corner window's start —
        // r steps of each lands exactly on the window's end regardless of
        // interleaving order; the order only decides which side of the L the
        // staircase runs on.
        Vec2i first = insideFirst ? d1 : d2;
        Vec2i second = insideFirst ? d2 : d1;
        Vec2i cursor = tilemap.GetCoordsFromId(path[i - r]);
        std::vector<int> staircase;
        for (int k = 0; k < r; k++)
        {
            for (Vec2i step : {first, second})
            {
                cursor = {cursor.x + step.x, cursor.y + step.y};
                if (!tilemap.IsInside(cursor))
                    return false;
                staircase.push_back(tilemap.GetIdFromCoords(cursor));
            }
        }
        if (staircase.back() != path[i + r])
            return false;
        // The window's final tile is unchanged by construction; every NEW
        // intermediate tile must be legal ground for the track.
        for (size_t k = 0; k + 1 < staircase.size(); k++)
            if (staircase[k] != path[i - r + 1 + k] && !tileOk(staircase[k]))
                return false;

        for (size_t k = 0; k + 1 < staircase.size(); k++)
            path[i - r + 1 + k] = staircase[k];
        return true;
    }

    void RoundCorners(const TileMap& tilemap, std::vector<int>& path, int protectedMargin,
                      const std::function<bool(int)>& tileOkStrict,
                      const std::function<bool(int)>& tileOkRelaxed)
    {
        auto stepDir = [&](int fromId, int toId) -> Vec2i
        {
            Vec2i a = tilemap.GetCoordsFromId(fromId);
            Vec2i b = tilemap.GetCoordsFromId(toId);
            return {b.x - a.x, b.y - a.y};
        };

        for (size_t i = 1; i + 1 < path.size(); i++)
        {
            Vec2i d1 = stepDir(path[i - 1], path[i]);
            Vec2i d2 = stepDir(path[i], path[i + 1]);
            if (d1.x == d2.x && d1.y == d2.y)
                continue;  // straight — nothing to round

            // Strict variants first; the relaxed validator (may clip a tile
            // or two off a resource field — the carve resets such tiles
            // exactly like the relaxed search tiers do) is the last resort
            // for elbows threading the gap BETWEEN two deposits, where both
            // the inside cut and the outside bulge hit a field.
            bool rounded = false;
            for (const auto* tileOk : {&tileOkStrict, &tileOkRelaxed})
            {
                for (int r : {kCornerRadius, kCornerRadius - 1})
                {
                    if (r < 2)
                        break;
                    if (static_cast<int>(i) < protectedMargin + r ||
                        i + static_cast<size_t>(protectedMargin + r) >= path.size())
                        continue;
                    if (TryRoundCornerAt(tilemap, path, i, r, true, *tileOk) ||
                        TryRoundCornerAt(tilemap, path, i, r, false, *tileOk))
                    {
                        i += r;  // skip past this corner — don't re-round the staircase
                        rounded = true;
                        break;
                    }
                }
                if (rounded)
                    break;
            }
        }
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

    // Pass 1 — lay every ring edge's guaranteed-straight stub BEFORE any
    // inter-HQ pathfinding runs (user report 2026-07-20, screenshot: tracks
    // visually merging near a base). Previously gates/stubs were decided
    // edge-by-edge, interleaved with each edge's own Dijkstra — so an early
    // edge's route had no idea where a LATER edge's stub would want to sit,
    // and CorridorSeparationPenalty (via usedRouteTiles) only ever saw
    // FULLY CARVED earlier edges, never a not-yet-processed edge's reserved
    // space. Registering every stub's tiles into usedRouteTiles up front, in
    // one pre-pass over every HQ, fixes that blind spot: pass 2's Dijkstra
    // (below) sees every OTHER edge's stub from the very first edge it runs.
    struct EdgeStubs
    {
        std::vector<int> stubA, stubB;
        GateExit exitA{}, exitB{};
    };
    std::vector<EdgeStubs> edgeStubs(edges.size());

    for (size_t edgeIndex = 0; edgeIndex < edges.size(); edgeIndex++)
    {
        const auto& [playerA, playerB] = edges[edgeIndex];
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
        // (the Dijkstra in pass 2 is free to route however it needs to reach
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

        // Guaranteed straight stubs: carve kStubSeedLength tiles straight
        // outward (away from the HQ center) from each gate; pass 2's
        // Dijkstra then only connects the two stub ENDS. The gate tile
        // itself (i == 0) is always kept even when another edge shares it;
        // deeper stub tiles stop at anything already claimed or the map
        // edge — a truncated stub degrades gracefully toward the old
        // behavior.
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
            for (int i = 0; i < kStubSeedLength; i++)
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

        for (int tileId : stubA)
            usedRouteTiles.insert(tileId);
        for (int tileId : stubB)
            usedRouteTiles.insert(tileId);

        edgeStubs[edgeIndex] = EdgeStubs{std::move(stubA), std::move(stubB), exitA, exitB};
    }

    // Pass 2 — connect stub tip to stub tip (instead of HQ to HQ / gate to
    // gate) using the precomputed stubs above; every OTHER edge's stub is
    // already in usedRouteTiles, so CorridorSeparationPenalty sees the full
    // picture from the first edge processed, not just earlier ones.
    for (size_t edgeIndex = 0; edgeIndex < edges.size(); edgeIndex++)
    {
        const auto& [playerA, playerB] = edges[edgeIndex];
        EdgeStubs& stubs = edgeStubs[edgeIndex];
        if (stubs.stubA.empty() || stubs.stubB.empty())
            continue;

        std::vector<int>& stubA = stubs.stubA;
        std::vector<int>& stubB = stubs.stubB;
        const GateExit& exitA = stubs.exitA;
        const GateExit& exitB = stubs.exitB;

        // The search only connects the two stub ENDS; stub interiors (and
        // the gates) are hard-blocked — already true via usedRouteTiles,
        // which pass 1 seeded with every edge's stub tiles — so the
        // mid-route can never double back over its own straight exits, nor
        // another edge's. Strict tiers additionally avoid earlier FULLY
        // CARVED edges' corridors as before; the last-resort relaxed tiers
        // still drop that constraint (connectivity first) but keep this
        // edge's own stub blocked via stubTiles.
        int fromTile = stubA.back();
        int toTile = stubB.back();
        std::vector<int> exempt{fromTile, toTile};
        std::set<int> stubTiles(stubA.begin(), stubA.end());
        stubTiles.insert(stubB.begin(), stubB.end());
        stubTiles.erase(fromTile);
        stubTiles.erase(toTile);
        // usedRouteTiles already contains this edge's own stub tiles (pass
        // 1) plus every other edge's stubs/carved routes — a plain copy is
        // enough, no need to re-insert stubTiles into it.
        std::set<int> usedPlusStubs = usedRouteTiles;

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
        // fields) → other edges' tiles → other players' starting-base
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

        // Round every long-legged elbow into a staircase arc (playtest
        // 2026-07-17 #3). Replacement tiles must be honest track ground:
        // untouched by other corridors, outside every starting-base
        // rectangle, off resource deposits. An elbow whose replacements
        // fail stays as carved — connectivity always wins.
        // The relaxed floor mirrors what the carved path itself was allowed
        // to do: never another corridor's tiles, never an actual HQ
        // footprint — but the WIDE base rectangles are fair game, because
        // the route legally runs through them on its final approach to the
        // gate (fallback tiers) and elbows there must be roundable too
        // (found via the square-elbow detector: un-rounded corners clustered
        // right before the gates).
        auto chamferTileRelaxed = [&](int tileId)
        {
            if (usedRouteTiles.count(tileId) > 0)
                return false;
            Vec2i pos = tilemap.GetCoordsFromId(tileId);
            for (const auto& rect : allFootprintRects)
                if (rect.Contains(pos))
                    return false;
            return true;
        };
        auto chamferTileStrict = [&](int tileId)
        {
            return chamferTileRelaxed(tileId) && tilemap.tilemap[tileId].resourceRichness <= 0;
        };
        RoundCorners(tilemap, path, kStubSeedLength, chamferTileStrict, chamferTileRelaxed);

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
