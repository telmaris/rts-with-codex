#include "economy/Building.h"
#include "economy/Player.h"
#include "warfare/BattleUnit.h"
#include "warfare/UnitDefinition.h"

#include <algorithm>

bool RecruitmentComponent::QueueRecruitment(Building& self, const std::string& unitDefId)
{
    if (self.owner == nullptr)
        return false;

    const UnitDefinition* def = FindUnitDefinition(unitDefId);
    if (def == nullptr || def->recruitBuilding != self.buildingType)
        return false;

    auto* storage = self.GetComponent<StorageComponent>();
    if (storage == nullptr)
        return false;

    for (const auto& cost : def->cost)
    {
        auto it = storage->buffers.find(cost.type);
        if (it == storage->buffers.end() || static_cast<int>(it->second.buffer.size()) < cost.amount)
            return false;
    }

    // TD(etap-9): recruit time/manpower cost stay tunable via tech/focus so
    // planning an attack (queue now, unit arrives later) remains a real
    // strategic decision — floored so a multiplier can never make recruitment
    // instant or free.
    double manpowerCost = std::max(0.0,
        self.owner->ModifyBalanceForUnit(BalanceStat::UnitRecruitManpowerCost, def->manpowerCost, unitDefId));
    double recruitTime = std::max(1.0,
        self.owner->ModifyBalanceForUnit(BalanceStat::UnitRecruitTime, def->recruitTime, unitDefId));

    if (self.owner->strategicResources.Get(StrategicResourceType::Manpower) < manpowerCost)
        return false;

    // All checks passed — deduct everything up front, then queue the timed build.
    for (const auto& cost : def->cost)
        for (int i = 0; i < cost.amount; i++)
            storage->buffers[cost.type].FreeResource();
    self.owner->strategicResources.Consume(StrategicResourceType::Manpower, manpowerCost);

    queue.push_back(RecruitmentQueueEntry{unitDefId, recruitTime, recruitTime});
    return true;
}

void RecruitmentComponent::Update(Building& self, double dt)
{
    if (queue.empty() || self.owner == nullptr)
        return;

    RecruitmentQueueEntry& front = queue.front();
    front.remaining -= dt;
    if (front.remaining > 0.0)
        return;

    int instanceId = self.owner->id * 100000 + self.owner->nextUnitInstanceId++;
    BattleUnit unit(instanceId, self.owner->id, front.unitDefId);
    unit.currentHp = unit.GetEffectiveMaxHp(*self.owner);
    self.owner->roster.AddUnit(std::move(unit));

    queue.pop_front();
}
