#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "economy/Player.h"

#include <memory>

class GameWorld;

class IController
{
public:
    virtual ~IController() = default;
    explicit IController(int controlledPlayerId) : playerId(controlledPlayerId) {}

    virtual void Update(GameWorld& world, double dt) = 0;

    int playerId{0};
};

class LocalController : public IController
{
public:
    explicit LocalController(int controlledPlayerId) : IController(controlledPlayerId) {}

    void Update(GameWorld& world, double dt) override;
};

class RemoteController : public IController
{
public:
    explicit RemoteController(int controlledPlayerId) : IController(controlledPlayerId) {}

    void Update(GameWorld& world, double dt) override;
};

// Difficulty scale for AI opponents, indexed by MapParameters::aiDifficulty
// (0-3, set in the new-game / lobby UI). Higher levels grant a bigger
// starting advantage and less decision noise (AI rework, TODO #2).
enum class AIDifficulty
{
    Primitive,
    Easy,
    Normal,
    Hard
};

class UtilityAIModel;

// AI rework (TODO #2, 2026-07-16): the old PrimitiveAIModel decision layer
// (strategy axes, goals, milestones, personality, unified action scoring)
// was removed — priority-axis thinking doesn't fit the tower defense loop.
// AIController is the IController seam owning the utility-based
// UtilityAIModel (ai/AIModel.h); the mechanical actuators any model executes
// decisions through live in ai/AIActions.h.
class AIController : public IController
{
public:
    explicit AIController(int controlledPlayerId);
    ~AIController();  // out-of-line: unique_ptr over the forward-declared model

    void Update(GameWorld& world, double dt) override;
    void SetDifficulty(AIDifficulty newDifficulty);

private:
    AIDifficulty difficulty{AIDifficulty::Primitive};
    std::unique_ptr<UtilityAIModel> model;
};

#endif
