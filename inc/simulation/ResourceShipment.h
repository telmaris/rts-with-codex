#ifndef RESOURCE_SHIPMENT_H
#define RESOURCE_SHIPMENT_H

#include "data/Resource.h"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

// State owned by the value-semantic shipment record. The legacy
// Transportable remains the gameplay payload during the incremental migration;
// this record is deliberately independent of buildings, players, tile maps,
// and road-network pointers.
enum class ResourceShipmentState : std::uint8_t
{
    InTransit,
    HandedOff,
    Cancelled,
    Completed
};

struct ResourceShipment
{
    ShipmentId id{0};
    ResourceType type{ResourceType::Null};
    int quantity{1};

    // Stable domain identifiers. These are intentionally not object pointers.
    int sourceBuildingId{-1};
    int targetBuildingId{-1};

    std::vector<int> pathTileIds;
    int currentPathStep{0};
    double elapsedTime{0.0};
    double transportTime{0.0};
    ResourceShipmentState state{ResourceShipmentState::InTransit};
};

// Deterministic value index used by RoadNetwork while the pointer-based
// carrier lists are still in service. It is intentionally small: the next
// migration stage can move ownership here without changing the public API.
class ResourceShipmentIndex
{
public:
    bool Insert(ResourceShipment shipment)
    {
        if (shipment.id == 0 || shipment.type == ResourceType::Null || shipment.quantity <= 0)
            return false;
        return shipments.emplace(shipment.id, std::move(shipment)).second;
    }

    ResourceShipment* Find(ShipmentId id)
    {
        auto it = shipments.find(id);
        return it == shipments.end() ? nullptr : &it->second;
    }

    const ResourceShipment* Find(ShipmentId id) const
    {
        auto it = shipments.find(id);
        return it == shipments.end() ? nullptr : &it->second;
    }

    bool Erase(ShipmentId id)
    {
        return shipments.erase(id) != 0;
    }

    void Clear() noexcept
    {
        shipments.clear();
    }

    std::size_t Size() const noexcept
    {
        return shipments.size();
    }

private:
    std::map<ShipmentId, ResourceShipment> shipments;
};

#endif
