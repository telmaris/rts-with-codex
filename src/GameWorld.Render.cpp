#include "../inc/GameWorldInternal.h"
#include "../inc/SectorGraph.h"
#include "../inc/DivisionSector.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

using namespace GameWorldInternal;

namespace
{
    // One deployed division standing on the map, snapshotted for combat resolution.
    struct FieldUnit
    {
        int playerId{0};
        Player* player{nullptr};
        SoldierDivision* div{nullptr};
        Vec2i tile{-1, -1};
        DivisionCombatStats stats;
    };

    bool TilesAdjacent(Vec2i a, Vec2i b)
    {
        return (a.x != b.x || a.y != b.y) && std::abs(a.x - b.x) <= 1 && std::abs(a.y - b.y) <= 1;
    }

    long long TileKey(Vec2i t) { return static_cast<long long>(t.x) * 100000 + t.y; }

    Building* FindHeadquarters(Player* player)
    {
        if (player == nullptr) return nullptr;
        for (Building* b : player->GetTrackedBuildings())
            if (b != nullptr && b->buildingType == BuildingType::Headquarters)
                return b;
        return nullptr;
    }

    // Flips a captured military building to `attacker`. The defender's divisions
    // homed there are relocated to their HQ so deployed troops on the map are not
    // deleted with the building; ownership + territory are recomputed.
    void CaptureBuilding(GameWorld& world, int positionId, Player* attacker)
    {
        Building* b = world.tilemap.GetBuilding(positionId);
        if (b == nullptr || attacker == nullptr || b->owner == attacker)
            return;

        Player* defender = b->owner;
        if (auto* captured = b->GetComponent<GarrisonComponent>())
        {
            Building* defenderHq = FindHeadquarters(defender);
            auto* hqGarrison = defenderHq != nullptr ? defenderHq->GetComponent<GarrisonComponent>() : nullptr;
            if (hqGarrison != nullptr && defenderHq != b)
                for (const auto& d : captured->divisions)
                    hqGarrison->divisions.push_back(d);
            captured->divisions.clear();
            captured->Recount();
        }

        if (defender != nullptr)
            defender->UnregisterBuilding(b);
        b->owner = attacker;
        attacker->RegisterBuilding(b);

        if (auto* territory = b->GetComponent<TerritoryComponent>())
        {
            territory->hp = territory->GetMaxHp(*b);
            territory->siegeBuffer = 0.0f;
        }
        world.tilemap.RecalculateTerritory(attacker);
        if (defender != nullptr)
            world.tilemap.RecalculateTerritory(defender);
    }

    // Enemy building (with territory) on any 8-neighbour of `tile`, owned by another
    // player; nullptr when none.
    Building* AdjacentEnemyBuilding(GameWorld& world, Vec2i tile, const Player* owner)
    {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
            {
                if (dx == 0 && dy == 0) continue;
                Vec2i n{tile.x + dx, tile.y + dy};
                if (!world.tilemap.IsInside(n)) continue;
                Building* b = world.tilemap.GetBuilding(n);
                if (b != nullptr && b->owner != owner && b->GetComponent<TerritoryComponent>() != nullptr)
                    return b;
            }
        return nullptr;
    }

