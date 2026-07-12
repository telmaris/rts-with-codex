#ifndef CONQUERED_ECONOMY_H
#define CONQUERED_ECONOMY_H

#include <vector>

class Player;

// One captured production building's productivity ramp (TD etap-6.3,
// anti-snowball: a conquered building starts at ~30% throughput and
// linearly recovers to 100% over `rampDuration` seconds).
struct ConqueredBuildingRamp
{
    int buildingId{-1};
    double elapsed{0.0};
    double rampDuration{60.0};
};

// Per-player (the conqueror's) tracker for productivity ramps on buildings
// captured by eliminating another player. Implemented as a per-building
// BalanceModifier on ProductionCycleTime (scope Building(id)), refreshed
// every tick while ramping and removed once it completes — the modifier
// naturally becomes a no-op (multiplier 1.0) at full productivity, so
// removing it at that point is just cleanup, not a behavior change.
class ConqueredEconomy
{
public:
    void AddRamp(int buildingId, double rampDuration);
    // Advances every active ramp's elapsed time and refreshes its
    // BalanceModifier on `owner`; removes ramps that have completed.
    void Tick(Player& owner, double dt);

    const std::vector<ConqueredBuildingRamp>& GetRamps() const { return ramps; }
    void SetRamps(std::vector<ConqueredBuildingRamp> loaded) { ramps = std::move(loaded); }

private:
    std::vector<ConqueredBuildingRamp> ramps;
};

#endif
