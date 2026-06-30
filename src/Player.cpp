#include "Player.h"
#include "MapGenerator.h"
#include "BuildingConfig.h"
#include "Technology.h"

#include <algorithm>
#include <cmath>

void Player::UpdateFocus(double dt)
{
    std::string completedFocus = focuses.GetActiveFocusId();
    if (focuses.UpdateActiveFocus(dt))
    {
        economyTelemetry.RecordResearchMilestone(ResearchMilestoneType::Focus, completedFocus);
        RefreshTechnologyModifiers();
        tilemap.RecalculateTerritory(this);
    }
}

void Player::UpdateResearch(double dt)
{
    bool territoryChanged = false;
    for (auto* building : GetTrackedBuildingsWithComponent<ResearchComponent>())
    {
        auto* research = building != nullptr ? building->GetComponent<ResearchComponent>() : nullptr;
        if (research == nullptr || building->owner != this ||
            building->buildingType != BuildingType::University)
            continue;

        std::string completedTechnology = research->technologyId;
        if (completedTechnology.empty() || !research->Tick(dt))
            continue;

        research->technologyId.clear();
        research->remaining = 0.0;
        research->total = 0.0;
        if (technologies.UnlockTechnology(completedTechnology))
        {
            economyTelemetry.RecordResearchMilestone(ResearchMilestoneType::Technology, completedTechnology);
            RefreshTechnologyModifiers();
            territoryChanged = true;
        }
    }
    if (territoryChanged)
        tilemap.RecalculateTerritory(this);
}

bool Player::IsTechnologyInProgress(const std::string& id) const
{
    for (auto* building : GetTrackedBuildingsWithComponent<ResearchComponent>())
    {
        const auto* research = building != nullptr ? building->GetComponent<ResearchComponent>() : nullptr;
        if (research != nullptr && building->owner == this &&
            building->buildingType == BuildingType::University &&
            research->technologyId == id)
            return true;
    }
    return false;
}

bool Player::HasBuildResources(const std::vector<ResourceAmountDefinition>& costs) const
{
    for (const auto& cost : costs)
    {
        int available = 0;
        for (const auto* building : GetTrackedBuildingsWithComponent<StorageComponent>())
        {
            const auto* storage = building != nullptr ? building->GetComponent<StorageComponent>() : nullptr;
            if (storage == nullptr || building->owner != this)
                continue;

            auto it = storage->buffers.find(cost.type);
            if (it != storage->buffers.end())
                available += static_cast<int>(it->second.buffer.size());
        }

        if (available < cost.amount)
            return false;
    }
    return true;
}

std::vector<std::string> Player::GetBuildRequirementFailures(const BuildingDefinition& definition,
                                                              bool ignoreDebugFreeBuild) const
{
    std::vector<std::string> failures;
    for (const auto& technology : definition.requiredTechnologies)
        if (!technologies.HasTechnology(technology))
            failures.push_back("Requires tech: " + technology);
    for (const auto& focus : definition.requiredFocuses)
        if (!focuses.HasFocus(focus))
            failures.push_back("Requires focus: " + focus);
    if ((!tilemap.params.debugMode || !ignoreDebugFreeBuild) && !HasBuildResources(definition.buildCosts))
        failures.push_back("Not enough resources");
    return failures;
}

int Player::GetPopulationCap() const
{
    int cap = 0;
    for (const auto* building : GetTrackedBuildingsWithComponent<PopulationComponent>())
    {
        const auto* population = building != nullptr ? building->GetComponent<PopulationComponent>() : nullptr;
        if (population != nullptr && building->owner == this && !building->IsUnderConstruction())
            cap += ResolveStat(population->populationCap, building);
    }
    return cap;
}

ArmyRegistry Player::GetArmyRegistry() const
{
    ArmyRegistry registry;
    for (const auto* building : GetTrackedBuildingsWithComponent<GarrisonComponent>())
    {
        const auto* garrison = building != nullptr ? building->GetComponent<GarrisonComponent>() : nullptr;
        const auto* supply = building != nullptr ? building->GetComponent<SupplyBufferComponent>() : nullptr;
        if (garrison == nullptr || building->owner != this || building->IsUnderConstruction())
            continue;

        registry.militia += garrison->militia;
        registry.swordsmen += garrison->swordsmen;
        registry.archers += garrison->archers;
        registry.garrisonCapacity += garrison->GetTotalTroops() + garrison->GetFreeGarrisonSpace(*building);
        if (supply != nullptr)
        {
            registry.supply += supply->stored;
            registry.supplyCapacity += supply->GetModifiedCapacity(*building);
            registry.supplyConsumption += supply->GetSupplyConsumption(*building, *garrison);
        }
        registry.strength += garrison->GetEffectiveStrength(*building);

        if (const auto* recruitment = building->GetComponent<RecruitmentComponent>())
        {
            for (const auto& job : recruitment->queue)
            {
                switch (job.type)
                {
                    case MilitaryUnitType::Swordsman: registry.queuedSwordsmen++; break;
                    case MilitaryUnitType::Archer:    registry.queuedArchers++;   break;
                    case MilitaryUnitType::Militia:
                    default:                          registry.queuedMilitia++;   break;
                }
            }
        }
    }
    return registry;
}

