#include "../inc/RoadNetwork.h"
#include "../inc/MapGenerator.h"

// Advances this object's state for one frame.
bool Transportable::Update(double dt)
{
    auto* owner = sourceBuilding != nullptr ? sourceBuilding->owner : nullptr;
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

    if (owner == nullptr || map == nullptr || currentPathStep < 0 || currentPathStep >= static_cast<int>(transportPath.size()))
    {
        cancelTransport();
        return true;
    }

    int currentTileId = transportPath[currentPathStep];
    auto* currentOwner = (currentTileId >= 0 && currentTileId < map->tilemap.size()) ? map->GetTile(currentTileId).owner : nullptr;
    if (currentOwner != owner)
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
        auto* nextOwner = (nextTileId >= 0 && nextTileId < map->tilemap.size()) ? map->GetTile(nextTileId).owner : nullptr;
        if (nextOwner != owner)
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
        Log::Msg(tag, "Path is empty! aborting transport...");
        return false;
    }
    if (!CanReserveTransportPath(dest, res, path))
    {
        Log::Msg(tag, "Transport path or destination is full! aborting transport...");
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
        if (start < 0 || start >= maxIndex)
            continue;
        if (tilemap->GetTile(start).owner != src->owner)
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

            if (tilemap->GetTile(next).owner != src->owner)
                continue;

            if (!(navMap->map[next].IsRoad() || navMap->map[next].node == dest))
                continue;

            visited[next] = true;
            parent[next] = current;
            q.push(next);
        }
    }

    if (reachedEnd < 0)
        return {};

    std::vector<int> path;
    for (int at = reachedEnd; at != -1; at = parent[at])
        path.push_back(at);

    std::reverse(path.begin(), path.end());
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

// Initializes RoadNetwork::CountReservedRoadCapacity.
int RoadNetwork::CountReservedRoadCapacity(int roadTileId) const
{
    if (tilemap == nullptr)
        return 0;

    int reserved = 0;
    for (const auto& tile : tilemap->tilemap)
    {
        Building* building = tile.building.get();
        if (building == nullptr)
            continue;

        for (auto* transportable : building->transportables)
        {
            if (transportable == nullptr)
                continue;

            auto it = std::find(transportable->transportPath.begin(), transportable->transportPath.end(), roadTileId);
            if (it == transportable->transportPath.end())
                continue;

            int roadPathIndex = static_cast<int>(std::distance(transportable->transportPath.begin(), it));
            if (roadPathIndex == transportable->currentPathStep || roadPathIndex == transportable->currentPathStep + 1)
                reserved++;
        }
    }

    return reserved;
}

// Initializes RoadNetwork::CountIncomingToDestination.
int RoadNetwork::CountIncomingToDestination(Building* dest, ResourceType type) const
{
    if (tilemap == nullptr || dest == nullptr)
        return 0;

    int incoming = 0;
    for (const auto& tile : tilemap->tilemap)
    {
        Building* building = tile.building.get();
        if (building == nullptr)
            continue;

        for (auto* transportable : building->transportables)
        {
            auto* resource = dynamic_cast<Resource*>(transportable);
            if (resource != nullptr && resource->targetBuilding == dest && resource->type == type)
                incoming++;
        }
    }

    return incoming;
}

// Returns whether this condition is currently true.
bool RoadNetwork::CheckIfPathWasTaken(int id, std::vector<int> &path)
{
    auto it = std::find(path.begin(), path.end(), id);

    return it == path.end() ? true : false;
}
