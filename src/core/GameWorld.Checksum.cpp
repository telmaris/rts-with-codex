#include "core/GameWorld.h"

#include <algorithm>
#include <vector>

namespace
{
    void HashValue(std::uint64_t& hash, std::uint64_t value)
    {
        hash ^= value;
        hash *= 1099511628211ull;
    }

    void HashInt(std::uint64_t& hash, int value)
    {
        HashValue(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(value)));
    }

    // Character-by-character rather than std::hash<std::string>, which is not
    // guaranteed stable across STL versions/architectures — this checksum is
    // compared between host and client processes.
    void HashString(std::uint64_t& hash, const std::string& value)
    {
        HashValue(hash, static_cast<std::uint64_t>(value.size()));
        for (char c : value)
            HashInt(hash, static_cast<int>(static_cast<unsigned char>(c)));
    }
}

std::uint64_t GameWorld::BuildChecksum() const
{
    std::uint64_t hash = 1469598103934665603ull;
    HashValue(hash, simulationTick);
    HashInt(hash, tilemap.params.sizeX);
    HashInt(hash, tilemap.params.sizeY);
    HashValue(hash, tilemap.params.seed);

    // Military road ring (TD etap-2): immutable after generation, but included
    // so any host/client generation divergence surfaces as a checksum
    // mismatch immediately rather than only once units start marching.
    const auto& militaryRoutes = militaryRoads.GetRoutes();
    HashValue(hash, static_cast<std::uint64_t>(militaryRoutes.size()));
    for (const auto& route : militaryRoutes)
    {
        HashInt(hash, route.playerA);
        HashInt(hash, route.playerB);
        HashValue(hash, static_cast<std::uint64_t>(route.tiles.size()));
        for (int tileId : route.tiles)
            HashInt(hash, tileId);
    }

    for (const auto& [playerId, player] : playerHandler.players)
    {
        HashInt(hash, playerId);
        if (player == nullptr)
        {
            HashValue(hash, 0);
            continue;
        }

        HashInt(hash, player->dataTracker.CountBuildings(BuildingType::Headquarters));
        HashValue(hash, static_cast<std::uint64_t>(player->dataTracker.buildings.size()));

        // dataTracker.buildings is a std::set<Building*> ordered by raw pointer
        // value, which differs between independently-allocated host/client
        // processes. Sort by the stable, assigned building id before hashing so
        // the checksum doesn't depend on heap layout.
        std::vector<const Building*> orderedBuildings(player->dataTracker.buildings.begin(),
                                                        player->dataTracker.buildings.end());
        std::sort(orderedBuildings.begin(), orderedBuildings.end(), [](const Building* a, const Building* b)
        {
            return a->id < b->id;
        });

        for (const auto* building : orderedBuildings)
        {
            if (building == nullptr)
                continue;

            HashInt(hash, building->id);
            HashInt(hash, building->positionId);
            HashInt(hash, static_cast<int>(building->buildingType));
            HashInt(hash, building->owner != nullptr ? building->owner->id : -1);
            HashInt(hash, static_cast<int>(building->constructionRemaining * 1000.0));
            HashInt(hash, building->GetTotalProduced());
            HashInt(hash, building->IsProductionBlocked() ? 1 : 0);
        }

        for (const auto& [commandType, count] : player->dataTracker.processedCommands)
        {
            HashInt(hash, static_cast<int>(commandType));
            HashInt(hash, count);
        }

        for (const auto& w : player->diplomatic.wars)
        {
            HashInt(hash, w.id);
            HashInt(hash, w.attackerId);
            HashInt(hash, w.defenderId);
            HashInt(hash, w.active ? 1 : 0);
        }
        for (const auto& [otherId, rel] : player->diplomatic.relations)
        {
            HashInt(hash, otherId);
            HashInt(hash, static_cast<int>(rel));
        }

        // TD(etap-3): recruited-but-not-deployed roster. std::map<int, BattleUnit>
        // keyed by instanceId is already deterministically ordered.
        HashValue(hash, static_cast<std::uint64_t>(player->roster.units.size()));
        for (const auto& [instanceId, unit] : player->roster.units)
        {
            HashInt(hash, unit.instanceId);
            HashInt(hash, unit.ownerPlayerId);
            HashString(hash, unit.unitDefId);
            HashInt(hash, static_cast<int>(unit.currentHp * 1000.0));
            HashInt(hash, static_cast<int>(unit.state));
        }
    }

    // TD(etap-4): deployed (marching/fighting/arrived) units and their spawn
    // queues. std::map ordering (instanceId; (fromPlayerId,toPlayerId) pair)
    // is already deterministic; std::deque preserves FIFO order.
    HashValue(hash, static_cast<std::uint64_t>(deployedUnits.size()));
    for (const auto& [instanceId, unit] : deployedUnits)
    {
        HashInt(hash, unit.instanceId);
        HashInt(hash, unit.ownerPlayerId);
        HashString(hash, unit.unitDefId);
        HashInt(hash, static_cast<int>(unit.currentHp * 1000.0));
        HashInt(hash, static_cast<int>(unit.state));
        HashInt(hash, unit.routeFromPlayerId);
        HashInt(hash, unit.routeToPlayerId);
        HashInt(hash, unit.tileIndex);
        HashInt(hash, static_cast<int>(unit.tileProgress * 1000.0));
        // TD(etap-5): mutated every tick while FightingUnit.
        HashInt(hash, static_cast<int>(unit.attackTimer * 1000.0));
    }

    HashValue(hash, static_cast<std::uint64_t>(spawnQueues.size()));
    for (const auto& [routeKey, queue] : spawnQueues)
    {
        HashInt(hash, routeKey.first);
        HashInt(hash, routeKey.second);
        HashValue(hash, static_cast<std::uint64_t>(queue.size()));
        for (int unitInstanceId : queue)
            HashInt(hash, unitInstanceId);
    }

    return hash;
}
