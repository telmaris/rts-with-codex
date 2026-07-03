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
        Vec2i cell{-1, -1};   // quadrant, derived from tile (single source of truth)
        DivisionCombatStats stats;
    };



    // Flips a captured military building to `attacker`. Divisions are owned by the
    // Player (not the building), so capture does NOT touch them — any homed here are
    // auto-rehomed to a surviving building by RebuildGarrisonViews (run after
    // combat). Ownership + territory are recomputed.
    void CaptureBuilding(GameWorld& world, int positionId, Player* attacker)
    {
        Building* b = world.tilemap.GetBuilding(positionId);
        if (b == nullptr || attacker == nullptr || b->owner == attacker)
            return;

        Player* defender = b->owner;
        if (auto* captured = b->GetComponent<GarrisonComponent>())
        {
            captured->divisions.clear();   // stale view; forces re-home on rebuild
            captured->Recount();
        }

        if (defender != nullptr)
            defender->UnregisterBuilding(b);
        b->owner = attacker;
        attacker->RegisterBuilding(b);

        // Fully integrate the captured building into the new owner the same way a
        // freshly-built one is: seed its tiles into the owner's road-network nav map
        // and auto-connect it, otherwise it's owned on paper but invisible to
        // logistics/pathing. (Registering only in dataTracker is not enough.)
        if (attacker->roadNetwork != nullptr)
            for (int tileId : world.tilemap.GetBuildingTileIds(b))
                attacker->roadNetwork->UpdateNavMap(tileId, b);
        world.tilemap.AutoConnectBuilding(b);

        if (auto* territory = b->GetComponent<TerritoryComponent>())
        {
            territory->hp = territory->GetMaxHp(*b);
            territory->siegeBuffer = 0.0f;
        }
        // Order matters: SetTerritory never flips tiles held by ANOTHER player, so
        // the defender must release its ground FIRST — only then can the attacker's
        // recompute claim the radius around the captured building. The old order
        // left that ground neutral until some unrelated refresh re-ran the claim.
        if (defender != nullptr)
            world.tilemap.RecalculateTerritory(defender);
        world.tilemap.RecalculateTerritory(attacker);
    }

    // Enemy MILITARY TARGET (defensive work or HQ) on any 8-neighbour of `tile`,
    // owned by another player; nullptr when none. Civil buildings (Barracks
    // included) are never siege targets.
    Building* AdjacentEnemyBuilding(GameWorld& world, Vec2i tile, const Player* owner)
    {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
            {
                if (dx == 0 && dy == 0) continue;
                Vec2i n{tile.x + dx, tile.y + dy};
                if (!world.tilemap.IsInside(n)) continue;
                Building* b = world.tilemap.GetBuilding(n);
                if (b != nullptr && b->owner != owner && IsMilitaryAttackTarget(*b))
                    return b;
            }
        return nullptr;
    }

    // Combat is per-QUADRANT (HoI4 provinces). Two divisions engage when their 2x2
    // quadrants are the same or 8-adjacent — not their individual tiles.
    bool SectorsFight(Vec2i a, Vec2i b)
    {
        return std::abs(a.x - b.x) <= 1 && std::abs(a.y - b.y) <= 1;
    }

    // Number of divisions physically stationed INSIDE a building. Deployed
    // divisions homed here fight in the field — they do not man the walls.
    int StationedDefenders(const Building& building)
    {
        const auto* garrison = building.GetComponent<GarrisonComponent>();
        if (garrison == nullptr)
            return 0;
        int count = 0;
        for (const auto* d : garrison->divisions)
            if (d != nullptr && d->occupiedTile.x < 0)
                count++;
        return count;
    }

    // An empty defensive work puts up no fight: contact simply takes it, no
    // battle. Manned works (and the HQ, which always resists) must be besieged.
    bool FallsWithoutAFight(const Building& building)
    {
        return IsDefensiveGarrisonBuilding(building) && StationedDefenders(building) == 0;
    }

    // Enemy building adjacent to ANY tile of a division's quadrant; nullptr if none.
    Building* SectorAdjacentEnemyBuilding(GameWorld& world, Vec2i cell, const Player* owner)
    {
        Vec2i anchor{cell.x * 2, cell.y * 2};
        for (int i = 0; i < 4; i++)
        {
            Vec2i t{anchor.x + (i % 2), anchor.y + (i / 2)};
            if (!world.tilemap.IsInside(t)) continue;
            if (Building* b = AdjacentEnemyBuilding(world, t, owner))
                return b;
        }
        return nullptr;
    }

    // Every distinct enemy building adjacent to a division's quadrant.
    std::vector<Building*> SectorAdjacentEnemyBuildings(GameWorld& world, Vec2i cell, const Player* owner)
    {
        std::vector<Building*> result;
        std::set<int> seen;
        Vec2i anchor{cell.x * 2, cell.y * 2};
        for (int i = 0; i < 4; i++)
        {
            Vec2i t{anchor.x + (i % 2), anchor.y + (i / 2)};
            if (!world.tilemap.IsInside(t)) continue;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                {
                    if (dx == 0 && dy == 0) continue;
                    Vec2i n{t.x + dx, t.y + dy};
                    if (!world.tilemap.IsInside(n)) continue;
                    Building* b = world.tilemap.GetBuilding(n);
                    if (b != nullptr && b->owner != owner &&
                        IsMilitaryAttackTarget(*b) &&
                        seen.insert(b->positionId).second)
                        result.push_back(b);
                }
        }
        return result;
    }

    // Order-to-start-then-sticky field combat. Battles start from an Attack order
    // while adjacent to an enemy (division or building) OR automatically when two
    // hostile divisions share one quadrant (entering the enemy's province is an
    // attack in itself). Positions are PHYSICAL: a marching column fights in the
    // quadrant its body is crossing and is intercepted (halted) when a battle
    // catches it — it cannot roll through a defended province. Once begun, a
    // battle persists (engaged) while the division stays adjacent to a live
    // enemy, even after the order clears. Idle units in merely adjacent quadrants
    // never auto-fight. Deterministic (sorted + simultaneous apply) for lockstep.
    void RunFieldCombat(GameWorld& world, double dt)
    {
        std::vector<FieldUnit> units;
        for (auto& [pid, player] : world.playerHandler.players)
        {
            if (player == nullptr) continue;
            for (auto& fptr : player->forces)
            {
                auto& div = *fptr;
                if (div.occupiedTile.x < 0) continue;   // not deployed
                // Combat uses the division's PHYSICAL position: a marching column
                // fights (and is intercepted) in the quadrant its body is actually
                // crossing — occupiedTile is only its reserved destination and must
                // not let it siege/duel from range.
                Vec2i tile = div.occupiedTile;
                if (div.inTransit && div.worldPos.x >= 0.0f)
                    tile = {std::clamp(static_cast<int>(div.worldPos.x / TILE_SIZE), 0, world.tilemap.params.sizeX - 1),
                            std::clamp(static_cast<int>(div.worldPos.y / TILE_SIZE), 0, world.tilemap.params.sizeY - 1)};
                units.push_back({pid, player.get(), &div, tile,
                                 SectorCellOf(tile),
                                 ComputeDivisionCombatStats(div, &player->balanceModifiers)});
            }
        }

        std::sort(units.begin(), units.end(), [](const FieldUnit& a, const FieldUnit& b)
        {
            if (a.playerId != b.playerId) return a.playerId < b.playerId;
            if (a.tile.x != b.tile.x) return a.tile.x < b.tile.x;
            if (a.tile.y != b.tile.y) return a.tile.y < b.tile.y;
            return a.div->id < b.div->id;
        });

        // Buildings defeated this tick (sieged down or taken without a fight);
        // filled from Phase 1 on, captured after damage resolution.
        std::set<int> razedBuildings;
        std::map<int, Player*> buildingAttacker;  // positionId -> capturing player

        // Phase 1 — start engagements from active Attack orders.
        for (auto& U : units)
        {
            SoldierDivision* div = U.div;
            if (div->currentOrder != MilitaryOrderType::Attack || div->orderTargetPositionId < 0)
                continue;

            Vec2i targetTile = world.tilemap.GetCoordsFromId(div->orderTargetPositionId);
            Building* tb = world.tilemap.GetBuilding(div->orderTargetPositionId);
            if (tb != nullptr && tb->owner != U.player && IsMilitaryAttackTarget(*tb))
            {
                bool targetAdjacent = false;
                bool anyAdjacent = false;
                for (Building* b : SectorAdjacentEnemyBuildings(world, U.cell, U.player))
                {
                    anyAdjacent = true;
                    if (b == tb)
                        targetAdjacent = true;
                }
                if (targetAdjacent && FallsWithoutAFight(*tb))
                {
                    // No battle with an empty garrison — the undefended work is
                    // simply taken and the order is fulfilled.
                    razedBuildings.insert(tb->positionId);
                    buildingAttacker.emplace(tb->positionId, U.player);
                    div->currentOrder = MilitaryOrderType::None;
                    div->orderTargetPositionId = -1;
                }
                else if (anyAdjacent)
                    div->engaged = true;     // our quadrant borders the enemy structure
            }
            else
            {
                // Engage when our quadrant borders (or holds) the targeted enemy's
                // quadrant. If the target enemy is gone and no enemy building borders
                // us, the order is spent.
                Vec2i targetSector = SectorCellOf(targetTile);
                bool enemyThere = false;
                for (const auto& E : units)
                    if (E.playerId != U.playerId && SectorsFight(U.cell, E.cell) &&
                        SectorsFight(E.cell, targetSector))
                    { enemyThere = true; break; }
                if (enemyThere)
                    div->engaged = true;
                else if (!div->inTransit &&
                         SectorAdjacentEnemyBuilding(world, U.cell, U.player) == nullptr)
                {
                    // Order is spent only once the division has ARRIVED and finds
                    // nothing — a column still marching keeps its attack order.
                    div->currentOrder = MilitaryOrderType::None;  // target gone
                    div->orderTargetPositionId = -1;
                }
            }
        }

        // Phase 1b — physical contact is itself a battle: hostile divisions
        // standing in the SAME quadrant engage automatically. Walking onto the
        // enemy's province starts the fight, no explicit order needed. (Units in
        // merely adjacent quadrants still need an order or an ongoing battle to
        // engage — standing at the border does not auto-grind the front.)
        for (size_t i = 0; i < units.size(); i++)
            for (size_t j = i + 1; j < units.size(); j++)
                if (units[i].playerId != units[j].playerId &&
                    units[i].cell == units[j].cell)
                {
                    units[i].div->engaged = true;
                    units[j].div->engaged = true;
                }

        // Phase 2 — spread engagement to enemies in adjacent quadrants (battles drag
        // neighbouring provinces in).
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
                    if (SectorsFight(units[i].cell, units[j].cell))
                    {
                        units[j].div->engaged = true;
                        changed = true;
                    }
                }
            }
        }

        // Phase 2b — interception: a marching column that got engaged stops and
        // fights where its body is. It cannot roll through a defended province;
        // it settles on the tile it was caught on and the battle plays out there.
        for (auto& U : units)
        {
            SoldierDivision* div = U.div;
            if (!div->engaged || !div->inTransit)
                continue;
            div->travelPath.clear();
            div->travelStepDurations.clear();
            div->inTransit = false;
            div->occupiedTile = U.tile;
            div->sectorCell = U.cell;
        }

        // Phase 3 — resolve damage (simultaneous).
        std::map<SoldierDivision*, float> divLosses;
        std::map<int, float> buildingDamage;

        for (size_t i = 0; i < units.size(); i++)
        {
            for (size_t j = i + 1; j < units.size(); j++)
            {
                if (units[i].playerId == units[j].playerId) continue;
                if (!units[i].div->engaged || !units[j].div->engaged) continue;
                if (!SectorsFight(units[i].cell, units[j].cell)) continue;
                DivisionDuelResult duel = ResolveDivisionDuel(units[i].stats, units[j].stats, dt);
                divLosses[units[i].div] += duel.attackerStrengthLoss;
                divLosses[units[j].div] += duel.defenderStrengthLoss;
            }
        }

        for (const auto& U : units)
        {
            if (!U.div->engaged) continue;
            for (Building* b : SectorAdjacentEnemyBuildings(world, U.cell, U.player))
            {
                // Undefended works fall without a fight; only a manned garrison
                // (or the HQ) resists the siege and takes attrition damage.
                if (FallsWithoutAFight(*b))
                {
                    razedBuildings.insert(b->positionId);
                    buildingAttacker.emplace(b->positionId, U.player);
                    continue;
                }
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

        // Phase 5 — disengage units whose quadrant no longer borders a live enemy
        // division or an enemy building (battle ended).
        for (auto& U : units)
        {
            if (!U.div->engaged) continue;
            bool stillFighting = false;
            for (const auto& E : units)
                if (E.playerId != U.playerId && E.div->health > 0 &&
                    SectorsFight(U.cell, E.cell))
                { stillFighting = true; break; }
            if (!stillFighting)
                for (Building* b : SectorAdjacentEnemyBuildings(world, U.cell, U.player))
                    if (razedBuildings.find(b->positionId) == razedBuildings.end())
                    { stillFighting = true; break; }
            if (!stillFighting)
                U.div->engaged = false;
        }

        // Phase 6 — remove destroyed divisions from the owner's forces (they are
        // player-owned now, not building-owned).
        for (auto& [pid, player] : world.playerHandler.players)
        {
            if (player == nullptr) continue;
            bool removedAny = false;
            for (const auto& f : player->forces)
                if (f != nullptr && f->health <= 0)
                {
                    player->armyGroups.RemoveDivision(f->id);
                    removedAny = true;
                }
            auto& forces = player->forces;
            forces.erase(std::remove_if(forces.begin(), forces.end(),
                [](const std::unique_ptr<SoldierDivision>& d) { return d == nullptr || d->health <= 0; }), forces.end());
            if (removedAny)
                player->armyGroups.PruneEmptyArmies();
        }

        // Capture (flip ownership of) defeated military buildings — HQ is spared,
        // that's a game-over path. Done last so combat's unit pointers stay valid.
        for (int positionId : razedBuildings)
            CaptureBuilding(world, positionId, buildingAttacker.count(positionId) ? buildingAttacker[positionId] : nullptr);

        // Views held raw pointers into forces / referenced now-captured buildings —
        // rebuild them so garrison stats, GUI and next tick see the current state.
        for (auto& [pid, player] : world.playerHandler.players)
            if (player != nullptr)
                player->RebuildGarrisonViews();
    }

    // The frontline advances with the army: a deployed division claims the ground
    // it stands on for its owner, so pushing into enemy land flips those tiles
    // immediately (the border moves with the troops). Runs every tick after
    // combat; re-asserts over any building-driven RecalculateTerritory. Ground is
    // not vacated when troops leave — it stays yours until a building recompute or
    // an enemy re-occupies it. Deterministic (map + vector iteration order).
    void ClaimTilesUnderDivisions(GameWorld& world)
    {
        for (auto& [pid, player] : world.playerHandler.players)
        {
            if (player == nullptr) continue;
            for (const auto& fptr : player->forces)
            {
                const SoldierDivision& d = *fptr;
                // Physical tile: worldPos while marching, occupiedTile at rest.
                Vec2i tile{-1, -1};
                if (d.inTransit && d.worldPos.x >= 0.0f)
                    tile = {std::clamp(static_cast<int>(d.worldPos.x / TILE_SIZE), 0, world.tilemap.params.sizeX - 1),
                            std::clamp(static_cast<int>(d.worldPos.y / TILE_SIZE), 0, world.tilemap.params.sizeY - 1)};
                else if (d.occupiedTile.x >= 0)
                    tile = d.occupiedTile;
                else
                    continue;
                if (!world.tilemap.IsInside(tile)) continue;
                // Claim the WHOLE 2x2 quadrant the division stands in — territory
                // moves per-province, not per-tile. As the unit marches it passes
                // through each quadrant on its route (100 Hz → no skipping), so the
                // front advances smoothly and contiguously behind the army.
                Vec2i cell = SectorCellOf(tile);
                Vec2i anchor{cell.x * 2, cell.y * 2};
                for (int dy = 0; dy < 2; dy++)
                    for (int dx = 0; dx < 2; dx++)
                    {
                        Vec2i qt{anchor.x + dx, anchor.y + dy};
                        if (!world.tilemap.IsInside(qt)) continue;
                        Tile& t = world.tilemap.tilemap[world.tilemap.GetIdFromCoords(qt)];
                        if (t.owner != player.get())
                        {
                            t.owner = player.get();
                            world.tilemap.territoryDirty = true;
                        }
                    }
            }
        }
    }

    // Auto-close encirclements: any ground a player has surrounded (a pocket that
    // cannot reach the map edge without crossing that player's territory) flips to
    // them. A defeated enemy building inside is left for capture, but the pocket
    // around it becomes yours — so after you clear the enemy the hole disappears.
    // Flood from the border through non-P tiles; whatever isn't reached is enclosed.
    // Deterministic (map iteration + border-seed order). Throttled by the caller.
    void CloseEncirclements(GameWorld& world)
    {
        const int W = world.tilemap.params.sizeX;
        const int H = world.tilemap.params.sizeY;
        const int N = W * H;
        if (N <= 0 || static_cast<int>(world.tilemap.tilemap.size()) != N)
            return;

        for (auto& [pid, playerPtr] : world.playerHandler.players)
        {
            if (playerPtr == nullptr) continue;
            Player* P = playerPtr.get();

            std::vector<char> reached(N, 0);
            std::vector<int> stack;
            auto seed = [&](int x, int y)
            {
                int id = y * W + x;
                if (world.tilemap.tilemap[id].owner != P && !reached[id])
                { reached[id] = 1; stack.push_back(id); }
            };
            for (int x = 0; x < W; x++) { seed(x, 0); seed(x, H - 1); }
            for (int y = 0; y < H; y++) { seed(0, y); seed(W - 1, y); }

            const int dxs[4] = {1, -1, 0, 0};
            const int dys[4] = {0, 0, 1, -1};
            while (!stack.empty())
            {
                int id = stack.back(); stack.pop_back();
                int x = id % W, y = id / W;
                for (int k = 0; k < 4; k++)
                {
                    int nx = x + dxs[k], ny = y + dys[k];
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    int nid = ny * W + nx;
                    if (!reached[nid] && world.tilemap.tilemap[nid].owner != P)
                    { reached[nid] = 1; stack.push_back(nid); }
                }
            }

            for (int id = 0; id < N; id++)
            {
                Tile& t = world.tilemap.tilemap[id];
                if (reached[id] || t.owner == P) continue;
                if (t.HasBuilding()) continue;  // leave the building; capture handles it
                t.owner = P;
                world.tilemap.territoryDirty = true;
            }
        }
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
        }
    ProcessCommands();
    // Assign each player's builders to the front of their construction queue
    // before ticking buildings, so only funded builder slots progress this tick.
    for (auto& [id, player] : playerHandler.players)
        if (player != nullptr)
            player->construction.Refresh(*player);
    tilemap.UpdateBuildings(dt);
    RunFieldCombat(*this, dt);
    ClaimTilesUnderDivisions(*this);
    // Encirclement resolution is coarse in time — 5 Hz is plenty and keeps the
    // per-player flood-fill off the hot 100 Hz path.
    if (simulationTick % 20 == 0)
        CloseEncirclements(*this);
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
