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
    return allowed;
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
    Tile &tile = tilemap[id];
    if (tile.CanBuild(player))
    {
        Log::Msg(building->tag, building->id, " Created");
        building->owner = player;
        building->positionId = id;
        tile.CreateBuilding(std::move(building));
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
    return tilemap[id].building.get();
}

Building* TileMap::GetBuilding(Vec2i pos)
{
    GetBuilding(GetIdFromCoords(pos));
}

int TileMap::GetIdFromCoords(Vec2i coords)
{
    return (coords.x + coords.y*params.sizeX);
}

Vec2i TileMap::GetCoordsFromId(int id)
{
    return Vec2i{id % params.sizeX, id / params.sizeX};
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
    for(int x = 0; x < tilemap.params.sizeX; x++)
    {
        for(int y = 0; y < tilemap.params.sizeY; y++)
        {
            auto& tile = tilemap.tilemap[y*tilemap.params.sizeX + x];

            Vec2f pos = {x*64, y*64};

            // draw base terrain
            switch(tile.tileType)
            {
                case TileType::GRASS:
                {
                    render->DrawOnLayer(0, 0, 0, pos);
                    break;
                }
                case TileType::COAL:
                {
                    render->DrawOnLayer(0, 0, 1, pos);
                    break;
                }
                case TileType::IRON_ORE:
                {
                    render->DrawOnLayer(0, 0, 2, pos);
                    break;
                }
                default:
                    break;
            }

            // draw buildings

            if(tile.building)
            {
                switch(tile.building->buildingType)
                {
                    case BuildingType::Woodcutter:
                    {
                        render->DrawOnLayer(1, 1, 0, pos);
                        break;
                    }
                    case BuildingType::LumberMill:
                    {
                        render->DrawOnLayer(1, 1, 1, pos);
                        break;
                    }
                    case BuildingType::Mine:
                    {
                        render->DrawOnLayer(1, 1, 2, pos);
                        break;
                    }
                    case BuildingType::Foundry:
                    {
                        render->DrawOnLayer(1, 1, 3, pos);
                        break;
                    }
                    case BuildingType::StorageBuilding:
                    {
                        render->DrawOnLayer(1, 1, 4, pos);
                        break;
                    }
                    case BuildingType::Road:
                    {
                        render->DrawOnLayer(1, 1, 5, pos);
                        break;
                    }
                    default: break;
                }
            }
        }
    }
}