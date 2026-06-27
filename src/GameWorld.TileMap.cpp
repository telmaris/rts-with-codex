#include "../inc/GameWorldInternal.h"

using namespace GameWorldInternal;

// Creates and registers the requested runtime object.
void Tile::CreateBuilding(std::unique_ptr<Building> &&bld)
{
    building = std::move(bld);
    building->placement = this;
    building->InitBuilding(tileType);
}

// Clears the building anchored on this tile.
void Tile::DestroyBuilding()
{
    building = nullptr;
    buildingRef = nullptr;
}

// Updates the requested state value.
void Tile::SetOwner(Player *player)
{
    owner = player;
}

// Returns whether this condition is currently true.
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

// Returns the building occupying this tile, following footprint references.
Building* Tile::GetBuilding()
{
    return building != nullptr ? building.get() : buildingRef;
}

// Returns the building occupying this tile, following footprint references.
const Building* Tile::GetBuilding() const
{
    return building != nullptr ? building.get() : buildingRef;
}

Tile &TileMap::GetTile(int id)
{
    return tilemap[id];
}

// Updates the requested state value.
void TileMap::SetTile(int id, Tile &&tile)
{
    tilemap[id] = std::move(tile);
}

// Builds the requested map object or helper path.
void TileMap::BuildOnTile(int id, Player *player, std::unique_ptr<Building> &&building)
{
    if (id < 0 || id >= tilemap.size())
        return;

    Vec2i anchor = GetCoordsFromId(id);
    Vec2i footprint = building->GetFootprint();
    if (CanPlaceBuilding(building->buildingType, anchor, footprint, player))
    {
        Log::Msg(building->tag, building->id, " Created");
        building->owner = player;
        building->positionId = id;

        Tile &anchorTile = tilemap[id];
        anchorTile.CreateBuilding(std::move(building));
        Building* placed = anchorTile.building.get();
        if (player != nullptr)
            player->RegisterBuilding(placed);

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

        buildingsDirty = true;
        if (!placed->IsUnderConstruction())
        {
            if (auto* military = dynamic_cast<IMilitaryBuilding*>(placed))
            {
                Vec2i center{anchor.x + footprint.x / 2, anchor.y + footprint.y / 2};
                SetTerritory(center, military->GetTerritoryRadius() * 2 + 1, player);
            }
        }
    }
}

// Initializes TileMap::DestroyBuildingAt.
void TileMap::DestroyBuildingAt(int id)
{
    if (id < 0 || id >= tilemap.size())
        return;

    Building* building = tilemap[id].building.get();
    if (building == nullptr)
        return;

    Player* owner = building->owner;
    Vec2i anchor = GetCoordsFromId(building->positionId);
    Vec2i footprint = building->GetFootprint();
    std::vector<int> occupiedTileIds = GetBuildingTileIds(building);

    for (int y = 0; y < footprint.y; y++)
    {
        for (int x = 0; x < footprint.x; x++)
        {
            Vec2i pos{anchor.x + x, anchor.y + y};
            if (!IsInside(pos))
                continue;

            int tileId = GetIdFromCoords(pos);
            if (tileId != id && tilemap[tileId].buildingRef == building)
                tilemap[tileId].buildingRef = nullptr;
        }
    }

    bool wasRoad = building->buildingType == BuildingType::Road;
    bool wasMilitary = dynamic_cast<IMilitaryBuilding*>(building) != nullptr;
    if (owner != nullptr)
        owner->UnregisterBuilding(building);
    if (auto* production = dynamic_cast<ProductionBuilding*>(building))
    {
        if (owner != nullptr && production->assignedWorkers > 0)
        {
            double workers = owner->strategicResources.Get(StrategicResourceType::Workers);
            owner->strategicResources.Set(StrategicResourceType::Workers, workers - production->assignedWorkers);
            owner->strategicResources.Add(StrategicResourceType::Manpower, production->assignedWorkers);
            production->assignedWorkers = 0;
        }
    }
    tilemap[id].DestroyBuilding();

    if (owner != nullptr && owner->roadNetwork != nullptr)
    {
        for (int tileId : occupiedTileIds)
            owner->roadNetwork->UpdateNavMap(tileId, nullptr);
    }

    if (wasRoad)
        RefreshRoadTilesAround(anchor);

    buildingsDirty = true;
    if (wasMilitary)
        RecalculateTerritory(owner);
}

