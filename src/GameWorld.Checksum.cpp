#include "../inc/GameWorld.h"

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
        for (const auto* building : player->dataTracker.buildings)
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
    }

    return hash;
}
