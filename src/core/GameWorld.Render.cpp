#include "core/GameWorldInternal.h"
#include "simulation/SectorGraph.h"
#include "warfare/DivisionSector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <vector>

using namespace GameWorldInternal;

namespace
{
    // ─── Deployed-division upkeep ─────────────────────────────────────────────
    // Per-tick maintenance for divisions deployed on the map (occupiedTile set).
    // Garrisoned (in-building) divisions are handled in GarrisonComponent::Update,
    // so nothing is double-counted here. Consumes supply, regenerates cohesion,
    // draws manpower reinforcements while supplied, and removes divisions that
    // have starved to death (strength <= 0). Deterministic: players in map id
    // order, divisions in forces push_back order.
    //
    // NOTE: field COMBAT was removed (see git history) — divisions no longer fight,
    // capture buildings, retreat or encircle. This pass is what remains of the old
    // RunFieldCombat: movement + logistics upkeep only.
    void UpdateDeployedDivisions(GameWorld& world, double dt)
    {
        for (auto& [pid, player] : world.playerHandler.players)
        {
            if (player == nullptr) continue;
            const double conservation = PlayerSupplyConservation(*player);
            for (auto& fptr : player->forces)
            {
                if (fptr == nullptr) continue;
                SoldierDivision& div = *fptr;
                if (div.occupiedTile.x < 0) continue;   // garrisoned — handled elsewhere

                // Supply upkeep. With combat gone a division is never `engaged`, so
                // this drains only the idle food ration (weapons/materiel are spent
                // in battle, which no longer happens). Starvation still bleeds
                // strength when the food pool hits zero.
                ConsumeDivisionSupply(div, dt, /*engaged=*/false, /*deployed=*/true, conservation);

                // Physical tile: worldPos while marching, occupiedTile at rest.
                const Vec2i tile = (div.inTransit && div.worldPos.x >= 0.0f)
                    ? Vec2i{std::clamp(static_cast<int>(div.worldPos.x / TILE_SIZE), 0, world.tilemap.params.sizeX - 1),
                            std::clamp(static_cast<int>(div.worldPos.y / TILE_SIZE), 0, world.tilemap.params.sizeY - 1)}
                    : div.occupiedTile;
                const bool inOwnTerritory = world.tilemap.IsInside(tile) &&
                    world.tilemap.tilemap[world.tilemap.GetIdFromCoords(tile)].owner == player.get();

                RegenerateDivisionCohesion(div, dt, inOwnTerritory, &player->balanceModifiers);
                ReinforceDivisionStrength(div, *player, dt, &player->balanceModifiers);
            }
        }

        // Remove divisions that have starved to death (strength <= 0). They are
        // player-owned (not building-owned), so drop them from forces and prune any
        // army group left empty.
        for (auto& [pid, player] : world.playerHandler.players)
        {
            if (player == nullptr) continue;
            bool removedAny = false;
            for (const auto& f : player->forces)
                if (f != nullptr && f->strength <= 0)
                {
                    player->armyGroups.RemoveDivision(f->id);
                    removedAny = true;
                }
            auto& forces = player->forces;
            forces.erase(std::remove_if(forces.begin(), forces.end(),
                [](const std::unique_ptr<SoldierDivision>& d) { return d == nullptr || d->strength <= 0; }), forces.end());
            if (removedAny)
                player->armyGroups.PruneEmptyArmies();
        }

        // Views held raw pointers into forces — rebuild so garrison stats/GUI and the
        // next tick see the current set after any removals.
        for (auto& [pid, player] : world.playerHandler.players)
            if (player != nullptr)
                player->RebuildGarrisonViews();
    }
}

