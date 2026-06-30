#ifndef BALANCE_STATS_H
#define BALANCE_STATS_H

// Stable identifiers for gameplay parameters affected by technologies and bonuses.
enum class BalanceStat
{
    BuildTime,
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
    ManpowerRate,
    PopulationCap,
    RecruitmentTime,
    RecruitmentManpowerCost,
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
    UnitArmoredShare    // fraction of strength that counts as armored (0..1)
};

#endif
