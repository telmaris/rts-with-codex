#include "../inc/RoadNetwork.h"
#include "../inc/MapGenerator.h"

// ========= TRANSPORTABLE =============

bool Transportable::Update(double dt)
{
    elapsedTime += dt;
    if(elapsedTime >= transportTime)
    {
        Building* next = (*map)[transportPath[currentPathStep+1]].building.get();
        // Log::Msg("[Transportable]", "calling recept transport at ", map->GetCoordsFromId(next->positionId));
        next->ReceptTransport(this);

        return true;
    }
    return false;
}

void Transportable::BeginTransport(Building* src,Building* target, TileMap* tmap, std::vector<int>& path)
{
    sourceBuilding = src;
    targetBuilding = target;
    map = tmap;
    transportPath = path;
    transportTime = 0.0;
}

// ========================================

RoadNetwork::RoadNetwork(TileMap &tmap)
{
    navMap = std::make_unique<NavigationMap>();
    navMap->map = std::vector<NavigationNode>(tmap.tilemap.size());
    tilemap = &tmap;
}

void RoadNetwork::Update(double dt)
{
}

void RoadNetwork::BeginTransport(Building *src, Building *dest, Transportable* res)
{
    auto path = CalculatePath(src, dest);
    if(path.empty()) 
    {
        Log::Msg(tag, "Path is empty! aborting transport...");
        return;
    }
    res->BeginTransport(src, dest, tilemap, path);
    src->ReceptTransport(res);

    // for (auto &i : path)
    // {
    //     std::cout << tilemap->GetCoordsFromId(i) << " ";
    // }
    // std::cout << std::endl;
}

double RoadNetwork::CalculateTransportTime(Building *src, Building *dest)
{
    // todo:: obliczyć czas z src do dest
    // w przyszłości: zaplanowanie trasy

    return 3.0;
}

void RoadNetwork::UpdateNavMap(int id, Building *bld)
{
    if (id < 0 || id >= navMap->map.size())
        return;
    Log::Msg(tag, bld->name, " added to Navigation Map at map id ", id);
    navMap->map[id].node = bld;
}

std::vector<int> RoadNetwork::CalculatePath(Building *src, Building *dest)
{
    int start = src->placement->id;
    int end = dest->placement->id;

    int maxColumns = tilemap->params.sizeX;
    int maxRows = tilemap->params.sizeY;
    int maxIndex = maxColumns * maxRows;

    const std::vector<int> directions{
        -maxColumns, // up
        maxColumns,  // down
        -1,          // left
        1            // right
    };

    std::vector<bool> visited(maxIndex, false);
    std::vector<int> parent(maxIndex, -1);

    std::queue<int> q;
    q.push(start);
    visited[start] = true;

    while (!q.empty())
    {
        int current = q.front();
        q.pop();

        if (current == end)
            break;

        int currentCol = current % maxColumns;
        int currentRow = current / maxColumns;

        for (int dir = 0; dir < 4; dir++)
        {
            int next = current + directions[dir];

            if (next < 0 || next >= maxIndex)
                continue;

            int col = next % maxColumns;
            int row = next / maxColumns;

            // brak wrapowania
            if (abs(col - currentCol) + abs(row - currentRow) != 1)
                continue;

            if (visited[next])
                continue;

            // warunek przejścia
            if (!(navMap->map[next].IsRoad() || navMap->map[next].node == dest))
                continue;

            visited[next] = true;
            parent[next] = current;
            q.push(next);
        }
    }

    if (!visited[end])
        return {};

    std::vector<int> path;
    for (int at = end; at != -1; at = parent[at])
        path.push_back(at);

    std::reverse(path.begin(), path.end());
    return path;
}

bool RoadNetwork::CheckIfPathWasTaken(int id, std::vector<int> &path)
{
    auto it = std::find(path.begin(), path.end(), id);

    return it == path.end() ? true : false;
}