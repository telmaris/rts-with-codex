#ifndef BATTLE_H
#define BATTLE_H

#include <string>
#include <vector>

class TileMap;

enum class BattleState : int
{
    Ongoing = 0,
    AttackerWon = 1,
    DefenderWon = 2,
    Withdrawn = 3
};

struct BattleLogEntry
{
    double time{0.0};
    std::string message;
};

// Represents one ongoing or recently resolved battle between two military buildings.
struct BattleInstance
{
    int id{0};
    int attackerTileId{-1};
    int defenderTileId{-1};
    BattleState state{BattleState::Ongoing};
    double elapsedTime{0.0};
    double postBattleTimer{0.0};

    std::vector<int> attackerSupportTileIds;
    std::vector<int> defenderSupportTileIds;
    std::vector<BattleLogEntry> log;

    int lastKnownDefenderHp{-1};
    int lastKnownAttackerHp{-1};

    bool IsOver() const { return state != BattleState::Ongoing; }
    void AddLogEntry(double t, std::string msg);
};

// Tracks all active and recently ended battles in the world simulation.
class BattleRegistry
{
public:
    // Starts a battle or returns existing ongoing battle ID between these two tiles.
    int StartBattle(int attackerTileId, int defenderTileId);
    // Registers a support building on one side of an active battle.
    void AddSupport(int battleId, int supportTileId, bool forAttacker);
    // Advances all active battles and detects end conditions.
    void Update(TileMap& tilemap, double dt);
    // Returns the ongoing battle that involves a tile as attacker, defender, or supporter.
    BattleInstance* FindBattleByBuilding(int tileId);
    const BattleInstance* FindBattleByBuilding(int tileId) const;
    // Returns the battle with the given ID, or nullptr if not found.
    BattleInstance* FindBattle(int battleId);

    const std::vector<BattleInstance>& GetBattles() const { return battles; }

private:
    void ResolveTick(BattleInstance& battle, TileMap& tilemap, double dt);

    std::vector<BattleInstance> battles;
    int nextBattleId{1};
};

#endif