    // Order-to-start-then-sticky field combat. A division only fights once a battle
    // has begun — battles start from an Attack order while adjacent to an enemy
    // (division or building), then persist (engaged) while the division stays
    // adjacent to a live enemy, even after the order clears. Idle units touching
    // never fight. Deterministic (sorted + simultaneous apply) for lockstep.
    void RunFieldCombat(GameWorld& world, double dt)
    {
        std::vector<FieldUnit> units;
        for (auto& [pid, player] : world.playerHandler.players)
        {
            if (player == nullptr) continue;
            for (Building* building : player->GetTrackedBuildingsWithComponent<GarrisonComponent>())
            {
                auto* garrison = building != nullptr ? building->GetComponent<GarrisonComponent>() : nullptr;
                if (garrison == nullptr) continue;
                for (auto& div : garrison->divisions)
                {
                    if (div.occupiedTile.x < 0) continue;  // only deployed divisions fight
                    units.push_back({pid, player.get(), &div, div.occupiedTile,
                                     ComputeDivisionCombatStats(div, &player->balanceModifiers)});
                }
            }
        }

        std::sort(units.begin(), units.end(), [](const FieldUnit& a, const FieldUnit& b)
        {
            if (a.playerId != b.playerId) return a.playerId < b.playerId;
            if (a.tile.x != b.tile.x) return a.tile.x < b.tile.x;
            if (a.tile.y != b.tile.y) return a.tile.y < b.tile.y;
            return a.div->id < b.div->id;
        });

        std::map<long long, int> tileToUnit;
        for (size_t i = 0; i < units.size(); i++)
            tileToUnit[TileKey(units[i].tile)] = static_cast<int>(i);
        auto enemyUnitAt = [&](Vec2i tile, int ownerId) -> int
        {
            auto it = tileToUnit.find(TileKey(tile));
            if (it == tileToUnit.end() || units[it->second].playerId == ownerId) return -1;
            return it->second;
        };

        // Phase 1 — start engagements from active Attack orders.
        for (auto& U : units)
        {
            SoldierDivision* div = U.div;
            if (div->currentOrder != MilitaryOrderType::Attack || div->orderTargetPositionId < 0)
                continue;

            Vec2i targetTile = world.tilemap.GetCoordsFromId(div->orderTargetPositionId);
            Building* tb = world.tilemap.GetBuilding(div->orderTargetPositionId);
            if (tb != nullptr && tb->owner != U.player && tb->GetComponent<TerritoryComponent>() != nullptr)
            {
                if (AdjacentEnemyBuilding(world, U.tile, U.player) != nullptr)
                    div->engaged = true;     // adjacent to an enemy structure
            }
            else
            {
                int v = enemyUnitAt(targetTile, U.playerId);
                if (v >= 0 && TilesAdjacent(U.tile, units[v].tile))
                    div->engaged = true;     // adjacent to the targeted enemy division
                else if (v < 0 && AdjacentEnemyBuilding(world, U.tile, U.player) == nullptr)
                {
                    div->currentOrder = MilitaryOrderType::None;  // target gone
                    div->orderTargetPositionId = -1;
                }
            }
        }

        // Phase 2 — spread engagement to adjacent enemies (battles drag neighbours in).
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (size_t i = 0; i < units.size(); i++)
            {
                if (!units[i].div->engaged) continue;
                for (size_t j = 0; j < units.size(); j++)
                {
                    if (units[i].playerId == units[j].playerId) continue;
                    if (units[j].div->engaged) continue;
                    if (TilesAdjacent(units[i].tile, units[j].tile))
                    {
                        units[j].div->engaged = true;
                        changed = true;
                    }
                }
            }
        }

        // Phase 3 — resolve damage (simultaneous).
        std::map<SoldierDivision*, float> divLosses;
        std::map<int, float> buildingDamage;
        std::map<int, Player*> buildingAttacker;  // positionId -> capturing player

        for (size_t i = 0; i < units.size(); i++)
        {
            for (size_t j = i + 1; j < units.size(); j++)
            {
                if (units[i].playerId == units[j].playerId) continue;
                if (!units[i].div->engaged || !units[j].div->engaged) continue;
                if (!TilesAdjacent(units[i].tile, units[j].tile)) continue;
                DivisionDuelResult duel = ResolveDivisionDuel(units[i].stats, units[j].stats, dt);
                divLosses[units[i].div] += duel.attackerStrengthLoss;
                divLosses[units[j].div] += duel.defenderStrengthLoss;
            }
        }

