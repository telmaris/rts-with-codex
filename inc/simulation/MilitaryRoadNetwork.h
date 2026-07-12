#ifndef MILITARY_ROAD_NETWORK_H
#define MILITARY_ROAD_NETWORK_H

#include "core/Utils.h"

#include <map>
#include <vector>

class TileMap;

// One immutable HQ-to-HQ route on the military road ring (TD etap-2). Tiles
// are ordered from playerA's gate to playerB's gate.
struct MilitaryRoute
{
    int playerA{-1};
    int playerB{-1};
    std::vector<int> tiles;

    // Returns the neighbor at the other end of this route from `playerId`, or
    // -1 if playerId isn't one of this route's two endpoints.
    int OtherPlayer(int playerId) const
    {
        if (playerId == playerA) return playerB;
        if (playerId == playerB) return playerA;
        return -1;
    }

    bool Connects(int a, int b) const
    {
        return (playerA == a && playerB == b) || (playerA == b && playerB == a);
    }
};

// Immutable HQ-ring road network used by marching units (TD etap-2). Unlike
// RoadNetwork (resource transport: capacity, reservations, mutated every
// tick), this network carries no capacity concept and is built exactly once
// during world generation; nothing mutates it afterward except restoring it
// verbatim from a save file.
class MilitaryRoadNetwork
{
public:
    // Generates the ring topology from HQ anchors (sorted deterministically by
    // angle around the map center, tie-broken by player id) and carves every
    // route into `tilemap` (marking Tile::isMilitaryRoad). Routes avoid other
    // players' starting-base rectangles where a path exists without crossing
    // them; if a player would otherwise be left disconnected, those
    // rectangles are re-tried as passable (deterministic fallback) rather
    // than leaving a ring gap. Safe to call at most once per freshly
    // generated world.
    void Generate(TileMap& tilemap, const std::map<int, Vec2i>& hqAnchors, Vec2i hqFootprint,
                  int startingAreaSize, unsigned int seed);

    // Returns the route connecting two players, or nullptr if they aren't
    // direct ring neighbors.
    const MilitaryRoute* FindRoute(int playerA, int playerB) const;
    // Returns true when two players are directly connected by a ring route.
    bool AreConnected(int playerA, int playerB) const;
    // Returns this route's tiles ordered from fromPlayerId's gate to
    // toPlayerId's gate (reversing the stored order when needed), or an empty
    // vector if the two players aren't direct ring neighbors. TD(etap-4): the
    // single place that turns the undirected stored route into a marching
    // direction — used by UnitMarchSystem and PathingService::FindMilitaryPath.
    std::vector<int> GetDirectedTiles(int fromPlayerId, int toPlayerId) const;
    // Returns every player id directly reachable by one ring route from `playerId`.
    std::vector<int> GetNeighbors(int playerId) const;

    const std::vector<MilitaryRoute>& GetRoutes() const { return routes; }

    // Restores routes read back from a save file. Tile::isMilitaryRoad flags
    // are expected to already be applied to the loaded tilemap by the caller
    // (GameWorld::LoadFromFile) — this only rebuilds the in-memory lookup.
    void RestoreRoutes(std::vector<MilitaryRoute> loadedRoutes) { routes = std::move(loadedRoutes); }

private:
    std::vector<MilitaryRoute> routes;
};

#endif
