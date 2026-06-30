#include "../inc/GameWorldInternal.h"
#include "../inc/DivisionSector.h"

using namespace GameWorldInternal;

namespace
{
    constexpr int MultiplayerHumanSlots = 2;

    Color PlayerSlotColor(int id)
    {
        static const std::array<Color, 7> colors{
            Color{66, 154, 255, 255},
            Color{220, 72, 72, 255},
            Color{230, 151, 62, 255},
            Color{176, 86, 216, 255},
            Color{73, 181, 126, 255},
            Color{217, 210, 82, 255},
            Color{88, 196, 210, 255}
        };
        return colors[static_cast<size_t>(std::clamp(id, 0, static_cast<int>(colors.size()) - 1))];
    }

    // Adds a debug resource package to the player's headquarters.
    void GrantDebugResourcesToHeadquarters(Player* player, int amount)
    {
        if (player == nullptr || amount <= 0)
            return;

        for (auto* building : player->GetTrackedBuildingsWithComponent<StorageComponent>())
        {
            auto* storage = building != nullptr ? building->GetComponent<StorageComponent>() : nullptr;
            if (storage == nullptr || building->buildingType != BuildingType::Headquarters)
                continue;

            for (ResourceType type : resourceTypes)
            {
                auto& buffer = storage->buffers[type];
                if (buffer.type == ResourceType::Null)
                    buffer = ResourceBuffer{type, amount};
                buffer.bufferSize = std::max(buffer.bufferSize, static_cast<int>(buffer.buffer.size()) + amount);
                for (int i = 0; i < amount; i++)
                    buffer.GenerateResource(type);
            }
            Log::Msg("[Debug]", "starting HQ received ", amount, " of every resource");
            return;
        }
    }

    void GrantDebugManpower(Player* player)
    {
        if (player == nullptr)
            return;
        int cap = player->GetPopulationCap();
        int gift = static_cast<int>(cap * 0.7);
        if (gift > 0)
        {
            player->AddManpower(static_cast<double>(gift));
            Log::Msg("[Debug]", player->name, " received ", gift, " manpower (70% of cap ", cap, ")");
        }
    }

    // Debug only: drops a small enemy outpost (Guard Tower + a few deployed
    // divisions) just past the human player's border so combat can be tested.
    void SpawnDebugEnemyOutpost(TileMap& map, Player* enemy, Vec2i humanHqCenter)
    {
        if (enemy == nullptr)
            return;

        int half = MapGenerator::HeadquartersTerritorySize() / 2;
        Vec2i towerAnchor{
            std::clamp(humanHqCenter.x + half + 5, 2, map.params.sizeX - 4),
            std::clamp(humanHqCenter.y - 1, 2, map.params.sizeY - 4)};
        Vec2i towerCenter{towerAnchor.x + 1, towerAnchor.y + 1};

        map.SetTerritory(towerCenter, 9, enemy);
        enemy->Build<GuardTower>(towerAnchor, false);
        Building* tower = map.GetBuilding(map.GetIdFromCoords(towerAnchor));
        auto* garrison = tower != nullptr ? tower->GetComponent<GarrisonComponent>() : nullptr;
        if (garrison == nullptr)
        {
            Log::Msg("[Debug]", "enemy outpost build failed (terrain?)");
            return;
        }

        int spawned = 0;
        for (int dy = -1; dy <= 2 && spawned < 4; dy++)
        {
            Vec2i tile{towerAnchor.x - 2, towerAnchor.y + dy};  // a line facing the human (-x)
            if (!map.IsInside(tile) || !IsTileWalkableForDivision(map, tile))
                continue;
            if (map.tilemap[map.GetIdFromCoords(tile)].owner != enemy)
                continue;

            SoldierDivision d = CreateMilitaryDivision(MilitaryUnitType::Swordsman, garrison->nextDivisionId++);
            d.occupiedTile = tile;
            d.sectorCell = {tile.x / 2, tile.y / 2};
            d.worldPos = {(tile.x + 0.5f) * TILE_SIZE, (tile.y + 0.5f) * TILE_SIZE};
            d.inTransit = false;
            garrison->divisions.push_back(d);
            spawned++;
        }
        garrison->Recount();
        Log::Msg("[Debug]", "spawned enemy outpost with ", spawned, " deployed divisions");
    }
}

// Creates and registers the requested runtime object.
Player* GameWorld::CreatePlayer(int id, PlayerControllerType controllerType, const std::string& name, Color color)
{
    auto player = std::make_unique<Player>(id, tilemap);
    player->name = name;
    player->controllerType = controllerType;
    player->color = color;

    Player* ptr = player.get();
    playerHandler.players[id] = std::move(player);
    AttachControllerForPlayer(ptr);
    return ptr;
}

