#include "../inc/DiplomaticState.h"

void DiplomaticState::DeclareWar(int attackerId, int defenderId, double gameTime)
{
    // Prevent duplicate active wars between the same pair.
    for (const auto& w : wars)
        if (w.active && w.Involves(attackerId) && w.Involves(defenderId))
            return;
    int id = static_cast<int>(wars.size());
    wars.push_back({id, attackerId, defenderId, gameTime, true});
}

void DiplomaticState::EndWar(int otherId)
{
    for (auto& w : wars)
        if (w.active && w.Involves(otherId))
            w.active = false;
    relations.erase(otherId);
}

void DiplomaticState::SetRelation(int otherId, DiplomaticRelation rel)
{
    if (rel == DiplomaticRelation::Neutral)
        relations.erase(otherId);
    else
        relations[otherId] = rel;
}

bool DiplomaticState::IsAtWar(int otherId) const
{
    for (const auto& w : wars)
        if (w.active && w.Involves(otherId))
            return true;
    return false;
}

bool DiplomaticState::IsAllied(int otherId) const
{
    auto it = relations.find(otherId);
    return it != relations.end() && it->second == DiplomaticRelation::Alliance;
}

bool DiplomaticState::IsAtWarWithAny() const
{
    for (const auto& w : wars)
        if (w.active)
            return true;
    return false;
}

const DiplomaticWar* DiplomaticState::FindActiveWar(int otherId) const
{
    for (const auto& w : wars)
        if (w.active && w.Involves(otherId))
            return &w;
    return nullptr;
}

std::vector<int> DiplomaticState::GetWarEnemies() const
{
    std::vector<int> result;
    for (const auto& w : wars)
        if (w.active)
            result.push_back(w.OpponentOf(ownerId));
    return result;
}

std::vector<int> DiplomaticState::GetAllies() const
{
    std::vector<int> result;
    for (const auto& [id, rel] : relations)
        if (rel == DiplomaticRelation::Alliance)
            result.push_back(id);
    return result;
}

DiplomaticRelation DiplomaticState::GetRelation(int otherId) const
{
    if (IsAtWar(otherId))
        return DiplomaticRelation::War;
    auto it = relations.find(otherId);
    return it != relations.end() ? it->second : DiplomaticRelation::Neutral;
}