double Player::GetFoodProductivity() const
{
    int villageCount = 0;
    double productivity = 0.0;
    for (const auto* building : GetTrackedBuildingsWithComponent<PopulationComponent>())
    {
        const auto* population = building != nullptr ? building->GetComponent<PopulationComponent>() : nullptr;
        if (population == nullptr || building->owner != this || building->IsUnderConstruction())
            continue;

        villageCount++;
        productivity += population->GetWorkerProductivity();
    }

    return villageCount > 0 ? productivity / villageCount : 1.0;
}

bool Player::CanResearchTechnology(const std::string& id) const
{
    const auto* definition = FindTechnologyDefinition(id);
    return definition != nullptr &&
           technologies.CanUnlock(id) &&
           !IsTechnologyInProgress(id) &&
           (tilemap.params.debugMode || HasBuildResources(definition->costs));
}

bool Player::UnlockFocus(const std::string& id)
{
    const auto* definition = FindFocusDefinition(id);
    if (definition == nullptr || !focuses.CanUnlock(id))
        return false;

    if (!focuses.UnlockFocus(id))
        return false;

    economyTelemetry.RecordResearchMilestone(ResearchMilestoneType::Focus, id);
    RefreshTechnologyModifiers();
    return true;
}

bool Player::StartFocus(const std::string& id)
{
    const auto* definition = FindFocusDefinition(id);
    if (definition == nullptr)
        return false;

    bool wasUnlocked = focuses.HasFocus(id);
    if (!focuses.StartFocus(id))
        return false;

    if (!wasUnlocked && focuses.HasFocus(id))
    {
        economyTelemetry.RecordResearchMilestone(ResearchMilestoneType::Focus, id);
        RefreshTechnologyModifiers();
        tilemap.RecalculateTerritory(this);
    }
    return true;
}

bool Player::UnlockTechnology(const std::string& id)
{
    const auto* definition = FindTechnologyDefinition(id);
    if (definition == nullptr || !technologies.CanUnlock(id))
        return false;

    if (!tilemap.params.debugMode && !TryPayBuildCost(definition->costs))
        return false;

    if (!technologies.UnlockTechnology(id))
        return false;

    economyTelemetry.RecordResearchMilestone(ResearchMilestoneType::Technology, id);
    RefreshTechnologyModifiers();
    return true;
}

bool Player::StartTechnologyResearch(const std::string& id, Building* university)
{
    const auto* definition = FindTechnologyDefinition(id);
    auto* research = university != nullptr ? university->GetComponent<ResearchComponent>() : nullptr;
    if (definition == nullptr || university == nullptr || university->owner != this ||
        university->buildingType != BuildingType::University || research == nullptr ||
        !research->technologyId.empty())
        return false;

    if (!CanResearchTechnology(id))
        return false;

    if (!tilemap.params.debugMode && !TryPayBuildCost(definition->costs))
        return false;

    double researchTime = ModifyBalanceForBuilding(
        BalanceStat::ProductionCycleTime,
        definition->researchTime,
        university,
        ResourceType::Null,
        std::nullopt);
    if (!research->Start(id, researchTime))
        return false;

    return true;
}

void Player::RefreshTechnologyModifiers()
{
    balanceModifiers.ClearSourcePrefix("tech:");
    balanceModifiers.ClearSourcePrefix("focus:");
    balanceModifiers.ClearSourcePrefix("state:");
    std::set<std::string> unlockedGovernmentIds;
    for (const auto& focusId : focuses.GetUnlocked())
    {
        const auto* focus = FindFocusDefinition(focusId);
        if (focus != nullptr && !focus->governmentId.empty())
            unlockedGovernmentIds.insert(focus->governmentId);
    }
    stateDevelopment.RefreshFromGovernmentIds(unlockedGovernmentIds);
    technologies.CollectModifiers(balanceModifiers);
    focuses.CollectModifiers(balanceModifiers);
    stateDevelopment.CollectModifiers(balanceModifiers);
}

double Player::AddManpower(double amount)
{
    if (amount <= 0.0)
        return 0.0;

    double cap = static_cast<double>(GetPopulationCap());
    double room = std::max(0.0, cap - GetTotalPopulation());
    double added = std::min(amount, room);
    if (added > 0.0)
        strategicResources.Add(StrategicResourceType::Manpower, added);
    return added;
}

