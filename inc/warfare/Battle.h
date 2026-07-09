#ifndef BATTLE_H
#define BATTLE_H

#include "core/Utils.h"
#include <vector>
#include <cstdint>

class SoldierDivision;
class Player;

// A Battle represents a single combat engagement between two sides on a specific quadrant.
// Battles are created automatically when divisions enter hostile territory, and resolved
// per-tick through aggregated duel resolution. A battle ends when one side's cohesion
// is exhausted, triggering territorial/morale consequences (retreat/occupation).
class Battle
{
public:
    int id{0};
    Vec2i quadrant{-1, -1};     // 2x2 sector where the battle occurs
    double elapsedTime{0.0};    // Duration in seconds (for visualization/timeout logic)

    // Sides of the conflict: each grouped by contributing players (allies vs allies).
    // A multi-sided (2v1) battle has one Battle with two sides.
    std::vector<int> attackerDivisionIds;    // Division IDs on attacking side
    std::vector<int> attackerPlayerIds;      // Player IDs on attacking side
    std::vector<int> defenderDivisionIds;    // Division IDs on defending side
    std::vector<int> defenderPlayerIds;      // Player IDs on defending side

    // Aggregate cohesion per side (sum of division cohesion on that side).
    // When either hits 0, battle resolves with defeat for that side.
    int attackerCohesion{0};
    int defenderCohesion{0};

    // Whether battle has ended and consequences have been applied.
    bool resolved{false};

    Battle() = default;
    Battle(int battleId, Vec2i sector) : id(battleId), quadrant(sector) {}

    bool IsValid() const { return id > 0 && quadrant.x >= 0 && quadrant.y >= 0; }

    enum class Side { None, Attacker, Defender };

    Side SideOf(int divisionId) const;
    // Adds a division id to the given side (and its owning player id to the
    // side's player list) if not already present on either side. No-op otherwise.
    void AddToSide(Side side, int divisionId, int playerId);
};

#endif
