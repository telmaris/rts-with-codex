#include "warfare/UnitMarchSystem.h"
#include "core/GameWorld.h"
#include "simulation/MilitaryRoadNetwork.h"

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

namespace
{
    using RouteKey = std::pair<int, int>;

    // Units march single-file: a unit cannot advance onto (or past) whatever
    // tile the unit directly ahead of it in the same column currently
    // occupies. This includes the final tile at the target's gate for now —
    // ETAP 6 relaxes that specific rule to let multiple units group up and
    // attack the HQ together.
    constexpr double kBlockedProgress = 0.999;
}

void UnitMarchSystem::Update(GameWorld& world, double dt)
{
    auto& deployedUnits = world.GetDeployedUnits();
    auto& spawnQueues = world.GetSpawnQueues();
    const MilitaryRoadNetwork& militaryRoads = world.GetMilitaryRoads();
    PlayerHandler& playerHandler = world.GetPlayerHandler();

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
        std::vector<int> route = militaryRoads.GetDirectedTiles(routeKey.first, routeKey.second);
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
            if (unit.state == BattleUnitState::AttackingHq || unit.state == BattleUnitState::Dying)
            {
                precedingTileIndex = unit.tileIndex;
                continue; // arrived or dead; not this system's concern anymore
            }

            auto playerIt = playerHandler.players.find(unit.ownerPlayerId);
            double moveSpeed = playerIt != playerHandler.players.end() && playerIt->second != nullptr
                ? unit.GetEffectiveMoveSpeed(*playerIt->second)
                : 0.0;

            unit.tileProgress += moveSpeed * dt;
            while (unit.tileProgress >= 1.0 && unit.tileIndex < lastTileIndex)
            {
                int nextTileIndex = unit.tileIndex + 1;
                if (precedingTileIndex >= 0 && nextTileIndex >= precedingTileIndex)
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
                unit.state = BattleUnitState::AttackingHq;
            }

            precedingTileIndex = unit.tileIndex;
        }
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

        std::vector<int> route = militaryRoads.GetDirectedTiles(routeKey.first, routeKey.second);
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
