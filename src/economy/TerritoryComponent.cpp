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

// ─── TerritoryComponent ──────────────────────────────────────────────────────

int TerritoryComponent::GetRadius(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(radius, &self, ResourceType::Null, std::nullopt, 0)
        : radius.GetBase();
}

int TerritoryComponent::GetMaxHp(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(maxHp, &self, ResourceType::Null, std::nullopt, 1)
        : maxHp.GetBase();
}

void TerritoryComponent::ReceiveDamage(int damage)
{
    hp = std::max(0, hp - std::max(0, damage));
}

