#ifndef BALANCE_STATS_H
#define BALANCE_STATS_H

// Stable identifiers for gameplay parameters affected by technologies and bonuses.
// TD(etap-1): the old war system's stats (Military/Army/Unit/Combat/Supply/
// Territory/Garrison/Recruitment*) were dropped along with Division/UnitStats/
// GarrisonComponent/SupplyBufferComponent. The Tower Defense rework introduces
// fresh unit/tower/HQ stats data-driven from units.rtsdata starting in ETAP 3.
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
    ManpowerRate,
    PopulationCap,
    BuilderAmount,     // number of concurrent construction builders a player commands

    // TD(etap-3): BattleUnit stats. Filterable per unit definition via
    // BalanceModifier::unitDefId (see BalanceModifiers.h).
    UnitHp,
    UnitRoadAttack,     // unit-vs-unit combat damage (ETAP 5)
    UnitSiegeAttack,    // damage dealt to an enemy HQ (ETAP 6)
    UnitArmor,
    UnitMoveSpeed,
    UnitAttackSpeed,

    // Reserved now per the rework plan so ETAP 6/7 don't need another stat-enum
    // migration; unused until then.
    HqMaxHp,
    HqDefense,
    HqThorns,
    TowerDamage,
    TowerRange,
    TowerAttackSpeed,
    TowerAmmoEfficiency
};

#endif
