#include "economy/BuildingComponents.h"
#include "economy/Player.h"

double HqComponent::GetModifiedMaxHp(const Building& self) const
{
    return self.owner != nullptr ? self.owner->ResolveStat(maxHp, &self) : maxHp.GetBase();
}

double HqComponent::GetModifiedHardDefense(const Building& self) const
{
    return self.owner != nullptr ? self.owner->ResolveStat(hardDefense, &self) : hardDefense.GetBase();
}

double HqComponent::GetModifiedThornsDamage(const Building& self) const
{
    return self.owner != nullptr ? self.owner->ResolveStat(thornsDamage, &self) : thornsDamage.GetBase();
}
