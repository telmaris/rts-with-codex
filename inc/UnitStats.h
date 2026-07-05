#ifndef UNIT_STATS_H
#define UNIT_STATS_H

#include "Stat.h"
#include <cstdint>

// Forward declaration — the full enum lives in Building.h, but UnitStats only
// needs the type name for the default-stat factory signature.
enum class MilitaryUnitType : int;
struct DivisionEquipment;
class SoldierDivision;
class Player;

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

    // Combat damage multipliers (balHP/balOrg — Phase C duel formula). Base 1.0;
    // tuned via tech/focus without touching the formula constants.
    Stat<float> hpDamageMultiplier{BalanceStat::CombatHpDamageMultiplier, 1.0f};
    Stat<float> orgDamageMultiplier{BalanceStat::CombatOrgDamageMultiplier, 1.0f};
};

// Returns the baseline stat block for a unit type (modifiers applied later).
UnitStats MakeDefaultUnitStats(MilitaryUnitType type);

// Resolves one stat to its modified value for the given unit type, applying all
// modifiers in `mods` that target this stat/unit (tech, focus, commander, …).
// `mods` may be null, in which case the unmodified base value is returned.
float ResolveUnitStat(const Stat<float>& stat, MilitaryUnitType unitType,
                      const BalanceModifierSet* mods);

// Modifier-resolved pool maxima for a division's own stat block (tech/focus/
// commander/etc via `mods`). These read `division.stats` directly rather than
// the unit-type default, so they respect any per-instance stat overrides.
float ResolveDivisionMaxCohesion(const SoldierDivision& division, const BalanceModifierSet* mods);
float ResolveDivisionMaxStrength(const SoldierDivision& division, const BalanceModifierSet* mods);
float ResolveDivisionMorale(const SoldierDivision& division, const BalanceModifierSet* mods);

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
    float maxCohesion{0.0f};
    float equipmentQuality{1.0f};  // 1.0 = baseline gear; >1 better, <1 makeshift

    // Phase C duel inputs (docs/war_system_phase2_design.md).
    float strength{0.0f};          // current manpower (not the pool maximum)
    float armoredShare{0.0f};
    bool isArmored{false};         // armoredShare > 0.3 or a mounted unit class
    float hpDamageMultiplier{1.0f};
    float orgDamageMultiplier{1.0f};
    // Fraction [0.1, 1.0] of combat output this division can actually deliver given
    // its logistics: an out-of-ammo, starving unit still swings fists (never 0) but
    // hits far softer. Scales the ENTIRE damage output (scaled term + constant
    // floor) so supply % genuinely drives combat. See ComputeDivisionCombatStats.
    float supplyEfficiency{1.0f};
};

// Average effectiveness of the gear a division carries (weapon/armor/ranged/ammo),
// from the equipment quality table. Returns ~0.5 ("makeshift") when unarmed.
float DivisionEquipmentQuality(const DivisionEquipment& equipment);

// Fraction [0.1, 1.0] of combat output a division can deliver given its logistics
// (weapon supply % and whether it has food). Drives supplyEfficiency in the duel
// so an out-of-supply unit is genuinely weak. Reused by the UI to show readiness.
float DivisionSupplyEfficiency(const SoldierDivision& division);

// Resolves a division's combat stats: applies `mods` (tech/focus/army/commander)
// then scales offensive/defensive output by the carried gear's quality, so a unit
// holding a steel sword hits harder than the same unit holding a copper one.
DivisionCombatStats ComputeDivisionCombatStats(const SoldierDivision& division,
                                               const BalanceModifierSet* mods);

// Cohesion (organization) and strength (manpower/HP) lost by each side in one
// tick of a field duel — the two HoI4-style bars (see docs/war_system_phase2_design.md
// Phase C). Cohesion depletes fast and drives retreat; strength drains slowly and
// is the "real" casualty count.
struct DivisionDuelResult
{
    float attackerCohesionLoss{0.0f};
    float defenderCohesionLoss{0.0f};
    float attackerStrengthLoss{0.0f};
    float defenderStrengthLoss{0.0f};
};

