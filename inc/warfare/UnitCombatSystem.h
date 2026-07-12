#ifndef UNIT_COMBAT_SYSTEM_H
#define UNIT_COMBAT_SYSTEM_H

class GameWorld;

// Road combat (TD etap-5.2), built on the shared attack pipeline (5.1,
// warfare/CombatPipeline.h). Runs BEFORE UnitMarchSystem::Update each tick:
// - Detects the two opposing columns' spearheads (the front-most marching
//   unit in each direction on a route) coming within melee contact range and
//   locks both into FightingUnit — UnitMarchSystem freezes units in that
//   state, so once engaged neither column can march past the other.
// - Advances each fighting unit's attack timer; on expiry, emits an
//   AttackEmission at the target's position and resolves it immediately
//   (melee = same-tick contact, no travel time) via CombatResolver.
// - On death (currentHp <= 0): removes the unit from GameWorld's deployed
//   map this same tick (no lingering "zombie" that could still land a hit —
//   the exact bug class the old war system had) and frees the survivor back
//   to Marching so it engages the next unit in the queue next tick.
// Only the front-most unit of each side ever fights; the rest of each
// column queues up behind its own spearhead via UnitMarchSystem's existing
// single-file spacing.
class UnitCombatSystem
{
public:
    static void Update(GameWorld& world, double dt);
};

#endif
