#ifndef PATHING_SERVICE_H
#define PATHING_SERVICE_H

#include "core/Utils.h"
#include "simulation/MilitaryRoadNetwork.h"
#include <vector>
#include <functional>

// Forward declarations
class TileMap;
class RoadNetwork;
class Building;

// Result of a military-ring route lookup (TD etap-2).
struct MilitaryPath
{
    std::vector<int> tiles;
    bool found{false};

    bool IsValid() const { return found && !tiles.empty(); }
};

struct RoadPath
{
    std::vector<int> tiles;  // Tile IDs along path
    int tileCount = 0;       // Number of tiles
    double totalCost = 0.0;  // Total movement cost/time
    bool found = false;      // Whether path exists

    bool IsValid() const { return found && !tiles.empty(); }
};

struct PathOptions
{
    double maxCost = 1e9;  // Maximum acceptable path cost
    bool allowDiagonal = true;
};

// Domain filter for FindNearest. TD(etap-1): Territory/TerritoryUnion dropped
// along with the old territory system — every call site already used Global().
class Domain
{
public:
    enum class Type { Global };

    static Domain Global() { return Domain(Type::Global); }

    Type type = Type::Global;

private:
    explicit Domain(Type t) : type(t) {}
};

// Central service for pathfinding and distance calculations
class PathingService
{
public:
    PathingService(TileMap& tilemap, RoadNetwork& roadNetwork);

    // Road pathfinding using Dijkstra
    RoadPath FindRoadPath(Vec2i from, Vec2i to, const PathOptions& options = PathOptions());

    // Distance calculations (Euclidean/Manhattan)
    double Distance(Vec2f a, Vec2f b) const;
    int TileDistance(Vec2i a, Vec2i b) const;

    // Find nearest tile/building matching predicate, optionally filtered by domain
    using TilePredicate = std::function<bool(int tileId)>;
    int FindNearest(Vec2i from, const TilePredicate& predicate, const Domain& domain = Domain::Global());

    // Find nearest building of type matching predicate
    Building* FindNearestBuilding(Vec2i from, const std::function<bool(const Building*)>& predicate, const Domain& domain = Domain::Global());

    // Looks up the ring route connecting two players' HQs (TD etap-2). This is
    // a read-only lookup into a route generated once at world init
    // (MilitaryRoadNetwork::Generate) — not a new pathfind. Concatenating
    // segments through captured HQs (ETAP 6) extends this later.
    MilitaryPath FindMilitaryPath(const MilitaryRoadNetwork& militaryRoads, int fromPlayerId, int toPlayerId) const;
    // Returns true when two players are directly connected by a ring route.
    bool AreHqsConnected(const MilitaryRoadNetwork& militaryRoads, int playerA, int playerB) const;

private:
    TileMap& tilemap;
    RoadNetwork& roadNetwork;

    // Internal Dijkstra implementation
    RoadPath DijkstraRoad(int fromTile, int toTile, const PathOptions& options);

    // Deterministic tie-breaking by tile ID
    bool IsBetter(int candidateTile, int currentBestTile) const;
};

#endif
