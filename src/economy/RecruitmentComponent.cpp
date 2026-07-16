#include "economy/Building.h"
#include "economy/Player.h"
#include "warfare/BattleUnit.h"
#include "warfare/UnitDefinition.h"
#include "BuildingComponentsInternal.h"

#include <algorithm>

namespace
{
    // Checks this building's own buffer for every cost in `def`; if it's all
    // there, consumes it and returns true. Pure check-and-consume — never
    // requests anything, so callers stay free of transport side effects.
    bool TryConsumeCostFromBuffer(StorageComponent& storage, const UnitDefinition& def)
    {
        for (const auto& cost : def.cost)
        {
            auto it = storage.buffers.find(cost.type);
            int have = (it != storage.buffers.end()) ? static_cast<int>(it->second.buffer.size()) : 0;
            if (have < cost.amount)
                return false;
        }

        for (const auto& cost : def.cost)
            for (int i = 0; i < cost.amount; i++)
                storage.buffers[cost.type].FreeResource();
        return true;
    }

    // Requests exactly what's still missing for one unit's cost — buffered and
    // in-flight deliveries both count as covered, so calling this every tick
    // never re-orders what's already on the road (the over-request bug this
    // replaced: RequestResource dispatches physical transport immediately and
    // does NOT net out incoming, unlike MaintainRequests).
    void RequestMissingCost(Building& self, StorageComponent& storage, LogisticsComponent& logistics,
                            const UnitDefinition& def)
    {
        for (const auto& cost : def.cost)
        {
            auto it = storage.buffers.find(cost.type);
            int have = (it != storage.buffers.end()) ? static_cast<int>(it->second.buffer.size()) : 0;
            int missing = cost.amount - have - CountIncomingResources(&self, cost.type);
            if (missing > 0)
                logistics.RequestResource(cost.type, missing, self);
        }
    }
}

bool RecruitmentComponent::QueueRecruitment(Building& self, const std::string& unitDefId)
{
    if (self.owner == nullptr)
        return false;

    const UnitDefinition* def = FindUnitDefinition(unitDefId);
    if (def == nullptr || def->recruitBuilding != self.buildingType)
        return false;

    auto* storage = self.GetComponent<StorageComponent>();
    auto* logistics = self.GetComponent<LogisticsComponent>();
    if (storage == nullptr || logistics == nullptr)
        return false;

    // TD(etap-9): recruit time/manpower cost stay tunable via tech/focus so
    // planning an attack (queue now, unit arrives later) remains a real
    // strategic decision — floored so a multiplier can never make recruitment
    // instant or free.
    double manpowerCost = std::max(0.0,
        self.owner->ModifyBalanceForUnit(BalanceStat::UnitRecruitManpowerCost, def->manpowerCost, unitDefId));
    double recruitTime = std::max(1.0,
        self.owner->ModifyBalanceForUnit(BalanceStat::UnitRecruitTime, def->recruitTime, unitDefId));

    // Manpower is the only hard gate — it's a global pool, not something
    // that physically travels, so there's nothing to wait on. Resources are
    // different (see below): an order joins the queue immediately either way.
    if (self.owner->strategicResources.Get(StrategicResourceType::Manpower) < manpowerCost)
        return false;
    self.owner->strategicResources.Consume(StrategicResourceType::Manpower, manpowerCost);

    // User request (docs/work_plan_2026-07-13.md, 2026-07-15): clicking
    // recruit queues the order right away, tagged "waiting for resources" if
    // its cost isn't already sitting in this Barracks' own buffer. Strict
    // FIFO: a new entry may only consume from the buffer when no earlier
    // entry is still waiting — otherwise it would steal resources an older
    // order is queued for. Requesting the shortfall is Update's job (next
    // tick, in-flight-aware), not done here.
    bool eligible = std::none_of(queue.begin(), queue.end(),
        [](const RecruitmentQueueEntry& e) { return !e.resourcesReady; });
    bool resourcesReady = eligible && TryConsumeCostFromBuffer(*storage, *def);
    queue.push_back(RecruitmentQueueEntry{unitDefId, recruitTime, recruitTime, resourcesReady});
    return true;
}

