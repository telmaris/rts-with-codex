#ifndef UNIT_MARCH_SYSTEM_H
#define UNIT_MARCH_SYSTEM_H

#include "core/Utils.h"

class GameWorld;
class BattleUnit;

// Advances every deployed BattleUnit one fixed tick along its military-road
// route (TD etap-4): spawns the next queued unit onto a free gate tile,
// marches column members forward at their effective moveSpeed, and enforces
// single-file column spacing so a stopped spearhead queues up the units
// behind it. A unit that reaches the far end transitions to AttackingHq and
// stops there — actually resolving that state (damage, elimination) is
// ETAP 6's job; this system only gets units to the door. Units in
// FightingUnit or Dying state (TD etap-5) are frozen in place — combat
// resolution (UnitCombatSystem) owns what happens to them next.
// Called once per simulation tick from GameWorld::UpdateSimulation.
class UnitMarchSystem
{
public:
    static void Update(GameWorld& world, double dt);

    // Interpolated world position (tile-unit coordinates, center of tile) of
    // a deployed unit along its directed route. Shared with rendering
    // (GameWorld::DrawMap) and combat contact detection (UnitCombatSystem)
    // so "where a unit visually is" and "where combat thinks it is" can
    // never disagree. Returns the unit's own gate-tile position (tileIndex
    // 0) if it's still waiting in the spawn queue (tileIndex < 0).
    static Vec2f ComputeWorldPosition(const GameWorld& world, const BattleUnit& unit);
};

#endif
