#ifndef BUILDING_COMPONENTS_INTERNAL_H
#define BUILDING_COMPONENTS_INTERNAL_H

#include "economy/Building.h"

// Private helpers shared by two or more BuildingComponents translation units
// (LogisticsComponent.cpp, StorageComponent.cpp, PopulationComponent.cpp).
// Not part of the public inc/ API — implementation detail of the component
// split (ETAP 8.2), same role as src/ui/GuiInternal.h for the GUI split.

// Counts resource units already in transit toward `target` (so callers don't
// over-request capacity that's already spoken for).
int CountIncomingResources(Building* target, ResourceType type);

// Free capacity `target` can still accept for `type`, net of anything already
// incoming.
int GetReceiveCapacity(Building* target, ResourceType type);

#endif
