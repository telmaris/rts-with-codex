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
    BuilderAmount      // number of concurrent construction builders a player commands
};

#endif
