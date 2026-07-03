#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Bilateral political/military relation (used for alliances; wars are tracked by DiplomaticWar objects).
enum class DiplomaticRelation : uint8_t
{
    Neutral  = 0,
    War      = 1,   // legacy / derived from wars vector; prefer DiplomaticWar
    Alliance = 2,
    Vassal   = 3,   // future
};

// ── Faction ───────────────────────────────────────────────────────────────────
// Players within the same faction are allied. Placeholder — inter-faction
// rules, faction goals, and dynamic member behaviours will be added later.
struct Faction
{
    int id{-1};
    std::string name;
    // Future: std::vector<int> memberIds, goals, inter-faction relations...
};

// ── DiplomaticWar ─────────────────────────────────────────────────────────────
// Represents a war between two players. Objectified so war score, peace terms,
// casus belli, and other complex lifecycle state can be added incrementally.
struct DiplomaticWar
{
    int id{-1};
    int attackerId{-1};   // player who declared war
    int defenderId{-1};   // player who was attacked
    double startTime{0.0};
    bool active{true};
    // Future: int warScore, std::string casusBelli, peace terms, duration...

    bool Involves(int playerId) const { return attackerId == playerId || defenderId == playerId; }
    int  OpponentOf(int playerId) const { return (attackerId == playerId) ? defenderId : attackerId; }
};

// ── DiplomaticState ───────────────────────────────────────────────────────────
// Per-player diplomatic state: faction membership, active wars, and other
// bilateral relations (alliances, vassalage). Read by the AI pressure pipeline
// (Risk, Military, Diplomacy axes) and the UI for war/peace status display.
struct DiplomaticState
{
    int ownerId{-1};                               // which player owns this state
    int factionId{-1};                             // -1 = independent
    std::vector<DiplomaticWar> wars;               // all wars (active + ended)
    std::map<int, DiplomaticRelation> relations;   // non-war bilateral relations

    // ── Mutators ─────────────────────────────────────────────────────────────
    // Records a new war between attackerId and defenderId. Both players should
    // call this with the same (attackerId, defenderId) pair so the record is symmetric.
    void DeclareWar(int attackerId, int defenderId, double gameTime = 0.0);
    void EndWar(int otherId);
    void MakePeace(int otherId) { EndWar(otherId); }  // alias for API compatibility

    void SetRelation(int otherId, DiplomaticRelation rel);
    void JoinFaction(int id) { factionId = id; }
    void LeaveFaction()      { factionId = -1; }

    // ── Queries ───────────────────────────────────────────────────────────────
    bool IsAtWar(int otherId) const;
    bool IsAllied(int otherId) const;
    bool IsAtWarWithAny() const;

    const DiplomaticWar* FindActiveWar(int otherId) const;
    std::vector<int> GetWarEnemies() const;
    std::vector<int> GetAllies() const;
    DiplomaticRelation GetRelation(int otherId) const;
};
