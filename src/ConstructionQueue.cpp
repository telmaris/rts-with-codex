#include "ConstructionQueue.h"

#include "Building.h"
#include "Player.h"

#include <algorithm>

int ConstructionQueue::EffectiveBuilders(const Player& player) const
{
    // Never drop below a single builder, otherwise construction would deadlock.
    return player.ModifyBalanceInt(BalanceStat::BuilderAmount, builders.GetBase(),
                                   BuildingType::Building, ResourceType::Null,
                                   std::nullopt, 1);
}

void ConstructionQueue::Refresh(Player& player)
{
    std::vector<Building*> pending;
    for (Building* building : player.dataTracker.buildings)
        if (building != nullptr && building->IsUnderConstruction())
            pending.push_back(building);

    // dataTracker.buildings iterates by pointer address (non-deterministic), so
    // sort by building id to get a stable, cross-machine-identical queue order.
    std::sort(pending.begin(), pending.end(), [](const Building* a, const Building* b)
    {
        return a->id < b->id;
    });

    int builderCount = EffectiveBuilders(player);
    order.clear();
    order.reserve(pending.size());
    for (std::size_t i = 0; i < pending.size(); ++i)
    {
        pending[i]->constructionActive = static_cast<int>(i) < builderCount;
        order.push_back(pending[i]->id);
    }
    activeCount = std::min<int>(builderCount, static_cast<int>(pending.size()));
}

int ConstructionQueue::QueuePosition(int buildingId) const
{
    auto it = std::find(order.begin(), order.end(), buildingId);
    if (it == order.end())
        return 0;
    return static_cast<int>(std::distance(order.begin(), it)) + 1;
}

bool ConstructionQueue::IsActive(int buildingId) const
{
    int position = QueuePosition(buildingId);
    return position > 0 && position <= activeCount;
}
