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

// ─── WorkerComponent ─────────────────────────────────────────────────────────

float WorkerComponent::GetRatio() const
{
    int cap = capacity.GetBase();
    if (cap <= 0) return 1.0f;
    return std::clamp(assigned / static_cast<float>(cap), 0.0f, 1.0f);
}

int WorkerComponent::GetModifiedCapacity(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(capacity, &self, ResourceType::Null, std::nullopt, 0)
        : capacity.GetBase();
}

