#include "core/GameWorldInternal.h"
#include "warfare/UnitMarchSystem.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <vector>

using namespace GameWorldInternal;

namespace
{
    // Placeholder per-unit-type fill color (pending real sprites) — lets units
    // be told apart at a glance regardless of owner; owner identity is still
    // shown via the box outline. Unknown/future unit ids fall back to a
    // deterministic hash-derived color rather than a single flat default, so
    // adding a new unit to units.rtsdata never makes two types look identical.
    Color PlaceholderUnitColor(const std::string& unitDefId)
    {
        if (unitDefId == "militia")
            return Color{170, 170, 170, 255};
        if (unitDefId == "swordsman")
            return Color{70, 130, 220, 255};
        if (unitDefId == "knight")
            return Color{230, 190, 40, 255};
        if (unitDefId == "ram")
            return Color{150, 90, 40, 255};

        std::size_t hash = std::hash<std::string>{}(unitDefId);
        return Color{
            static_cast<unsigned char>(80 + (hash % 150)),
            static_cast<unsigned char>(80 + ((hash / 150) % 150)),
            static_cast<unsigned char>(80 + ((hash / 22500) % 150)),
            255};
    }

    // Small health bar centered above a world-space anchor (already flipped
    // to screen Y). `ratio` is clamped to [0,1]; green->red by remaining HP.
    void DrawHealthBar(int screenX, int screenTopY, int width, float ratio)
    {
        ratio = std::clamp(ratio, 0.0f, 1.0f);
        int height = 4;
        int x = screenX - width / 2;
        Color fill = Color{
            static_cast<unsigned char>(220 * (1.0f - ratio) + 30 * ratio),
            static_cast<unsigned char>(200 * ratio + 30 * (1.0f - ratio)),
            30,
            255};
        DrawRectangle(x, screenTopY, width, height, Color{20, 20, 20, 200});
        DrawRectangle(x, screenTopY, static_cast<int>(width * ratio), height, fill);
        DrawRectangleLines(x, screenTopY, width, height, Color{0, 0, 0, 200});
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
    // Assign each player's builders to the front of their construction queue
    // before ticking buildings, so only funded builder slots progress this tick.
    for (auto& [id, player] : playerHandler.players)
        if (player != nullptr)
            player->construction.Refresh(*player);
    // Update buildings by iterating through Player registries instead of tilemap scan.
    // Avoids O(1M) tilemap iteration every tick; now O(n_buildings) which is typically ~100-1000.
    //
    // Determinism fix (docs/work_plan_2026-07-13.md, found while verifying
    // B1/B2 via a flaky HqCombatSystemTests.SiegeToEliminationIsDeterministic-
    // ForSameSeed): this is THE main per-tick building update loop — every
    // ProductionComponent::Update call along the way competes for the same
    // player-wide Manpower/Workers pool via AutoAssignWorkers, and the same
    // shared road capacity/resource pool via LogisticsComponent/Storage-
    // Component dispatch. Which building's turn comes first each tick
    // therefore is simulation-visible (it decides who gets scarce workers/
    // resources this tick), so — same reasoning as every other fixed
    // instance of this bug class — iteration order must not depend on
    // Building* heap addresses, which differ across independently-
    // constructed GameWorld instances (confirmed root cause via targeted
    // instrumentation: two identically-seeded worlds' same building ended up
    // with a different GetTotalProduced() count by tick ~5700).
    std::vector<Building*> orderedBuildings;
    for (auto& [id, player] : playerHandler.players)
    {
        if (player == nullptr) continue;
        orderedBuildings.insert(orderedBuildings.end(), player->GetTrackedBuildings().begin(), player->GetTrackedBuildings().end());
    }
    std::sort(orderedBuildings.begin(), orderedBuildings.end(), [](Building* a, Building* b) { return a->id < b->id; });

    for (Building* building : orderedBuildings)
    {
        if (building == nullptr || building->owner == nullptr) continue;

        bool wasUnderConstruction = building->IsUnderConstruction();
        building->Update(dt);

        if (wasUnderConstruction && !building->IsUnderConstruction())
        {
            tilemap.buildingsDirty = true;
            if (building->owner->roadNetwork != nullptr)
            {
                for (int tileId : tilemap.GetBuildingTileIds(building))
                    building->owner->roadNetwork->UpdateNavMap(tileId, building);
            }
            tilemap.AutoConnectBuilding(building);
        }
    }

    for (auto& [id, player] : playerHandler.players)
        if (player != nullptr)
        {
            player->UpdateEconomyTelemetry(dt);
            player->UpdateConqueredEconomy(dt);
        }

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
        // tile.owner is a relic of the removed territory system (ETAP 1) —
        // always nullptr in production today, so hasOwner/ownerColor are
        // effectively dead wire fields. Left in place to avoid a snapshot
        // wire-version bump for a rendering-only cleanup; see
        // docs/post_pivot_audit_2026-07-12.md T2.
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
        Player* owner = ownerIt != playerHandler.players.end() ? ownerIt->second.get() : nullptr;
        Color ownerColor = owner != nullptr ? owner->color : WHITE;

        // Placeholder fill = unit type (told apart at a glance), outline =
        // owner color (whose unit it is) — real sprites will replace both.
        Color fillColor = PlaceholderUnitColor(unit.unitDefId);
        if (unit.state == BattleUnitState::Dying)
            fillColor.a = 90;

        int screenX = static_cast<int>(worldPos.x);
        int screenY = static_cast<int>(RENDER_HEIGHT) - static_cast<int>(worldPos.y);
        int halfSize = static_cast<int>(TILE_SIZE * 0.3f);
        Rectangle box{static_cast<float>(screenX - halfSize), static_cast<float>(screenY - halfSize),
                      static_cast<float>(halfSize * 2), static_cast<float>(halfSize * 2)};
        DrawRectangleRec(box, fillColor);
        DrawRectangleLinesEx(box, unit.state == BattleUnitState::FightingUnit ? 2.0f : 1.0f, ownerColor);

        if (owner != nullptr && unit.state != BattleUnitState::Dying)
        {
            double maxHp = unit.GetEffectiveMaxHp(*owner);
            float ratio = maxHp > 0.0 ? static_cast<float>(unit.currentHp / maxHp) : 0.0f;
            DrawHealthBar(screenX, screenY - halfSize - 7, halfSize * 2, ratio);
        }
    }

