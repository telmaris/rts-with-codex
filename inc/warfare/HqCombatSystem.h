#ifndef HQ_COMBAT_SYSTEM_H
#define HQ_COMBAT_SYSTEM_H

class GameWorld;

// HQ defense (TD etap-6.1/6.2), built on the shared attack pipeline
// (warfare/CombatPipeline.h). Runs after UnitCombatSystem/UnitMarchSystem
// each tick (see GameWorld::UpdateUnits):
// - Every unit in `AttackingHq` state independently ticks its own attack
//   timer and, on expiry, deals max(1, siegeAttack - hardDefense) to the
//   target HQ via CombatResolver — no front-only restriction like road
//   combat, since besieging units are meant to group up and DPS in parallel
//   (TD etap-6.2, [DECISION]: no cap on group size in v1).
// - Each HQ periodically emits a thorns AoE (a CircleShape around its
//   footprint) that damages every besieging unit within range — the first
//   real consumer of an area (not just front-vs-front) collision test.
// - When an HQ's HP reaches 0, triggers GameWorld::EliminatePlayer for its
//   owner (elimination, conquest, storage drain, victory check all live
//   there — this system only detects the moment it happens).
class HqCombatSystem
{
public:
    static void Update(GameWorld& world, double dt);
};

#endif
