#include "warfare/UnitMarchSystem.h"
#include "core/GameWorld.h"
#include "simulation/MilitaryRoadNetwork.h"
#include "simulation/PathingService.h"

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

namespace
{
    using RouteKey = std::pair<int, int>;

    // Units march single-file: a unit cannot advance onto (or past) whatever
    // tile the unit directly ahead of it in the same column currently
    // occupies. The final tile (the target's gate) is the one exception
    // (TD etap-6.2): any number of units may stack there and independently
    // besiege the HQ in parallel — see the `nextTileIndex != lastTileIndex`
    // carve-out below.
    constexpr double kBlockedProgress = 0.999;

    // TD(etap-6.3): resolves the directed route between two players, falling
    // back to a path through any conquered (eliminated) intermediate players'
    // HQs when there's no direct ring edge — see PathingService::FindMilitaryPath.
    std::vector<int> ResolveRouteTiles(const MilitaryRoadNetwork& militaryRoads, const PlayerHandler& playerHandler,
                                       int fromPlayerId, int toPlayerId)
    {
        auto isEliminated = [&](int playerId)
        {
            auto it = playerHandler.players.find(playerId);
            return it != playerHandler.players.end() && it->second != nullptr && it->second->defeated;
        };
        return PathingService::FindMilitaryPath(militaryRoads, fromPlayerId, toPlayerId, isEliminated).tiles;
    }
}

void UnitMarchSystem::Update(GameWorld& world, double dt)
{
    auto& deployedUnits = world.GetDeployedUnits();
    auto& spawnQueues = world.GetSpawnQueues();
    const MilitaryRoadNetwork& militaryRoads = world.GetMilitaryRoads();
    PlayerHandler& playerHandler = world.GetPlayerHandler();
    std::vector<int> unitsReturningToRoster;

    // Group marching/arrived units by direction, front-to-back, so followers
    // react to the leader's already-updated position within this same tick.
    std::map<RouteKey, std::vector<int>> columns;
    for (auto& [id, unit] : deployedUnits)
    {
        if (unit.tileIndex < 0)
            continue; // still waiting in the spawn queue
        columns[{unit.routeFromPlayerId, unit.routeToPlayerId}].push_back(id);
    }

    for (auto& [routeKey, ids] : columns)
    {
        std::vector<int> route = ResolveRouteTiles(militaryRoads, playerHandler, routeKey.first, routeKey.second);
        if (route.empty())
            continue;
        int lastTileIndex = static_cast<int>(route.size()) - 1;

        std::sort(ids.begin(), ids.end(), [&](int a, int b)
        {
            const BattleUnit& ua = deployedUnits.at(a);
            const BattleUnit& ub = deployedUnits.at(b);
            if (ua.tileIndex != ub.tileIndex)
                return ua.tileIndex > ub.tileIndex;
            if (ua.tileProgress != ub.tileProgress)
                return ua.tileProgress > ub.tileProgress;
            return a < b; // deterministic tie-break
        });

        int precedingTileIndex = -1; // nothing ahead of the front-most unit
        for (int id : ids)
        {
            BattleUnit& unit = deployedUnits.at(id);
            if (unit.state == BattleUnitState::AttackingHq || unit.state == BattleUnitState::Dying ||
                unit.state == BattleUnitState::FightingUnit)
            {
                precedingTileIndex = unit.tileIndex;
                continue; // arrived, dead, or locked in melee — UnitCombatSystem's concern
            }

            auto playerIt = playerHandler.players.find(unit.ownerPlayerId);
            double moveSpeed = playerIt != playerHandler.players.end() && playerIt->second != nullptr
                ? unit.GetEffectiveMoveSpeed(*playerIt->second)
                : 0.0;

            unit.tileProgress += moveSpeed * dt;
            while (unit.tileProgress >= 1.0 && unit.tileIndex < lastTileIndex)
            {
                int nextTileIndex = unit.tileIndex + 1;
                bool blocked = precedingTileIndex >= 0 && nextTileIndex >= precedingTileIndex &&
                               nextTileIndex != lastTileIndex;
                if (blocked)
                {
                    unit.tileProgress = kBlockedProgress;
                    break;
                }
                unit.tileIndex = nextTileIndex;
                unit.tileProgress -= 1.0;
            }

            if (unit.tileIndex >= lastTileIndex)
            {
                unit.tileIndex = lastTileIndex;
                unit.tileProgress = 0.0;
                auto targetIt = playerHandler.players.find(unit.routeToPlayerId);
                if (targetIt != playerHandler.players.end() && targetIt->second != nullptr && targetIt->second->defeated)
                {
                    // The target fell while this unit was marching. It has
                    // reached the captured HQ and is available for a newly
                    // unlocked enemy instead of attacking a defeated player.
                    unitsReturningToRoster.push_back(id);
                    precedingTileIndex = unit.tileIndex;
                    continue;
                }

                unit.state = BattleUnitState::AttackingHq;
                // Full attack cooldown on arrival (TD etap-6.2) — no free
                // first hit against the HQ, mirroring road combat's
                // LockIntoMelee (UnitCombatSystem.cpp).
                double attackSpeed = playerIt != playerHandler.players.end() && playerIt->second != nullptr
                    ? unit.GetEffectiveAttackSpeed(*playerIt->second)
                    : 1.0;
                unit.attackTimer = attackSpeed > 0.0 ? 1.0 / attackSpeed : 1.0;
            }

            precedingTileIndex = unit.tileIndex;
        }
    }

    for (int id : unitsReturningToRoster)
    {
        auto it = deployedUnits.find(id);
        if (it == deployedUnits.end())
            continue;
        auto ownerIt = playerHandler.players.find(it->second.ownerPlayerId);
        if (ownerIt == playerHandler.players.end() || ownerIt->second == nullptr || ownerIt->second->defeated)
            continue;

        BattleUnit unit = std::move(it->second);
        unit.state = BattleUnitState::InRoster;
        unit.routeFromPlayerId = -1;
        unit.routeToPlayerId = -1;
        unit.tileIndex = 0;
        unit.tileProgress = 0.0;
        unit.attackTimer = 0.0;
        ownerIt->second->roster.AddUnit(std::move(unit));
        deployedUnits.erase(it);
    }

    // Spawn the next queued unit once its column's gate tile (index 0) is free.
    for (auto& [routeKey, queue] : spawnQueues)
    {
        if (queue.empty())
            continue;

        bool gateOccupied = false;
        for (const auto& [id, unit] : deployedUnits)
        {
            if (unit.routeFromPlayerId == routeKey.first && unit.routeToPlayerId == routeKey.second &&
                unit.tileIndex == 0)
            {
                gateOccupied = true;
                break;
            }
        }
        if (gateOccupied)
            continue;

        std::vector<int> route = ResolveRouteTiles(militaryRoads, playerHandler, routeKey.first, routeKey.second);
        if (route.empty())
            continue;

        int nextId = queue.front();
        queue.pop_front();
        auto it = deployedUnits.find(nextId);
        if (it == deployedUnits.end())
            continue;

        it->second.tileIndex = 0;
        it->second.tileProgress = 0.0;
        // A one-tile route (degenerate/tiny map) means the gate tile IS the
        // target's doorstep — arrive immediately instead of waiting to "move"
        // off a tile that doesn't exist.
        if (route.size() == 1)
        {
            it->second.tileIndex = 0;
            it->second.state = BattleUnitState::AttackingHq;
        }
    }
}