// Initializes TileMap::PlaceLoadedBuilding.
Building* TileMap::PlaceLoadedBuilding(int id, Player *player, std::unique_ptr<Building> &&building)
{
    if (id < 0 || id >= tilemap.size() || building == nullptr)
        return nullptr;

    Vec2i anchor = GetCoordsFromId(id);
    Vec2i footprint = building->GetFootprint();
    if (!IsInsideFootprint(anchor, footprint))
        return nullptr;

    building->owner = player;
    building->positionId = id;

    Tile &anchorTile = tilemap[id];
    anchorTile.CreateBuilding(std::move(building));
    Building* placed = anchorTile.building.get();
    if (player != nullptr)
        player->RegisterBuilding(placed);

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

    buildingsDirty = true;
    return placed;
}

// Advances UpdateBuildings for one frame or simulation tick.
void TileMap::UpdateBuildings(double dt)
{
    std::vector<int> destroyedBuildings;
    for(auto& tile : tilemap)
    {
        if(tile.building == nullptr)
            continue;

        bool wasUnderConstruction = tile.building->IsUnderConstruction();
        tile.building->Update(dt);
        if (wasUnderConstruction && !tile.building->IsUnderConstruction() && tile.building->owner != nullptr)
        {
            Player* owner = tile.building->owner;
            if (owner->roadNetwork != nullptr)
            {
                for (int tileId : GetBuildingTileIds(tile.building.get()))
                    owner->roadNetwork->UpdateNavMap(tileId, tile.building.get());
            }

            AutoConnectBuilding(tile.building.get());
            if (dynamic_cast<IMilitaryBuilding*>(tile.building.get()) != nullptr)
                RecalculateTerritory(owner);
        }

        auto* military = dynamic_cast<IMilitaryBuilding*>(tile.building.get());
        if (military != nullptr && military->GetHitPoints() <= 0)
            destroyedBuildings.push_back(tile.id);
    }

    for (int id : destroyedBuildings)
    {
        DestroyBuildingAt(id);
    }
}

// Returns the building occupying a tile id, or nullptr.
Building* TileMap::GetBuilding(int id)
{
    if(id < 0 || id >= tilemap.size()) return nullptr;
    return tilemap[id].GetBuilding();
}

// Returns the building occupying map coordinates, or nullptr.
Building* TileMap::GetBuilding(Vec2i pos)
{
    if (!IsInside(pos))
        return nullptr;

    return GetBuilding(GetIdFromCoords(pos));
}

// Converts map coordinates to a linear tile id.
int TileMap::GetIdFromCoords(Vec2i coords) const
{
    return (coords.x + coords.y*params.sizeX);
}

// Converts a linear tile id to map coordinates.
Vec2i TileMap::GetCoordsFromId(int id) const
{
    return Vec2i{id % params.sizeX, id / params.sizeX};
}

// Returns whether this condition is currently true.
bool TileMap::IsInside(Vec2i coords) const
{
    return coords.x >= 0 && coords.x < params.sizeX &&
           coords.y >= 0 && coords.y < params.sizeY;
}

// Returns whether this condition is currently true.
bool TileMap::IsInsideFootprint(Vec2i anchor, Vec2i footprint) const
{
    return IsInside(anchor) && IsInside({anchor.x + footprint.x - 1, anchor.y + footprint.y - 1});
}

// Returns whether this condition is currently true.
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

// Returns whether this condition is currently true.
bool TileMap::HasRequiredTerrainForBuilding(BuildingType type, Vec2i anchor, Vec2i footprint, int minimumTiles) const
{
    std::vector<TileType> allowedTypes;
    if (type == BuildingType::Woodcutter)
    {
        allowedTypes.push_back(TileType::WOOD);
    }
    else
    {
        const auto& definition = GetBuildingDefinition(type);
        for (const auto& terrainProduction : definition.terrainProductions)
            allowedTypes.push_back(terrainProduction.tileType);
    }

    if (allowedTypes.empty())
        return true;

    if (!IsInside(anchor))
        return false;

    const Tile& anchorTile = tilemap[GetIdFromCoords(anchor)];
    if (std::find(allowedTypes.begin(), allowedTypes.end(), anchorTile.tileType) == allowedTypes.end() ||
        anchorTile.resourceRichness <= 0)
        return false;

    int matchingTiles = 0;
    for (int y = 0; y < footprint.y; y++)
    {
        for (int x = 0; x < footprint.x; x++)
        {
            Vec2i pos{anchor.x + x, anchor.y + y};
            if (!IsInside(pos))
                continue;

            const Tile& tile = tilemap[GetIdFromCoords(pos)];
            bool matches = std::find(allowedTypes.begin(), allowedTypes.end(), tile.tileType) != allowedTypes.end();
            if (matches && tile.resourceRichness > 0)
                matchingTiles++;
        }
    }

    return matchingTiles >= minimumTiles;
}

