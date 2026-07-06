#include "economy/Building.h"
#include "economy/Player.h"
#include "simulation/MapGenerator.h"
#include "warfare/DivisionSector.h"
#include "warfare/MovementPlanner.h"
#include "simulation/SectorGraph.h"
#include "warfare/Equipment.h"
#include "BuildingComponentsInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

// ─── LogisticsComponent ──────────────────────────────────────────────────────

bool LogisticsComponent::HasSupplier(ResourceType type) const
{
    auto it = suppliers.find(type);
    if (it == suppliers.end())
        return false;
    for (auto* s : it->second)
        if (s != nullptr) return true;
    return false;
}

bool LogisticsComponent::HasReceiver(ResourceType type) const
{
    return receivers.contains(type) && receivers.at(type) != nullptr;
}

void LogisticsComponent::SetSupplier(ResourceType type, Building* supplier, Building& self)
{
    if (supplier == nullptr)
        return;

    auto& sups = suppliers[type];
    bool changed = false;

    if (!supplier->IsStorageLike())
    {
        size_t before = sups.size();
        sups.erase(std::remove_if(sups.begin(), sups.end(),
            [](Building* e){ return e != nullptr && e->IsStorageLike(); }), sups.end());
        changed = sups.size() != before;
    }

    if (std::find(sups.begin(), sups.end(), supplier) == sups.end())
    {
        sups.push_back(supplier);
        changed = true;
    }

    if (changed)
        pendingRequests[type] = CountIncomingResources(&self, type);
}

void LogisticsComponent::SetReceiver(ResourceType type, Building* receiver, Building& self,
                                     ProductionComponent& prod)
{
    auto it = receivers.find(type);
    Building* prev = (it != receivers.end()) ? it->second : nullptr;
    if (prev != nullptr && prev != receiver)
    {
        prev->RemoveSupplier(type, &self);
        if (prev->owner != nullptr && prev->CanAcceptResource(type) && !prev->HasSupplier(type))
        {
            Building* storage = prev->owner->tilemap.FindNearestStorage(prev, prev->owner);
            if (storage != nullptr && storage != prev)
                prev->SetSupplier(type, storage);
        }
    }

    receivers[type] = receiver;
    auto alt = altReceivers.find(type);
    if (alt != altReceivers.end() && alt->second == receiver)
        altReceivers.erase(alt);
    if (receiver != nullptr)
        receiver->SetSupplier(type, &self);
}

void LogisticsComponent::SetAltReceiver(ResourceType type, Building* receiver, Building& self)
{
    if (receiver == nullptr)
        return;

    auto primary = receivers.find(type);
    if (primary != receivers.end() && primary->second == receiver)
        return;

    auto prev = altReceivers.find(type);
    if (prev != altReceivers.end() && prev->second != nullptr && prev->second != receiver)
        prev->second->RemoveSupplier(type, &self);

    altReceivers[type] = receiver;
    receiver->SetSupplier(type, &self);
}

void LogisticsComponent::RemoveSupplier(ResourceType type, Building* supplier)
{
    auto it = suppliers.find(type);
    if (it == suppliers.end())
        return;

    auto& sups = it->second;
    sups.erase(std::remove(sups.begin(), sups.end(), supplier), sups.end());
    if (sups.empty())
        suppliers.erase(it);

    pendingRequests.erase(type);
}

void LogisticsComponent::RemoveReceiver(ResourceType type, Building* receiver, Building& self,
                                         ProductionComponent&)
{
    auto it = receivers.find(type);
    if (it != receivers.end() && it->second == receiver)
    {
        receivers.erase(it);
        auto alt = altReceivers.find(type);
        if (alt != altReceivers.end() && alt->second != nullptr && alt->second != receiver)
        {
            receivers[type] = alt->second;
            altReceivers.erase(alt);
        }
        return;
    }

    auto alt = altReceivers.find(type);
    if (alt != altReceivers.end() && alt->second == receiver)
        altReceivers.erase(alt);
}

int LogisticsComponent::RequestResource(ResourceType type, int amount, Building& self)
{
    if (amount <= 0)
        return 0;

    if (!suppliers.contains(type))
    {
        requestBlocked = true;
        return 0;
    }

    int sent = 0;
    for (auto* sup : suppliers[type])
    {
        if (sup == nullptr) continue;
        int missing = amount - sent;
        if (missing <= 0) break;
        sent += sup->HandleTransport(type, missing, &self);
    }

    if (sent < amount)
        requestBlocked = true;

    pendingRequests[type] += sent;
    return sent;
}

