#include "simulation/RoadNetwork.h"
#include "simulation/MapGenerator.h"
#include "economy/Player.h"
#include "core/Log.h"

namespace
{
    // A path tile is still traversable for `owner` when it is the origin
    // building (step 0 of any path), the final destination building, or a
    // road owned by `owner` (every intermediate step, per
    // RoadNetwork::CalculatePath's BFS). Checked against the actual building
    // occupying the tile (source of truth), not Tile::owner — the old
    // territory system that populated Tile::owner was removed in the Tower
    // Defense pivot (ETAP 1) and nothing sets it anymore.
    bool IsTileTraversableForOwner(TileMap* map, int tileId, Player* owner, Building* origin, Building* destination)
    {
        if (map == nullptr || tileId < 0 || tileId >= static_cast<int>(map->tilemap.size()))
            return false;

        Building* building = map->GetBuilding(tileId);
        if (building == origin || building == destination)
            return true;

        // B6: Bridge is road-like for transport purposes (IsRoadLike, economy/Building.h).
        return building != nullptr && building->owner == owner &&
               IsRoadLike(building->buildingType);
    }
}

// Advances this object's state for one frame.
bool Transportable::Update(double dt)
{
    auto cancelTransport = [&]()
    {
        auto* resource = dynamic_cast<Resource*>(this);
        if (resource != nullptr)
        {
            if (sourceBuilding != nullptr)
                sourceBuilding->ReturnOutgoingResource(resource);
            if (targetBuilding != nullptr)
                targetBuilding->CancelRequestedResource(resource->type);
        }
    };

    if (originatingOwner == nullptr || map == nullptr || currentPathStep < 0 || currentPathStep >= static_cast<int>(transportPath.size()))
    {
        cancelTransport();
        return true;
    }

    int currentTileId = transportPath[currentPathStep];
    if (!IsTileTraversableForOwner(map, currentTileId, originatingOwner, sourceBuilding, targetBuilding))
    {
        cancelTransport();
        return true;
    }

    elapsedTime += dt;
    if(elapsedTime >= transportTime)
    {
        if (currentPathStep + 1 >= transportPath.size())
        {
            Building* current = map->GetBuilding(currentTileId);
            if (current != nullptr && current == targetBuilding)
                current->ReceptTransport(this);
            else
                cancelTransport();
            return true;
        }

        int nextTileId = transportPath[currentPathStep + 1];
        if (!IsTileTraversableForOwner(map, nextTileId, originatingOwner, sourceBuilding, targetBuilding))
        {
            cancelTransport();
            return true;
        }

        Building* next = map->GetBuilding(nextTileId);
        if (next == nullptr)
        {
            cancelTransport();
            return true;
        }

        if (next->HasComponent<RoadComponent>())
        {
            auto* road = static_cast<Road*>(next);
            if (static_cast<int>(road->transportables.size()) >= road->GetModifiedMaxCapacity())
            {
                Building* currentBuilding = map->GetBuilding(currentTileId);
                auto* currentRoad = currentBuilding != nullptr && currentBuilding->HasComponent<RoadComponent>()
                    ? static_cast<Road*>(currentBuilding)
                    : nullptr;
                if (currentRoad != nullptr)
                {
                    auto currentIt = std::find(currentRoad->transportables.begin(), currentRoad->transportables.end(), this);
                    auto oncomingIt = std::find_if(road->transportables.begin(), road->transportables.end(),
                        [currentTileId](Transportable* other)
                        {
                            return other != nullptr &&
                                   other->currentPathStep + 1 < static_cast<int>(other->transportPath.size()) &&
                                   other->transportPath[other->currentPathStep + 1] == currentTileId &&
                                   other->elapsedTime >= other->transportTime;
                        });

                    if (currentIt != currentRoad->transportables.end() && oncomingIt != road->transportables.end())
                    {
                        Transportable* oncoming = *oncomingIt;
                        *currentIt = oncoming;
                        *oncomingIt = this;

                        currentPathStep++;
                        elapsedTime = 0.0;
                        transportTime = road->GetModifiedTransportTime();

                        oncoming->currentPathStep++;
                        oncoming->elapsedTime = 0.0;
                        oncoming->transportTime = currentRoad->GetModifiedTransportTime();
                    }
                }
                return false;
            }
        }

        next->ReceptTransport(this);

        return true;
    }
    return false;
}

