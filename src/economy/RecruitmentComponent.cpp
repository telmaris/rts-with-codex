#include "economy/Building.h"
#include "economy/Player.h"
#include "warfare/BattleUnit.h"
#include "warfare/UnitDefinition.h"

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

    if (self.owner->strategicResources.Get(StrategicResourceType::Manpower) < def->manpowerCost)
        return false;

    // All checks passed — deduct everything up front, then queue the timed build.
    for (const auto& cost : def->cost)
        for (int i = 0; i < cost.amount; i++)
            storage->buffers[cost.type].FreeResource();
    self.owner->strategicResources.Consume(StrategicResourceType::Manpower, def->manpowerCost);

    queue.push_back(RecruitmentQueueEntry{unitDefId, def->recruitTime, def->recruitTime});
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
