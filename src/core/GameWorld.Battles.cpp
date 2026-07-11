#include "core/GameWorldInternal.h"
#include "warfare/DivisionSector.h"
#include "warfare/UnitStats.h"

#include <algorithm>
#include <cmath>

using namespace GameWorldInternal;

namespace
{
    // How long a division that lost a battle refuses to be dragged back into one
    // (Attack orders are ignored while this is > 0). See SoldierDivision::regroupTimer.
    constexpr float kPostBattleLockout = 15.0f;

    struct DivisionRef
    {
        SoldierDivision* div;
        Player* owner;
    };

    // Every division currently deployed on the map (garrisoned divisions have
    // occupiedTile == {-1,-1} and never participate in field battles).
    std::vector<DivisionRef> CollectDeployedDivisions(GameWorld& world)
    {
        std::vector<DivisionRef> all;
        for (auto& [pid, player] : world.GetPlayerHandler().players)
        {
            if (player == nullptr) continue;
            for (auto& fptr : player->forces)
                if (fptr != nullptr && fptr->occupiedTile.x >= 0 && fptr->strength > 0)
                    all.push_back({fptr.get(), player.get()});
        }
        return all;
    }

    DivisionRef* FindRef(std::vector<DivisionRef>& all, int divisionId)
    {
        for (auto& r : all)
            if (r.div->id == divisionId)
                return &r;
        return nullptr;
    }

    // Duel strength loss per tick is a small fraction (100 Hz sim vs. a ~30-60s
    // conclusion) — applying std::lround directly would truncate to 0 every
    // tick. Accumulate in the division's existing (previously-unused)
    // strengthBuffer, same pattern as DrainPool in UnitStats.cpp.
    void ApplyStrengthLoss(SoldierDivision& div, float loss)
    {
        if (loss <= 0.0f)
            return;
        div.strengthBuffer += loss;
        const int whole = static_cast<int>(div.strengthBuffer);
        if (whole > 0)
        {
            div.strength = std::max(0, div.strength - whole);
            div.strengthBuffer -= static_cast<float>(whole);
        }
    }

    Battle* FindBattleAt(std::map<int, Battle>& battles, Vec2i quadrant)
    {
        for (auto& [id, battle] : battles)
            if (battle.quadrant == quadrant && !battle.resolved)
                return &battle;
        return nullptr;
    }

    // A defeated division falls back ONE sector step (2x2 quadrant) from where it
    // stood, toward its home building, rather than teleporting the full distance
    // home in one go. Landing directly on `homeTile` never worked anyway: every
    // military building's footprint is >= a full 2x2 sector, so the sector
    // ResolveDivisionSector computes AT the building is always fully blocked by
    // the building itself — see the MoveDivisionTo fix in GarrisonComponent.cpp.
    // Stepping one quadrant back keeps the retreat local to the fight (the loser
    // clears the contested ground so the winner can claim it) and reads as "the
    // front pulls back one province", matching the intended HoI4-style flow.
    //
    // CARDINAL-ONLY step (never diagonal): stepping both axes at once would let a
    // unit slip diagonally past a wall of enemies that only covers the four
    // cardinal approaches, dodging IsQuadrantEncircled below instead of being
    // caught by it. Picks whichever axis is currently farther from home.
    Vec2i RetreatTargetTile(const TileMap& tilemap, Vec2i fromTile, Vec2i homeTile)
    {
        Vec2i fromCell = SectorCellOf(fromTile);
        Vec2i homeCell = SectorCellOf(homeTile);
        int dx = homeCell.x - fromCell.x;
        int dy = homeCell.y - fromCell.y;
        Vec2i step{0, 0};
        if (std::abs(dx) >= std::abs(dy) && dx != 0)
            step.x = dx > 0 ? 1 : -1;
        else if (dy != 0)
            step.y = dy > 0 ? 1 : -1;
        Vec2i targetCell{fromCell.x + step.x, fromCell.y + step.y};
        return Vec2i{std::clamp(targetCell.x * 2, 0, tilemap.params.sizeX - 1),
                     std::clamp(targetCell.y * 2, 0, tilemap.params.sizeY - 1)};
    }