// Initializes Transportable::BeginTransport.
void Transportable::BeginTransport(Building* src,Building* target, TileMap* tmap, const std::vector<int>& path)
{
    sourceBuilding = src;
    targetBuilding = target;
    originatingOwner = src != nullptr ? src->owner : nullptr;
    map = tmap;
    transportPath = path;
    transportTime = 0.0;
    elapsedTime = 0.0;
    currentPathStep = 0;
}

// Initializes RoadNetwork::RoadNetwork.
RoadNetwork::RoadNetwork(TileMap &tmap)
{
    navMap = std::make_unique<NavigationMap>();
    navMap->map = std::vector<NavigationNode>(tmap.tilemap.size());
    tilemap = &tmap;
}

// Advances this object's state for one frame.
void RoadNetwork::Update(double dt)
{
}

// Initializes RoadNetwork::BeginTransport.
bool RoadNetwork::BeginTransport(Building *src, Building *dest, Transportable* res)
{
    auto path = CalculatePath(src, dest);
    if(path.empty())
    {
        // Debug-level: no spam when supply packages retry without drogi (happens constantly).
        // Only log if you really need to debug routing issues (use Logger level DEBUG to see).
        return false;
    }
    if (!CanReserveTransportPath(dest, res, path))
    {
        // Debug-level: destination full is common during supply congestion, don't spam logs.
        return false;
    }
    res->BeginTransport(src, dest, tilemap, path);
    src->ReceptTransport(res);
    return true;
}

// Initializes RoadNetwork::CalculateTransportTime.
double RoadNetwork::CalculateTransportTime(Building *src, Building *dest)
{
    return 3.0;
}

// Advances UpdateNavMap for one frame or simulation tick.
void RoadNetwork::UpdateNavMap(int id, Building *bld)
{
    if (id < 0 || id >= navMap->map.size())
        return;

    // Any topology change can change which paths are valid — drop every cached
    // CalculatePath result rather than risk serving a stale route.
    pathCache.clear();

    if (bld == nullptr)
    {
        navMap->map[id].node = nullptr;
        return;
    }

    Log::Msg(tag, bld->name, " added to Navigation Map at map id ", id);
    navMap->map[id].node = bld;
}

