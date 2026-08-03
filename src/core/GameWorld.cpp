#include "core/GameWorld.h"

#include <algorithm>

// GameWorld implementation is split across GameWorld.*.cpp files by responsibility.

std::size_t GameWorld::GetLiveShipmentCount() const
{
    std::size_t count = 0;
    for (const auto& [id, player] : playerHandler.players)
        if (player != nullptr)
            count += player->GetLiveShipmentCount();
    return count;
}

int GameWorld::GetStoredResourceUnits() const
{
    int total = 0;
    for (const auto& [id, player] : playerHandler.players)
    {
        if (player == nullptr)
            continue;
        for (Building* building : player->GetTrackedBuildings())
        {
            if (building == nullptr)
                continue;
            for (const auto& view : building->GetInputBufferViews())
                total += std::max(0, view.amount);
            for (const auto& view : building->GetOutputBufferViews())
                total += std::max(0, view.amount);
        }
    }
    return total;
}
