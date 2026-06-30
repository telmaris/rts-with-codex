#ifndef PLAYER_ECONOMY_H
#define PLAYER_ECONOMY_H

#include "Resource.h"

#include <deque>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

class Player;

enum class ResearchMilestoneType
{
    Technology,
    Focus
};

struct ResearchMilestone
{
    double time{0.0};
    ResearchMilestoneType type{ResearchMilestoneType::Technology};
    std::string id;
};

struct ResourceFlowSnapshot
{
    double time{0.0};
    std::map<ResourceType, int> producedAmounts;
    std::map<ResourceType, int> consumedAmounts;
    std::map<ResourceType, int> productionRatesPerMinute;
    std::map<ResourceType, int> consumptionRatesPerMinute;
};

struct PlayerEconomyTelemetry
{
    ResourceFlowSnapshot current;
    std::deque<ResourceFlowSnapshot> history;
    std::vector<ResearchMilestone> researchMilestones;
    double elapsedTime{0.0};
    double sampleTimer{0.0};

    void RecordProduction(ResourceType type, int amount = 1);
    void RecordConsumption(ResourceType type, int amount = 1);
    void RecordResearchMilestone(ResearchMilestoneType type, const std::string& id);
    void Update(Player& player, double dt);
    ResourceFlowSnapshot BuildSnapshot(double time) const;
    static ResourceFlowSnapshot BuildSnapshot(Player& player, double time);

private:
    std::map<ResourceType, int> pendingProducedAmounts;
    std::map<ResourceType, int> pendingConsumedAmounts;
    std::set<std::pair<ResearchMilestoneType, std::string>> recordedMilestones;
};

#endif