    // True when every one of the four CARDINAL-adjacent quadrant cells (N/S/E/W —
    // diagonals deliberately excluded, matching RetreatTargetTile's cardinal-only
    // step) around `cell` holds at least one enemy (at-war) division. A division
    // boxed in on all four cardinal sides has no route home — the HoI4 "kocioł" —
    // regardless of how much friendly territory a diagonal neighbour might have.
    bool IsQuadrantEncircled(const std::vector<DivisionRef>& all, Vec2i cell, const Player& owner)
    {
        const Vec2i cardinal[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (Vec2i d : cardinal)
        {
            Vec2i neighborCell{cell.x + d.x, cell.y + d.y};
            bool enemyPresent = false;
            for (const auto& ref : all)
            {
                if (ref.owner == &owner || !owner.diplomatic.IsAtWar(ref.owner->id))
                    continue;
                if (SectorCellOf(ref.div->occupiedTile) == neighborCell)
                {
                    enemyPresent = true;
                    break;
                }
            }
            if (!enemyPresent)
                return false;
        }
        return true;
    }

    // Sends a division falling back toward the building it calls home (its
    // pre-battle rear area) and puts it on a temporary Attack-order lockout.
    // Used both for a defeated attacker (blocked in place / falls back) and a
    // defeated defender (forced retreat deeper into own territory). If the
    // division is fully encircled (IsQuadrantEncircled), there is no retreat to
    // attempt — it is destroyed outright instead of looping "retreat" onto the
    // same tile forever (see RetreatTargetTile's earlier failure mode: a cornered
    // division that can never find a better candidate than the tile it already
    // stands on, because IsTileFree excludes the mover itself from the check).
    void DisengageDivision(GameWorld& world, DivisionRef& ref, const std::vector<DivisionRef>& all)
    {
        SoldierDivision& div = *ref.div;
        div.engaged = false;
        div.retreating = true;
        div.regroupTimer = std::max(div.regroupTimer, kPostBattleLockout);
        div.currentOrder = MilitaryOrderType::None;
        div.orderTargetPositionId = -1;

        if (div.occupiedTile.x >= 0 &&
            IsQuadrantEncircled(all, SectorCellOf(div.occupiedTile), *ref.owner))
        {
            div.strength = 0;
            SyncDerivedHealth(div);
            return;
        }

        if (div.garrisonBuildingId < 0)
            return;
        Building* home = world.GetTileMap().GetBuilding(div.garrisonBuildingId);
        if (home == nullptr || home->owner != ref.owner)
            return;
        auto* garrison = home->GetComponent<GarrisonComponent>();
        if (garrison == nullptr)
            return;
        if (div.occupiedTile.x < 0)
            return;
        Vec2i homeTile = world.GetTileMap().GetCoordsFromId(home->positionId);
        Vec2i retreatTile = RetreatTargetTile(world.GetTileMap(), div.occupiedTile, homeTile);

        // Fall back one quadrant toward home if that ground is still ours.
        if (garrison->MoveDivisionTo(div.id, retreatTile, *home,
                                     /*requireOwnedTerritory=*/true, /*snapToSector=*/true))
            return;
        // The one-step-back cell has no reachable owned tile — the front just
        // collapsed around this division (exactly the "lost the quadrant over the
        // guard tower" case). Retreat all the way home instead; home territory is
        // owned by definition, so this path normally exists. Without this fallback
        // MoveDivisionTo simply returned false and the loser froze in place.
        if (garrison->MoveDivisionTo(div.id, homeTile, *home,
                                     /*requireOwnedTerritory=*/true, /*snapToSector=*/true))
            return;
        // Cut off from home even through friendly ground: allow the retreat to
        // cross any walkable tile rather than stand still and get mopped up frozen.
        garrison->MoveDivisionTo(div.id, homeTile, *home,
                                 /*requireOwnedTerritory=*/false, /*snapToSector=*/true);
    }

    // Sends a victorious division to physically occupy the quadrant it just won.
    // Engagement only requires the attacker be ADJACENT to its target (dx,dy<=1 —
    // see the engagement pass above), so a division that fought from a
    // neighbouring quadrant would otherwise just stand down without ever
    // crossing onto the ground it captured, leaving ClaimTilesUnderDivisions
    // (GameWorld.Render.cpp) nothing to claim it with. Harmless no-op-ish when
    // the division was already standing in the contested quadrant.
    void AdvanceIntoQuadrant(GameWorld& world, DivisionRef& ref, Vec2i quadrantCell)
    {
        SoldierDivision& div = *ref.div;
        if (div.garrisonBuildingId < 0)
            return;
        Building* home = world.GetTileMap().GetBuilding(div.garrisonBuildingId);
        if (home == nullptr || home->owner != ref.owner)
            return;
        auto* garrison = home->GetComponent<GarrisonComponent>();
        if (garrison == nullptr)
            return;
        Vec2i targetTile{quadrantCell.x * 2, quadrantCell.y * 2};
        // requireOwnedTerritory=false: this ground is still enemy/contested until
        // the move lands and ClaimTilesUnderDivisions picks it up on a later
        // tick — that IS the point of advancing into it.
        garrison->MoveDivisionTo(div.id, targetTile, *home, /*requireOwnedTerritory=*/false, /*snapToSector=*/true);
    }

    void RemoveBattleFromPlayer(Player& player, int battleId)
    {
        auto& ids = player.activeBattleIds;
        ids.erase(std::remove(ids.begin(), ids.end(), battleId), ids.end());
    }

    void TrackBattleForPlayer(Player& player, int battleId)
    {
        auto& ids = player.activeBattleIds;
        if (std::find(ids.begin(), ids.end(), battleId) == ids.end())
            ids.push_back(battleId);
    }
}

// ETAP 11.2 — Battle lifecycle: creation/joining, per-tick aggregated
// resolution, and end-of-battle consequences. See docs/grand_refactor_plan.md
// ETAP 11 and inc/warfare/Battle.h for the design.
void GameWorld::UpdateBattles(double dt)
{
    std::vector<DivisionRef> all = CollectDeployedDivisions(*this);

    // Tick down the post-battle lockout so a defeated division can eventually
    // be re-ordered to attack again.
    for (auto& ref : all)
        if (ref.div->regroupTimer > 0.0f)
            ref.div->regroupTimer = std::max(0.0f, ref.div->regroupTimer - static_cast<float>(dt));

    // ── 1) Engagement: a division carrying a live Attack order that has
    // reached (or stands adjacent to) its target tile joins/creates the Battle
    // for that tile's quadrant, alongside every enemy division already holding
    // that quadrant. ──────────────────────────────────────────────────────────
    for (auto& ref : all)
    {
        SoldierDivision& div = *ref.div;
        if (div.currentOrder != MilitaryOrderType::Attack || div.orderTargetPositionId < 0)
            continue;
        if (div.regroupTimer > 0.0f)
            continue;
        if (!tilemap.IsInside(tilemap.GetCoordsFromId(div.orderTargetPositionId)))
            continue;

        Vec2i targetTile = tilemap.GetCoordsFromId(div.orderTargetPositionId);
        const int dx = std::abs(div.occupiedTile.x - targetTile.x);
        const int dy = std::abs(div.occupiedTile.y - targetTile.y);
        if (dx > 1 || dy > 1)
            continue;  // not there yet — still marching

        Vec2i quadrant = SectorCellOf(targetTile);

        std::vector<DivisionRef*> defenders;
        for (auto& other : all)
        {
            if (other.owner == ref.owner)
                continue;
            if (!ref.owner->diplomatic.IsAtWar(other.owner->id))
                continue;
            if (SectorCellOf(other.div->occupiedTile) != quadrant)
                continue;
            defenders.push_back(&other);
        }
        if (defenders.empty())
            continue;  // nothing to fight here (e.g. an empty/building-only target)

        Battle* battle = FindBattleAt(battles, quadrant);
        if (battle == nullptr)
        {
            Battle created(nextBattleId++, quadrant);
            battle = &battles.emplace(created.id, created).first->second;
        }

        battle->AddToSide(Battle::Side::Attacker, div.id, ref.owner->id);
        div.engaged = true;
        div.retreating = false;
        TrackBattleForPlayer(*ref.owner, battle->id);

        for (DivisionRef* defRef : defenders)
        {
            battle->AddToSide(Battle::Side::Defender, defRef->div->id, defRef->owner->id);
            defRef->div->engaged = true;
            defRef->div->retreating = false;
            TrackBattleForPlayer(*defRef->owner, battle->id);
        }
    }

    // ── 2) Resolution: one tick of aggregated duels per active battle. ───────
    std::vector<int> toRemove;
    for (auto& [battleId, battle] : battles)
    {
        if (battle.resolved)
        {
            toRemove.push_back(battleId);
            continue;
        }
        battle.elapsedTime += dt;

        auto resolveSide = [&](std::vector<int>& ids)
        {
            std::vector<DivisionRef*> found;
            ids.erase(std::remove_if(ids.begin(), ids.end(), [&](int id)
            {
                DivisionRef* r = FindRef(all, id);
                if (r == nullptr)
                    return true;  // died / undeployed since last tick — drop silently
                found.push_back(r);
                return false;
            }), ids.end());
            return found;
        };

        std::vector<DivisionRef*> attackers = resolveSide(battle.attackerDivisionIds);
        std::vector<DivisionRef*> defenders = resolveSide(battle.defenderDivisionIds);

        if (attackers.empty() || defenders.empty())
        {
            // One side evaporated (all dead/left) without a formal duel result.
            for (DivisionRef* r : attackers) { r->div->engaged = false; RemoveBattleFromPlayer(*r->owner, battleId); }
            for (DivisionRef* r : defenders) { r->div->engaged = false; RemoveBattleFromPlayer(*r->owner, battleId); }
            battle.resolved = true;
            toRemove.push_back(battleId);
            continue;
        }

        // Round-robin pairing so every division on the smaller side gets
        // ganged up on by (and returns fire against) each division that maps to
        // it on the larger side — ResolveDivisionDuel already computes damage
        // in both directions for one pairing.
        const std::size_t pairCount = std::max(attackers.size(), defenders.size());
        for (std::size_t i = 0; i < pairCount; i++)
        {
            DivisionRef* atk = attackers[i % attackers.size()];
            DivisionRef* def = defenders[i % defenders.size()];

            DivisionCombatStats atkStats = ComputeDivisionCombatStats(*atk->div, &atk->owner->balanceModifiers);
            DivisionCombatStats defStats = ComputeDivisionCombatStats(*def->div, &def->owner->balanceModifiers);
            DivisionDuelResult result = ResolveDivisionDuel(atkStats, defStats, dt, simulationTick,
                                                            atk->div->id, def->div->id);

            ApplyStrengthLoss(*atk->div, result.attackerStrengthLoss);
            ApplyStrengthLoss(*def->div, result.defenderStrengthLoss);
            atk->div->cohesion = std::max(0.0f, atk->div->cohesion - result.attackerCohesionLoss);
            def->div->cohesion = std::max(0.0f, def->div->cohesion - result.defenderCohesionLoss);
        }

        float attackerCohesion = 0.0f;
        for (DivisionRef* r : attackers) attackerCohesion += r->div->cohesion;
        float defenderCohesion = 0.0f;
        for (DivisionRef* r : defenders) defenderCohesion += r->div->cohesion;
        battle.attackerCohesion = static_cast<int>(std::lround(attackerCohesion));
        battle.defenderCohesion = static_cast<int>(std::lround(defenderCohesion));

        // ── 3) End conditions ────────────────────────────────────────────────
        if (battle.defenderCohesion <= 0)
        {
            // Attacker wins: defender falls back to rebuild; attacker advances
            // onto the contested quadrant so it actually holds the ground it won.
            for (DivisionRef* r : defenders) { DisengageDivision(*this, *r, all); RemoveBattleFromPlayer(*r->owner, battleId); }
            for (DivisionRef* r : attackers)
            {
                r->div->engaged = false;
                r->div->currentOrder = MilitaryOrderType::None;
                r->div->orderTargetPositionId = -1;
                AdvanceIntoQuadrant(*this, *r, battle.quadrant);
                RemoveBattleFromPlayer(*r->owner, battleId);
            }
            battle.resolved = true;
            toRemove.push_back(battleId);
        }
        else if (battle.attackerCohesion <= 0)
        {
            // Defender wins: attacker is thrown back and locked out; defender
            // stays put, disengaged.
            for (DivisionRef* r : attackers) { DisengageDivision(*this, *r, all); RemoveBattleFromPlayer(*r->owner, battleId); }
            for (DivisionRef* r : defenders)
            {
                r->div->engaged = false;
                RemoveBattleFromPlayer(*r->owner, battleId);
            }
            battle.resolved = true;
            toRemove.push_back(battleId);
        }
    }

    for (int battleId : toRemove)
        battles.erase(battleId);
}
