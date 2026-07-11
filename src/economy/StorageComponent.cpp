#include "economy/Building.h"
#include "economy/Player.h"
#include "simulation/MapGenerator.h"
#include "BuildingComponentsInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

// ─── StorageComponent ────────────────────────────────────────────────────────

bool StorageComponent::CanAccept(ResourceType type) const
{
    return buffers.contains(type);
}

bool StorageComponent::CanReceive(ResourceType type) const
{
    auto it = buffers.find(type);
    return it != buffers.end() &&
           static_cast<int>(it->second.buffer.size()) < it->second.bufferSize;
}

void StorageComponent::AddResource(Resource* res, Building& self)
{
    if (res == nullptr)
        return;

    auto it = buffers.find(res->type);
    if (it == buffers.end() || static_cast<int>(it->second.buffer.size()) >= it->second.bufferSize)
    {
        if (res->sourceBuilding != nullptr)
            res->sourceBuilding->ReturnOutgoingResource(res);
        return;
    }

    Log::Msg(self.tag, "resource added!");
    it->second.AddResource(res);
}

void StorageComponent::ReturnOutgoingResource(Resource* res)
{
    if (res == nullptr)
        return;
    buffers[res->type].AddResource(res);
}

Resource StorageComponent::GetResource(ResourceType type)
{
    auto [avail, res] = buffers[type].GetResource();
    return avail ? *res : Resource{};
}

int StorageComponent::HandleTransport(ResourceType type, int amount, Building* receiver,
                                       Building& self)
{
    int sent = 0;
    int requested = std::min(amount, GetReceiveCapacity(receiver, type));
    for (int i = 0; i < requested; i++)
    {
        auto [avail, res] = buffers[type].GetResource();
        if (avail)
        {
            if (receiver == nullptr || !receiver->CanReceiveResource(type))
            {
                buffers[type].AddResource(res);
                break;
            }
            // Debug-level: "transport started" is too noisy (spam when retrying without roads)
            if (self.owner->BeginTransport(&self, receiver, res))
                sent++;
            else
            {
                buffers[type].AddResource(res);
                break;
            }
        }
    }
    return sent;
}

void StorageComponent::Update(Building& self, double dt)
{
    if (self.owner == nullptr)
        return;

    std::vector<Building*> visitedReceivers;
    for (auto& [res, buf] : buffers)
    {
        if (buf.buffer.empty())
            continue;

        visitedReceivers.clear();
        // OPTIMIZATION: Iterate tracked buildings instead of full tilemap (~100 vs 1M tiles).
        for (Building* receiver : self.owner->GetTrackedBuildings())
        {
            if (receiver == nullptr || receiver == &self)
                continue;
            if (std::find(visitedReceivers.begin(), visitedReceivers.end(), receiver) != visitedReceivers.end())
                continue;
            visitedReceivers.push_back(receiver);
            if (!receiver->CanAcceptResource(res))
                continue;

            int free = GetReceiveCapacity(receiver, res);
            if (free <= 0)
                continue;

            HandleTransport(res, free, receiver, self);
            if (buf.buffer.empty())
                break;
        }
    }
}

std::vector<ResourceBufferView> StorageComponent::GetBufferViews() const
{
    std::vector<ResourceBufferView> result;
    for (const auto& [res, buf] : buffers)
        result.push_back({res, static_cast<int>(buf.buffer.size()), buf.bufferSize, 0});
    return result;
}

