#include "ai/Controller.h"
#include "core/GameWorld.h"

// Advances this object's state for one frame.
void LocalController::Update(GameWorld& world, double dt)
{
}

// Advances this object's state for one frame.
void RemoteController::Update(GameWorld& world, double dt)
{
}

AIController::AIController(int controlledPlayerId)
    : IController(controlledPlayerId)
{
}

// AI rework czystka (TODO #2, 2026-07-16): deliberately inert until the new
// utility-based model lands (etap 2+) — an AI player builds and recruits
// nothing in the interim.
void AIController::Update(GameWorld& world, double dt)
{
}

void AIController::SetDifficulty(AIDifficulty newDifficulty)
{
    difficulty = newDifficulty;
}
