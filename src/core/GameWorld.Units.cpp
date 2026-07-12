#include "core/GameWorld.h"
#include "warfare/UnitMarchSystem.h"
#include "warfare/UnitCombatSystem.h"
#include "warfare/HqCombatSystem.h"

// Thin delegator (TD etap-4/5/6) — keeps UnitMarchSystem/UnitCombatSystem/
// HqCombatSystem's actual logic out of the already-large GameWorld.Render.cpp,
// matching how the rest of GameWorld is split into focused partial
// translation units. Order matters: road combat runs first so a spearhead
// already in contact this tick is locked into FightingUnit (and therefore
// frozen) before marching moves anyone (see UnitCombatSystem's class
// comment); marching runs next so a unit that just reached the door
// transitions to AttackingHq before HQ combat resolves this same tick.
void GameWorld::UpdateUnits(double dt)
{
    UnitCombatSystem::Update(*this, dt);
    UnitMarchSystem::Update(*this, dt);
    HqCombatSystem::Update(*this, dt);
}
