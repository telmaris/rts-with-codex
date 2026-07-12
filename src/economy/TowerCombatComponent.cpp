#include "economy/BuildingComponents.h"
#include "economy/Player.h"
#include "BuildingComponentsInternal.h"

double TowerCombatComponent::GetModifiedDamage(const Building& self) const
{
    return self.owner != nullptr ? self.owner->ResolveStat(damage, &self) : damage.GetBase();
}

double TowerCombatComponent::GetModifiedRange(const Building& self) const
{
    return self.owner != nullptr ? self.owner->ResolveStat(range, &self) : range.GetBase();
}

double TowerCombatComponent::GetModifiedAttackSpeed(const Building& self) const
{
    return self.owner != nullptr ? self.owner->ResolveStat(attackSpeed, &self) : attackSpeed.GetBase();
}

int TowerCombatComponent::GetModifiedAmmoPerShot(const Building& self) const
{
    return self.owner != nullptr ? self.owner->ResolveStat(ammoPerShot, &self, ammoResource, 0)
                                  : ammoPerShot.GetBase();
}

void TowerCombatComponent::Update(Building& self, double dt)
{
    (void)dt;
    auto* storage = self.GetComponent<StorageComponent>();
    auto* logistics = self.GetComponent<LogisticsComponent>();
    if (storage == nullptr || logistics == nullptr)
        return;

    auto bufferIt = storage->buffers.find(ammoResource);
    if (bufferIt == storage->buffers.end())
        return;

    // Same "top up to capacity, net of what's already incoming" formula as
    // LogisticsComponent::MaintainRequests, minus the ProductionComponent
    // coupling that method requires — a tower has no recipe, just one
    // buffer to keep full.
    int stored = static_cast<int>(bufferIt->second.buffer.size());
    int pending = CountIncomingResources(&self, ammoResource);
    logistics->pendingRequests[ammoResource] = pending;
    int missing = bufferIt->second.bufferSize - stored - pending;
    if (missing > 0)
        logistics->RequestResource(ammoResource, missing, self);
}
