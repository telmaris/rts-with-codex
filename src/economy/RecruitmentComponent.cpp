#include "economy/Building.h"
#include "economy/Player.h"
#include "warfare/BattleUnit.h"
#include "warfare/UnitDefinition.h"

#include <algorithm>

namespace
{
    // Checks this building's own buffer for every cost in `def`; if it's all
    // there, consumes it and returns true. If anything is short, requests the
    // shortfall from the wired supplier (same RequestResource a
    // ProductionComponent uses for its inputs) and returns false without
    // consuming anything — safe to call repeatedly each tick while an order
    // waits, since RequestResource itself is a no-op once nothing is missing.
    bool TryConsumeUnitCost(Building& self, StorageComponent& storage, LogisticsComponent& logistics,
                            const UnitDefinition& def)
    {
        bool allAvailable = true;
        for (const auto& cost : def.cost)
        {
            auto it = storage.buffers.find(cost.type);
            int have = (it != storage.buffers.end()) ? static_cast<int>(it->second.buffer.size()) : 0;
            if (have < cost.amount)
            {
                allAvailable = false;
                logistics.RequestResource(cost.type, cost.amount - have, self);
            }
        }
        if (!allAvailable)
            return false;

        for (const auto& cost : def.cost)
            for (int i = 0; i < cost.amount; i++)
                storage.buffers[cost.type].FreeResource();
        return true;
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
    // its cost isn't already sitting in this Barracks' own buffer — the
    // build timer only starts once RecruitmentComponent::Update sees the
    // front entry's cost fully arrive (TryConsumeUnitCost, requesting the
    // shortfall from the wired supplier each tick until then).
    bool resourcesReady = TryConsumeUnitCost(self, *storage, *logistics, *def);
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

    RecruitmentQueueEntry& front = queue.front();

    if (!front.resourcesReady)
    {
        // Still waiting on the delivery QueueRecruitment kicked off (or a
        // fresh request each tick if it never fully landed) — the build
        // timer stays frozen until this resolves.
        const UnitDefinition* def = FindUnitDefinition(front.unitDefId);
        auto* storage = self.GetComponent<StorageComponent>();
        auto* logistics = self.GetComponent<LogisticsComponent>();
        if (def == nullptr || storage == nullptr || logistics == nullptr)
            return;
        if (!TryConsumeUnitCost(self, *storage, *logistics, *def))
            return;
        front.resourcesReady = true;
    }

    front.remaining -= dt;
    if (front.remaining > 0.0)
        return;

    int instanceId = self.owner->id * 100000 + self.owner->nextUnitInstanceId++;
    BattleUnit unit(instanceId, self.owner->id, front.unitDefId);
    unit.currentHp = unit.GetEffectiveMaxHp(*self.owner);
    self.owner->roster.AddUnit(std::move(unit));

    queue.pop_front();
}