Vec2f UnitMarchSystem::ComputeWorldPosition(const GameWorld& world, const BattleUnit& unit)
{
    const TileMap& tilemap = world.GetTileMap();
    std::vector<int> route = ResolveRouteTiles(world.GetMilitaryRoads(), world.GetPlayerHandler(),
                                                unit.routeFromPlayerId, unit.routeToPlayerId);
    if (route.empty())
        return {0.0f, 0.0f};

    // Still waiting in the spawn queue — show it sitting at its own gate.
    int tileIndex = std::clamp(unit.tileIndex, 0, static_cast<int>(route.size()) - 1);

    Vec2i currentTile = tilemap.GetCoordsFromId(route[tileIndex]);
    Vec2f from{static_cast<float>(currentTile.x * TILE_SIZE + TILE_SIZE / 2),
               static_cast<float>(currentTile.y * TILE_SIZE + TILE_SIZE / 2)};
    if (unit.tileIndex < 0 || tileIndex + 1 >= static_cast<int>(route.size()))
        return from;

    Vec2i nextTile = tilemap.GetCoordsFromId(route[tileIndex + 1]);
    Vec2f to{static_cast<float>(nextTile.x * TILE_SIZE + TILE_SIZE / 2),
             static_cast<float>(nextTile.y * TILE_SIZE + TILE_SIZE / 2)};
    float t = static_cast<float>(std::clamp(unit.tileProgress, 0.0, 1.0));
    return {from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t};
}
