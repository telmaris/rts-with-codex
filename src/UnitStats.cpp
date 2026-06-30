#include "../inc/UnitStats.h"
#include "../inc/Building.h"          // MilitaryUnitType, SoldierDivision, DivisionEquipment
#include "../inc/Equipment.h"         // equipment quality table
#include "../inc/BalanceModifiers.h"  // BalanceModifierSet / context

UnitStats MakeDefaultUnitStats(MilitaryUnitType type)
{
    UnitStats stats;
    switch (type)
    {
        case MilitaryUnitType::Swordsman:
            stats.lightAttack   = 14.0f; stats.armoredAttack = 10.0f; stats.shock = 8.0f;
            stats.armor         = 6.0f;  stats.piercing      = 3.0f;  stats.defense = 12.0f;
            stats.maxStrength   = 130.0f; stats.maxCohesion  = 40.0f; stats.morale = 68.0f;
            stats.speed         = 10.0f; stats.supplyUse     = 1.2f;  stats.fatigueRate = 1.0f;
            stats.armoredShare  = 0.35f;
            break;
        case MilitaryUnitType::Archer:
            stats.lightAttack   = 16.0f; stats.armoredAttack = 6.0f;  stats.shock = 4.0f;
            stats.armor         = 2.0f;  stats.piercing      = 6.0f;  stats.defense = 7.0f;
            stats.maxStrength   = 90.0f;  stats.maxCohesion  = 30.0f; stats.morale = 64.0f;
            stats.speed         = 12.0f; stats.supplyUse     = 1.0f;  stats.fatigueRate = 1.1f;
            stats.armoredShare  = 0.10f;
            break;
        default: // Militia
            stats.lightAttack   = 8.0f;  stats.armoredAttack = 4.0f;  stats.shock = 5.0f;
            stats.armor         = 2.0f;  stats.piercing      = 1.0f;  stats.defense = 8.0f;
            stats.maxStrength   = 100.0f; stats.maxCohesion  = 25.0f; stats.morale = 55.0f;
            stats.speed         = 14.0f; stats.supplyUse     = 0.8f;  stats.fatigueRate = 1.3f;
            stats.armoredShare  = 0.05f;
            break;
    }
    return stats;
}

float ResolveUnitStat(const Stat<float>& stat, MilitaryUnitType unitType,
                      const BalanceModifierSet* mods)
{
    const float base = stat.GetBase();
    if (mods == nullptr)
        return base;

    BalanceModifierContext context;
    context.stat = stat.GetStatId();
    context.unitType = unitType;
    return static_cast<float>(mods->ModifyDouble(static_cast<double>(base), context));
}

float DivisionEquipmentQuality(const DivisionEquipment& equipment)
{
    const ResourceType slots[] = {
        equipment.weapon, equipment.armor, equipment.rangedWeapon, equipment.ammo};

    float total = 0.0f;
    int count = 0;
    for (ResourceType type : slots)
    {
        const EquipmentProfile* profile = FindEquipmentProfile(type);
        if (profile == nullptr)
            continue;
        total += profile->quality;
        count++;
    }

    // No recognised gear → makeshift equipment (e.g. militia with WEAPON_SUPPLY).
    return count > 0 ? total / static_cast<float>(count) : 0.5f;
}

DivisionCombatStats ComputeDivisionCombatStats(const SoldierDivision& division,
                                               const BalanceModifierSet* mods)
{
    const UnitStats& s = division.stats;
    const MilitaryUnitType type = division.type;
    const float quality = DivisionEquipmentQuality(division.equipment);

    DivisionCombatStats out;
    out.equipmentQuality = quality;

    // Gear scales how hard you hit and how well you shrug off blows; morale, speed
    // and the manpower pool come straight from the (modified) unit stats.
    out.lightAttack   = ResolveUnitStat(s.lightAttack, type, mods) * quality;
    out.armoredAttack = ResolveUnitStat(s.armoredAttack, type, mods) * quality;
    out.shock         = ResolveUnitStat(s.shock, type, mods) * quality;
    out.piercing      = ResolveUnitStat(s.piercing, type, mods) * quality;
    out.armor         = ResolveUnitStat(s.armor, type, mods) * quality;
    out.defense       = ResolveUnitStat(s.defense, type, mods) * quality;
    out.morale        = ResolveUnitStat(s.morale, type, mods);
    out.speed         = ResolveUnitStat(s.speed, type, mods);
    out.maxStrength   = ResolveUnitStat(s.maxStrength, type, mods);
    return out;
}

namespace
{
    // Strength damage `attacker` inflicts on `defender` per unit time.
    float DuelOffense(const DivisionCombatStats& attacker, const DivisionCombatStats& defender)
    {
        float power = attacker.lightAttack + attacker.shock * 0.5f;
        float defense = defender.armor + defender.defense;
        float pierced = std::min(defender.armor, attacker.piercing); // piercing cancels armor
        float effectiveDefense = std::max(0.0f, defense - pierced);
        return std::max(0.25f, power - effectiveDefense * 0.5f);
    }
}

DivisionDuelResult ResolveDivisionDuel(const DivisionCombatStats& attacker,
                                       const DivisionCombatStats& defender, double dt)
{
    DivisionDuelResult result;
    result.defenderStrengthLoss = DuelOffense(attacker, defender) * static_cast<float>(dt);
    result.attackerStrengthLoss = DuelOffense(defender, attacker) * static_cast<float>(dt);
    return result;
}
