#include "core/GameWorld.h"
#include "warfare/UnitMarchSystem.h"
#include "warfare/UnitCombatSystem.h"
#include "warfare/HqCombatSystem.h"
#include "warfare/TowerAttackSystem.h"

// Thin delegator (TD etap-4/5/6/7) — keeps UnitMarchSystem/UnitCombatSystem/
// HqCombatSystem/TowerAttackSystem's actual logic out of the already-large
// GameWorld.Render.cpp, matching how the rest of GameWorld is split into
// focused partial translation units. Order matters: road combat runs first
// so a spearhead already in contact this tick is locked into FightingUnit
// (and therefore frozen) before marching moves anyone (see UnitCombatSystem's
// class comment); marching runs next so a unit that just reached the door
// transitions to AttackingHq before HQ combat resolves this same tick; towers
// run last since they can fire at any deployed unit regardless of march/siege
// state and don't depend on this tick's earlier transitions.
void GameWorld::UpdateUnits(double dt)
{
    UnitCombatSystem::Update(*this, dt);
    UnitMarchSystem::Update(*this, dt);
    HqCombatSystem::Update(*this, dt);
    TowerAttackSystem::Update(*this, dt);
}