// Returns whether this condition is currently true.
bool TileMap::CanPlaceBuilding(BuildingType type, Vec2i anchor, Vec2i footprint, Player* player) const
{
    if (!CanBuildFootprint(anchor, footprint, player))
        return false;

    if (!HasRequiredTerrainForBuilding(type, anchor, footprint, 2))
        return false;

    return true;
}

// Returns every tile id occupied by a building footprint.
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

// Returns walkable neighbor tile ids around a building footprint.
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

// Returns the default texture id for a terrain type.
int TileMap::GetTerrainTextureId(TileType type) const
{
    auto it = terrainVariants.find(type);
    if (it == terrainVariants.end() || it->second.empty())
        return 0;
    return it->second.front().textureId;
}

// Picks a map position or generated value.
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

// Returns the road-neighborhood bitmask for autotiling.
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

// Returns the texture id matching a road autotile mask.
int TileMap::GetRoadTextureId(Vec2i pos) const
{
    constexpr int roadAtlasBaseId = 5;
    return roadAtlasBaseId + GetRoadAutotileMask(pos);
}

// Initializes TileMap::RefreshRoadTilesAround.
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
    buildingsDirty = true;
}

// Finds the best matching runtime object.
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

        if (building->owner != player || !building->IsStorageLike())
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

// Initializes TileMap::ConnectReceiver.
void TileMap::ConnectReceiver(Building* source, Building* receiver)
{
    if (source == nullptr || receiver == nullptr || source == receiver)
        return;

    for (const auto& output : source->GetOutputBufferViews())
    {
        if (!receiver->CanAcceptResource(output.type))
            continue;

        bool disconnect = false;
        for (const auto& view : source->GetReceiverViews())
        {
            if (view.type == output.type && view.building == receiver)
            {
                disconnect = true;
                break;
            }
        }

        if (disconnect)
        {
            source->RemoveReceiver(output.type, receiver);
            receiver->RemoveSupplier(output.type, source);
            if (receiver->CanAcceptResource(output.type) && !receiver->HasSupplier(output.type))
            {
                Building* storage = FindNearestStorage(receiver, receiver->owner);
                if (storage != nullptr && storage != receiver)
                    receiver->SetSupplier(output.type, storage);
            }
        }
        else
        {
            source->SetReceiver(output.type, receiver);
        }
    }
}

// Initializes TileMap::AutoConnectBuilding.
void TileMap::AutoConnectBuilding(Building* building)
{
    if (building == nullptr || building->owner == nullptr)
        return;

    if (building->IsStorageLike())
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

// Updates the requested state value.
void TileMap::SetTerritory(Vec2i source, int size, Player* player)
{
    if (player == nullptr || size <= 0)
        return;

    int half = size / 2;
    int minX = std::max(0, source.x - half);
    int maxX = std::min(params.sizeX - 1, source.x + half);
    int minY = std::max(0, source.y - half);
    int maxY = std::min(params.sizeY - 1, source.y + half);

    Log::Msg("Set territory", "territory bounds: ", minX, " ", minY, " -> ", maxX, " ", maxY);
    int radiusSq = half * half;
    int edgeAllowance = std::max(1, half);

    for(int x = minX; x <= maxX; x++)
    {
        for(int y = minY; y <= maxY; y++)
        {
            int dx = x - source.x;
            int dy = y - source.y;
            if (dx * dx + dy * dy > radiusSq + edgeAllowance)
                continue;

            Tile& tile = tilemap[GetIdFromCoords({x, y})];
            if (tile.owner == nullptr || tile.owner == player)
                tile.SetOwner(player);
        }
    }
    territoryDirty = true;
}

// Initializes TileMap::RecalculateTerritory.
void TileMap::RecalculateTerritory(Player* player)
{
    if (player == nullptr)
        return;

    for (auto& tile : tilemap)
    {
        if (tile.owner == player)
            tile.owner = nullptr;
    }
    territoryDirty = true;

    for (auto& tile : tilemap)
    {
        Building* building = tile.building.get();
        if (building == nullptr || building->owner != player)
            continue;

        auto* military = dynamic_cast<IMilitaryBuilding*>(building);
        if (military == nullptr || military->GetHitPoints() <= 0 || building->IsUnderConstruction())
            continue;

        Vec2i anchor = GetCoordsFromId(building->positionId);
        Vec2i footprint = building->GetFootprint();
        Vec2i center{anchor.x + footprint.x / 2, anchor.y + footprint.y / 2};
        SetTerritory(center, military->GetTerritoryRadius() * 2 + 1, player);
    }
}

