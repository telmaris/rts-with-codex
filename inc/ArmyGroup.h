#ifndef ARMY_GROUP_H
#define ARMY_GROUP_H

#include "BalanceModifiers.h"

#include <optional>
#include <string>
#include <vector>

// ─── ArmyCommander ────────────────────────────────────────────────────────────
// A named character that leads an ArmyGroup and provides modifier bonuses.
// Stats are 0-100; higher values yield larger bonuses via the modifier system.
struct ArmyCommander
{
    std::string name{"Commander"};
    int leadership{50};  // boosts morale of all divisions in army
    int tactics{50};     // boosts attack efficiency
    int logistics{50};   // boosts road movement speed

    // Fills `out` with BalanceModifiers derived from this commander's stats.
    // Formula: (stat - 50) / 100 * scale  → range ±(scale/2)
    void CollectModifiers(std::vector<BalanceModifier>& out, const std::string& source) const;
};

// ─── ArmyDivisionRef ─────────────────────────────────────────────────────────
// Identifies a division by its home building tile and division id.
struct ArmyDivisionRef
{
    int homeTileId{-1};
    int divisionId{-1};
};

// ─── ArmyGroup ────────────────────────────────────────────────────────────────
// A named group of divisions that share a commander and modifier bonuses.
// Bonuses are stored as a BalanceModifierSet and applied to combat/movement.
struct ArmyGroup
{
    int id{0};
    std::string name{"Army"};
    std::optional<ArmyCommander> commander;
    std::vector<ArmyDivisionRef> divisions;

    // Active modifiers: commander bonuses + player tech/focus army bonuses.
    // Rebuilt by RebuildModifiers() whenever composition or commander changes.
    BalanceModifierSet modifiers;

    // Returns true when this army contains the given division id.
    bool HasDivision(int divisionId) const;

    // Replaces the commander and rebuilds modifiers.
    void SetCommander(ArmyCommander cmd);
    // Removes the commander and rebuilds modifiers.
    void ClearCommander();

    // Rebuilds the internal modifier set from current commander stats.
    // Also merges in `globalArmyMods` (player-level tech/focus army bonuses).
    void RebuildModifiers(const BalanceModifierSet& globalArmyMods);

    // Applies army modifiers to a stat value.
    // E.g. ModifyDivisionStat(ArmyAttackBonus, 10) → modified damage
    double ModifyStat(BalanceStat stat, double base) const;
};

// ─── ArmyGroupRegistry ────────────────────────────────────────────────────────
// Owned by Player; manages all army groups for a single player.
class ArmyGroupRegistry
{
public:
    // Creates a new army and returns its id.
    int CreateArmy(const std::string& name);
    // Removes an army (divisions are released, not destroyed).
    void DisbandArmy(int armyId);

    // Assigns a division to an army; removes it from any previous army first.
    void AddDivision(int armyId, int homeTileId, int divisionId);
    // Releases a division from whichever army it belongs to.
    void RemoveDivision(int divisionId);

    // Sets the commander for an army and rebuilds its modifiers.
    void SetCommander(int armyId, ArmyCommander commander,
                      const BalanceModifierSet& globalArmyMods);
    // Removes the commander for an army.
    void ClearCommander(int armyId, const BalanceModifierSet& globalArmyMods);

    // Rebuilds all army modifier sets (call after tech/focus unlocks).
    void RebuildAllModifiers(const BalanceModifierSet& globalArmyMods);

    ArmyGroup* FindArmy(int armyId);
    const ArmyGroup* FindArmy(int armyId) const;
    ArmyGroup* FindArmyByDivision(int divisionId);
    const ArmyGroup* FindArmyByDivision(int divisionId) const;
    const std::vector<ArmyGroup>& GetArmies() const { return armies; }

private:
    std::vector<ArmyGroup> armies;
    int nextId{1};
};

#endif