void LogisticsComponent::MaintainRequests(Building& self, ProductionComponent& prod)
{
    requestBlocked = false;
    for (auto& [res, buf] : prod.inputBuffers)
    {
        int stored  = static_cast<int>(buf.buffer.size());
        int pending = CountIncomingResources(&self, res);
        pendingRequests[res] = pending;
        int missing = buf.bufferSize - stored - pending;
        if (missing > 0)
            RequestResource(res, missing, self);
    }
}

void LogisticsComponent::DispatchOutputs(Building& self, ProductionComponent& prod)
{
    for (auto& [res, amount] : prod.products)
    {
        auto recvIt = receivers.find(res);
        Building* recv = recvIt != receivers.end() ? recvIt->second : nullptr;
        if (recv == nullptr && self.owner != nullptr)
        {
            Building* storage = self.owner->tilemap.FindNearestStorage(&self, self.owner);
            if (storage != nullptr && storage->CanAcceptResource(res))
            {
                receivers[res] = storage;
                recv = storage;
            }
        }

        std::vector<Building*> targets;
        if (recv != nullptr)
            targets.push_back(recv);
        auto alt = altReceivers.find(res);
        if (alt != altReceivers.end() && alt->second != nullptr && alt->second != recv)
            targets.push_back(alt->second);
        if (self.owner != nullptr)
        {
            Building* storage = self.owner->tilemap.FindNearestStorage(&self, self.owner);
            if (storage != nullptr && storage != recv && storage->CanAcceptResource(res) &&
                std::find(targets.begin(), targets.end(), storage) == targets.end())
                targets.push_back(storage);
        }

        for (auto* target : targets)
        {
            int freeCapacity = GetReceiveCapacity(target, res);
            while (freeCapacity > 0)
            {
                auto [avail, r] = prod.outputBuffers[res].GetResource();
                if (!avail)
                    break;

                Log::Msg(self.tag, "ID: ", self.id, " ", rt2s(r->type),
                         " transport started to ", target->name, " with ID ", target->id);
                if (!self.owner->BeginTransport(&self, target, r))
                {
                    prod.outputBuffers[res].AddResource(r);
                    break;
                }
                freeCapacity--;
            }
        }
    }
}

int LogisticsComponent::HandleTransportFrom(ResourceType type, int amount, Building* receiver,
                                              Building& self, ProductionComponent& prod)
{
    int sent = 0;
    int requested = std::min(amount, GetReceiveCapacity(receiver, type));
    for (int i = 0; i < requested; i++)
    {
        auto [avail, res] = prod.outputBuffers[type].GetResource();
        if (avail)
        {
            if (receiver == nullptr || !receiver->CanReceiveResource(type))
            {
                prod.outputBuffers[type].AddResource(res);
                break;
            }
            Log::Msg(self.tag, "ID: ", self.id, " ", rt2s(res->type),
                     " transport started to ", receiver->name, " with ID ", receiver->id);
            if (self.owner->BeginTransport(&self, receiver, res))
                sent++;
            else
            {
                prod.outputBuffers[type].AddResource(res);
                break;
            }
        }
    }
    return sent;
}

std::vector<BuildingConnectionView> LogisticsComponent::GetSupplierViews(
    const ProductionComponent& prod) const
{
    std::vector<BuildingConnectionView> result;
    for (const auto& [res, amount] : prod.ingredients)
    {
        auto it = suppliers.find(res);
        if (it == suppliers.end() || it->second.empty())
        {
            result.push_back({res, nullptr});
            continue;
        }
        for (auto* sup : it->second)
            result.push_back({res, sup});
    }
    return result;
}

std::vector<BuildingConnectionView> LogisticsComponent::GetReceiverViews(
    const ProductionComponent& prod) const
{
    std::vector<BuildingConnectionView> result;
    for (const auto& [res, amount] : prod.products)
    {
        auto it = receivers.find(res);
        result.push_back({res, it != receivers.end() ? it->second : nullptr, false});
        auto alt = altReceivers.find(res);
        if (alt != altReceivers.end() && alt->second != nullptr &&
            (it == receivers.end() || alt->second != it->second))
            result.push_back({res, alt->second, true});
    }
    return result;
}

