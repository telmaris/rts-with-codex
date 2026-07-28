#ifndef PATHING_SERVICE_H
#define PATHING_SERVICE_H

#include "core/Types.h"
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

    // Looks up the ring route connecting two players' HQs (TD etap-2). A
    // direct ring edge is returned as-is. If none exists, and `isEliminated`
    // is provided, falls back to a BFS over the ring graph (TD etap-6.3) that
    // may only transit through ELIMINATED intermediate players — concatenating
    // each hop's directed tiles (dropping the duplicate join tile at each
    // conquered HQ) into one path. Deterministic: BFS explores neighbors in
    // MilitaryRoadNetwork's stable insertion order, so ties (equidistant
    // clockwise/counterclockwise ring paths) always resolve the same way for
    // the same topology. Static — neither this lookup nor AreHqsConnected
    // ever needed the instance's tilemap/roadNetwork.
    static MilitaryPath FindMilitaryPath(const MilitaryRoadNetwork& militaryRoads, int fromPlayerId, int toPlayerId,
                                         const std::function<bool(int)>& isEliminated = {});
    // Returns true when two players are connected by a ring route, direct or
    // (with `isEliminated` provided) through a chain of conquered HQs.
    static bool AreHqsConnected(const MilitaryRoadNetwork& militaryRoads, int playerA, int playerB,
                               const std::function<bool(int)>& isEliminated = {});

private:
    TileMap& tilemap;
    RoadNetwork& roadNetwork;

    // Internal Dijkstra implementation
    RoadPath DijkstraRoad(int fromTile, int toTile, const PathOptions& options);

    // Deterministic tie-breaking by tile ID
    bool IsBetter(int candidateTile, int currentBestTile) const;
};

#endif
