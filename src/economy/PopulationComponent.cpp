#include "economy/Building.h"
#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "simulation/MapGenerator.h"
#include "BuildingComponentsInternal.h"

#include <algorithm>
#include <cmath>

void PopulationComponent::Update(Building& self, double dt)
{
    if (self.owner == nullptr)
        return;

    RequestSupply(self, ResourceType::FOOD_PROVISIONS);
    if (settlementLevel >= 2)
        RequestSupply(self, ResourceType::HOUSEHOLD_GOODS);
    if (settlementLevel >= 3)
        RequestSupply(self, ResourceType::URBAN_GOODS);

    bool hasBufferedFood = !foodBuffer.buffer.empty();
    bool hasIncomingFood = CountIncomingResources(&self, ResourceType::FOOD_PROVISIONS) > 0;
    if (!hasBufferedFood && !hasIncomingFood)
    {
        double dropRate = 0.08 / std::max(0.45, foodSupplyLevel);
        foodSupplyLevel = std::max(0.0, foodSupplyLevel - dropRate * dt);
    }

    upkeepTimer += dt;
    if (upkeepTimer >= upkeepInterval)
    {
        upkeepTimer = 0.0;
        auto consumeSupply = [&](ResourceType type, double& supplyLevel)
        {
            if (!RequiresSupply(type))
                return;

            ResourceBuffer* buffer = GetSupplyBuffer(type);
            int needed = GetSupplyUpkeep(type);
            if (buffer != nullptr && static_cast<int>(buffer->buffer.size()) >= needed)
            {
                for (int i = 0; i < needed; i++)
                {
                    buffer->FreeResource();
                    self.owner->economyTelemetry.RecordConsumption(type);
                }
                supplyLevel = std::min(1.0, supplyLevel + 0.45);
            }
            else
            {
                supplyLevel = std::max(0.0, supplyLevel - foodSupplyDropPerMissedUpkeep);
            }
        };

        consumeSupply(ResourceType::FOOD_PROVISIONS, foodSupplyLevel);
        consumeSupply(ResourceType::HOUSEHOLD_GOODS, householdSupplyLevel);
        consumeSupply(ResourceType::URBAN_GOODS, urbanSupplyLevel);
        hasFood = foodSupplyLevel > 0.0;
    }

    if (settlementLevel > 1)
    {
        populationCap = GetActivePopulationCap();
        int effectiveLevel = populationCap.GetBase() >= levelPopulationCaps[3] ? 3 :
                             populationCap.GetBase() >= levelPopulationCaps[2] ? 2 : 1;
        manpowerRate = levelManpowerRates[effectiveLevel];
    }

    double efficiency = GetManpowerProductivity();
    double modRate = self.owner->ResolveStat(manpowerRate, &self);
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
    return RequestSupply(self, ResourceType::FOOD_PROVISIONS);
}

int PopulationComponent::RequestSupply(Building& self, ResourceType type)
{
    ResourceBuffer* buffer = GetSupplyBuffer(type);
    if (self.owner == nullptr || buffer == nullptr || !RequiresSupply(type) ||
        static_cast<int>(buffer->buffer.size()) >= buffer->bufferSize)
        return 0;

    int stored = static_cast<int>(buffer->buffer.size());
    int incoming = CountIncomingResources(&self, type);
    int missing = buffer->bufferSize - stored - incoming;
    if (missing <= 0)
        return 0;

    for (Building* warehouse : StockpileIndex::RankSourcesFor(type, self))
    {
        missing -= warehouse->HandleTransport(type, missing, &self);
        if (missing <= 0)
            break;
    }
    return std::max(0, missing);
}

int PopulationComponent::GetFoodDemand() const
{
    return std::max(0, foodBuffer.bufferSize - static_cast<int>(foodBuffer.buffer.size()));
}

void PopulationComponent::SetSettlementLevel(int level)
{
    settlementLevel = std::clamp(level, 1, 3);
    populationCap = levelPopulationCaps[settlementLevel];
    manpowerRate = levelManpowerRates[settlementLevel];

    // Keep exactly one upkeep payment plus one local reserve. The starting
    // village therefore requests only two packages, while upgraded
    // settlements can still hold a complete higher-tier upkeep payment.
    foodBuffer.bufferSize = GetSupplyUpkeep(ResourceType::FOOD_PROVISIONS) + 1;
    while (static_cast<int>(foodBuffer.buffer.size()) > foodBuffer.bufferSize)
        foodBuffer.FreeResource();
}

int PopulationComponent::GetActivePopulationCap() const
{
    int activeLevel = settlementLevel;
    if (activeLevel >= 2 && householdSupplyLevel < 0.5)
        activeLevel = 1;
    else if (activeLevel >= 3 && urbanSupplyLevel < 0.5)
        activeLevel = 2;
    return levelPopulationCaps[activeLevel];
}

bool PopulationComponent::RequiresSupply(ResourceType type) const
{
    if (type == ResourceType::FOOD_PROVISIONS)
        return true;
    if (type == ResourceType::HOUSEHOLD_GOODS)
        return settlementLevel >= 2;
    if (type == ResourceType::URBAN_GOODS)
        return settlementLevel >= 3;
    return false;
}

int PopulationComponent::GetSupplyUpkeep(ResourceType type) const
{
    if (type == ResourceType::FOOD_PROVISIONS)
        return settlementLevel == 1 ? 1 : settlementLevel == 2 ? 3 : 10;
    if (type == ResourceType::HOUSEHOLD_GOODS)
        return settlementLevel == 2 ? 1 : settlementLevel >= 3 ? 3 : 0;
    if (type == ResourceType::URBAN_GOODS)
        return settlementLevel >= 3 ? 1 : 0;
    return 0;
}

ResourceBuffer* PopulationComponent::GetSupplyBuffer(ResourceType type)
{
    if (type == ResourceType::FOOD_PROVISIONS) return &foodBuffer;
    if (type == ResourceType::HOUSEHOLD_GOODS) return &householdGoodsBuffer;
    if (type == ResourceType::URBAN_GOODS) return &urbanGoodsBuffer;
    return nullptr;
}

const ResourceBuffer* PopulationComponent::GetSupplyBuffer(ResourceType type) const
{
    if (type == ResourceType::FOOD_PROVISIONS) return &foodBuffer;
    if (type == ResourceType::HOUSEHOLD_GOODS) return &householdGoodsBuffer;
    if (type == ResourceType::URBAN_GOODS) return &urbanGoodsBuffer;
    return nullptr;
}
