#include "../inc/GameWorldInternal.h"

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

        for (auto* building : player->GetTrackedBuildings())
        {
            auto* headquarters = dynamic_cast<Headquarters*>(building);
            if (headquarters == nullptr)
                continue;

            for (ResourceType type : resourceTypes)
            {
                auto& buffer = headquarters->resourceBuffers[type];
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
        GrantDebugResourcesToHeadquarters(human, 50);

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
        auto it = playerHandler.players.find(localPlayerId);
        if (it != playerHandler.players.end())
            GrantDebugResourcesToHeadquarters(it->second.get(), 50);
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

