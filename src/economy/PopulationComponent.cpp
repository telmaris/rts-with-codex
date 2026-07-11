#include "economy/Building.h"
#include "economy/Player.h"
#include "simulation/MapGenerator.h"
#include "BuildingComponentsInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

// ─── PopulationComponent ─────────────────────────────────────────────────────

void PopulationComponent::Update(Building& self, double dt)
{
    if (self.owner == nullptr)
        return;

    // ETAP 9: Villages now receive food via supply packages (SupplyCategory::Food)
    // instead of pulling from storage (RequestFoodSupply). Legacy pull-request
    // kept for backward compat, but disabled by default. Check GetFoodDemand() instead.
    bool hasBufferedFood = !foodBuffer.buffer.empty();
    bool hasIncomingFood = CountIncomingResources(&self, ResourceType::FOOD_PROVISIONS) > 0;
    if (!hasBufferedFood && !hasIncomingFood)
    {
        double dropRate = 0.08 / std::max(0.45, foodSupplyLevel);  // No food → rapid productivity drop
        foodSupplyLevel = std::max(0.0, foodSupplyLevel - dropRate * dt);
    }

    upkeepTimer += dt;
    if (upkeepTimer >= upkeepInterval)
    {
        upkeepTimer = 0.0;
        int needed = std::max(1, static_cast<int>(std::ceil(foodPackageUpkeep)));
        if (static_cast<int>(foodBuffer.buffer.size()) >= needed)
        {
            for (int i = 0; i < needed; i++)
            {
                foodBuffer.FreeResource();
                self.owner->economyTelemetry.RecordConsumption(ResourceType::FOOD_PROVISIONS);
            }
            foodSupplyLevel = std::min(1.0, foodSupplyLevel + 0.45);
        }
        else
        {
            foodSupplyLevel = std::max(0.0, foodSupplyLevel - foodSupplyDropPerMissedUpkeep);
        }
        hasFood = foodSupplyLevel > 0.0;
    }

    double efficiency  = GetManpowerProductivity();
    double modRate     = self.owner->ResolveStat(manpowerRate, &self);
    self.owner->AddManpower(modRate * efficiency * dt);
    self.activeTime += dt * efficiency;
}

double PopulationComponent::GetFoodSupplyRatio() const
{
    return std::clamp(foodSupplyLevel, 0.0, 1.0);
}

double PopulationComponent::GetManpowerProductivity() const
{
    return GetFoodSupplyRatio();
}

double PopulationComponent::GetWorkerProductivity() const
{
    return 0.3 + 0.7 * GetFoodSupplyRatio();
}

int PopulationComponent::RequestFoodSupply(Building& self)
{
    if (self.owner == nullptr || static_cast<int>(foodBuffer.buffer.size()) >= foodBuffer.bufferSize)
        return 0;

    int stored   = static_cast<int>(foodBuffer.buffer.size());
    int incoming = CountIncomingResources(&self, ResourceType::FOOD_PROVISIONS);
    int missing  = foodBuffer.bufferSize - stored - incoming;
    if (missing <= 0)
        return 0;

    for (auto& tile : self.owner->tilemap.tilemap)
    {
        Building* storage = tile.building.get();
        if (storage == nullptr || storage->owner != self.owner ||
            !storage->HasComponent<StorageComponent>())
            continue;

        missing -= storage->HandleTransport(ResourceType::FOOD_PROVISIONS, missing, &self);
        if (missing <= 0) break;
    }
    return std::max(0, missing);
}

int PopulationComponent::GetFoodDemand() const
{
    return std::max(0, foodBuffer.bufferSize - static_cast<int>(foodBuffer.buffer.size()));
}

