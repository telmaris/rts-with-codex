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

    // T3 fix (docs/post_pivot_audit_2026-07-12.md): a tower's ammo buffer and
    // Barracks' unit-cost buffers exist purely as demand sinks for this
    // building's own consumption (TowerCombatComponent/RecruitmentComponent),
    // not a general warehouse to redistribute from. Without this guard, the
    // moment ammo/costs arrive they get auto-pushed straight back out to the
    // nearest other building that also accepts the type (e.g. the warehouse
    // they just came from) — a real, reproducible bounce that never lets
    // ammo/costs actually accumulate for use.
    if (self.HasComponent<TowerCombatComponent>() || self.HasComponent<RecruitmentComponent>())
        return;

    // Determinism audit (docs/work_plan_2026-07-13.md, pre-Block-C): with 2+
    // valid receivers competing for a finite buffer, whichever one is visited
    // first drains it — so iteration order is simulation-visible and must not
    // depend on Building* heap addresses (same bug class as the main
    // per-tick building loop / PrimitiveAIModel::TryBuildRoads). Sort once
    // per Update() call rather than per-resource-type.
    std::vector<Building*> sortedReceivers(self.owner->GetTrackedBuildings().begin(), self.owner->GetTrackedBuildings().end());
    std::sort(sortedReceivers.begin(), sortedReceivers.end(), [](Building* a, Building* b) { return a->id < b->id; });

    std::vector<Building*> visitedReceivers;
    for (auto& [res, buf] : buffers)
    {
        if (buf.buffer.empty())
            continue;

        visitedReceivers.clear();
        // OPTIMIZATION: Iterate tracked buildings instead of full tilemap (~100 vs 1M tiles).
        for (Building* receiver : sortedReceivers)
        {
            if (receiver == nullptr || receiver == &self)
                continue;
            if (std::find(visitedReceivers.begin(), visitedReceivers.end(), receiver) != visitedReceivers.end())
                continue;
            visitedReceivers.push_back(receiver);
            // User report (docs/work_plan_2026-07-13.md, 2026-07-15): a Barracks
            // has buffer room from the moment it's built, so this ambient
            // "push to anyone who'll accept" scan was delivering unit-cost
            // resources to it starting turn one, regardless of whether the
            // player had ever ordered a unit. Recruitment costs now move only
            // via RecruitmentComponent's own explicit RequestResource call
            // (RecruitmentComponent::QueueRecruitment) — never via this
            // ambient distribution.
            if (receiver->HasComponent<RecruitmentComponent>())
                continue;
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