std::string RecruitmentComponent::DiagnoseRecruitmentBlock(const Building& self, const std::string& unitDefId) const
{
    if (self.owner == nullptr)
        return "No owner";

    const UnitDefinition* def = FindUnitDefinition(unitDefId);
    if (def == nullptr || def->recruitBuilding != self.buildingType)
        return "Not recruitable here";

    // Deliberately a GLOBAL scan, unlike QueueRecruitment's own local-buffer
    // check (see BuildingComponents.h for why) — sums each cost across every
    // storage-like building the player owns, same shape as
    // Player::HasBuildResources.
    std::vector<std::string> reasons;
    for (const auto& cost : def->cost)
    {
        int have = 0;
        for (const auto* building : self.owner->GetTrackedBuildingsWithComponent<StorageComponent>())
        {
            const auto* storage = building != nullptr ? building->GetComponent<StorageComponent>() : nullptr;
            if (storage == nullptr || building->owner != self.owner)
                continue;
            auto it = storage->buffers.find(cost.type);
            if (it != storage->buffers.end())
                have += static_cast<int>(it->second.buffer.size());
        }
        if (have < cost.amount)
            reasons.push_back("Missing " + std::to_string(cost.amount - have) + " " + rt2s(cost.type));
    }

    double manpowerCost = std::max(0.0,
        self.owner->ModifyBalanceForUnit(BalanceStat::UnitRecruitManpowerCost, def->manpowerCost, unitDefId));
    if (self.owner->strategicResources.Get(StrategicResourceType::Manpower) < manpowerCost)
    {
        reasons.push_back("Not enough manpower (" +
            std::to_string(static_cast<int>(self.owner->strategicResources.Get(StrategicResourceType::Manpower))) +
            "/" + std::to_string(static_cast<int>(manpowerCost)) + ")");
    }

    std::string result;
    for (size_t i = 0; i < reasons.size(); i++)
    {
        if (i > 0)
            result += "; ";
        result += reasons[i];
    }
    return result;
}

void RecruitmentComponent::Update(Building& self, double dt)
{
    if (queue.empty() || self.owner == nullptr)
        return;

    // Readiness pass — strict FIFO: entries flip resourcesReady (consuming
    // their cost from the buffer) in queue order as deliveries land, so the
    // GUI's "Waiting for resources" label clears the moment an entry's cost
    // is physically here, even while an earlier entry still trains. Only the
    // FIRST waiting entry places a request, and only for what's neither
    // buffered nor already on the road — one unit's cost at a time.
    auto* storage = self.GetComponent<StorageComponent>();
    auto* logistics = self.GetComponent<LogisticsComponent>();
    if (storage != nullptr && logistics != nullptr)
    {
        for (auto& entry : queue)
        {
            if (entry.resourcesReady)
                continue;
            const UnitDefinition* def = FindUnitDefinition(entry.unitDefId);
            if (def == nullptr)
                break;
            if (TryConsumeCostFromBuffer(*storage, *def))
            {
                entry.resourcesReady = true;
                continue;
            }
            RequestMissingCost(self, *storage, *logistics, *def);
            break;
        }
    }

    RecruitmentQueueEntry& front = queue.front();
    if (!front.resourcesReady)
        return;

    front.remaining -= dt;
    if (front.remaining > 0.0)
        return;

    int instanceId = self.owner->id * 100000 + self.owner->nextUnitInstanceId++;
    BattleUnit unit(instanceId, self.owner->id, front.unitDefId);
    unit.currentHp = unit.GetEffectiveMaxHp(*self.owner);
    self.owner->roster.AddUnit(std::move(unit));

    queue.pop_front();
}
