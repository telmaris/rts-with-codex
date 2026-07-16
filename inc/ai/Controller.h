#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "economy/Player.h"

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

// AI rework czystka (TODO #2, 2026-07-16): the whole PrimitiveAIModel
// decision layer (strategy axes, goals, milestones, personality, unified
// action scoring) was removed — priority-axis thinking doesn't fit the tower
// defense loop. AIController stays as the IController seam the new
// utility-based model plugs into; the mechanical actuators the old model
// executed decisions through live on in ai/AIActions.h.
class AIController : public IController
{
public:
    explicit AIController(int controlledPlayerId);

    void Update(GameWorld& world, double dt) override;
    void SetDifficulty(AIDifficulty newDifficulty);

private:
    AIDifficulty difficulty{AIDifficulty::Primitive};
};

#endif