    // HQ health bar — shown for a few seconds after an HQ last took siege
    // damage (HqComponent::recentDamageTimer), so a defender gets an obvious
    // "under attack" cue without cluttering the view of HQs at full health
    // that aren't currently being sieged.
    for (auto& [playerId, player] : playerHandler.players)
    {
        if (player == nullptr)
            continue;
        for (Building* hqBuilding : player->GetTrackedBuildingsWithComponent<HqComponent>())
        {
            const auto* hq = hqBuilding->GetComponent<HqComponent>();
            if (hq == nullptr || hq->recentDamageTimer <= 0.0)
                continue;

            Vec2f center = ComputeBuildingCenter(tilemap, *hqBuilding);
            int screenX = static_cast<int>(center.x);
            int screenY = static_cast<int>(RENDER_HEIGHT) - static_cast<int>(center.y);
            Vec2i footprint = hqBuilding->GetFootprint();
            int barWidth = static_cast<int>(std::max(footprint.x, footprint.y) * TILE_SIZE * 0.8f);
            int barTopY = screenY - (footprint.y * TILE_SIZE) / 2 - 14;
            double modifiedMaxHp = hq->GetModifiedMaxHp(*hqBuilding);
            float ratio = modifiedMaxHp > 0.0 ? static_cast<float>(hq->currentHp / modifiedMaxHp) : 0.0f;
            DrawHealthBar(screenX, barTopY, barWidth, ratio);
        }
    }

    // TD(etap-7.2): in-flight tower projectiles. Same always-redrawn layer as
    // units (they move every tick too) — placeholder small circle pending
    // real projectile art (plan 7.2's "krótkie linie/prostokąty/okręgi").
    for (const auto& [id, projectile] : projectiles)
    {
        auto ownerIt = playerHandler.players.find(projectile.sourcePlayerId);
        Color color = ownerIt != playerHandler.players.end() && ownerIt->second != nullptr
            ? ownerIt->second->color
            : WHITE;

        int screenX = static_cast<int>(projectile.position.x);
        int screenY = static_cast<int>(RENDER_HEIGHT) - static_cast<int>(projectile.position.y);
        DrawCircle(screenX, screenY, TILE_SIZE * 0.12f, color);
    }
    render->EndLayer();

    cachedCameraTarget = {render->camera.target.x, render->camera.target.y};
    cachedCameraZoom = render->camera.zoom;
}
