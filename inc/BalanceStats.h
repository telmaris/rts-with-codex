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
    ArmyMoraleBonus     // additive to division morale (affects decay)
};

#endif
