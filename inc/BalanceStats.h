#ifndef BALANCE_STATS_H
#define BALANCE_STATS_H

// Stable identifiers for gameplay parameters affected by technologies and bonuses.
enum class BalanceStat
{
    BuildTime,
    BuildCost,
    ProductionCycleTime,
    ProductionOutputAmount,
    WorkerCapacity,
    TransportTime,
    RoadCapacity,
    RoadSpeed,
    MilitaryStrength,
    AttackDamage,
    HitPoints,
    TerritoryRadius,
    GarrisonCapacity,
    SupplyCapacity,
    SupplyConsumption,
    // Fraction [0, kMaxConservation] of required supply that is waived — from
    // logistics tech, focuses and state development. See docs/war_system_phase2_design.md
    // Phase C "Supply Conservation".
    SupplyConservation,
    ManpowerRate,
    PopulationCap,
    RecruitmentTime,
    RecruitmentManpowerCost,
    BuilderAmount,      // number of concurrent construction builders a player commands
    // Army-level stats (applied via ArmyGroup / ArmyCommander bonuses)
    ArmyRoadSpeed,      // tiles/minute on roads; base = 60 (1/sec)
    ArmyMarchSpeed,     // tiles/minute off-road; base = 12
    ArmyAttackBonus,    // additive to division attack damage
    ArmyDefenseBonus,   // additive to territory HP (passive defense)
    ArmyMoraleBonus,    // additive to division morale (affects decay)

    // Per-unit combat stats (UnitStats) — modifiable by tech/focus/commander/building
    // bonuses through the same BalanceModifierSet pipeline as everything else.
    UnitLightAttack,    // damage vs unarmored manpower
    UnitArmoredAttack,  // damage vs armored manpower
    UnitShock,          // burst impact that erodes enemy cohesion
    UnitArmor,          // mitigates incoming light attack
    UnitPiercing,       // negates a portion of enemy armor
    UnitDefense,        // general defensive multiplier
    UnitMaxStrength,    // manpower pool of the unit
    UnitMaxCohesion,    // organisation pool (how long it holds the line)
    UnitMorale,         // resistance to cohesion loss
    UnitSpeed,          // movement speed (tiles/minute baseline)
    UnitSupplyUse,      // supply consumed per tick
    UnitFatigueRate,    // how fast the unit tires
    UnitArmoredShare,   // fraction of strength that counts as armored (0..1)

    // Combat damage multipliers (balHP/balOrg in the Phase C duel formula) —
    // start at 1.0, tunable via tech/focus without touching the base constants.
    CombatHpDamageMultiplier,
    CombatOrgDamageMultiplier,

    // Supply logistics for deployed divisions (BUG 3 fix).
    // SupplyRange: maximum Manhattan tile distance a deployed division can draw
    // supply from a friendly military building / HQ. Base = 20 tiles.
    SupplyRange,

    // ReinforcementRate: strength points restored per second per deployed division
    // from the player's global Manpower pool when in range of a supply depot.
    // Base = 0.5 strength/s (at 100 Hz that is 0.005 per tick).
    ReinforcementRate
};

#endif
