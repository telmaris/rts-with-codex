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

    // Sends a division back toward the building it calls home (its pre-battle
    // rear area) and puts it on a temporary Attack-order lockout. Used both for
    // a defeated attacker (blocked in place / falls back) and a defeated
    // defender (forced retreat deeper into own territory) — see ETAP 11 plan,
    // which asks for a "retreat toward the rear" pathing filter that doesn't
    // exist yet; routing home through the existing garrison mover is the
    // pragmatic stand-in until PathingService grows one.
    void DisengageDivision(GameWorld& world, DivisionRef& ref)
    {
        SoldierDivision& div = *ref.div;
        div.engaged = false;
        div.retreating = true;
        div.regroupTimer = std::max(div.regroupTimer, kPostBattleLockout);
        div.currentOrder = MilitaryOrderType::None;
        div.orderTargetPositionId = -1;

        if (div.garrisonBuildingId < 0)
            return;
        Building* home = world.GetTileMap().GetBuilding(div.garrisonBuildingId);
        if (home == nullptr || home->owner != ref.owner)
            return;
        auto* garrison = home->GetComponent<GarrisonComponent>();
        if (garrison == nullptr)
            return;
        Vec2i homeTile = world.GetTileMap().GetCoordsFromId(home->positionId);
        garrison->MoveDivisionTo(div.id, homeTile, *home, /*requireOwnedTerritory=*/true, /*snapToSector=*/true);
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
            // Attacker wins: defender falls back to rebuild; attacker holds the
            // quadrant (no further action needed — it already stands there).
            for (DivisionRef* r : defenders) { DisengageDivision(*this, *r); RemoveBattleFromPlayer(*r->owner, battleId); }
            for (DivisionRef* r : attackers)
            {
                r->div->engaged = false;
                r->div->currentOrder = MilitaryOrderType::None;
                r->div->orderTargetPositionId = -1;
                RemoveBattleFromPlayer(*r->owner, battleId);
            }
            battle.resolved = true;
            toRemove.push_back(battleId);
        }
        else if (battle.attackerCohesion <= 0)
        {
            // Defender wins: attacker is thrown back and locked out; defender
            // stays put, disengaged.
            for (DivisionRef* r : attackers) { DisengageDivision(*this, *r); RemoveBattleFromPlayer(*r->owner, battleId); }
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
