#include "economy/Building.h"
#include "economy/Player.h"
#include "simulation/MapGenerator.h"
#include "warfare/DivisionSector.h"
#include "warfare/MovementPlanner.h"
#include "simulation/SectorGraph.h"
#include "warfare/Equipment.h"

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
    if (!g.divisions.empty())
    {
        int manpower = 0;
        for (const auto& d : g.divisions) manpower += d->manpowerScale;
        return self.owner != nullptr
            ? self.owner->ModifyBalanceIntForBuilding(BalanceStat::SupplyConsumption, manpower,
                                                       &self, ResourceType::Null, std::nullopt, 0)
            : manpower;
    }
    int troops = g.GetTotalTroops();
    return self.owner != nullptr
        ? self.owner->ModifyBalanceIntForBuilding(BalanceStat::SupplyConsumption, troops,
                                                   &self, ResourceType::Null, std::nullopt, 0)
        : troops;
}

