#include "economy/ConqueredEconomy.h"
#include "economy/Player.h"
#include "economy/BalanceModifiers.h"

#include <algorithm>
#include <string>

namespace
{
    std::string RampModifierSource(int buildingId)
    {
        return "conquered:" + std::to_string(buildingId);
    }
}

void ConqueredEconomy::AddRamp(int buildingId, double rampDuration)
{
    ramps.push_back({buildingId, 0.0, rampDuration});
}

void ConqueredEconomy::Tick(Player& owner, double dt)
{
    for (auto it = ramps.begin(); it != ramps.end();)
    {
        it->elapsed += dt;
        double t = it->rampDuration > 0.0 ? std::min(1.0, it->elapsed / it->rampDuration) : 1.0;
        std::string source = RampModifierSource(it->buildingId);
        owner.balanceModifiers.ClearSource(source);

        if (t >= 1.0)
        {
            it = ramps.erase(it);
            continue;
        }

        // Anti-snowball ramp: 30% -> 100% productivity, linear. Modeled as a
        // longer ProductionCycleTime (inverse of productivity) rather than a
        // separate "productivity" stat, so it composes with every existing
        // tech/focus modifier on the same stat without new plumbing.
        double productivity = 0.3 + 0.7 * t;
        double cycleMultiplier = 1.0 / productivity;
        owner.balanceModifiers.AddModifier(BalanceModifier{
            BalanceStat::ProductionCycleTime, 0.0, cycleMultiplier,
            BalanceModifierScope::Building(it->buildingId), std::nullopt, std::nullopt, source});
        owner.balanceModifiers.AddModifier(BalanceModifier{
            BalanceStat::ManpowerRate, 0.0, productivity,
            BalanceModifierScope::Building(it->buildingId), std::nullopt, std::nullopt, source});
        owner.balanceModifiers.AddModifier(BalanceModifier{
            BalanceStat::TowerAttackSpeed, 0.0, productivity,
            BalanceModifierScope::Building(it->buildingId), std::nullopt, std::nullopt, source});
        ++it;
    }
}