// Creates and registers the requested runtime object.
Vec2i GameWorld::CreateStartingBase(Player* player, Vec2i hqAnchor, unsigned int seed)
{
    if (player == nullptr)
        return hqAnchor;

    Vec2i hqFootprint = MapGenerator::HeadquartersFootprint();
    hqAnchor = ClampAnchor(hqAnchor, hqFootprint, tilemap.params);
    std::mt19937 resourceRng(seed ^ 0xC2B2AE35u);
    MapGenerator::PrepareStartingArea(tilemap, hqAnchor, resourceRng);
    SetFootprintTerrain(tilemap, hqAnchor, hqFootprint, TileType::GRASS, resourceRng, 3);

    Vec2i hqCenter{hqAnchor.x + hqFootprint.x / 2, hqAnchor.y + hqFootprint.y / 2};

    tilemap.SetTerritory(hqCenter, MapGenerator::HeadquartersTerritorySize(), player);
    player->Build<Headquarters>(hqAnchor, false);

    Village villagePreview{0};
    Vec2i villageFootprint = villagePreview.GetFootprint();
    std::mt19937 startRng(seed);
    std::uniform_int_distribution<int> sideDist(0, 3);
    std::uniform_int_distribution<int> offsetDist(-2, 2);
    int gap = 3;

    std::vector<Vec2i> villageCandidates;
    for (int attempt = 0; attempt < 12; attempt++)
    {
        int side = sideDist(startRng);
        int offset = offsetDist(startRng);
        switch (side)
        {
            case 0:
                villageCandidates.push_back({hqAnchor.x - gap - villageFootprint.x, hqAnchor.y + offset});
                break;
            case 1:
                villageCandidates.push_back({hqAnchor.x + hqFootprint.x + gap, hqAnchor.y + offset});
                break;
            case 2:
                villageCandidates.push_back({hqAnchor.x + offset, hqAnchor.y - gap - villageFootprint.y});
                break;
            default:
                villageCandidates.push_back({hqAnchor.x + offset, hqAnchor.y + hqFootprint.y + gap});
                break;
        }
    }
    villageCandidates.push_back({hqAnchor.x - gap - villageFootprint.x, hqAnchor.y});
    villageCandidates.push_back({hqAnchor.x + hqFootprint.x + gap, hqAnchor.y});

    Vec2i villageAnchor = villageCandidates.back();
    Building* village = nullptr;
    for (auto candidate : villageCandidates)
    {
        candidate = ClampAnchor(candidate, villageFootprint, tilemap.params);
        if (!tilemap.CanBuildFootprint(candidate, villageFootprint, player))
            continue;

        villageAnchor = candidate;
        SetFootprintTerrain(tilemap, villageAnchor, villageFootprint, TileType::GRASS, resourceRng, 3);
        village = player->Build<Village>(villageAnchor, false);
        break;
    }

    if (village != nullptr)
        BuildStartRoad(player, villageAnchor, villageFootprint, hqAnchor, hqFootprint);

    PlaceStartingResourcePatch(tilemap, hqAnchor, hqFootprint, villageAnchor, villageFootprint, TileType::WOOD, resourceRng);
    PlaceStartingResourcePatch(tilemap, hqAnchor, hqFootprint, villageAnchor, villageFootprint, TileType::STONE, resourceRng);

    return hqAnchor;
}

// Initializes runtime state for this object.
void GameWorld::InitWorld(std::string name, Renderer* r, MapParameters params)
{
    worldName = name;
    render = r;

    tilemap.generator.GenerateTileMap(tilemap, params);

    localPlayerId = 0;
    auto* human = CreatePlayer(0, PlayerControllerType::LocalHuman, "Player", PlayerSlotColor(0));

    Vec2i hqAnchor = CreateStartingBase(human, MapGenerator::PickHeadquartersAnchor(params), params.seed ^ 0x9E3779B9u);
    if (params.debugMode)
    {
        human->debugMode = true;
        GrantDebugResourcesToHeadquarters(human, 50);
        GrantDebugManpower(human);
    }

    Vec2i hqFootprint = MapGenerator::HeadquartersFootprint();
    std::vector<Vec2i> occupiedAnchors{hqAnchor};
    int opponentCount = std::clamp(params.aiOpponentCount, 0, 5);
    for (int i = 0; i < opponentCount; i++)
    {
        int playerId = i + 1;
        auto* enemy = CreatePlayer(playerId, PlayerControllerType::AI, "AI Opponent " + std::to_string(playerId), PlayerSlotColor(playerId));
        Vec2i enemyAnchor = PickEnemyHeadquartersAnchor(occupiedAnchors, hqFootprint, params, params.seed ^ (0xD1B54A32u + static_cast<unsigned int>(i * 7919)));
        occupiedAnchors.push_back(enemyAnchor);
        CreateStartingBase(enemy, enemyAnchor, params.seed ^ (0x85EBCA6Bu + static_cast<unsigned int>(i * 104729)));
        if (params.debugMode)
        {
            enemy->debugMode = true;
            if (i == 0)
            {
                Vec2i humanHqCenter{hqAnchor.x + hqFootprint.x / 2, hqAnchor.y + hqFootprint.y / 2};
                SpawnDebugEnemyOutpost(tilemap, enemy, humanHqCenter);
            }
        }
    }

    if (render != nullptr)
    {
        render->camera.zoom = 1.75f;
        render->camera.rotation = 0.0f;
        Vec2f hqWorldCenter{
            static_cast<float>(hqAnchor.x * TILE_SIZE) + hqFootprint.x * TILE_SIZE * 0.5f,
            static_cast<float>(hqAnchor.y * TILE_SIZE) + hqFootprint.y * TILE_SIZE * 0.5f};
        render->CenterCameraOnWorld(hqWorldCenter, {tilemap.params.sizeX, tilemap.params.sizeY});
        cachedCameraTarget = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        cachedCameraZoom = -1.0f;
    }
}

