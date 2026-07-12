#include "core/GameWorldInternal.h"
#include "warfare/UnitMarchSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <vector>

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
    // Assign each player's builders to the front of their construction queue
    // before ticking buildings, so only funded builder slots progress this tick.
    for (auto& [id, player] : playerHandler.players)
        if (player != nullptr)
            player->construction.Refresh(*player);
    // Update buildings by iterating through Player registries instead of tilemap scan.
    // Avoids O(1M) tilemap iteration every tick; now O(n_buildings) which is typically ~100-1000.
    for (auto& [id, player] : playerHandler.players)
    {
        if (player == nullptr) continue;
        for (Building* building : player->GetTrackedBuildings())
        {
            if (building == nullptr) continue;

            bool wasUnderConstruction = building->IsUnderConstruction();
            building->Update(dt);

            if (wasUnderConstruction && !building->IsUnderConstruction())
            {
                tilemap.buildingsDirty = true;
                if (player->roadNetwork != nullptr)
                {
                    for (int tileId : tilemap.GetBuildingTileIds(building))
                        player->roadNetwork->UpdateNavMap(tileId, building);
                }
                tilemap.AutoConnectBuilding(building);
            }
        }
    }

    for (auto& [id, player] : playerHandler.players)
        if (player != nullptr)
            player->UpdateEconomyTelemetry(dt);

    UpdateUnits(dt);
}

// Advances this object's state for one frame.
void GameWorld::Update(double dt)
{
    UpdateSimulation(dt);
    DrawMap();
}

bool GameWorld::IsPlayerDefeated(int playerId) const
{
    auto it = playerHandler.players.find(playerId);
    return it != playerHandler.players.end() && it->second != nullptr && it->second->defeated;
}

int GameWorld::GetVictorPlayerId() const
{
    int survivors = 0;
    int lastAlive = -1;
    for (const auto& [pid, player] : playerHandler.players)
    {
        if (player == nullptr) continue;
        if (!player->defeated) { survivors++; lastAlive = pid; }
    }
    // Only a decided game (started with >=2 players, one left) reports a victor.
    int total = 0;
    for (const auto& [pid, player] : playerHandler.players)
        if (player != nullptr) total++;
    return (total >= 2 && survivors == 1) ? lastAlive : -1;
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
        view.isMilitaryRoad = tile.isMilitaryRoad;
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
                // TD(etap-2): military road placeholder — a flat tint until a
                // dedicated texture exists; kept as its own visual type so
                // swapping in real art later doesn't touch this call site.
                if (tile.isMilitaryRoad)
                {
                    DrawRectangle(static_cast<int>(pos.x),
                                  static_cast<int>(RENDER_HEIGHT - TILE_SIZE - pos.y),
                                  TILE_SIZE,
                                  TILE_SIZE,
                                  Color{139, 90, 43, 150});
                }
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
                    // Buildings still under construction (actively built or waiting
                    // in the queue) are drawn shaded until they finish.
                    Color tint = tile.building->IsUnderConstruction()
                        ? Color{118, 128, 150, 165}
                        : WHITE;
                    render->DrawBuildingTexture(tile.building.get(), pos, tint);
                }
            }
        }
        render->EndLayer();
        tilemap.buildingsDirty = false;
    }

    // TD(etap-4/5): deployed units. Always redrawn (layer 3, never cached)
    // since marching/fighting units move or change tint every tick, unlike
    // the mostly-static layers above. Placeholder shape (owner-colored
    // rectangle — no unit texture yet) pending real sprites/animation
    // (plan 4.3) — deliberately simple so swapping in art later only touches
    // this block.
    render->ClearLayer(3);
    render->BeginLayer(3);
    for (const auto& [instanceId, unit] : deployedUnits)
    {
        if (unit.tileIndex < 0)
            continue; // still waiting in the spawn queue, not on the map yet

        Vec2f worldPos = UnitMarchSystem::ComputeWorldPosition(*this, unit);

        auto ownerIt = playerHandler.players.find(unit.ownerPlayerId);
        Color color = ownerIt != playerHandler.players.end() && ownerIt->second != nullptr
            ? ownerIt->second->color
            : WHITE;
        if (unit.state == BattleUnitState::FightingUnit)
            color.a = 255; // brighter while actively trading blows
        else if (unit.state == BattleUnitState::Dying)
            color.a = 90;

        int screenX = static_cast<int>(worldPos.x);
        int screenY = static_cast<int>(RENDER_HEIGHT) - static_cast<int>(worldPos.y);
        int halfSize = static_cast<int>(TILE_SIZE * 0.3f);
        Rectangle box{static_cast<float>(screenX - halfSize), static_cast<float>(screenY - halfSize),
                      static_cast<float>(halfSize * 2), static_cast<float>(halfSize * 2)};
        DrawRectangleRec(box, color);
        DrawRectangleLinesEx(box, 1.0f, BLACK);
    }
    render->EndLayer();

    cachedCameraTarget = {render->camera.target.x, render->camera.target.y};
    cachedCameraZoom = render->camera.zoom;
}
