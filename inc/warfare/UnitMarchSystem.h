#ifndef UNIT_MARCH_SYSTEM_H
#define UNIT_MARCH_SYSTEM_H

class GameWorld;

// Advances every deployed BattleUnit one fixed tick along its military-road
// route (TD etap-4): spawns the next queued unit onto a free gate tile,
// marches column members forward at their effective moveSpeed, and enforces
// single-file column spacing so a stopped spearhead queues up the units
// behind it. A unit that reaches the far end transitions to AttackingHq and
// stops there — actually resolving that state (damage, elimination) is
// ETAP 6's job; this system only gets units to the door.
// Called once per simulation tick from GameWorld::UpdateSimulation.
class UnitMarchSystem
{
public:
    static void Update(GameWorld& world, double dt);
};

#endif