// Advances authoritative gameplay state for one simulation tick.
void GameWorld::UpdateSimulation(double dt)
{
    simulationTick++;
    // Refresh each building's non-owning division view from the player's forces so
    // commands (movement/occupancy) and building updates see the current garrisons.
    for (auto& [id, player] : playerHandler.players)
        if (player != nullptr)
            player->RebuildGarrisonViews();
    UpdateControllers(dt);
    for (auto& [id, player] : playerHandler.players)
        if (player != nullptr)
        {
            player->UpdateFocus(dt);
            player->UpdateResearch(dt);
            player->UpdateArmyOrders(dt);  // Local army order simulation (issues MoveDivision commands)
        }
    ProcessCommands();
    // Assign each player's builders to the front of their construction queue
    // before ticking buildings, so only funded builder slots progress this tick.
    for (auto& [id, player] : playerHandler.players)
        if (player != nullptr)
            player->construction.Refresh(*player);
    tilemap.UpdateBuildings(dt);
    // Field combat/conquest was removed. Deployed divisions still march (handled in
    // GarrisonComponent::Update) and consume supply / reinforce here.
    UpdateDeployedDivisions(*this, dt);
    // BUG 3b/3d — resupply deployed divisions from nearest stockpile, once per second.
    if (simulationTick % 100 == 0)
        ResupplyDeployedDivisions();
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

// BUG 3b/3d — deployed divisions pull supply from the nearest friendly military
// building or HQ within SupplyRange Manhattan tiles. Deterministic: players in map
// id order, depots by positionId asc, divisions in forces-push_back order.
// Called once per second from UpdateSimulation; also exposed for direct test use.
void GameWorld::ResupplyDeployedDivisions()
{
    constexpr double kBaseSupplyRange = 20.0;   // base tiles; BalanceStat::SupplyRange

    for (auto& [pid, player] : playerHandler.players)
    {
        if (player == nullptr) continue;

        // Collect friendly depot buildings (military with supply stockpile).
        struct Depot
        {
            int positionId;
            Vec2i coords;
            SupplyBufferComponent* supply;
        };
        std::vector<Depot> depots;
        for (const auto& tile : player->tilemap.tilemap)
        {
            Building* b = tile.building.get();
            if (b == nullptr || b->owner != player.get()) continue;
            auto* garrison = b->GetComponent<GarrisonComponent>();
            auto* supply   = b->GetComponent<SupplyBufferComponent>();
            if (garrison == nullptr || supply == nullptr) continue;
            depots.push_back({b->positionId, player->tilemap.GetCoordsFromId(b->positionId), supply});
        }

        // Sort by positionId for determinism (tilemap iteration is already ascending
        // but be explicit so the order is a documented invariant, not an accident).
        std::sort(depots.begin(), depots.end(), [](const Depot& a, const Depot& b)
        { return a.positionId < b.positionId; });

        const double supplyRange = player->ResolveStat(
            Stat<double>{BalanceStat::SupplyRange, kBaseSupplyRange}, nullptr);

        for (auto& fptr : player->forces)
        {
            if (fptr == nullptr) continue;
            SoldierDivision& div = *fptr;
            if (div.occupiedTile.x < 0) continue;   // garrisoned — handled by GarrisonComponent::Update

            // Nearest depot within range (tie-break: lower positionId first).
            Depot* best = nullptr;
            int bestDist = std::numeric_limits<int>::max();
            for (auto& depot : depots)
            {
                int dist = std::abs(depot.coords.x - div.occupiedTile.x)
                         + std::abs(depot.coords.y - div.occupiedTile.y);
                if (dist > static_cast<int>(supplyRange)) continue;
                if (best == nullptr || dist < bestDist ||
                    (dist == bestDist && depot.positionId < best->positionId))
                {
                    best    = &depot;
                    bestDist = dist;
                }
            }

            if (best == nullptr) continue;   // out of supply range → no resupply

            SupplyBufferComponent& sb = *best->supply;

            // Food: pull from the building's food ResourceBuffer.
            {
                int need = div.foodSupplyCapacity - div.foodSupply;
                while (need > 0 && !sb.buffer.buffer.empty())
                {
                    sb.buffer.buffer.pop_back();
                    div.foodSupply++;
                    need--;
                }
                sb.stored = static_cast<int>(sb.buffer.buffer.size());
            }

            // Weapons: pull from building's weaponStock.
            if (sb.weaponStock > 0)
            {
                int need = div.weaponSupplyCapacity - div.weaponSupply;
                if (need > 0)
                {
                    int give       = std::min(need, sb.weaponStock);
                    div.weaponSupply  += give;
                    sb.weaponStock    -= give;
                }
            }

            // Materiel: pull from building's materielStock.
            if (sb.materielStock > 0)
            {
                int need = div.materielSupplyCapacity - div.materielSupply;
                if (need > 0)
                {
                    int give         = std::min(need, sb.materielStock);
                    div.materielSupply  += give;
                    sb.materielStock    -= give;
                }
            }
        }
    }
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

    cachedCameraTarget = {render->camera.target.x, render->camera.target.y};
    cachedCameraZoom = render->camera.zoom;
}
