// Local-only army order simulation loop. Each order instance runs independently
// on the client, issuing MoveDivision commands each tick based on division positions
// and tactical requirements (e.g., maintaining a frontier).

#include "../inc/ArmyOrder.h"

#include "../inc/ArmyGroup.h"
#include "../inc/Player.h"
#include "../inc/GameCommand.h"
#include "../inc/Building.h"

#include <algorithm>

bool ArmyOrder::Update(double dt, ArmyGroup& army, Player& owner)
{
    if (type == ArmyOrderType::None || army.divisions.empty())
        return false;

    tickAccumulator += dt;
    if (tickAccumulator < kOrderTickRate)
        return true;  // order still valid, not time to tick yet

    tickAccumulator -= kOrderTickRate;

    switch (type)
    {
        case ArmyOrderType::BorderDeploy:
            return UpdateBorderDeploy(army, owner);
        case ArmyOrderType::Hold:
            return UpdateHold(army, owner);
        case ArmyOrderType::Attack:
            return UpdateAttack(army, owner);
        case ArmyOrderType::Retreat:
            return UpdateRetreat(army, owner);
        default:
            return false;
    }
}

bool ArmyOrder::UpdateBorderDeploy(ArmyGroup& army, Player& owner)
{
    // BorderDeploy: distribute divisions evenly across targetTileIds (frontier positions).
    // Each tick, check if divisions have drifted from their assigned targets and move them back.

    if (targetTileIds.empty())
        return false;

    // TODO: For each division in the army:
    // 1. Find its assigned target tile (round-robin or closest available)
    // 2. If division is at target: leave it
    // 3. If division is not at target or died: issue MoveDivision to target
    // 4. If division is under heavy fire and isolated: retreat to rear

    // MVP stub: just maintain current positions
    return true;
}

bool ArmyOrder::UpdateHold(ArmyGroup& army, Player& owner)
{
    // Hold: divisions stay in place.
    // Just monitor - if division dies, remove it from active roster.
    return !army.divisions.empty();
}

bool ArmyOrder::UpdateAttack(ArmyGroup& army, Player& owner)
{
    // Attack: all divisions move toward objectiveTileId.
    // TODO: implement coordinated assault
    return !army.divisions.empty();
}

bool ArmyOrder::UpdateRetreat(ArmyGroup& army, Player& owner)
{
    // Retreat: move divisions to rear.
    // TODO: implement retreat logic
    return !army.divisions.empty();
}

void ArmyOrder::Cancel()
{
    type = ArmyOrderType::None;
    targetTileIds.clear();
    objectiveTileId = -1;
    tickAccumulator = 0.0;
}
