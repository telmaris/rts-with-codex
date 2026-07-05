#ifndef ARMY_ORDER_H
#define ARMY_ORDER_H

#include <cstdint>
#include <vector>

class Player;
class ArmyGroup;

// Types of orders an army can execute at the strategic level.
enum class ArmyOrderType : int
{
    None = 0,
    BorderDeploy = 1,  // distribute divisions along frontier
    Attack = 2,        // coordinated assault on objective
    Hold = 3,          // maintain current positions
    Retreat = 4        // fallback to rear positions
};

// Strategic order instance: local-only simulation loop on the client.
// Each ArmyGroup has at most one active ArmyOrder. The order's Update() method
// runs each tick, monitors division positions/status, and issues MoveDivision
// commands to the player. The server only sees the individual move commands,
// making this MP-safe and deterministic (lockstep).
//
// Example (BorderDeploy):
// - targetTileIds = [tile1, tile2, tile3] (frontier positions to hold)
// - Each tick: check if divisions are at target tiles
// - If division drifts or dies: issue MoveDivision to nearest target
// - If front collapses: dynamically redistribute divisions from reserve
class ArmyOrder
{
public:
    ArmyOrderType type{ArmyOrderType::None};
    // For BorderDeploy: ordered list of frontier tile IDs where divisions deploy
    // For Attack: ignored (objective in objectiveTileId)
    // For Hold/Retreat: ignored
    std::vector<int> targetTileIds;
    // For Attack: the objective building/tile to assault
    // For others: -1
    int objectiveTileId{-1};
    // Priority level for multi-army coordination (not used in MVP)
    int priority{0};

    bool IsValid() const { return type != ArmyOrderType::None; }

    // Local simulation: issued each tick by ArmyGroup.
    // Monitors division positions and issues MoveDivision commands.
    // Returns false if order should be cancelled (all divisions dead, etc).
    bool Update(double dt, ArmyGroup& army, Player& owner);

    // Reset state when order is deactivated.
    void Cancel();

private:
    double tickAccumulator{0.0};
    static constexpr double kOrderTickRate = 1.0;  // check/update orders every 1 second

    // Order-type-specific update logic (return false if order should be cancelled).
    bool UpdateBorderDeploy(ArmyGroup& army, Player& owner);
    bool UpdateHold(ArmyGroup& army, Player& owner);
    bool UpdateAttack(ArmyGroup& army, Player& owner);
    bool UpdateRetreat(ArmyGroup& army, Player& owner);
};

#endif
