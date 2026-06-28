#include "../inc/ArmyGroup.h"

#include <algorithm>

// ─── ArmyCommander ───────────────────────────────────────────────────────────

// Converts a stat (0-100, centre 50) to a scaled additive bonus.
// leadership 50 → +0;  leadership 100 → +scale;  leadership 0 → -scale
static double CommanderBonus(int stat, double scale)
{
    return (stat - 50) / 100.0 * scale;
}

void ArmyCommander::CollectModifiers(std::vector<BalanceModifier>& out,
                                      const std::string& source) const
{
    auto addGlobal = [&](BalanceStat stat, double additive, double multiplier = 1.0)
    {
        BalanceModifier m;
        m.stat = stat;
        m.additive = additive;
        m.multiplier = multiplier;
        m.scope = BalanceModifierScope::Global();
        m.source = source;
        out.push_back(m);
    };

    // Leadership (0-100): up to +30 morale at 100, -30 at 0
    addGlobal(BalanceStat::ArmyMoraleBonus, CommanderBonus(leadership, 30.0));

    // Tactics (0-100): up to +50% attack damage multiplier at 100, -50% at 0
    addGlobal(BalanceStat::ArmyAttackBonus, CommanderBonus(tactics, 50.0));

    // Logistics (0-100): up to +30 tiles/min road speed at 100, -30 at 0
    addGlobal(BalanceStat::ArmyRoadSpeed, CommanderBonus(logistics, 30.0));
    addGlobal(BalanceStat::ArmyMarchSpeed, CommanderBonus(logistics, 6.0));
}

// ─── ArmyGroup ───────────────────────────────────────────────────────────────

bool ArmyGroup::HasDivision(int divisionId) const
{
    for (const auto& ref : divisions)
        if (ref.divisionId == divisionId) return true;
    return false;
}

void ArmyGroup::SetCommander(ArmyCommander cmd)
{
    commander = std::move(cmd);
}

void ArmyGroup::ClearCommander()
{
    commander.reset();
}

void ArmyGroup::RebuildModifiers(const BalanceModifierSet& globalArmyMods)
{
    modifiers.Clear();

    // Apply player-level army bonuses (tech/focus).
    for (const auto& m : globalArmyMods.GetModifiers())
        modifiers.AddModifier(m);

    // Apply commander bonuses.
    if (commander.has_value())
    {
        std::vector<BalanceModifier> cmdMods;
        commander->CollectModifiers(cmdMods, "commander:" + commander->name);
        for (auto& m : cmdMods)
            modifiers.AddModifier(m);
    }
}

double ArmyGroup::ModifyStat(BalanceStat stat, double base) const
{
    BalanceModifierContext ctx;
    ctx.stat = stat;
    return modifiers.ModifyDouble(base, ctx);
}

// ─── ArmyGroupRegistry ───────────────────────────────────────────────────────

int ArmyGroupRegistry::CreateArmy(const std::string& name)
{
    ArmyGroup army;
    army.id = nextId++;
    army.name = name;
    armies.push_back(std::move(army));
    return armies.back().id;
}

void ArmyGroupRegistry::DisbandArmy(int armyId)
{
    armies.erase(std::remove_if(armies.begin(), armies.end(),
        [armyId](const ArmyGroup& a) { return a.id == armyId; }),
        armies.end());
}

void ArmyGroupRegistry::AddDivision(int armyId, int homeTileId, int divisionId)
{
    // Remove from any existing army first
    RemoveDivision(divisionId);

    ArmyGroup* army = FindArmy(armyId);
    if (army == nullptr) return;
    army->divisions.push_back({homeTileId, divisionId});
}

void ArmyGroupRegistry::RemoveDivision(int divisionId)
{
    for (auto& army : armies)
    {
        auto it = std::remove_if(army.divisions.begin(), army.divisions.end(),
            [divisionId](const ArmyDivisionRef& r) { return r.divisionId == divisionId; });
        army.divisions.erase(it, army.divisions.end());
    }
}

void ArmyGroupRegistry::SetCommander(int armyId, ArmyCommander commander,
                                      const BalanceModifierSet& globalArmyMods)
{
    ArmyGroup* army = FindArmy(armyId);
    if (army == nullptr) return;
    army->SetCommander(std::move(commander));
    army->RebuildModifiers(globalArmyMods);
}

void ArmyGroupRegistry::ClearCommander(int armyId, const BalanceModifierSet& globalArmyMods)
{
    ArmyGroup* army = FindArmy(armyId);
    if (army == nullptr) return;
    army->ClearCommander();
    army->RebuildModifiers(globalArmyMods);
}

void ArmyGroupRegistry::RebuildAllModifiers(const BalanceModifierSet& globalArmyMods)
{
    for (auto& army : armies)
        army.RebuildModifiers(globalArmyMods);
}

ArmyGroup* ArmyGroupRegistry::FindArmy(int armyId)
{
    for (auto& a : armies)
        if (a.id == armyId) return &a;
    return nullptr;
}

const ArmyGroup* ArmyGroupRegistry::FindArmy(int armyId) const
{
    for (const auto& a : armies)
        if (a.id == armyId) return &a;
    return nullptr;
}

ArmyGroup* ArmyGroupRegistry::FindArmyByDivision(int divisionId)
{
    for (auto& a : armies)
        if (a.HasDivision(divisionId)) return &a;
    return nullptr;
}

const ArmyGroup* ArmyGroupRegistry::FindArmyByDivision(int divisionId) const
{
    for (const auto& a : armies)
        if (a.HasDivision(divisionId)) return &a;
    return nullptr;
}
