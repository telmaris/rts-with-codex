#ifndef BALANCE_MODIFIERS_H
#define BALANCE_MODIFIERS_H

#include "BalanceStats.h"
#include "Building.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

enum class BalanceModifierScopeType
{
    GlobalPlayer,
    Building,
    Area,
    Territory
};

struct BalanceModifierScope
{
    BalanceModifierScopeType type{BalanceModifierScopeType::GlobalPlayer};
    std::optional<int> buildingId;
    std::optional<int> positionId;
    std::optional<Vec2i> center;
    int radius{0};

    static BalanceModifierScope Global()
    {
        return {};
    }

    static BalanceModifierScope Building(int targetBuildingId)
    {
        BalanceModifierScope scope;
        scope.type = BalanceModifierScopeType::Building;
        scope.buildingId = targetBuildingId;
        return scope;
    }

    static BalanceModifierScope BuildingAtPosition(int targetPositionId)
    {
        BalanceModifierScope scope;
        scope.type = BalanceModifierScopeType::Building;
        scope.positionId = targetPositionId;
        return scope;
    }

    static BalanceModifierScope Area(Vec2i areaCenter, int areaRadius)
    {
        BalanceModifierScope scope;
        scope.type = BalanceModifierScopeType::Area;
        scope.center = areaCenter;
        scope.radius = std::max(0, areaRadius);
        return scope;
    }

    static BalanceModifierScope Territory()
    {
        BalanceModifierScope scope;
        scope.type = BalanceModifierScopeType::Territory;
        return scope;
    }
};

struct BalanceModifierContext
{
    BalanceStat stat{BalanceStat::BuildTime};
    BuildingType buildingType{BuildingType::Building};
    ResourceType resourceType{ResourceType::Null};
    std::optional<MilitaryUnitType> unitType;
    std::optional<Vec2i> position;
    std::optional<int> buildingId;
    std::optional<int> positionId;
    bool ownedTerritory{false};
};

struct BalanceModifier
{
    BalanceStat stat{BalanceStat::BuildTime};
    double additive{0.0};
    double multiplier{1.0};
    BalanceModifierScope scope;
    std::optional<BuildingType> buildingType;
    std::optional<ResourceType> resourceType;
    std::optional<MilitaryUnitType> unitType;
    std::string source;

    bool AppliesTo(const BalanceModifierContext& context) const
    {
        if (stat != context.stat)
            return false;
        if (buildingType.has_value() && buildingType.value() != context.buildingType)
            return false;
        if (resourceType.has_value() && resourceType.value() != context.resourceType)
            return false;
        if (unitType.has_value() && (!context.unitType.has_value() || unitType.value() != context.unitType.value()))
            return false;
        return AppliesToScope(context);
    }

    bool AppliesToScope(const BalanceModifierContext& context) const
    {
        switch (scope.type)
        {
            case BalanceModifierScopeType::GlobalPlayer:
                return true;

            case BalanceModifierScopeType::Building:
                if (scope.buildingId.has_value())
                    return context.buildingId.has_value() && context.buildingId.value() == scope.buildingId.value();
                if (scope.positionId.has_value())
                    return context.positionId.has_value() && context.positionId.value() == scope.positionId.value();
                return false;

            case BalanceModifierScopeType::Area:
            {
                if (!scope.center.has_value() || !context.position.has_value())
                    return false;

                int dx = context.position->x - scope.center->x;
                int dy = context.position->y - scope.center->y;
                return dx * dx + dy * dy <= scope.radius * scope.radius;
            }

            case BalanceModifierScopeType::Territory:
                return context.ownedTerritory;
        }

        return false;
    }
};

class IBalanceModifierSource
{
public:
    virtual ~IBalanceModifierSource() = default;
    virtual void CollectModifiers(std::vector<BalanceModifier>& out) const = 0;
};

class BalanceModifierSet
{
public:
    void AddModifier(BalanceModifier modifier)
    {
        modifiers.push_back(std::move(modifier));
    }

    void ClearSource(const std::string& source)
    {
        modifiers.erase(
            std::remove_if(modifiers.begin(), modifiers.end(), [&](const BalanceModifier& modifier)
            {
                return modifier.source == source;
            }),
            modifiers.end());
    }

    // Removes all modifiers emitted by a group of sources.
    void ClearSourcePrefix(const std::string& sourcePrefix)
    {
        modifiers.erase(
            std::remove_if(modifiers.begin(), modifiers.end(), [&](const BalanceModifier& modifier)
            {
                return modifier.source.rfind(sourcePrefix, 0) == 0;
            }),
            modifiers.end());
    }

    void Clear()
    {
        modifiers.clear();
    }

    double ModifyDouble(double base, const BalanceModifierContext& context) const
    {
        double additive = 0.0;
        double multiplier = 1.0;
        for (const auto& modifier : modifiers)
        {
            if (!modifier.AppliesTo(context))
                continue;

            additive += modifier.additive;
            multiplier *= modifier.multiplier;
        }

        return std::max(0.0, (base + additive) * multiplier);
    }

    int ModifyInt(int base, const BalanceModifierContext& context, int minimum = 0) const
    {
        return std::max(minimum, static_cast<int>(std::round(ModifyDouble(static_cast<double>(base), context))));
    }

    const std::vector<BalanceModifier>& GetModifiers() const
    {
        return modifiers;
    }

private:
    std::vector<BalanceModifier> modifiers;
};

#endif
