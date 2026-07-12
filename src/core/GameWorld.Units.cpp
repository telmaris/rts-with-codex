#include "core/GameWorld.h"
#include "warfare/UnitMarchSystem.h"

// Thin delegator (TD etap-4) — keeps UnitMarchSystem's actual movement logic
// out of the already-large GameWorld.Render.cpp, matching how the rest of
// GameWorld is split into focused partial translation units.
void GameWorld::UpdateUnits(double dt)
{
    UnitMarchSystem::Update(*this, dt);
}