// Resolves one tick of a field duel between two divisions from their combat stats,
// using a DETERMINISTIC adaptation of HoI4's damage algorithm (expected values in
// place of dice rolls — lockstep MP cannot tolerate RNG). See docs/war_system_phase2_design.md
// Phase C for the full derivation. `dt` (seconds) is converted to a fraction of a
// "combat hour" internally.
//
// BUG 4 fix: simTick + divisionIds seed a deterministic variance multiplier
// (±7.5%) so that otherwise-equal fights do not drag on identically forever.
// The same seed on every lockstep client → no desync. Pass 0/0/0 in tests or
// when division IDs are not available (disables variance, still correct).
DivisionDuelResult ResolveDivisionDuel(const DivisionCombatStats& attacker,
                                       const DivisionCombatStats& defender, double dt,
                                       uint64_t simTick = 0,
                                       int attackerDivId = 0,
                                       int defenderDivId = 0);

// Per-tick upkeep for a division's three supply pools (Phase B — see
// docs/war_system_phase2_design.md). Consumption scales with `strength/maxStrength`
// and is faster while `engaged` in combat than while merely `deployed` in the
// field, and slower still for a division sitting in a garrison (neither engaged
// nor deployed). Sub-1-per-tick amounts accumulate in per-pool buffers so they
// are not lost to rounding. When `foodSupply` is fully depleted the division
// starves: `strength` slowly drains (manpower attrition). `supplyConservation`
// (Phase C, from tech/focus/state — see PlayerSupplyConservation) scales down
// how much is actually burned: 1.0 conservation would mean free upkeep, so it is
// capped below that by the caller.
void ConsumeDivisionSupply(SoldierDivision& division, double dt, bool engaged, bool deployed,
                           double supplyConservation = 0.0);

// Upper bound for supplyConservation — upkeep and reinforcement can be discounted
// but never made free (docs/war_system_phase2_design.md Phase C, Supply Conservation).
constexpr double kMaxSupplyConservation = 0.8;

// Base fraction of `hpDamage` lost from `weaponSupply` when a division takes
// strength damage (equipment attrition, Phase C). Scaled down by the owner's
// supply conservation before use.
constexpr float kEquipmentLossFactor = 0.7f;

// Resolves a player's current Supply Conservation fraction (tech/focus/state
// development modifiers on BalanceStat::SupplyConservation), clamped to
// [0, kMaxSupplyConservation].
double PlayerSupplyConservation(const Player& player);

// Regenerates cohesion toward its (modifier-resolved) max when the division is
// NOT in combat (Phase C). Full-rate regen needs materiel supply; morale and
// standing on the owner's own territory (entrenchment) further scale the rate.
// A no-op once cohesion is already at (or above) max.
void RegenerateDivisionCohesion(SoldierDivision& division, double dt, bool inOwnTerritory,
                                const BalanceModifierSet* mods);

// Periodic strength reinforcement (Phase C): a supplied division (food and
// weapons both > 0) rebuilds `strength` toward its max by drawing manpower from
// the owner's Manpower pool, discounted by Supply Conservation. No manpower
// available => no reinforcement (strength stands still, or keeps dropping if
// still under attrition elsewhere). Returns the whole-point strength actually
// restored this call (usually 0 — sub-1 gains accumulate in `reinforcementBuffer`).
int ReinforceDivisionStrength(SoldierDivision& division, Player& owner, double dt,
                              const BalanceModifierSet* mods);

// Recomputes the legacy `health` field as a pure derivative of `strength` /
// `maxStrength` (Phase C — health is display-only, never a combat target).
// Call after any mutation of `strength` so UI/telemetry/saves stay in sync.
void SyncDerivedHealth(SoldierDivision& division);

#endif
