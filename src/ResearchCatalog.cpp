#include "../inc/ResearchCatalog.h"
#include "../inc/Player.h"

#include <limits>

// Returns all technology nodes decorated with player-specific availability state.
std::vector<ResearchNodeView> ResearchCatalog::BuildView(const Player& player)
{
    std::vector<ResearchNodeView> nodes;
    const auto& definitions = GetTechnologyDefinitions();
    nodes.reserve(definitions.size());

    for (size_t i = 0; i < definitions.size(); i++)
    {
        const auto& definition = definitions[i];
        ResearchNodeView node;
        node.definition = &definition;
        node.id = definition.id;
        node.name = definition.name;
        node.description = definition.description;
        node.category = definition.category;
        node.governmentId = definition.governmentId;
        node.researchTime = definition.researchTime;
        node.prerequisites = definition.prerequisites;
        node.costs = definition.costs;
        node.modifiers = definition.modifiers;
        node.tags = definition.tags;
        node.layoutLane = definition.layoutLane.empty() ? definition.category : definition.layoutLane;
        node.layoutOrder = definition.layoutOrder == std::numeric_limits<int>::max() ? static_cast<int>(i) : definition.layoutOrder;
        node.definitionIndex = static_cast<int>(i);
        node.researched = player.technologies.HasTechnology(definition.id);
        for (const auto* building : player.GetTrackedBuildingsWithComponent<ResearchComponent>())
        {
            const auto* research = building != nullptr ? building->GetComponent<ResearchComponent>() : nullptr;
            if (research != nullptr && building->owner == &player &&
                building->buildingType == BuildingType::University &&
                research->technologyId == definition.id)
            {
                node.active = true;
                node.remainingTime = research->remaining;
                node.progress = research->GetProgress();
                break;
            }
        }
        node.prerequisitesMet = player.technologies.CanUnlock(definition.id);
        node.canPay = player.HasBuildResources(definition.costs);
        node.available = player.CanResearchTechnology(definition.id);
        node.stateText = node.researched ? "Researched"
                       : node.active ? "In progress"
                       : node.available ? "Available"
                       : node.prerequisitesMet && !node.canPay ? "Need resources"
                       : "Locked";
        nodes.push_back(std::move(node));
    }

    return nodes;
}

std::vector<ResearchNodeView> ResearchCatalog::BuildFocusView(const Player& player)
{
    std::vector<ResearchNodeView> nodes;
    const auto& definitions = GetFocusDefinitions();
    nodes.reserve(definitions.size());

    for (size_t i = 0; i < definitions.size(); i++)
    {
        const auto& definition = definitions[i];
        ResearchNodeView node;
        node.definition = &definition;
        node.id = definition.id;
        node.name = definition.name;
        node.description = definition.description;
        node.category = definition.category;
        node.governmentId = definition.governmentId;
        node.researchTime = definition.researchTime;
        node.prerequisites = definition.prerequisites;
        node.costs.clear();
        node.modifiers = definition.modifiers;
        node.tags = definition.tags;
        node.layoutLane = definition.layoutLane.empty() ? definition.category : definition.layoutLane;
        node.layoutOrder = definition.layoutOrder == std::numeric_limits<int>::max() ? static_cast<int>(i) : definition.layoutOrder;
        node.definitionIndex = static_cast<int>(i);
        node.researched = player.focuses.HasFocus(definition.id);
        node.prerequisitesMet = player.focuses.CanUnlock(definition.id);
        node.canPay = true;
        node.available = player.CanUnlockFocus(definition.id);
        node.active = player.focuses.GetActiveFocusId() == definition.id;
        node.remainingTime = node.active ? player.focuses.GetActiveFocusRemaining() : 0.0;
        node.progress = node.active ? player.focuses.GetActiveFocusProgress() : (node.researched ? 1.0 : 0.0);
        node.stateText = node.researched ? "Completed"
                       : node.active ? "In progress"
                       : node.available ? "Available"
                       : "Locked";
        nodes.push_back(std::move(node));
    }

    return nodes;
}
