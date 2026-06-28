#include "../inc/Battle.h"
#include "../inc/MapGenerator.h"
#include "../inc/BuildingComponents.h"

#include <algorithm>

void BattleInstance::AddLogEntry(double t, std::string msg)
{
    log.push_back({t, std::move(msg)});
    if (log.size() > 200)
        log.erase(log.begin());
}

int BattleRegistry::StartBattle(int attackerTileId, int defenderTileId)
{
    for (auto& b : battles)
        if (!b.IsOver() && b.attackerTileId == attackerTileId && b.defenderTileId == defenderTileId)
            return b.id;

    BattleInstance battle;
    battle.id = nextBattleId++;
    battle.attackerTileId = attackerTileId;
    battle.defenderTileId = defenderTileId;
    battle.AddLogEntry(0.0, "Battle started");
    battles.push_back(std::move(battle));
    return battles.back().id;
}

void BattleRegistry::AddSupport(int battleId, int supportTileId, bool forAttacker)
{
    auto* battle = FindBattle(battleId);
    if (battle == nullptr || battle->IsOver())
        return;

    auto& side = forAttacker ? battle->attackerSupportTileIds : battle->defenderSupportTileIds;
    if (std::find(side.begin(), side.end(), supportTileId) == side.end())
        side.push_back(supportTileId);
}

void BattleRegistry::Update(TileMap& tilemap, double dt)
{
    for (auto& battle : battles)
    {
        if (!battle.IsOver())
            ResolveTick(battle, tilemap, dt);
        else
            battle.postBattleTimer += dt;
    }

    battles.erase(
        std::remove_if(battles.begin(), battles.end(),
            [](const BattleInstance& b) { return b.IsOver() && b.postBattleTimer > 60.0; }),
        battles.end());
}

void BattleRegistry::ResolveTick(BattleInstance& battle, TileMap& tilemap, double dt)
{
    battle.elapsedTime += dt;

    Building* attacker = tilemap.GetBuilding(battle.attackerTileId);
    Building* defender = tilemap.GetBuilding(battle.defenderTileId);

    if (attacker == nullptr)
    {
        battle.state = BattleState::DefenderWon;
        battle.AddLogEntry(battle.elapsedTime, "Attacker building lost");
        return;
    }
    if (defender == nullptr)
    {
        battle.state = BattleState::AttackerWon;
        battle.AddLogEntry(battle.elapsedTime, "Defender building captured!");
        return;
    }

    auto* defTerritory = defender->GetComponent<TerritoryComponent>();
    auto* atkTerritory = attacker->GetComponent<TerritoryComponent>();

    if (defTerritory != nullptr)
    {
        int hp = defTerritory->hp;
        if (battle.lastKnownDefenderHp >= 0 && hp < battle.lastKnownDefenderHp)
        {
            int dmg = battle.lastKnownDefenderHp - hp;
            battle.AddLogEntry(battle.elapsedTime,
                defender->name + " -" + std::to_string(dmg) +
                " HP (" + std::to_string(hp) + " left)");
        }
        battle.lastKnownDefenderHp = hp;

        if (hp <= 0)
        {
            battle.state = BattleState::AttackerWon;
            battle.AddLogEntry(battle.elapsedTime, defender->name + " defeated!");
            return;
        }
    }

    if (atkTerritory != nullptr)
    {
        int hp = atkTerritory->hp;
        if (battle.lastKnownAttackerHp >= 0 && hp < battle.lastKnownAttackerHp)
        {
            int dmg = battle.lastKnownAttackerHp - hp;
            battle.AddLogEntry(battle.elapsedTime,
                attacker->name + " -" + std::to_string(dmg) +
                " HP (" + std::to_string(hp) + " left)");
        }
        battle.lastKnownAttackerHp = hp;

        if (hp <= 0)
        {
            battle.state = BattleState::DefenderWon;
            battle.AddLogEntry(battle.elapsedTime, attacker->name + " destroyed!");
            return;
        }
    }

    // Detect attacker voluntarily cancelling their order
    auto* garrison = attacker->GetComponent<GarrisonComponent>();
    if (garrison != nullptr)
    {
        bool buildingAttacking = (garrison->currentOrder == MilitaryOrderType::Attack &&
                                  garrison->orderTargetId == battle.defenderTileId);
        if (!buildingAttacking)
        {
            bool anyDivisionAttacking = false;
            for (const auto& div : garrison->divisions)
            {
                if (div.currentOrder == MilitaryOrderType::Attack &&
                    div.orderTargetPositionId == battle.defenderTileId)
                {
                    anyDivisionAttacking = true;
                    break;
                }
            }
            if (!anyDivisionAttacking)
            {
                battle.state = BattleState::Withdrawn;
                battle.AddLogEntry(battle.elapsedTime, attacker->name + " withdrew");
            }
        }
    }
}

BattleInstance* BattleRegistry::FindBattleByBuilding(int tileId)
{
    for (auto& b : battles)
    {
        if (b.IsOver()) continue;
        if (b.attackerTileId == tileId || b.defenderTileId == tileId)
            return &b;
        for (int id : b.attackerSupportTileIds)
            if (id == tileId) return &b;
        for (int id : b.defenderSupportTileIds)
            if (id == tileId) return &b;
    }
    return nullptr;
}

const BattleInstance* BattleRegistry::FindBattleByBuilding(int tileId) const
{
    for (const auto& b : battles)
    {
        if (b.IsOver()) continue;
        if (b.attackerTileId == tileId || b.defenderTileId == tileId)
            return &b;
        for (int id : b.attackerSupportTileIds)
            if (id == tileId) return &b;
        for (int id : b.defenderSupportTileIds)
            if (id == tileId) return &b;
    }
    return nullptr;
}

BattleInstance* BattleRegistry::FindBattle(int battleId)
{
    for (auto& b : battles)
        if (b.id == battleId) return &b;
    return nullptr;
}
