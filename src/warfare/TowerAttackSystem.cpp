#include "warfare/TowerAttackSystem.h"
#include "warfare/CombatPipeline.h"
#include "warfare/UnitMarchSystem.h"
#include "warfare/UnitDefinition.h"
#include "core/GameWorld.h"
#include "simulation/MapGenerator.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace
{
    // Fixed implementation constants (not data-driven — no gameplay reason
    // yet to vary these per tower tier, unlike damage/range/attackSpeed).
    constexpr double kProjectileSpeedTilesPerSec = 12.0;
    constexpr float kProjectileHitRadius = 0.5f; // tile-units
    constexpr int kProjectileTimeoutTicks = 500; // ~5s at 100Hz — safety net if the target vanishes mid-flight

    double DistanceBetween(Vec2f a, Vec2f b)
    {
        double dx = a.x - b.x;
        double dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    // Same "closest to reaching its own march destination" priority as
    // UnitCombatSystem's road-combat FindFrontMostUnit, scoped to enemies of
    // `towerOwnerId` within `rangeTiles` of `towerCenter`. ComputeWorldPosition/
    // ComputeBuildingCenter return pixel-space positions (scaled by
    // TILE_SIZE), so rangeTiles is converted explicitly before comparing.
    int FindBestTarget(GameWorld& world, int towerOwnerId, double rangeTiles, Vec2f towerCenter,
                       TowerTargetMode targetMode)
    {
        double rangePixels = rangeTiles * TILE_SIZE;
        int bestId = -1;
        int bestTileIndex = -1;
        double bestProgress = -1.0;
        double bestStrength = -1.0;
        for (const auto& [id, unit] : world.GetDeployedUnits())
        {
            if (unit.ownerPlayerId == towerOwnerId || unit.tileIndex < 0 || unit.currentHp <= 0.0)
                continue;

            Vec2f pos = UnitMarchSystem::ComputeWorldPosition(world, unit);
            if (DistanceBetween(towerCenter, pos) > rangePixels)
                continue;

            bool better = false;
            if (targetMode == TowerTargetMode::StrongestUnit)
            {
                auto ownerIt = world.GetPlayerHandler().players.find(unit.ownerPlayerId);
                if (ownerIt == world.GetPlayerHandler().players.end() || ownerIt->second == nullptr)
                    continue;
                const Player& owner = *ownerIt->second;
                double strength = unit.GetEffectiveMaxHp(owner) +
                                  unit.GetEffectiveRoadAttack(owner) * 3.0 +
                                  unit.GetEffectiveSiegeAttack(owner) * 2.0 +
                                  unit.GetEffectiveArmor(owner) * 2.0;
                better = strength > bestStrength || (strength == bestStrength && (bestId == -1 || id < bestId));
                if (better)
                    bestStrength = strength;
            }
            else
            {
                better = unit.tileIndex > bestTileIndex ||
                         (unit.tileIndex == bestTileIndex && unit.tileProgress > bestProgress) ||
                         (unit.tileIndex == bestTileIndex && unit.tileProgress == bestProgress &&
                          (bestId == -1 || id < bestId));
            }
            if (better)
            {
                bestId = id;
                bestTileIndex = unit.tileIndex;
                bestProgress = unit.tileProgress;
            }
        }
        return bestId;
    }
}

void TowerAttackSystem::Update(GameWorld& world, double dt)
{
    auto& deployedUnits = world.GetDeployedUnits();
    auto& projectiles = world.GetProjectiles();
    PlayerHandler& playerHandler = world.GetPlayerHandler();
    const TileMap& tilemap = world.GetTileMap();
    unsigned int worldSeed = tilemap.params.seed;
    std::uint64_t tick = world.GetSimulationTick();
    static const std::map<DamageType, float> noResistances;

    // --- Advance in-flight projectiles (deterministic ascending-id order) ---
    std::vector<int> projectileIds;
    for (const auto& [id, projectile] : projectiles)
        projectileIds.push_back(id);
    std::sort(projectileIds.begin(), projectileIds.end());

    for (int id : projectileIds)
    {
        auto it = projectiles.find(id);
        if (it == projectiles.end())
            continue;
        AttackEmission& projectile = it->second;

        auto targetIt = deployedUnits.find(projectile.targetUnitInstanceId);
        projectile.ticksRemaining--;
        if (targetIt == deployedUnits.end() || targetIt->second.currentHp <= 0.0 || projectile.ticksRemaining <= 0)
        {
            projectiles.erase(it);
            continue;
        }

        BattleUnit& target = targetIt->second;
        Vec2f targetPos = UnitMarchSystem::ComputeWorldPosition(world, target);
        double dx = targetPos.x - projectile.position.x;
        double dy = targetPos.y - projectile.position.y;
        double dist = std::sqrt(dx * dx + dy * dy);

        const UnitDefinition* targetDef = FindUnitDefinition(target.unitDefId);
        float targetRadius = targetDef != nullptr ? static_cast<float>(targetDef->colliderRadius) : 0.4f;
        double hitRadiusPixels = (kProjectileHitRadius + targetRadius) * TILE_SIZE;

        if (dist <= hitRadiusPixels)
        {
            auto targetOwnerIt = playerHandler.players.find(target.ownerPlayerId);
            if (targetOwnerIt != playerHandler.players.end() && targetOwnerIt->second != nullptr)
            {
                double armor = target.GetEffectiveArmor(*targetOwnerIt->second);
                const auto& resistances = targetDef != nullptr ? targetDef->resistances : noResistances;
                double resolved = CombatResolver::ResolveDamage(projectile.damage, armor, projectile.damageType,
                                                                 resistances, worldSeed, tick,
                                                                 projectile.sourceUnitInstanceId);
                double applied = std::min(resolved, std::max(0.0, target.currentHp));
                bool lethal = target.currentHp - resolved <= 0.0;
                world.GetCombatTelemetry().RecordUnitDamage(target.instanceId, target.ownerPlayerId,
                                                             target.unitDefId, CombatDamageSource::Tower,
                                                             applied, lethal);
                target.currentHp -= resolved;
            }
            projectiles.erase(it);
            continue;
        }

        // Homing: re-aim toward the target's current position every tick — a
        // deliberate simplification over ballistic lead-shooting (see
        // docs/tech_debt.md); still deterministically catches a moving
        // target since its motion is itself fully deterministic.
        double moveDistance = projectile.speed * TILE_SIZE * dt;
        if (dist > 0.0)
        {
            double t = std::min(1.0, moveDistance / dist);
            projectile.position.x += static_cast<float>(dx * t);
            projectile.position.y += static_cast<float>(dy * t);
        }
    }

    // Units a projectile just killed are removed immediately (TD etap-5 v1
    // simplification — no lingering Dying+fade window — still in effect).
    std::vector<int> deadIds;
    for (const auto& [id, unit] : deployedUnits)
        if (unit.currentHp <= 0.0)
            deadIds.push_back(id);
    for (int deadId : deadIds)
        deployedUnits.erase(deadId);

    // --- Towers: fire at the best in-range target ---
    for (auto& [playerId, playerPtr] : playerHandler.players)
    {
        if (playerPtr == nullptr || playerPtr->defeated)
            continue;

        // Determinism audit (docs/work_plan_2026-07-13.md, pre-Block-C): two
        // towers of the same player can have overlapping range and target
        // the same unit in one tick — whichever fires first can kill it
        // before the other's FindBestTarget runs, changing that tower's own
        // pick. Iteration order must not depend on Building* heap addresses.
        std::vector<Building*> sortedTowers(playerPtr->GetTrackedBuildingsWithComponent<TowerCombatComponent>().begin(),
                                             playerPtr->GetTrackedBuildingsWithComponent<TowerCombatComponent>().end());
        std::sort(sortedTowers.begin(), sortedTowers.end(), [](Building* a, Building* b) { return a->id < b->id; });

        for (Building* towerBuilding : sortedTowers)
        {
            auto* combat = towerBuilding->GetComponent<TowerCombatComponent>();
            auto* storage = towerBuilding->GetComponent<StorageComponent>();
            if (combat == nullptr || storage == nullptr)
                continue;

            combat->attackTimer -= dt;
            if (combat->attackTimer > 0.0)
                continue;

            int ammoPerShot = combat->GetModifiedAmmoPerShot(*towerBuilding);
            auto bufferIt = storage->buffers.find(combat->ammoResource);
            if (bufferIt == storage->buffers.end() ||
                static_cast<int>(bufferIt->second.buffer.size()) < ammoPerShot)
                continue; // no ammo — doesn't fire; timer stays expired until resupplied

            double range = combat->GetModifiedRange(*towerBuilding);
            Vec2f towerCenter = ComputeBuildingCenter(tilemap, *towerBuilding);
            int targetId = FindBestTarget(world, playerId, range, towerCenter, combat->targetMode);
            if (targetId == -1)
                continue; // nothing in range — doesn't fire, cooldown re-checked next tick

            for (int i = 0; i < ammoPerShot; i++)
                bufferIt->second.FreeResource();

            double attackSpeed = combat->GetModifiedAttackSpeed(*towerBuilding);
            combat->attackTimer += attackSpeed > 0.0 ? 1.0 / attackSpeed : 1.0;

            AttackEmission projectile;
            projectile.sourcePlayerId = playerId;
            projectile.sourceUnitInstanceId = towerBuilding->id;
            projectile.position = towerCenter;
            projectile.damage = combat->GetModifiedDamage(*towerBuilding);
            projectile.damageType = DamageType::Physical;
            projectile.targetUnitInstanceId = targetId;
            projectile.speed = kProjectileSpeedTilesPerSec;
            projectile.ticksRemaining = kProjectileTimeoutTicks;
            projectiles[world.AllocateProjectileId()] = projectile;
        }
    }
}
