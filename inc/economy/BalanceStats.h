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
    // TD(etap-9): kept as real, tunable stats (not stripped like the rest of
    // the old RecruitmentTime/Cost concepts) — recruiting a frontline unit
    // needs a non-zero time cost so deploying an attack is a planning
    // decision, not an instant reaction. Floored at RecruitmentComponent's
    // call site so a multiplier can never make recruitment instant.
    UnitRecruitTime,
    UnitRecruitManpowerCost,

    // Reserved now per the rework plan so ETAP 6/7 don't need another stat-enum
    // migration; unused until then.
    HqMaxHp,
    HqDefense,
    HqThorns,
    // Fraction of a defeated player's stored resources captured on conquest.
    // Resolved for the conqueror, so focuses/technologies can improve spoils.
    ConquestSpoilsFraction,
    TowerDamage,
    TowerRange,
    TowerAttackSpeed,
    TowerAmmoEfficiency,

    // Time between reserving one resource at a source building and releasing
    // it onto the first road tile. Appended to preserve existing enum values.
    TransportDispatchDelay,

    // Relative cadence at which a Village consumes one upkeep package.
    // Resolve with a ResourceType context so FOOD_PROVISIONS,
    // HOUSEHOLD_GOODS and URBAN_GOODS can be balanced independently.
    // Lower is better: effective interval = base interval / consumption.
    VillageSupplyConsumption
};

#endif
