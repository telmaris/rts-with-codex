#include "warfare/HqCombatSystem.h"
#include "warfare/UnitMarchSystem.h"
#include "warfare/CombatPipeline.h"
#include "warfare/UnitDefinition.h"
#include "core/GameWorld.h"
#include "simulation/MapGenerator.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace
{
    // Thorns radius comfortably covers the contact tile just outside the
    // footprint edge, where every AttackingHq unit stands — a CircleShape
    // overlap test either way, per the plan's explicit ask to validate the
    // shared pipeline's area-collision path (not just front-vs-front).
    float ThornsRadius(Vec2i footprint)
    {
        return static_cast<float>(std::max(footprint.x, footprint.y)) * TILE_SIZE / 2.0f + TILE_SIZE;
    }

    // Finds a player's Headquarters (there is always exactly one, never
    // rebuilt), or {nullptr, nullptr} if somehow absent (defensive only).
    std::pair<Building*, HqComponent*> FindHq(Player& player)
    {
        for (Building* building : player.GetTrackedBuildingsWithComponent<HqComponent>())
        {
            auto* hq = building->GetComponent<HqComponent>();
            if (hq != nullptr)
                return {building, hq};
        }
        return {nullptr, nullptr};
    }

    std::vector<int> BesiegersOf(const std::map<int, BattleUnit>& deployedUnits, int hqOwnerId)
    {
        std::vector<int> ids;
        for (const auto& [id, unit] : deployedUnits)
            if (unit.state == BattleUnitState::AttackingHq && unit.routeToPlayerId == hqOwnerId)
                ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        return ids;
    }
}

void HqCombatSystem::Update(GameWorld& world, double dt)
{
    auto& deployedUnits = world.GetDeployedUnits();
    PlayerHandler& playerHandler = world.GetPlayerHandler();
    const TileMap& tilemap = world.GetTileMap();
    unsigned int worldSeed = tilemap.params.seed;
    std::uint64_t tick = world.GetSimulationTick();
    static const std::map<DamageType, float> noResistances;

    for (auto& [ownerId, ownerPtr] : playerHandler.players)
    {
        if (ownerPtr == nullptr || ownerPtr->defeated)
            continue;

        auto [hqBuilding, hq] = FindHq(*ownerPtr);
        if (hqBuilding == nullptr || hq == nullptr)
            continue;

        std::vector<int> besiegerIds = BesiegersOf(deployedUnits, ownerId);

        // Siege damage: every besieger acts independently in parallel (TD
        // etap-6.2, [DECISION]: no cap on group size) — unlike road combat,
        // there is no front-only restriction.
        int conquerorId = -1;
        for (int besiegerId : besiegerIds)
        {
            if (hq->currentHp <= 0.0)
                break; // HQ already fell to an earlier attacker this same tick

            auto besiegerIt = deployedUnits.find(besiegerId);
            if (besiegerIt == deployedUnits.end())
                continue;
            BattleUnit& besieger = besiegerIt->second;

            besieger.attackTimer -= dt;
            if (besieger.attackTimer > 0.0)
                continue;

            auto besiegerOwnerIt = playerHandler.players.find(besieger.ownerPlayerId);
            if (besiegerOwnerIt == playerHandler.players.end() || besiegerOwnerIt->second == nullptr)
                continue;
            Player& besiegerOwner = *besiegerOwnerIt->second;

            double attackSpeed = besieger.GetEffectiveAttackSpeed(besiegerOwner);
            besieger.attackTimer += attackSpeed > 0.0 ? 1.0 / attackSpeed : 1.0;

            double damage = besieger.GetEffectiveSiegeAttack(besiegerOwner);
            double hardDefense = hq->GetModifiedHardDefense(*hqBuilding);
            double resolved = CombatResolver::ResolveDamage(damage, hardDefense, DamageType::Physical, noResistances,
                                                             worldSeed, tick, besieger.instanceId);
            hq->currentHp -= resolved;
            if (hq->currentHp <= 0.0 && conquerorId == -1)
                conquerorId = besieger.ownerPlayerId;
        }

        // Thorns: a periodic area emission around the HQ's footprint that
        // damages every besieger within range (first real consumer of an
        // area — not just front-vs-front — collision test).
        if (hq->currentHp > 0.0)
        {
            hq->thornsTimer -= dt;
            if (hq->thornsTimer <= 0.0)
            {
                double thornsDamage = hq->GetModifiedThornsDamage(*hqBuilding);
                hq->thornsTimer += hq->thornsInterval > 0.0 ? hq->thornsInterval : 1.0;

                if (thornsDamage > 0.0)
                {
                    CircleShape thornsShape(ThornsRadius(hqBuilding->GetFootprint()));
                    Vec2f hqCenter = ComputeBuildingCenter(tilemap, *hqBuilding);

                    // Re-fetch besieger ids: the siege pass above may have
                    // just killed the HQ (handled below) or none at all —
                    // either way this list is still accurate for this tick.
                    for (int besiegerId : besiegerIds)
                    {
                        auto besiegerIt = deployedUnits.find(besiegerId);
                        if (besiegerIt == deployedUnits.end())
                            continue;
                        BattleUnit& besieger = besiegerIt->second;

                        auto besiegerOwnerIt = playerHandler.players.find(besieger.ownerPlayerId);
                        if (besiegerOwnerIt == playerHandler.players.end() || besiegerOwnerIt->second == nullptr)
                            continue;
                        Player& besiegerOwner = *besiegerOwnerIt->second;

                        Vec2f besiegerPos = UnitMarchSystem::ComputeWorldPosition(world, besieger);
                        const UnitDefinition* def = FindUnitDefinition(besieger.unitDefId);
                        float colliderRadius = def != nullptr ? static_cast<float>(def->colliderRadius) : 0.4f;
                        if (!thornsShape.Overlaps(hqCenter, besiegerPos, colliderRadius))
                            continue;

                        double armor = besieger.GetEffectiveArmor(besiegerOwner);
                        const auto& resistances = def != nullptr ? def->resistances : noResistances;
                        double resolved = CombatResolver::ResolveDamage(thornsDamage, armor, DamageType::Physical,
                                                                         resistances, worldSeed, tick, hqBuilding->id);
                        besieger.currentHp -= resolved;
                    }
                }
            }
        }

        // Remove any besiegers thorns just killed this tick (siege deaths
        // don't apply here — a dead HQ is handled via elimination below,
        // which removes every one of the loser's units anyway).
        for (int besiegerId : besiegerIds)
        {
            auto besiegerIt = deployedUnits.find(besiegerId);
            if (besiegerIt != deployedUnits.end() && besiegerIt->second.currentHp <= 0.0)
                deployedUnits.erase(besiegerIt);
        }

        if (hq->currentHp <= 0.0 && conquerorId != -1)
            world.EliminatePlayer(ownerId, conquerorId);
    }
}
