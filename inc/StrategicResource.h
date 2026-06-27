#ifndef STRATEGIC_RESOURCE_H
#define STRATEGIC_RESOURCE_H

#include "Utils.h"

// Non-tile resources tracked at player or faction level.
enum class StrategicResourceType : int
{
    Manpower = 0,
    Workers = 1,
    Soldiers = 2,
    FoodPackages = 3,
    SupplyPackages = 4,
    Weapons = 5
};

// Converts strategic resource type to a readable UI label.
inline std::string StrategicResourceName(StrategicResourceType type)
{
    switch (type)
    {
        case StrategicResourceType::Manpower: return "Manpower";
        case StrategicResourceType::Workers: return "Workers";
        case StrategicResourceType::Soldiers: return "Soldiers";
        case StrategicResourceType::FoodPackages: return "Food packages";
        case StrategicResourceType::SupplyPackages: return "Supply packages";
        case StrategicResourceType::Weapons: return "Weapons";
        default: return "Unknown";
    }
}

// Numeric store for abstract strategic resources such as manpower and supplies.
class StrategicResourcePool
{
public:
    // Adds amount to one strategic resource.
    void Add(StrategicResourceType type, double amount)
    {
        values[type] += amount;
    }

    // Consumes amount when available and returns whether it succeeded.
    bool Consume(StrategicResourceType type, double amount)
    {
        if (values[type] < amount)
            return false;

        values[type] -= amount;
        return true;
    }

    // Returns current amount of one strategic resource.
    double Get(StrategicResourceType type) const
    {
        auto it = values.find(type);
        return it != values.end() ? it->second : 0.0;
    }

    // Clamps one strategic resource to a specific value.
    void Set(StrategicResourceType type, double amount)
    {
        values[type] = std::max(0.0, amount);
    }

    std::map<StrategicResourceType, double> values;
};

#endif
