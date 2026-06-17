#include "../inc/GameWorld.h"

void Tile::CreateBuilding(std::unique_ptr<Building> &&bld)
{
    building = std::move(bld);
    building->placement = this;
    building->InitBuilding(tileType);
}

void Tile::DestroyBuilding()
{
    building = nullptr;
    buildingRef = nullptr;
}

void Tile::SetOwner(Player *player)
{
    owner = player;
}

bool Tile::CanBuild(Player *player)
{
    bool allowed = true;
    if (player != owner)
    {
        Log::Msg("[Tile]", " player is not an owner");
        allowed = false;
    }
    if (building != nullptr)
    {
        allowed = false;
    }
    if (buildingRef != nullptr)
    {
        allowed = false;
    }
    return allowed;
}

Building* Tile::GetBuilding()
{
    return building != nullptr ? building.get() : buildingRef;
}

const Building* Tile::GetBuilding() const
{
    return building != nullptr ? building.get() : buildingRef;
}

Tile &TileMap::GetTile(int id)
{
    return tilemap[id];
}

void TileMap::SetTile(int id, Tile &&tile)
{
    tilemap[id] = std::move(tile);
}

void TileMap::BuildOnTile(int id, Player *player, std::unique_ptr<Building> &&building)
{
    if (id < 0 || id >= tilemap.size())
        return;

    Vec2i anchor = GetCoordsFromId(id);
    Vec2i footprint = building->GetFootprint();
    if (CanBuildFootprint(anchor, footprint, player))
    {
        Log::Msg(building->tag, building->id, " Created");
        building->owner = player;
        building->positionId = id;

        Tile &anchorTile = tilemap[id];
        anchorTile.CreateBuilding(std::move(building));
        Building* placed = anchorTile.building.get();

        for (int y = 0; y < footprint.y; y++)
        {
            for (int x = 0; x < footprint.x; x++)
            {
                Vec2i pos{anchor.x + x, anchor.y + y};
                int tileId = GetIdFromCoords(pos);
                if (tileId != id)
                    tilemap[tileId].buildingRef = placed;
            }
        }

        if (placed->buildingType == BuildingType::Road)
            RefreshRoadTilesAround(anchor);
    }
}

void TileMap::UpdateBuildings(double dt)
{
    for(auto& tile : tilemap)
    {
        if(tile.building != nullptr) tile.building->Update(dt);
    }
}

Building* TileMap::GetBuilding(int id)
{
    if(id < 0 || id >= tilemap.size()) return nullptr;
    return tilemap[id].GetBuilding();
}

Building* TileMap::GetBuilding(Vec2i pos)
{
    return GetBuilding(GetIdFromCoords(pos));
}

int TileMap::GetIdFromCoords(Vec2i coords) const
{
    return (coords.x + coords.y*params.sizeX);
}

Vec2i TileMap::GetCoordsFromId(int id) const
{
    return Vec2i{id % params.sizeX, id / params.sizeX};
}

bool TileMap::IsInside(Vec2i coords) const
{
    return coords.x >= 0 && coords.x < params.sizeX &&
           coords.y >= 0 && coords.y < params.sizeY;
}

bool TileMap::IsInsideFootprint(Vec2i anchor, Vec2i footprint) const
{
    return IsInside(anchor) && IsInside({anchor.x + footprint.x - 1, anchor.y + footprint.y - 1});
}

bool TileMap::CanBuildFootprint(Vec2i anchor, Vec2i footprint, Player* player) const
{
    if (!IsInsideFootprint(anchor, footprint))
        return false;

    for (int y = 0; y < footprint.y; y++)
    {
        for (int x = 0; x < footprint.x; x++)
        {
            const Tile& tile = tilemap[GetIdFromCoords({anchor.x + x, anchor.y + y})];
            if (tile.owner != player || tile.HasBuilding())
                return false;
        }
    }

    return true;
}

std::vector<int> TileMap::GetBuildingTileIds(const Building* building) const
{
    std::vector<int> result;
    if (building == nullptr)
        return result;

    Vec2i anchor = GetCoordsFromId(building->positionId);
    Vec2i footprint = building->GetFootprint();
    for (int y = 0; y < footprint.y; y++)
    {
        for (int x = 0; x < footprint.x; x++)
        {
            Vec2i pos{anchor.x + x, anchor.y + y};
            if (IsInside(pos))
                result.push_back(GetIdFromCoords(pos));
        }
    }
    return result;
}

