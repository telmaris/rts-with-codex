#include "economy/Building.h"
#include "economy/Player.h"
#include "simulation/MapGenerator.h"
#include "BuildingComponentsInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <vector>

// ─── PopulationComponent ─────────────────────────────────────────────────────

void PopulationComponent::Update(Building& self, double dt)
{
    if (self.owner == nullptr)
        return;

    // T6 (docs/post_pivot_audit_2026-07-12.md): actively pull FOOD_PROVISIONS
    // from the owner's storage every tick — same pattern as DefenseTower's
    // ammo / Barracks' unit costs (T3). Before this fix a village only ever
    // received food pushed by a supplier's own StorageComponent::Update
    // (via AutoConnectBuilding's auto-wiring), with nothing to actively
    // request it if no producer happened to auto-connect nearby.
    RequestFoodSupply(self);

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

    // T6 (docs/post_pivot_audit_2026-07-12.md): tracked-buildings registry
    // instead of a full tilemap scan (same perf-follow-up pattern already
    // applied to CountIncomingResources/AutoConnectBuilding/TryBuildRoads).
    // Sorted by id — "first matching storage wins the delivery" is order-
    // sensitive, and GetTrackedBuildings() is a std::set<Building*> ordered
    // by heap address, not by anything deterministic across separately-
    // constructed GameWorld instances (see the lockstep-determinism bug this
    // exact mistake caused in AutoConnectBuilding/TryBuildRoads, same doc).
    std::vector<Building*> candidates(self.owner->GetTrackedBuildings().begin(),
                                       self.owner->GetTrackedBuildings().end());
    std::sort(candidates.begin(), candidates.end(), [](Building* a, Building* b) { return a->id < b->id; });

    for (Building* storage : candidates)
    {
        if (storage == nullptr || !storage->HasComponent<StorageComponent>())
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

