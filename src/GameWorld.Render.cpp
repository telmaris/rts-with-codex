#include "../inc/GameWorldInternal.h"

using namespace GameWorldInternal;

// Advances authoritative gameplay state for one simulation tick.
void GameWorld::UpdateSimulation(double dt)
{
    simulationTick++;
    UpdateControllers(dt);
    for (auto& [id, player] : playerHandler.players)
        if (player != nullptr)
        {
            player->UpdateFocus(dt);
            player->UpdateResearch(dt);
        }
    ProcessCommands();
    tilemap.UpdateBuildings(dt);
    battles.Update(tilemap, dt);
    for (auto& [id, player] : playerHandler.players)
        if (player != nullptr)
            player->UpdateEconomyTelemetry(dt);
}

// Advances this object's state for one frame.
void GameWorld::Update(double dt)
{
    UpdateSimulation(dt);
    DrawMap();
}

// Captures render-safe world state for another thread.
GameSnapshot GameWorld::BuildSnapshot() const
{
    GameSnapshot snapshot;
    snapshot.simulationTick = simulationTick;
    snapshot.localPlayerId = localPlayerId;
    snapshot.mapSize = {tilemap.params.sizeX, tilemap.params.sizeY};
    snapshot.tiles.reserve(tilemap.tilemap.size());

    for (const auto& tile : tilemap.tilemap)
    {
        GameSnapshotTile view;
        view.terrainTextureId = tile.terrainTextureId;
        if (tile.owner != nullptr)
        {
            view.hasOwner = true;
            view.ownerColor = tile.owner->color;
        }

        if (tile.building != nullptr)
        {
            view.hasBuilding = true;
            view.buildingType = tile.building->buildingType;
            view.buildingFootprint = tile.building->GetFootprint();
        }
        snapshot.tiles.push_back(view);
    }

    return snapshot;
}

// Draws cached terrain, territory and building layers.
void GameWorld::DrawMap()
{
    if (render == nullptr)
        return;

    bool cameraChanged =
        cachedCameraZoom != render->camera.zoom ||
        cachedCameraTarget.x != render->camera.target.x ||
        cachedCameraTarget.y != render->camera.target.y;

    Vec2f worldA = render->RenderToWorld({0.0f, 0.0f});
    Vec2f worldB = render->RenderToWorld({static_cast<float>(RENDER_WIDTH), static_cast<float>(RENDER_HEIGHT)});
    float minWorldX = std::min(worldA.x, worldB.x);
    float maxWorldX = std::max(worldA.x, worldB.x);
    float minWorldY = std::min(worldA.y, worldB.y);
    float maxWorldY = std::max(worldA.y, worldB.y);

    int minTileX = std::clamp(static_cast<int>(std::floor(minWorldX / TILE_SIZE)) - 2, 0, tilemap.params.sizeX - 1);
    int maxTileX = std::clamp(static_cast<int>(std::ceil(maxWorldX / TILE_SIZE)) + 2, 0, tilemap.params.sizeX - 1);
    int minTileY = std::clamp(static_cast<int>(std::floor(minWorldY / TILE_SIZE)) - 2, 0, tilemap.params.sizeY - 1);
    int maxTileY = std::clamp(static_cast<int>(std::ceil(maxWorldY / TILE_SIZE)) + 2, 0, tilemap.params.sizeY - 1);

    bool redrawTerrain = cameraChanged || tilemap.terrainDirty;
    bool redrawTerritory = cameraChanged || tilemap.territoryDirty;
    bool redrawBuildings = cameraChanged || tilemap.buildingsDirty;

    if (redrawTerrain)
    {
        render->ClearLayer(0);
        render->BeginLayer(0);
        for(int x = minTileX; x <= maxTileX; x++)
        {
            for(int y = minTileY; y <= maxTileY; y++)
            {
                auto& tile = tilemap.tilemap[y*tilemap.params.sizeX + x];

                Vec2f pos = {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};
                render->DrawAtlasTile(0, tile.terrainTextureId, pos);
            }
        }
        render->EndLayer();
        tilemap.terrainDirty = false;
    }

    if (redrawTerritory)
    {
        render->ClearLayer(2);
        render->BeginLayer(2);
        for(int x = minTileX; x <= maxTileX; x++)
        {
            for(int y = minTileY; y <= maxTileY; y++)
            {
                auto& tile = tilemap.tilemap[y*tilemap.params.sizeX + x];
                if (tile.owner == nullptr)
                    continue;

                Vec2f pos = {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};
                Color border = tile.owner->color;
                border.a = 230;
                if (render->camera.zoom < 0.75f)
                {
                    Color fill = tile.owner->color;
                    fill.a = render->camera.zoom < 0.45f ? 64 : 38;
                    DrawRectangle(static_cast<int>(pos.x),
                                  static_cast<int>(RENDER_HEIGHT - TILE_SIZE - pos.y),
                                  TILE_SIZE,
                                  TILE_SIZE,
                                  fill);
                }
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
        tilemap.territoryDirty = false;
    }

    if (redrawBuildings)
    {
        render->ClearLayer(1);
        render->BeginLayer(1);
        for(int x = minTileX; x <= maxTileX; x++)
        {
            for(int y = minTileY; y <= maxTileY; y++)
            {
                auto& tile = tilemap.tilemap[y*tilemap.params.sizeX + x];

                Vec2f pos = {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};

                if(tile.building)
                {
                    render->DrawBuildingTexture(tile.building.get(), pos);
                }
            }
        }
        render->EndLayer();
        tilemap.buildingsDirty = false;
    }

    cachedCameraTarget = {render->camera.target.x, render->camera.target.y};
    cachedCameraZoom = render->camera.zoom;
}