        for (const auto& U : units)
        {
            if (!U.div->engaged) continue;
            std::set<int> hitBuildings;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                {
                    if (dx == 0 && dy == 0) continue;
                    Vec2i n{U.tile.x + dx, U.tile.y + dy};
                    if (!world.tilemap.IsInside(n)) continue;
                    Building* b = world.tilemap.GetBuilding(n);
                    if (b == nullptr || b->owner == U.player) continue;
                    if (b->GetComponent<TerritoryComponent>() == nullptr) continue;
                    if (!hitBuildings.insert(b->positionId).second) continue;
                    buildingDamage[b->positionId] += U.stats.lightAttack * static_cast<float>(dt);
                    buildingAttacker.emplace(b->positionId, U.player);  // first (sorted) attacker captures
                }
        }

        // Phase 4 — apply. Damage accumulates in float buffers so sub-1-per-tick
        // attrition is not lost to integer rounding.
        for (auto& [div, loss] : divLosses)
        {
            div->damageBuffer += loss;
            int whole = static_cast<int>(div->damageBuffer);
            if (whole > 0)
            {
                div->health -= whole;
                div->damageBuffer -= static_cast<float>(whole);
            }
        }

        std::set<int> razedBuildings;
        for (auto& [positionId, damage] : buildingDamage)
        {
            Building* b = world.tilemap.GetBuilding(positionId);
            auto* territory = b != nullptr ? b->GetComponent<TerritoryComponent>() : nullptr;
            if (territory == nullptr) continue;
            territory->siegeBuffer += damage;
            int whole = static_cast<int>(territory->siegeBuffer);
            if (whole > 0)
            {
                territory->ReceiveDamage(whole);
                territory->siegeBuffer -= static_cast<float>(whole);
            }
            if (territory->hp <= 0 && b->buildingType != BuildingType::Headquarters)
                razedBuildings.insert(positionId);   // captured below, not deleted
        }

        // Phase 5 — disengage units no longer next to a live enemy (battle ended).
        for (auto& U : units)
        {
            if (!U.div->engaged) continue;
            bool stillFighting = false;
            for (int dy = -1; dy <= 1 && !stillFighting; dy++)
                for (int dx = -1; dx <= 1 && !stillFighting; dx++)
                {
                    if (dx == 0 && dy == 0) continue;
                    Vec2i n{U.tile.x + dx, U.tile.y + dy};
                    int v = enemyUnitAt(n, U.playerId);
                    if (v >= 0 && units[v].div->health > 0)
                        stillFighting = true;
                    Building* b = world.tilemap.IsInside(n) ? world.tilemap.GetBuilding(n) : nullptr;
                    if (b != nullptr && b->owner != U.player &&
                        b->GetComponent<TerritoryComponent>() != nullptr &&
                        razedBuildings.find(b->positionId) == razedBuildings.end())
                        stillFighting = true;
                }
            if (!stillFighting)
                U.div->engaged = false;
        }

        // Phase 6 — remove destroyed divisions (release from armies).
        for (auto& [pid, player] : world.playerHandler.players)
        {
            if (player == nullptr) continue;
            bool removedAny = false;
            for (Building* building : player->GetTrackedBuildingsWithComponent<GarrisonComponent>())
            {
                auto* garrison = building != nullptr ? building->GetComponent<GarrisonComponent>() : nullptr;
                if (garrison == nullptr) continue;
                auto& divs = garrison->divisions;
                for (const auto& d : divs)
                    if (d.health <= 0)
                    {
                        player->armyGroups.RemoveDivision(d.id);
                        removedAny = true;
                    }
                divs.erase(std::remove_if(divs.begin(), divs.end(),
                    [](const SoldierDivision& d) { return d.health <= 0; }), divs.end());
            }
            if (removedAny)
                player->armyGroups.PruneEmptyArmies();
        }

        // Capture (flip ownership of) defeated military buildings — HQ is spared,
        // that's a game-over path. Done last so combat's unit pointers stay valid.
        for (int positionId : razedBuildings)
            CaptureBuilding(world, positionId, buildingAttacker.count(positionId) ? buildingAttacker[positionId] : nullptr);
    }

}

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
    RunFieldCombat(*this, dt);
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