// Initializes deterministic multiplayer runtime state with server-assigned slots.
void GameWorld::InitMultiplayerWorld(std::string name, Renderer* r, MapParameters params, int localId, bool authoritativeHost)
{
    worldName = name;
    render = r;

    tilemap.generator.GenerateTileMap(tilemap, params);

    localPlayerId = std::clamp(localId, 0, MultiplayerHumanSlots - 1);

    Vec2i hqFootprint = MapGenerator::HeadquartersFootprint();
    std::vector<Vec2i> occupiedAnchors;
    Vec2i cameraAnchor = MapGenerator::PickHeadquartersAnchor(params);

    for (int playerId = 0; playerId < MultiplayerHumanSlots; playerId++)
    {
        PlayerControllerType controllerType = PlayerControllerType::Remote;
        if (playerId == localPlayerId)
            controllerType = PlayerControllerType::LocalHuman;

        std::string playerName = playerId == 0 ? "Host" : "Client";
        auto* player = CreatePlayer(playerId, controllerType, playerName, PlayerSlotColor(playerId));
        Vec2i anchor = playerId == 0
            ? MapGenerator::PickHeadquartersAnchor(params)
            : PickEnemyHeadquartersAnchor(occupiedAnchors, hqFootprint, params, params.seed ^ (0xA511E9B3u + static_cast<unsigned int>(playerId * 4099)));
        occupiedAnchors.push_back(anchor);
        CreateStartingBase(player, anchor, params.seed ^ (0x9E3779B9u + static_cast<unsigned int>(playerId * 104729)));
        if (playerId == localPlayerId)
            cameraAnchor = anchor;
    }

    int opponentCount = std::clamp(params.aiOpponentCount, 0, 5);
    for (int i = 0; i < opponentCount; i++)
    {
        int playerId = MultiplayerHumanSlots + i;
        PlayerControllerType controllerType = authoritativeHost ? PlayerControllerType::AI : PlayerControllerType::Remote;
        auto* enemy = CreatePlayer(playerId, controllerType, "AI Opponent " + std::to_string(i + 1), PlayerSlotColor(playerId));
        Vec2i enemyAnchor = PickEnemyHeadquartersAnchor(occupiedAnchors, hqFootprint, params, params.seed ^ (0xD1B54A32u + static_cast<unsigned int>(i * 7919)));
        occupiedAnchors.push_back(enemyAnchor);
        CreateStartingBase(enemy, enemyAnchor, params.seed ^ (0x85EBCA6Bu + static_cast<unsigned int>(i * 104729)));
    }

    if (params.debugMode)
    {
        for (auto& [id, player] : playerHandler.players)
        {
            if (player == nullptr) continue;
            player->debugMode = true;
            GrantDebugResourcesToHeadquarters(player.get(), 50);
            GrantDebugManpower(player.get());
        }
    }

    if (render != nullptr)
    {
        render->camera.zoom = 1.75f;
        render->camera.rotation = 0.0f;
        Vec2f hqWorldCenter{
            static_cast<float>(cameraAnchor.x * TILE_SIZE) + hqFootprint.x * TILE_SIZE * 0.5f,
            static_cast<float>(cameraAnchor.y * TILE_SIZE) + hqFootprint.y * TILE_SIZE * 0.5f};
        render->CenterCameraOnWorld(hqWorldCenter, {tilemap.params.sizeX, tilemap.params.sizeY});
        cachedCameraTarget = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        cachedCameraZoom = -1.0f;
    }
}

