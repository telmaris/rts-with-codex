#ifndef RESEARCH_CATALOG_H
#define RESEARCH_CATALOG_H

#include "research/Technology.h"

#include <string>
#include <vector>

class Player;

// UI-ready state for one technology node.
struct ResearchNodeView
{
    const TechnologyDefinition* definition{nullptr};
    std::string id;
    std::string name;
    std::string description;
    std::string category;
    std::string stateText;
    double researchTime{0.0};
    bool researched{false};
    bool prerequisitesMet{false};
    bool canPay{false};
    bool available{false};
    bool active{false};
    double remainingTime{0.0};
    double progress{0.0};
    std::vector<std::string> prerequisites;
    std::vector<ResourceAmountDefinition> costs;
    std::vector<BalanceModifier> modifiers;
    // Building names gated by this technology. Kept in the view model so
    // every research UI can explain the practical effect consistently.
    std::vector<std::string> unlockedBuildings;
    std::vector<std::string> tags;
    std::string layoutLane;
    int layoutOrder{0};
    int definitionIndex{0};
};

// Builds data-driven view models for research panels.
class ResearchCatalog
{
public:
    // Returns all technology nodes decorated with player-specific availability state.
    static std::vector<ResearchNodeView> BuildView(const Player& player);
    static std::vector<ResearchNodeView> BuildFocusView(const Player& player);
};

#endif
