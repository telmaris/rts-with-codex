#include "warfare/Battle.h"

#include <algorithm>

Battle::Side Battle::SideOf(int divisionId) const
{
    if (std::find(attackerDivisionIds.begin(), attackerDivisionIds.end(), divisionId) != attackerDivisionIds.end())
        return Side::Attacker;
    if (std::find(defenderDivisionIds.begin(), defenderDivisionIds.end(), divisionId) != defenderDivisionIds.end())
        return Side::Defender;
    return Side::None;
}

void Battle::AddToSide(Side side, int divisionId, int playerId)
{
    if (side == Side::None || SideOf(divisionId) != Side::None)
        return;

    std::vector<int>& divisionIds = (side == Side::Attacker) ? attackerDivisionIds : defenderDivisionIds;
    std::vector<int>& playerIds = (side == Side::Attacker) ? attackerPlayerIds : defenderPlayerIds;

    divisionIds.push_back(divisionId);
    if (std::find(playerIds.begin(), playerIds.end(), playerId) == playerIds.end())
        playerIds.push_back(playerId);
}