// Initializes RoadNetwork::CalculatePath.
std::vector<int> RoadNetwork::CalculatePath(Building *src, Building *dest)
{
    if (src == nullptr || dest == nullptr || src->owner == nullptr)
        return {};

    std::pair<int, int> cacheKey{src->id, dest->id};
    auto cached = pathCache.find(cacheKey);
    if (cached != pathCache.end())
        return cached->second;

    int maxColumns = tilemap->params.sizeX;
    int maxRows = tilemap->params.sizeY;
    int maxIndex = maxColumns * maxRows;
    auto startTiles = tilemap->GetBuildingTileIds(src);
    auto endTiles = tilemap->GetBuildingTileIds(dest);

    if (startTiles.empty() || endTiles.empty())
        return {};

    std::vector<bool> isEnd(maxIndex, false);
    for (int end : endTiles)
    {
        if (end >= 0 && end < maxIndex)
            isEnd[end] = true;
    }

    const std::vector<int> directions{
        -maxColumns,
        maxColumns,
        -1,
        1
    };

    std::vector<bool> visited(maxIndex, false);
    std::vector<int> parent(maxIndex, -1);

    std::queue<int> q;
    for (int start : startTiles)
    {
        // Start tiles are src's own footprint — no ownership check needed here
        // (unlike the old territory system, a building's own tiles are always
        // "its" tiles regardless of any separate Tile::owner bookkeeping).
        if (start < 0 || start >= maxIndex)
            continue;

        q.push(start);
        visited[start] = true;
    }

    int reachedEnd = -1;

    while (!q.empty())
    {
        int current = q.front();
        q.pop();

        if (isEnd[current])
        {
            reachedEnd = current;
            break;
        }

        int currentCol = current % maxColumns;
        int currentRow = current / maxColumns;

        for (int dir = 0; dir < 4; dir++)
        {
            int next = current + directions[dir];

            if (next < 0 || next >= maxIndex)
                continue;

            int col = next % maxColumns;
            int row = next / maxColumns;

            if (abs(col - currentCol) + abs(row - currentRow) != 1)
                continue;

            if (visited[next])
                continue;

            // Traversable when it's the destination itself, or a road owned by
            // src's owner — resolved from the nav map's building, not
            // Tile::owner (removed with the territory system, ETAP 1).
            bool isDestinationTile = navMap->map[next].node == dest;
            bool isOwnedRoad = navMap->map[next].IsRoad() &&
                               navMap->map[next].node->owner == src->owner;
            if (!isDestinationTile && !isOwnedRoad)
                continue;

            visited[next] = true;
            parent[next] = current;
            q.push(next);
        }
    }

    if (reachedEnd < 0)
    {
        pathCache[cacheKey] = {};
        return {};
    }

    std::vector<int> path;
    for (int at = reachedEnd; at != -1; at = parent[at])
        path.push_back(at);

    std::reverse(path.begin(), path.end());

    pathCache[cacheKey] = path;
    return path;
}

// Returns whether this condition is currently true.
bool RoadNetwork::CanReserveTransportPath(Building* dest, Transportable* res, const std::vector<int>& path) const
{
    auto* resource = dynamic_cast<Resource*>(res);
    if (resource != nullptr && dest != nullptr)
    {
        auto views = dest->GetInputBufferViews();
        auto outputViews = dest->GetOutputBufferViews();
        views.insert(views.end(), outputViews.begin(), outputViews.end());

        bool hasCapacityView = false;
        for (const auto& view : views)
        {
            if (view.type != resource->type)
                continue;

            hasCapacityView = true;
            int incoming = CountIncomingToDestination(dest, resource->type);
            if (view.amount + incoming >= view.capacity)
                return false;
            break;
        }

        if (!hasCapacityView || !dest->CanReceiveResource(resource->type))
            return false;
    }

    return true;
}

// Initializes RoadNetwork::CountIncomingToDestination.
int RoadNetwork::CountIncomingToDestination(Building* dest, ResourceType type) const
{
    if (dest == nullptr || dest->owner == nullptr)
        return 0;

    // Perf fix (2026-07-12): this ran a FULL tilemap scan (sizeX*sizeY tiles,
    // ~90k on the default map) on every call — and it's called from
    // CanReserveTransportPath once per BeginTransport, i.e. once per resource
    // unit shipped. Latent before the T1 tile.owner fix (CalculatePath failed
    // first, so this was never reached); with transport actually working it
    // froze the whole sim thread during dispatch bursts. In-flight
    // transportables are always held by a building (source or road), and every
    // building is in the owner's tracked-buildings registry — same query
    // shape as CountIncomingResources in src/economy/Building.cpp.
    int incoming = 0;
    for (Building* carrier : dest->owner->GetTrackedBuildings())
    {
        if (carrier == nullptr || carrier->transportables.empty())
            continue;

        for (auto* transportable : carrier->transportables)
        {
            auto* resource = dynamic_cast<Resource*>(transportable);
            if (resource != nullptr && resource->targetBuilding == dest && resource->type == type)
                incoming++;
        }
    }

    return incoming;
}