std::vector<int> TileMap::GetAdjacentTileIds(const Building* building) const
{
    std::vector<int> result;
    if (building == nullptr)
        return result;

    Vec2i anchor = GetCoordsFromId(building->positionId);
    Vec2i footprint = building->GetFootprint();
    for (int y = -1; y <= footprint.y; y++)
    {
        for (int x = -1; x <= footprint.x; x++)
        {
            bool insideFootprint = x >= 0 && x < footprint.x && y >= 0 && y < footprint.y;
            bool diagonalOnly = (x == -1 || x == footprint.x) && (y == -1 || y == footprint.y);
            if (insideFootprint || diagonalOnly)
                continue;

            Vec2i pos{anchor.x + x, anchor.y + y};
            if (IsInside(pos))
                result.push_back(GetIdFromCoords(pos));
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

int TileMap::GetTerrainTextureId(TileType type) const
{
    auto it = terrainVariants.find(type);
    if (it == terrainVariants.end() || it->second.empty())
        return 0;
    return it->second.front().textureId;
}

int TileMap::PickTerrainTexture(TileType type, std::mt19937& rng) const
{
    auto it = terrainVariants.find(type);
    if (it == terrainVariants.end() || it->second.empty())
        return 0;

    int totalWeight = 0;
    for (const auto& variant : it->second)
        totalWeight += std::max(0, variant.weight);

    if (totalWeight <= 0)
        return it->second.front().textureId;

    std::uniform_int_distribution<int> dist(1, totalWeight);
    int roll = dist(rng);
    for (const auto& variant : it->second)
    {
        roll -= std::max(0, variant.weight);
        if (roll <= 0)
            return variant.textureId;
    }

    return it->second.back().textureId;
}

int TileMap::GetRoadAutotileMask(Vec2i pos) const
{
    int mask = 0;
    const std::array<Vec2i, 9> offsets{
        Vec2i{-1, -1}, Vec2i{0, -1}, Vec2i{1, -1},
        Vec2i{-1, 0},  Vec2i{0, 0},  Vec2i{1, 0},
        Vec2i{-1, 1},  Vec2i{0, 1},  Vec2i{1, 1}
    };

    for (int i = 0; i < offsets.size(); i++)
    {
        Vec2i check{pos.x + offsets[i].x, pos.y + offsets[i].y};
        if (!IsInside(check))
            continue;

        auto* building = tilemap[GetIdFromCoords(check)].GetBuilding();
        if (building != nullptr && building->buildingType == BuildingType::Road)
            mask |= (1 << i);
    }

    return mask;
}

int TileMap::GetRoadTextureId(Vec2i pos) const
{
    constexpr int roadAtlasBaseId = 5;
    return roadAtlasBaseId + GetRoadAutotileMask(pos);
}

void TileMap::RefreshRoadTilesAround(Vec2i pos)
{
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            Vec2i check{pos.x + x, pos.y + y};
            if (!IsInside(check))
                continue;

            auto* building = GetBuilding(check);
            if (building != nullptr && building->buildingType == BuildingType::Road)
                building->textureId = GetRoadTextureId(check);
        }
    }
}

Building* TileMap::FindNearestStorage(Building* source, Player* player)
{
    if (source == nullptr)
        return nullptr;

    Vec2i origin = GetCoordsFromId(source->positionId);
    Building* best = nullptr;
    int bestDistance = std::numeric_limits<int>::max();

    for (auto& tile : tilemap)
    {
        auto* building = tile.building.get();
        if (building == nullptr || building == source)
            continue;

        if (building->owner != player || building->buildingType != BuildingType::StorageBuilding)
            continue;

        Vec2i pos = GetCoordsFromId(building->positionId);
        int distance = std::abs(pos.x - origin.x) + std::abs(pos.y - origin.y);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = building;
        }
    }

    return best;
}

void TileMap::ConnectReceiver(Building* source, Building* receiver)
{
    if (source == nullptr || receiver == nullptr || source == receiver)
        return;

    for (const auto& output : source->GetOutputBufferViews())
        source->SetReceiver(output.type, receiver);
}

void TileMap::AutoConnectBuilding(Building* building)
{
    if (building == nullptr || building->owner == nullptr)
        return;

    if (building->buildingType == BuildingType::StorageBuilding)
    {
        for (auto& tile : tilemap)
        {
            auto* other = tile.building.get();
            if (other == nullptr || other == building || other->owner != building->owner)
                continue;

            for (const auto& output : other->GetOutputBufferViews())
            {
                if (!other->HasReceiver(output.type))
                    other->SetReceiver(output.type, building);
            }

            for (const auto& input : other->GetInputBufferViews())
            {
                if (!other->HasSupplier(input.type))
                    other->SetSupplier(input.type, building);
            }
        }
        return;
    }

    Building* storage = FindNearestStorage(building, building->owner);
    if (storage == nullptr)
        return;

    for (const auto& output : building->GetOutputBufferViews())
    {
        if (!building->HasReceiver(output.type))
            building->SetReceiver(output.type, storage);
    }

    for (const auto& input : building->GetInputBufferViews())
    {
        if (!building->HasSupplier(input.type))
            building->SetSupplier(input.type, storage);
    }
}

