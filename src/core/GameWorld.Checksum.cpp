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
}

std::uint64_t GameWorld::BuildChecksum() const
{
    std::uint64_t hash = 1469598103934665603ull;
    HashValue(hash, simulationTick);
    HashInt(hash, tilemap.params.sizeX);
    HashInt(hash, tilemap.params.sizeY);
    HashValue(hash, tilemap.params.seed);

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
    }

    return hash;
}
