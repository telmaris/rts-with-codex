#include "ai/Controller.h"
#include "ai/AIModel.h"
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
    , model(std::make_unique<UtilityAIModel>(controlledPlayerId))
{
}

AIController::~AIController() = default;

void AIController::Update(GameWorld& world, double dt)
{
    auto playerIt = world.GetPlayerHandler().players.find(playerId);
    if (playerIt == world.GetPlayerHandler().players.end())
        return;

    if (model != nullptr)
        model->Update(world, playerIt->second.get(), dt);
}

void AIController::SetDifficulty(AIDifficulty newDifficulty)
{
    difficulty = newDifficulty;
}

std::string AIController::GetDecisionTrace() const
{
    return model != nullptr ? model->GetDecisionTrace() : std::string{};
}