int Player::AutoAssignWorkers(Building* building)
{
    if (building == nullptr || building->owner != this)
        return 0;

    auto* workers = building->GetComponent<WorkerComponent>();
    if (workers == nullptr)
        return 0;

    int needed = std::max(0, building->GetWorkerCapacity() - workers->assigned);
    int available = static_cast<int>(std::floor(strategicResources.Get(StrategicResourceType::Manpower)));
    int assigned = std::min(needed, available);
    if (assigned <= 0)
        return 0;

    strategicResources.Consume(StrategicResourceType::Manpower, assigned);
    strategicResources.Add(StrategicResourceType::Workers, assigned);
    workers->assigned += assigned;
    return assigned;
}

bool Player::TryPayBuildCost(const std::vector<ResourceAmountDefinition>& costs)
{
    if (!HasBuildResources(costs))
        return false;

    for (const auto& cost : costs)
    {
        int remaining = cost.amount;
        for (auto* building : GetTrackedBuildingsWithComponent<StorageComponent>())
        {
            auto* storage = building != nullptr ? building->GetComponent<StorageComponent>() : nullptr;
            if (storage == nullptr || building->owner != this)
                continue;

            auto it = storage->buffers.find(cost.type);
            if (it == storage->buffers.end())
                continue;

            while (remaining > 0 && !it->second.buffer.empty())
            {
                it->second.FreeResource();
                economyTelemetry.RecordConsumption(cost.type);
                remaining--;
            }

            if (remaining == 0)
                break;
        }
    }

    return true;
}

// ── PlayerEconomyTelemetry ──────────────────────────────────────────────────

namespace
{
    constexpr double FlowRateWindowSeconds = 60.0;
    constexpr double FlowHistoryRetentionSeconds = 300.0;

    void AddAmounts(std::map<ResourceType, int>& target, const std::map<ResourceType, int>& source)
    {
        for (const auto& [type, amount] : source)
            target[type] += amount;
    }
}

void PlayerEconomyTelemetry::RecordProduction(ResourceType type, int amount)
{
    if (type == ResourceType::Null || amount <= 0)
        return;
    pendingProducedAmounts[type] += amount;
}

void PlayerEconomyTelemetry::RecordConsumption(ResourceType type, int amount)
{
    if (type == ResourceType::Null || amount <= 0)
        return;
    pendingConsumedAmounts[type] += amount;
}

void PlayerEconomyTelemetry::RecordResearchMilestone(ResearchMilestoneType type,
                                                     const std::string& id)
{
    if (id.empty())
        return;

    auto key = std::make_pair(type, id);
    if (!recordedMilestones.insert(key).second)
        return;

    researchMilestones.push_back({elapsedTime, type, id});
}

void PlayerEconomyTelemetry::Update(Player&, double dt)
{
    elapsedTime += std::max(0.0, dt);
    sampleTimer += std::max(0.0, dt);

    if (sampleTimer >= 1.0 || history.empty())
    {
        ResourceFlowSnapshot sample = BuildSnapshot(elapsedTime);
        sample.producedAmounts = pendingProducedAmounts;
        sample.consumedAmounts = pendingConsumedAmounts;
        history.push_back(std::move(sample));

        pendingProducedAmounts.clear();
        pendingConsumedAmounts.clear();
        sampleTimer = 0.0;
        while (!history.empty() && elapsedTime - history.front().time > FlowHistoryRetentionSeconds)
            history.pop_front();
    }

    current = BuildSnapshot(elapsedTime);
}

ResourceFlowSnapshot PlayerEconomyTelemetry::BuildSnapshot(double time) const
{
    ResourceFlowSnapshot snapshot;
    snapshot.time = time;
    snapshot.producedAmounts = pendingProducedAmounts;
    snapshot.consumedAmounts = pendingConsumedAmounts;

    std::map<ResourceType, int> producedTotals = pendingProducedAmounts;
    std::map<ResourceType, int> consumedTotals = pendingConsumedAmounts;
    double oldestSampleTime = time;

    for (auto it = history.rbegin(); it != history.rend(); ++it)
    {
        if (time - it->time > FlowRateWindowSeconds)
            break;

        oldestSampleTime = std::min(oldestSampleTime, it->time);
        AddAmounts(producedTotals, it->producedAmounts);
        AddAmounts(consumedTotals, it->consumedAmounts);
    }

    double duration = std::clamp(time - oldestSampleTime + 1.0, 1.0, FlowRateWindowSeconds);
    for (const auto& [type, amount] : producedTotals)
        if (amount > 0)
            snapshot.productionRatesPerMinute[type] =
                static_cast<int>(std::round(amount * 60.0 / duration));
    for (const auto& [type, amount] : consumedTotals)
        if (amount > 0)
            snapshot.consumptionRatesPerMinute[type] =
                static_cast<int>(std::round(amount * 60.0 / duration));

    return snapshot;
}

ResourceFlowSnapshot PlayerEconomyTelemetry::BuildSnapshot(Player& player, double time)
{
    return player.economyTelemetry.BuildSnapshot(time);
}
