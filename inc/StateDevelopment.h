#ifndef STATE_DEVELOPMENT_H
#define STATE_DEVELOPMENT_H

#include "BalanceModifiers.h"
#include "raylib.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

enum class StateDevelopmentLevel
{
    TribalSociety,
    Chiefdom,
    Kingdom,
    AristocraticState
};

struct StateDevelopmentDefinition
{
    StateDevelopmentLevel level{StateDevelopmentLevel::TribalSociety};
    std::string id;
    std::string name;
    std::string description;
    Color color{180, 190, 205, 255};
    std::vector<BalanceModifier> modifiers;
};

inline const std::vector<StateDevelopmentDefinition>& GetStateDevelopmentDefinitions()
{
    static const std::vector<StateDevelopmentDefinition> definitions{
        StateDevelopmentDefinition{
            StateDevelopmentLevel::TribalSociety,
            "tribal_society",
            "Tribal Society",
            "Loose settlement authority with local obligations and limited central administration.",
            Color{190, 76, 72, 255},
            {}},
        StateDevelopmentDefinition{
            StateDevelopmentLevel::Chiefdom,
            "chiefdom",
            "Chiefdom",
            "A recognized ruling center coordinates villages and early military obligations.",
            Color{220, 132, 58, 255},
            {
                BalanceModifier{BalanceStat::PopulationCap, 6.0, 1.0, BalanceModifierScope::Global(), BuildingType::Village, std::nullopt, std::nullopt, "state:chiefdom"},
                BalanceModifier{BalanceStat::GarrisonCapacity, 2.0, 1.0, BalanceModifierScope::Global(), BuildingType::GuardTower, std::nullopt, std::nullopt, "state:chiefdom"}
            }},
        StateDevelopmentDefinition{
            StateDevelopmentLevel::Kingdom,
            "kingdom",
            "Kingdom",
            "A stable crown can coordinate construction, roads and military logistics at realm scale.",
            Color{118, 176, 108, 255},
            {
                BalanceModifier{BalanceStat::BuildTime, 0.0, 0.97, BalanceModifierScope::Global(), std::nullopt, std::nullopt, std::nullopt, "state:kingdom"},
                BalanceModifier{BalanceStat::RoadCapacity, 1.0, 1.0, BalanceModifierScope::Global(), BuildingType::Road, std::nullopt, std::nullopt, "state:kingdom"}
            }},
        StateDevelopmentDefinition{
            StateDevelopmentLevel::AristocraticState,
            "aristocratic_state",
            "Aristocratic State",
            "A landed elite and formal administration improve coordination but expect heavier state structure.",
            Color{92, 166, 216, 255},
            {
                BalanceModifier{BalanceStat::ProductionCycleTime, 0.0, 0.97, BalanceModifierScope::Global(), std::nullopt, std::nullopt, std::nullopt, "state:aristocratic_state"},
                BalanceModifier{BalanceStat::RecruitmentTime, 0.0, 0.95, BalanceModifierScope::Global(), BuildingType::Barracks, std::nullopt, std::nullopt, "state:aristocratic_state"}
            }}
    };
    return definitions;
}

class StateDevelopment
{
public:
    void RefreshFromGovernmentIds(const std::set<std::string>& unlockedGovernmentIds)
    {
        currentLevel = StateDevelopmentLevel::TribalSociety;
        for (const auto& definition : GetStateDevelopmentDefinitions())
        {
            if (definition.id == "tribal_society" || unlockedGovernmentIds.contains(definition.id))
                currentLevel = definition.level;
        }
    }

    const StateDevelopmentDefinition& GetDefinition() const
    {
        for (const auto& definition : GetStateDevelopmentDefinitions())
            if (definition.level == currentLevel)
                return definition;
        return GetStateDevelopmentDefinitions().front();
    }

    static const StateDevelopmentDefinition* FindDefinition(const std::string& id)
    {
        for (const auto& definition : GetStateDevelopmentDefinitions())
            if (definition.id == id)
                return &definition;
        return nullptr;
    }

    StateDevelopmentLevel GetLevel() const
    {
        return currentLevel;
    }

    void CollectModifiers(BalanceModifierSet& target) const
    {
        for (auto modifier : GetDefinition().modifiers)
            target.AddModifier(std::move(modifier));
    }

private:
    StateDevelopmentLevel currentLevel{StateDevelopmentLevel::TribalSociety};
};

#endif
