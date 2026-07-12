#include "core/GameWorld.h"
#include "warfare/UnitMarchSystem.h"
#include "warfare/UnitCombatSystem.h"

// Thin delegator (TD etap-4/5) — keeps UnitMarchSystem/UnitCombatSystem's
// actual logic out of the already-large GameWorld.Render.cpp, matching how
// the rest of GameWorld is split into focused partial translation units.
// Combat runs first so a spearhead already in contact this tick is locked
// into FightingUnit (and therefore frozen) before marching moves anyone —
// see UnitCombatSystem's class comment for why the ordering matters.
void GameWorld::UpdateUnits(double dt)
{
    UnitCombatSystem::Update(*this, dt);
    UnitMarchSystem::Update(*this, dt);
}
