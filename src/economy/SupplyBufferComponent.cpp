#include "economy/Building.h"
#include "economy/Player.h"
#include "simulation/MapGenerator.h"
#include "warfare/DivisionSector.h"
#include "warfare/MovementPlanner.h"
#include "simulation/SectorGraph.h"
#include "warfare/Equipment.h"
#include "warfare/UnitStats.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

// ─── SupplyBufferComponent ───────────────────────────────────────────────────

bool SupplyBufferComponent::CanReceive() const
{
    return static_cast<int>(buffer.buffer.size()) < buffer.bufferSize;
}

void SupplyBufferComponent::AddResource(Resource* res)
{
    if (res == nullptr) return;
    buffer.AddResource(res);
    stored = static_cast<int>(buffer.buffer.size());
}

void SupplyBufferComponent::ReturnOutgoingResource(Resource* res)
{
    if (res == nullptr) return;
    buffer.AddResource(res);
    stored = static_cast<int>(buffer.buffer.size());
}

Resource SupplyBufferComponent::GetResource()
{
    auto [avail, res] = buffer.GetResource();
    stored = static_cast<int>(buffer.buffer.size());
    return avail ? *res : Resource{};
}

int SupplyBufferComponent::HandleTransport(int amount, Building* receiver, Building& self)
{
    if (receiver == nullptr || amount <= 0)
        return 0;

    int sent = 0;
    for (int i = 0; i < amount; i++)
    {
        if (!receiver->CanReceiveResource(ResourceType::FOOD_PROVISIONS))
            break;

        auto [avail, res] = buffer.GetResource();
        if (!avail)
            break;

        if (self.owner->BeginTransport(&self, receiver, res))
            sent++;
        else
        {
            buffer.AddResource(res);
            break;
        }
    }

    stored = static_cast<int>(buffer.buffer.size());
    return sent;
}

int SupplyBufferComponent::GetModifiedCapacity(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(capacity, &self, ResourceType::Null, std::nullopt, 0)
        : capacity.GetBase();
}

int SupplyBufferComponent::GetSupplyConsumption(const Building& self,
                                                  const GarrisonComponent& g) const
{
    // Real food drain estimate (units/minute), not a stand-in: this used to sum
    // `manpowerScale` (a division's raw HP/manpower pool size, e.g. 200 for a
    // Swordsman) as if it were a consumption RATE — completely unrelated to
    // ConsumeDivisionSupply's actual per-tick drain, and the reason the HUD
    // showed ~400/min for two idle Swordsmen no matter how the real rate was
    // tuned. EstimateDivisionFoodPerMinute mirrors the real formula instead.
    if (!g.divisions.empty())
    {
        float perMinute = 0.0f;
        for (const auto& d : g.divisions)
            perMinute += EstimateDivisionFoodPerMinute(*d, d->occupiedTile.x >= 0);
        int consumption = static_cast<int>(std::lround(perMinute));
        return self.owner != nullptr
            ? self.owner->ModifyBalanceIntForBuilding(BalanceStat::SupplyConsumption, consumption,
                                                       &self, ResourceType::Null, std::nullopt, 0)
            : consumption;
    }
    int troops = g.GetTotalTroops();
    return self.owner != nullptr
        ? self.owner->ModifyBalanceIntForBuilding(BalanceStat::SupplyConsumption, troops,
                                                   &self, ResourceType::Null, std::nullopt, 0)
        : troops;
}

