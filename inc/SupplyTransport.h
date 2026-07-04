#ifndef SUPPLY_TRANSPORT_H
#define SUPPLY_TRANSPORT_H

#include "Transport.h"
#include "SupplyPackage.h"

// A supply package in transit over the road network (Phase B). Unlike Resource,
// packages are not pool-allocated — ownership stays with the SupplyHub that
// launched them (SupplyPackageComponent::inFlight holds the unique_ptr; only a
// raw observing pointer travels through Building::transportables). On arrival
// Building::ReceptTransport applies the payload and flags `delivered`; if the
// road network cancels the transport (owner change, missing building, capacity)
// it flags `cancelled` instead so the hub can requeue the payload rather than
// leak it. See docs/war_system_phase2_design.md (Phase B, task B5).
struct SupplyPackageTransportable : Transportable
{
    SupplyPackage payload;
    bool delivered{false};
    bool cancelled{false};
};

#endif
