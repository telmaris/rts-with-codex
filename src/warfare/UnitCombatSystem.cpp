#include "warfare/UnitCombatSystem.h"
#include "warfare/UnitMarchSystem.h"
#include "warfare/CombatPipeline.h"
#include "warfare/UnitDefinition.h"
#include "core/GameWorld.h"
#include "simulation/MilitaryRoadNetwork.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace
{
    // Units engage once within this many tile-units of each other — comfortably
    // larger than one tick's worth of movement (moveSpeed ~1 tile/s at 100 Hz
    // is ~0.01 tile/tick) so contact is never missed mid-approach.
    constexpr float kMeleeContactRange = 0.6f;

    // Finds the unit closest to arrival (highest tileIndex, then highest
    // tileProgress, then lowest instanceId) among units marching or fighting
    // in the given direction — the one "front fights front" combat always
    // resolves against, recomputed fresh rather than stored as a target so a
    // dead unit's slot is instantly backfilled by whoever's now closest.
    int FindFrontMostUnit(const std::map<int, BattleUnit>& deployedUnits, int fromPlayerId, int toPlayerId)
    {
        int bestId = -1;
        int bestTileIndex = -1;
        double bestProgress = -1.0;
        for (const auto& [id, unit] : deployedUnits)
        {
            if (unit.routeFromPlayerId != fromPlayerId || unit.routeToPlayerId != toPlayerId)
                continue;
            if (unit.state != BattleUnitState::Marching && unit.state != BattleUnitState::FightingUnit)
                continue;
            if (unit.tileIndex < 0 || unit.currentHp <= 0.0)
                continue; // dead but not yet removed this pass — never a valid target or actor

            bool better = unit.tileIndex > bestTileIndex ||
                          (unit.tileIndex == bestTileIndex && unit.tileProgress > bestProgress) ||
                          (unit.tileIndex == bestTileIndex && unit.tileProgress == bestProgress && (bestId == -1 || id < bestId));
            if (better)
            {
                bestId = id;
                bestTileIndex = unit.tileIndex;
                bestProgress = unit.tileProgress;
            }
        }
        return bestId;
    }

    double DistanceBetween(Vec2f a, Vec2f b)
    {
        double dx = a.x - b.x;
        double dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    // TD(etap-6.2) mode-conflict fallback: besiegers parked in AttackingHq
    // are deliberately excluded from FindFrontMostUnit's normal front-vs-front
    // road-combat pool (a fresh column must never be able to yank one of them
    // back into FightingUnit and interrupt its siege via Pass 3). But a
    // defender that Pass 4 already locked onto one still needs to find it
    // again each tick via this dedicated lookup — same direction-scoping as
    // FindFrontMostUnit, tie-broken by lowest instanceId (a simpler,
    // equally-deterministic stand-in for the plan's "last besieging unit"
    // proposal; the exact identity has no user-facing meaning yet).
    int FindBesiegerOpponent(const std::map<int, BattleUnit>& deployedUnits, int fromPlayerId, int toPlayerId)
    {
        int bestId = -1;
        for (const auto& [id, unit] : deployedUnits)
        {
            if (unit.routeFromPlayerId != fromPlayerId || unit.routeToPlayerId != toPlayerId)
                continue;
            if (unit.state != BattleUnitState::AttackingHq || unit.currentHp <= 0.0)
                continue;
            if (bestId == -1 || id < bestId)
                bestId = id;
        }
        return bestId;
    }

    void LockIntoMelee(BattleUnit& unit, Player* owner)
    {
        unit.state = BattleUnitState::FightingUnit;
        double attackSpeed = owner != nullptr ? unit.GetEffectiveAttackSpeed(*owner) : 1.0;
        unit.attackTimer = attackSpeed > 0.0 ? 1.0 / attackSpeed : 1.0;
    }
}

void UnitCombatSystem::Update(GameWorld& world, double dt)
{
    auto& deployedUnits = world.GetDeployedUnits();
    PlayerHandler& playerHandler = world.GetPlayerHandler();
    unsigned int worldSeed = world.GetTileMap().params.seed;
    std::uint64_t tick = world.GetSimulationTick();

    // Pass 1: advance attack timers for units already locked in melee, in a
    // deterministic ascending-instanceId order, so a lethal hit landed
    // earlier in this same pass reliably stops the now-dead unit from also
    // attacking later in the pass — no zombie hits (the exact bug class the
    // old war system had).
    std::vector<int> fightingIds;
    for (const auto& [id, unit] : deployedUnits)
        if (unit.state == BattleUnitState::FightingUnit)
            fightingIds.push_back(id);
    std::sort(fightingIds.begin(), fightingIds.end());

    for (int id : fightingIds)
    {
        auto selfIt = deployedUnits.find(id);
        if (selfIt == deployedUnits.end() || selfIt->second.state != BattleUnitState::FightingUnit ||
            selfIt->second.currentHp <= 0.0)
            continue; // died earlier in this same pass — never gets a retaliation hit
        BattleUnit& self = selfIt->second;

        self.attackTimer -= dt;
        if (self.attackTimer > 0.0)
            continue;

        int opponentId = FindFrontMostUnit(deployedUnits, self.routeToPlayerId, self.routeFromPlayerId);
        if (opponentId == -1)
            opponentId = FindBesiegerOpponent(deployedUnits, self.routeToPlayerId, self.routeFromPlayerId);
        if (opponentId == -1)
        {
            self.state = BattleUnitState::Marching; // opponent gone; re-approach/re-engage later
            continue;
        }

        BattleUnit& opponent = deployedUnits.at(opponentId);
        Vec2f selfPos = UnitMarchSystem::ComputeWorldPosition(world, self);
        Vec2f opponentPos = UnitMarchSystem::ComputeWorldPosition(world, opponent);
        if (DistanceBetween(selfPos, opponentPos) > kMeleeContactRange)
        {
            self.state = BattleUnitState::Marching; // opponent stepped out of range
            continue;
        }

        auto ownerIt = playerHandler.players.find(self.ownerPlayerId);
        auto opponentOwnerIt = playerHandler.players.find(opponent.ownerPlayerId);
        if (ownerIt == playerHandler.players.end() || ownerIt->second == nullptr ||
            opponentOwnerIt == playerHandler.players.end() || opponentOwnerIt->second == nullptr)
            continue;

        double attackSpeed = self.GetEffectiveAttackSpeed(*ownerIt->second);
        self.attackTimer += attackSpeed > 0.0 ? 1.0 / attackSpeed : 1.0;

        double damage = self.GetEffectiveRoadAttack(*ownerIt->second);
        double armor = opponent.GetEffectiveArmor(*opponentOwnerIt->second);
        const UnitDefinition* opponentDef = FindUnitDefinition(opponent.unitDefId);
        static const std::map<DamageType, float> noResistances;
        const auto& resistances = opponentDef != nullptr ? opponentDef->resistances : noResistances;

        double resolved = CombatResolver::ResolveDamage(damage, armor, DamageType::Physical, resistances,
                                                         worldSeed, tick, self.instanceId);
        double applied = std::min(resolved, std::max(0.0, opponent.currentHp));
        bool lethal = opponent.currentHp - resolved <= 0.0;
        world.GetCombatTelemetry().RecordUnitDamage(opponent.instanceId, opponent.ownerPlayerId,
                                                     opponent.unitDefId, CombatDamageSource::Unit,
                                                     applied, lethal);
        opponent.currentHp -= resolved;
    }

    // Pass 2: remove the dead immediately (TD etap-5 v1 simplification — no
    // lingering Dying+fade window since there's no sprite to fade yet; see
    // the class comment). A removed unit's surviving opponent (if any) is
    // freed back to Marching so Pass 3 (or a later tick) can re-engage
    // whoever's now front-most.
    std::vector<int> deadIds;
    for (const auto& [id, unit] : deployedUnits)
        if (unit.currentHp <= 0.0)
            deadIds.push_back(id);

    for (int deadId : deadIds)
    {
        BattleUnit dead = deployedUnits.at(deadId); // copy before erasing
        deployedUnits.erase(deadId);

        int survivorId = FindFrontMostUnit(deployedUnits, dead.routeToPlayerId, dead.routeFromPlayerId);
        if (survivorId != -1)
        {
            BattleUnit& survivor = deployedUnits.at(survivorId);
            if (survivor.state == BattleUnitState::FightingUnit)
                survivor.state = BattleUnitState::Marching;
        }
    }

    // Pass 3: lock any newly-adjacent pair of front-most units into melee.
    // Freshly engaged pairs get a full attack cooldown rather than fighting
    // immediately — no "free" alpha strike on first contact.
    for (const auto& route : world.GetMilitaryRoads().GetRoutes())
    {
        int frontAB = FindFrontMostUnit(deployedUnits, route.playerA, route.playerB);
        int frontBA = FindFrontMostUnit(deployedUnits, route.playerB, route.playerA);
        if (frontAB == -1 || frontBA == -1)
            continue;

        BattleUnit& unitAB = deployedUnits.at(frontAB);
        BattleUnit& unitBA = deployedUnits.at(frontBA);
        if (unitAB.state == BattleUnitState::FightingUnit && unitBA.state == BattleUnitState::FightingUnit)
            continue; // already engaged with each other

        Vec2f posAB = UnitMarchSystem::ComputeWorldPosition(world, unitAB);
        Vec2f posBA = UnitMarchSystem::ComputeWorldPosition(world, unitBA);
        if (DistanceBetween(posAB, posBA) > kMeleeContactRange)
            continue;

        auto ownerAB = playerHandler.players.find(unitAB.ownerPlayerId);
        auto ownerBA = playerHandler.players.find(unitBA.ownerPlayerId);
        LockIntoMelee(unitAB, ownerAB != playerHandler.players.end() ? ownerAB->second.get() : nullptr);
        LockIntoMelee(unitBA, ownerBA != playerHandler.players.end() ? ownerBA->second.get() : nullptr);
    }

    // Pass 4 (TD etap-6.2, mode-conflict proposal): a fresh defender reaching
    // its own gate may find enemy besiegers already parked there. Besiegers
    // never interrupt their siege on the HQ (their own state and attack
    // timer are untouched here — HqCombatSystem keeps ticking them down
    // independently); only the defender locks into FightingUnit and starts
    // trading ordinary road-combat blows with one of them (resolved by Pass 1
    // next tick via FindBesiegerOpponent above).
    for (const auto& route : world.GetMilitaryRoads().GetRoutes())
    {
        for (auto [besiegedId, besiegerHomeId] : {std::pair{route.playerA, route.playerB},
                                                   std::pair{route.playerB, route.playerA}})
        {
            int besiegerId = FindBesiegerOpponent(deployedUnits, besiegerHomeId, besiegedId);
            if (besiegerId == -1)
                continue;

            int defenderId = -1;
            Vec2f besiegerPos = UnitMarchSystem::ComputeWorldPosition(world, deployedUnits.at(besiegerId));
            for (const auto& [id, unit] : deployedUnits)
            {
                if (unit.routeFromPlayerId != besiegedId || unit.routeToPlayerId != besiegerHomeId)
                    continue;
                if (unit.state != BattleUnitState::Marching || unit.tileIndex < 0)
                    continue;
                if (DistanceBetween(UnitMarchSystem::ComputeWorldPosition(world, unit), besiegerPos) > kMeleeContactRange)
                    continue;
                if (defenderId == -1 || id < defenderId)
                    defenderId = id;
            }
            if (defenderId == -1)
                continue;

            auto defenderOwner = playerHandler.players.find(deployedUnits.at(defenderId).ownerPlayerId);
            LockIntoMelee(deployedUnits.at(defenderId),
                          defenderOwner != playerHandler.players.end() ? defenderOwner->second.get() : nullptr);
        }
    }
}
