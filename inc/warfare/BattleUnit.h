#ifndef BATTLE_UNIT_H
#define BATTLE_UNIT_H

#include "economy/BalanceStats.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

class Player;

enum class BattleUnitState
{
    InRoster,
    Marching,
    FightingUnit,
    AttackingHq,
    Dying
};

// DLC-ready equipment seam (ETAP 3.4 — not implemented, only reserved). Always
// present in the save/wire format, even though the list stays empty until a
// real equipment system exists, so adding one later doesn't break old saves.
struct EquipmentInstance
{
    std::string itemDefId;
    std::map<BalanceStat, double> statModifiers;
};

// One recruited unit instance. Composition over inheritance: a single
// BattleUnit class plus a UnitDefinition id — no Swordsman : BattleUnit
// hierarchy. Effective stats are resolved from the definition + the owning
// player's BalanceModifierSet at query time (never cached on the instance),
// so a tech-tree/focus buff affects units that are already deployed.
class BattleUnit
{
public:
    BattleUnit() = default;
    BattleUnit(int instanceId, int ownerPlayerId, std::string unitDefId);

    double GetEffectiveMaxHp(const Player& owner) const;
    double GetEffectiveRoadAttack(const Player& owner) const;
    double GetEffectiveSiegeAttack(const Player& owner) const;
    double GetEffectiveArmor(const Player& owner) const;
    double GetEffectiveMoveSpeed(const Player& owner) const;
    double GetEffectiveAttackSpeed(const Player& owner) const;

    int instanceId{0};
    int ownerPlayerId{-1};
    std::string unitDefId;
    double currentHp{0.0};
    BattleUnitState state{BattleUnitState::InRoster};

    // Ring-route position while marching/fighting — ETAP 4+ populates and
    // consumes these; left at defaults for a unit still InRoster.
    int routeFromPlayerId{-1};
    int routeToPlayerId{-1};
    int tileIndex{0};
    double tileProgress{0.0};
    double attackTimer{0.0};

    // DLC seam — always empty in v1.
    std::vector<EquipmentInstance> equipment;
};

// Per-player pool of recruited-but-not-yet-deployed BattleUnit instances.
// Deterministic iteration order (std::map keyed by instanceId).
class UnitRoster
{
public:
    void AddUnit(BattleUnit unit);
    // Removes a unit from the roster (e.g. once ETAP 4 deploys it). Returns
    // it by value, or std::nullopt if no such instance is in the roster.
    std::optional<BattleUnit> RemoveUnit(int instanceId);
    BattleUnit* FindUnit(int instanceId);
    const BattleUnit* FindUnit(int instanceId) const;

    std::map<int, BattleUnit> units;
};

#endif
