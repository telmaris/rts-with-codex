#ifndef TOWER_ATTACK_SYSTEM_H
#define TOWER_ATTACK_SYSTEM_H

class GameWorld;

// Defensive towers (TD etap-7), built on the shared attack pipeline
// (warfare/CombatPipeline.h). Runs after HqCombatSystem each tick (see
// GameWorld::UpdateUnits):
// - Advances every in-flight projectile (a homing AttackEmission — re-aims
//   toward its target's current position every tick at a fixed speed, a
//   deliberate simplification over ballistic lead-shooting that's still
//   fully deterministic; see docs/tech_debt.md). On contact, resolves damage
//   via CombatResolver and removes the projectile; if its target vanished
//   (died/moved out of the world) or its timeout elapsed, it's removed
//   without dealing damage.
// - For every tower with ammo and an expired attack cooldown, picks the
//   enemy unit within range closest to reaching its own march destination
//   (same "front fights front" priority UnitCombatSystem's road combat
//   uses), consumes ammo, and spawns a new projectile at the tower's
//   position. No target in range or no ammo -> the tower simply doesn't fire.
class TowerAttackSystem
{
public:
    static void Update(GameWorld& world, double dt);
};

#endif
