#ifndef PATHING_SERVICE_H
#define PATHING_SERVICE_H

#include "core/Utils.h"
#include <vector>
#include <functional>

// Forward declarations
class TileMap;
class RoadNetwork;
class Building;

struct RoadPath
{
    std::vector<int> tiles;  // Tile IDs along path
    int tileCount = 0;       // Number of tiles
    double totalCost = 0.0;  // Total movement cost/time
    bool found = false;      // Whether path exists

    bool IsValid() const { return found && !tiles.empty(); }
};

struct FieldPath
{
    std::vector<int> tiles;  // Tile IDs (mixed road/terrain)
    int tileCount = 0;
    double totalCost = 0.0;
    bool found = false;

    bool IsValid() const { return found && !tiles.empty(); }
};

struct PathOptions
{
    double maxCost = 1e9;  // Maximum acceptable path cost
    bool allowDiagonal = true;
};

// Domain filter for FindNearest
class Domain
{
public:
    enum class Type { Global, Territory, TerritoryUnion };

    static Domain Global() { return Domain(Type::Global, {}); }
    static Domain Territory(int playerId) { return Domain(Type::Territory, {playerId}); }
    static Domain TerritoryUnion(const std::vector<int>& playerIds) { return Domain(Type::TerritoryUnion, playerIds); }

    Type type = Type::Global;
    std::vector<int> playerIds;

private:
    Domain(Type t, const std::vector<int>& ids) : type(t), playerIds(ids) {}
};

// Central service for pathfinding and distance calculations
class PathingService
{
public:
    PathingService(TileMap& tilemap, RoadNetwork& roadNetwork);

    // Road pathfinding using Dijkstra
    RoadPath FindRoadPath(Vec2i from, Vec2i to, const PathOptions& options = PathOptions());

    // Mixed terrain/road pathfinding (uses MovementPlanner logic)
    FieldPath FindFieldPath(Vec2i from, Vec2i to, const std::vector<int>& blockedTiles = {});

    // Distance calculations (Euclidean/Manhattan)
    double Distance(Vec2f a, Vec2f b) const;
    int TileDistance(Vec2i a, Vec2i b) const;

    // Find nearest tile/building matching predicate, optionally filtered by domain
    using TilePredicate = std::function<bool(int tileId)>;
    int FindNearest(Vec2i from, const TilePredicate& predicate, const Domain& domain = Domain::Global());

    // Find nearest building of type matching predicate
    Building* FindNearestBuilding(Vec2i from, const std::function<bool(const Building*)>& predicate, const Domain& domain = Domain::Global());

private:
    TileMap& tilemap;
    RoadNetwork& roadNetwork;

    // Internal Dijkstra implementation
    RoadPath DijkstraRoad(int fromTile, int toTile, const PathOptions& options);

    // Deterministic tie-breaking by tile ID
    bool IsBetter(int candidateTile, int currentBestTile) const;
};

#endif
