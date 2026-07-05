#ifndef ARMY_ORDER_H
#define ARMY_ORDER_H

#include <cstdint>
#include <vector>

// Types of orders an army can execute at the strategic level.
enum class ArmyOrderType : int
{
    None = 0,
    BorderDeploy = 1,  // distribute divisions along frontier
    Attack = 2,        // coordinated assault on objective
    Hold = 3,          // maintain current positions
    Retreat = 4        // fallback to rear positions
};

// Strategic order issued to an entire army (all divisions follow coordinated instructions).
// Only one order is active per ArmyGroup at a time.
struct ArmyOrder
{
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
    // Whether this order is currently being executed
    bool isActive{false};

    bool IsValid() const { return type != ArmyOrderType::None; }
};

#endif
