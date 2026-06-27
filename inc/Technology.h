#ifndef TECHNOLOGY_H
#define TECHNOLOGY_H

#include "BalanceModifiers.h"
#include "BuildingConfig.h"

#include <limits>
#include <set>
#include <string>
#include <vector>

struct TechnologyDefinition
{
    std::string id;
    std::string name;
    std::string description;
    std::string category{"PRODUCTION"};
    double researchTime{0.0};
    std::string governmentId;
    std::vector<std::string> prerequisites;
    std::vector<ResourceAmountDefinition> costs;
    std::vector<BalanceModifier> modifiers;
    std::vector<std::string> tags;
    std::string layoutLane;
    int layoutOrder{std::numeric_limits<int>::max()};
};

// Returns all technology definitions loaded from data files.
const std::vector<TechnologyDefinition>& GetTechnologyDefinitions();
const std::vector<TechnologyDefinition>& GetFocusDefinitions();

// Loads technology definitions from a specific data file.
std::vector<TechnologyDefinition> LoadTechnologyDefinitionsFromFile(const std::string& path);
std::vector<TechnologyDefinition> LoadFocusDefinitionsFromFile(const std::string& path);

// Finds one technology definition by id.
const TechnologyDefinition* FindTechnologyDefinition(const std::string& id);
const TechnologyDefinition* FindFocusDefinition(const std::string& id);

// Player-owned research state that exports unlocked technology modifiers.
class TechnologyState
{
public:
    // Returns whether the technology has already been unlocked.
    bool HasTechnology(const std::string& id) const;

    // Returns whether all prerequisites for the technology are currently met.
    bool CanUnlock(const std::string& id) const;

    // Unlocks a technology and makes its modifiers available.
    bool UnlockTechnology(const std::string& id);

    // Restores a technology from save data without prerequisite checks.
    void RestoreTechnology(const std::string& id);

    // Clears all unlocked technologies.
    void Clear();

    // Adds unlocked technology modifiers to a balance modifier set.
    void CollectModifiers(BalanceModifierSet& target) const;

    // Returns the unlocked technology ids for save files and UI.
    const std::set<std::string>& GetUnlocked() const { return unlocked; }

private:
    std::set<std::string> unlocked;
};

class FocusState
{
public:
    bool HasFocus(const std::string& id) const;
    bool CanUnlock(const std::string& id) const;
    bool CanStartFocus(const std::string& id) const;
    bool StartFocus(const std::string& id);
    bool UpdateActiveFocus(double dt);
    bool UnlockFocus(const std::string& id);
    void RestoreFocus(const std::string& id);
    void Clear();
    void CollectModifiers(BalanceModifierSet& target) const;
    const std::set<std::string>& GetUnlocked() const { return unlocked; }
    const std::string& GetActiveFocusId() const { return activeFocusId; }
    double GetActiveFocusRemaining() const { return activeFocusRemaining; }
    double GetActiveFocusProgress() const;

private:
    std::set<std::string> unlocked;
    std::string activeFocusId;
    double activeFocusRemaining{0.0};
};

#endif
