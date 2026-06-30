#ifndef UNIT_STATS_H
#define UNIT_STATS_H

#include "Stat.h"

// Forward declaration — the full enum lives in Building.h, but UnitStats only
// needs the type name for the default-stat factory signature.
enum class MilitaryUnitType : int;
struct DivisionEquipment;
class SoldierDivision;

class BalanceModifierSet;

// ─── UnitStats ──────────────────────────────────────────────────────────────
// Combat parameters resolved during battle. Every field is a Stat<float> so it
// can be modified through the standard BalanceModifierSet pipeline (focus tree,
// tech tree, commander bonuses, defensive building auras, …). Base values are
// the unit-type defaults; modifiers are applied at query time via ResolveStat,
// never by mutating the base — this keeps the data deterministic and saveable
// without bespoke serialization (bases are always re-derived from the type).
struct UnitStats
{
    // Offensive
    Stat<float> lightAttack{BalanceStat::UnitLightAttack, 0.0f};
    Stat<float> armoredAttack{BalanceStat::UnitArmoredAttack, 0.0f};
    Stat<float> shock{BalanceStat::UnitShock, 0.0f};

    // Defensive
    Stat<float> armor{BalanceStat::UnitArmor, 0.0f};
    Stat<float> piercing{BalanceStat::UnitPiercing, 0.0f};
    Stat<float> defense{BalanceStat::UnitDefense, 0.0f};

    // Pools
    Stat<float> maxStrength{BalanceStat::UnitMaxStrength, 0.0f};
    Stat<float> maxCohesion{BalanceStat::UnitMaxCohesion, 0.0f};
    Stat<float> morale{BalanceStat::UnitMorale, 0.0f};

    // Logistics / movement
    Stat<float> speed{BalanceStat::UnitSpeed, 0.0f};
    Stat<float> supplyUse{BalanceStat::UnitSupplyUse, 0.0f};
    Stat<float> fatigueRate{BalanceStat::UnitFatigueRate, 0.0f};

    // Composition
    Stat<float> armoredShare{BalanceStat::UnitArmoredShare, 0.0f};
};

// Returns the baseline stat block for a unit type (modifiers applied later).
UnitStats MakeDefaultUnitStats(MilitaryUnitType type);

// Resolves one stat to its modified value for the given unit type, applying all
// modifiers in `mods` that target this stat/unit (tech, focus, commander, …).
// `mods` may be null, in which case the unmodified base value is returned.
float ResolveUnitStat(const Stat<float>& stat, MilitaryUnitType unitType,
                      const BalanceModifierSet* mods);

// Combat parameters of a division after applying modifiers AND the quality of
// the gear it actually carries. This is what battle/UI should read — not the raw
// Stat bases.
struct DivisionCombatStats
{
    float lightAttack{0.0f};
    float armoredAttack{0.0f};
    float shock{0.0f};
    float armor{0.0f};
    float piercing{0.0f};
    float defense{0.0f};
    float morale{0.0f};
    float speed{0.0f};
    float maxStrength{0.0f};
    float equipmentQuality{1.0f};  // 1.0 = baseline gear; >1 better, <1 makeshift
};

// Average effectiveness of the gear a division carries (weapon/armor/ranged/ammo),
// from the equipment quality table. Returns ~0.5 ("makeshift") when unarmed.
float DivisionEquipmentQuality(const DivisionEquipment& equipment);

// Resolves a division's combat stats: applies `mods` (tech/focus/army/commander)
// then scales offensive/defensive output by the carried gear's quality, so a unit
// holding a steel sword hits harder than the same unit holding a copper one.
DivisionCombatStats ComputeDivisionCombatStats(const SoldierDivision& division,
                                               const BalanceModifierSet* mods);

// Strength lost by each side in one tick of a field duel.
struct DivisionDuelResult
{
    float attackerStrengthLoss{0.0f};
    float defenderStrengthLoss{0.0f};
};

// Resolves one tick of a field duel between two divisions from their combat stats.
// Each side's offense (attack + a fraction of shock) is mitigated by the other's
// armor + defense, with piercing cancelling part of the armor. `dt` scales the
// exchange. This is the field-combat adaptation of the old building-vs-building
// attrition: it operates on division strength rather than building HP.
DivisionDuelResult ResolveDivisionDuel(const DivisionCombatStats& attacker,
                                       const DivisionCombatStats& defender, double dt);

#endif