void TileMap::SetTerritory(Vec2i source, int size, Player* player)
{
    // size is a square side length
    if((source.x - size/2) < 0 || (source.x + size/2) >= params.sizeX ||
        (source.y - size/2) < 0 || (source.y + size/2) >= params.sizeY) return;

        Vec2i startingPos{source.x - size/2, source.y - size/2};
        Log::Msg("Set territory", "territory starting pos: ", startingPos.x, " ", startingPos.y);

    for(int x = 0; x < size; x++)
    {
        for(int y = 0; y < size; y++)
        {
            tilemap[GetIdFromCoords({startingPos.x + x, startingPos.y + y})].SetOwner(player);
            // todo: check if neighbouring territory isnt occupied by enemy player
        }
    }
}

// =====================================================

void GameWorld::InitWorld(std::string name, Renderer* r)
{
    worldName = name;
    render = r;

    MapParameters params;
    params.sizeX = 101;
    params.sizeY = 101;

    tilemap.generator.GenerateTileMap(tilemap, params);

    playerHandler.players.insert({0, std::make_unique<Player>(0, tilemap)});
    auto p = playerHandler.players[0].get();

    tilemap.SetTerritory({12,12}, 25, p);
    tilemap[{1,11}].tileType = TileType::IRON_ORE;
    tilemap[{1,11}].terrainTextureId = tilemap.GetTerrainTextureId(TileType::IRON_ORE);

    p->Build<Woodcutter>({5,5});
    p->Build<Mine>({7,5});
    p->Build<Foundry>({8,5});
    p->Build<StorageBuilding>({9,5});
    p->Build<LumberMill>({6,5});
}

void GameWorld::Update(double dt)
{
    // update everything like prodution timers, transport, combat, research etc.
    
    // update tilemap with buildings
    tilemap.UpdateBuildings(dt);

    // draw map
    DrawMap();
}

void GameWorld::DrawMap()
{
    render->BeginLayer(0);
    for(int x = 0; x < tilemap.params.sizeX; x++)
    {
        for(int y = 0; y < tilemap.params.sizeY; y++)
        {
            auto& tile = tilemap.tilemap[y*tilemap.params.sizeX + x];

            Vec2f pos = {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};
            render->DrawAtlasTile(0, tile.terrainTextureId, pos);
        }
    }
    render->EndLayer();

    render->BeginLayer(2);
    for(int x = 0; x < tilemap.params.sizeX; x++)
    {
        for(int y = 0; y < tilemap.params.sizeY; y++)
        {
            auto& tile = tilemap.tilemap[y*tilemap.params.sizeX + x];
            if (tile.owner == nullptr)
                continue;

            Vec2f pos = {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};
            Color border{66, 154, 255, 230};
            auto drawEdge = [&](Vec2i neighbour, Vector2 a, Vector2 b)
            {
                if (!tilemap.IsInside(neighbour) || tilemap[neighbour].owner != tile.owner)
                    DrawLineEx(a, b, 4.0f, border);
            };

            float sx = pos.x;
            float sy = static_cast<float>(RENDER_HEIGHT - TILE_SIZE - pos.y);
            drawEdge({x, y - 1}, {sx, sy + TILE_SIZE}, {sx + TILE_SIZE, sy + TILE_SIZE});
            drawEdge({x + 1, y}, {sx + TILE_SIZE, sy}, {sx + TILE_SIZE, sy + TILE_SIZE});
            drawEdge({x, y + 1}, {sx, sy}, {sx + TILE_SIZE, sy});
            drawEdge({x - 1, y}, {sx, sy}, {sx, sy + TILE_SIZE});
        }
    }
    render->EndLayer();

    render->BeginLayer(1);
    for(int x = 0; x < tilemap.params.sizeX; x++)
    {
        for(int y = 0; y < tilemap.params.sizeY; y++)
        {
            auto& tile = tilemap.tilemap[y*tilemap.params.sizeX + x];

            Vec2f pos = {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};

            if(tile.building)
            {
                auto footprint = tile.building->GetFootprint();
                Vec2f drawSize{
                    static_cast<float>(footprint.x * TILE_SIZE),
                    static_cast<float>(footprint.y * TILE_SIZE)};
                render->DrawAtlasTile(1, tile.building->GetTextureId(), pos, drawSize);
            }
        }
    }
    render->EndLayer();
}
