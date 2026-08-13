#include "TreeModel.h"

#include "data/RtsDataFile.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>

namespace
{
    // Counts `technology` blocks the way the game's parser sees them. Used only
    // to detect the game's silent fallback to built-in defaults (see Reload).
    size_t CountBlocksInFile(const std::string& path)
    {
        size_t count = 0;
        for (const auto& tokens : ReadRtsDataLines(path))
        {
            if (!tokens.empty() && tokens[0] == "technology")
                count++;
        }
        return count;
    }
}

TreeDocument::TreeDocument(TreeKind kind, std::string path)
    : kind(kind), path(std::move(path))
{
    Reload();
}

void TreeDocument::Reload()
{
    definitions = kind == TreeKind::Focus
        ? LoadFocusDefinitionsFromFile(path)
        : LoadTechnologyDefinitionsFromFile(path);

    // LoadXDefinitionsFromFile silently substitutes hardcoded defaults when the
    // file is missing or parses to zero blocks. That is fine for the game (it
    // still boots) but actively harmful in an editor — you would be editing
    // phantom nodes that are not in the file. Surface it instead.
    if (!std::filesystem::exists(path))
    {
        status = "FILE NOT FOUND: " + path + " - showing built-in defaults";
    }
    else if (CountBlocksInFile(path) == 0)
    {
        status = "No 'technology' blocks parsed - showing built-in defaults";
    }
    else
    {
        status = "Loaded " + std::to_string(definitions.size()) + " nodes from " + path;
    }

    dirty = false;
    LoadBuildingUnlocks();

    // Drop selections whose node disappeared from the file.
    for (auto it = taken.begin(); it != taken.end();)
    {
        bool stillExists = std::any_of(definitions.begin(), definitions.end(),
            [&](const TechnologyDefinition& definition) { return definition.id == *it; });
        it = stillExists ? std::next(it) : taken.erase(it);
    }
    ClearHistory();
}

TreeDocument::EditSnapshot TreeDocument::CaptureSnapshot() const
{
    return {definitions, buildingUnlocks, buildings, taken, dirty, buildingUnlocksDirty};
}

void TreeDocument::RestoreSnapshot(const EditSnapshot& snapshot)
{
    definitions = snapshot.definitions;
    buildingUnlocks = snapshot.buildingUnlocks;
    buildings = snapshot.buildings;
    taken = snapshot.taken;
    dirty = snapshot.dirty;
    buildingUnlocksDirty = snapshot.buildingUnlocksDirty;
}

bool TreeDocument::MatchesSnapshot(const EditSnapshot& snapshot) const
{
    if (SerializeTree(definitions) != SerializeTree(snapshot.definitions) ||
        buildings.size() != snapshot.buildings.size() ||
        buildingUnlocksDirty != snapshot.buildingUnlocksDirty)
        return false;
    for (size_t i = 0; i < buildings.size(); i++)
    {
        if (buildings[i].id != snapshot.buildings[i].id ||
            buildings[i].name != snapshot.buildings[i].name ||
            buildings[i].requiredTechnologies != snapshot.buildings[i].requiredTechnologies ||
            buildings[i].requiredFocuses != snapshot.buildings[i].requiredFocuses)
            return false;
    }
    return true;
}

void TreeDocument::CommitPendingHistory()
{
    if (!pendingHistory.has_value())
        return;
    if (!MatchesSnapshot(pendingHistory.value()))
    {
        undoHistory.push_back({std::move(pendingHistory.value()), CaptureSnapshot()});
        if (undoHistory.size() > HistoryCapacity)
            undoHistory.erase(undoHistory.begin());
        redoHistory.clear();
    }
    pendingHistory.reset();
}

void TreeDocument::BeginHistoryFrame()
{
    if (!historyTransaction && !pendingHistory.has_value())
        pendingHistory = CaptureSnapshot();
}

void TreeDocument::CommitHistoryFrame()
{
    if (!historyTransaction)
        CommitPendingHistory();
}

void TreeDocument::BeginHistoryTransaction()
{
    if (!pendingHistory.has_value())
        pendingHistory = CaptureSnapshot();
    historyTransaction = true;
}

void TreeDocument::CommitHistoryTransaction()
{
    if (!historyTransaction)
        return;
    historyTransaction = false;
    CommitPendingHistory();
}

void TreeDocument::CancelHistoryTransaction()
{
    if (pendingHistory.has_value())
        RestoreSnapshot(pendingHistory.value());
    pendingHistory.reset();
    historyTransaction = false;
}

bool TreeDocument::Undo()
{
    if (historyTransaction || undoHistory.empty())
        return false;
    EditCommand command = std::move(undoHistory.back());
    undoHistory.pop_back();
    RestoreSnapshot(command.before);
    redoHistory.push_back(std::move(command));
    return true;
}

bool TreeDocument::Redo()
{
    if (historyTransaction || redoHistory.empty())
        return false;
    EditCommand command = std::move(redoHistory.back());
    redoHistory.pop_back();
    RestoreSnapshot(command.after);
    undoHistory.push_back(std::move(command));
    return true;
}

void TreeDocument::ClearHistory()
{
    undoHistory.clear();
    redoHistory.clear();
    pendingHistory.reset();
    historyTransaction = false;
}

void TreeDocument::LoadBuildingUnlocks()
{
    buildingUnlocks.clear();
    buildings.clear();
    buildingUnlocksDirty = false;

    buildingsPath = (std::filesystem::path(path).parent_path() / "buildings.rtsdata").string();
    std::string buildingId;
    std::string buildingName;
    std::vector<std::string> requiredTechnologies;
    std::vector<std::string> requiredFocuses;
    int blockDepth = 0;

    // Only top-level `requires_tech` / `requires_focus` belong to a building
    // itself. Recipe and terrain-production blocks may have their own
    // requirements, but those unlock products rather than construction and
    // must stay out of this list.
    for (const auto& tokens : ReadRtsDataLines(buildingsPath))
    {
        if (blockDepth == 0)
        {
            if (tokens.size() >= 2 && tokens[0] == "building")
            {
                buildingId = tokens[1];
                buildingName = buildingId;
                requiredTechnologies.clear();
                requiredFocuses.clear();
                blockDepth = 1;
            }
            continue;
        }

        if (tokens[0] == "end")
        {
            if (--blockDepth == 0)
            {
                buildings.push_back({buildingId, buildingName, requiredTechnologies, requiredFocuses});
                const auto& requiredNodes = kind == TreeKind::Technology
                    ? requiredTechnologies
                    : requiredFocuses;
                for (const auto& nodeId : requiredNodes)
                {
                    auto& buildings = buildingUnlocks[nodeId];
                    if (std::find(buildings.begin(), buildings.end(), buildingName) == buildings.end())
                        buildings.push_back(buildingName);
                }
            }
            continue;
        }

        if (tokens[0] == "production" || tokens[0] == "recipe" || tokens[0] == "terrain_production")
        {
            blockDepth++;
            continue;
        }

        if (blockDepth == 1 && tokens.size() >= 2)
        {
            if (tokens[0] == "name")
                buildingName = tokens[1];
            else if (tokens[0] == "requires_tech")
                requiredTechnologies.push_back(tokens[1]);
            else if (tokens[0] == "requires_focus")
                requiredFocuses.push_back(tokens[1]);
        }
    }
}

const std::vector<std::string>& TreeDocument::GetUnlockedBuildings(const std::string& nodeId) const
{
    static const std::vector<std::string> none;
    const auto it = buildingUnlocks.find(nodeId);
    return it == buildingUnlocks.end() ? none : it->second;
}

std::vector<std::string> TreeDocument::GetBuildingUnlockOptions(const std::string&) const
{
    std::vector<std::string> result;
    result.reserve(buildings.size());
    for (const auto& building : buildings)
        result.push_back(building.name);

    // A building already gated by another node must remain selectable. Adding
    // it here creates an additional real requires_tech/requires_focus condition
    // in buildings.rtsdata; hiding it made University impossible to select from
    // most decision nodes even though the game supports multiple requirements.
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> TreeDocument::GetUnlockedBuildingIds(const std::string& nodeId) const
{
    std::vector<std::string> result;
    for (const auto& building : buildings)
    {
        const auto& requiredNodes = kind == TreeKind::Technology
            ? building.requiredTechnologies
            : building.requiredFocuses;
        if (std::find(requiredNodes.begin(), requiredNodes.end(), nodeId) != requiredNodes.end())
            result.push_back(building.id);
    }
    return result;
}

std::string TreeDocument::GetBuildingUnlockLabel(const std::string& buildingId) const
{
    const auto it = std::find_if(buildings.begin(), buildings.end(), [&](const BuildingUnlockDefinition& building)
    {
        return building.id == buildingId;
    });
    return it == buildings.end() ? buildingId : it->name;
}

std::string TreeDocument::GetBuildingUnlockIdForLabel(const std::string& label) const
{
    const auto it = std::find_if(buildings.begin(), buildings.end(), [&](const BuildingUnlockDefinition& building)
    {
        return building.name == label;
    });
    return it == buildings.end() ? std::string() : it->id;
}

void TreeDocument::SetBuildingUnlocks(const std::string& nodeId, const std::vector<std::string>& buildingIds)
{
    for (auto& building : buildings)
    {
        auto& requiredNodes = kind == TreeKind::Technology
            ? building.requiredTechnologies
            : building.requiredFocuses;
        const bool selected = std::find(buildingIds.begin(), buildingIds.end(), building.id) != buildingIds.end();
        auto node = std::find(requiredNodes.begin(), requiredNodes.end(), nodeId);
        if (selected && node == requiredNodes.end())
        {
            requiredNodes.push_back(nodeId);
            buildingUnlocksDirty = true;
        }
        else if (!selected && node != requiredNodes.end())
        {
            requiredNodes.erase(node);
            buildingUnlocksDirty = true;
        }
    }

    if (!buildingUnlocksDirty)
        return;

    buildingUnlocks.clear();
    for (const auto& building : buildings)
    {
        const auto& requiredNodes = kind == TreeKind::Technology
            ? building.requiredTechnologies
            : building.requiredFocuses;
        for (const auto& requiredNode : requiredNodes)
            buildingUnlocks[requiredNode].push_back(building.name);
    }
    dirty = true;
}

bool TreeDocument::SaveBuildingUnlocks()
{
    if (!buildingUnlocksDirty)
        return true;

    std::ifstream input(buildingsPath, std::ios::binary);
    if (!input.is_open())
        return false;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line))
        lines.push_back(line);

    std::ofstream output(buildingsPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
        return false;

    std::string currentId;
    int blockDepth = 0;
    bool wroteRequirements = false;
    auto writeRequirements = [&]()
    {
        const auto it = std::find_if(buildings.begin(), buildings.end(), [&](const BuildingUnlockDefinition& building)
        {
            return building.id == currentId;
        });
        if (it != buildings.end())
        {
            for (const auto& technologyId : it->requiredTechnologies)
                output << "    requires_tech " << technologyId << "\n";
            for (const auto& focusId : it->requiredFocuses)
                output << "    requires_focus " << focusId << "\n";
        }
        wroteRequirements = true;
    };

    for (const auto& sourceLine : lines)
    {
        const auto tokens = TokenizeRtsDataLine(sourceLine);
        if (blockDepth == 0 && tokens.size() >= 2 && tokens[0] == "building")
        {
            currentId = tokens[1];
            blockDepth = 1;
            wroteRequirements = false;
            output << sourceLine << "\n";
            continue;
        }
        if (blockDepth == 1 && !tokens.empty() &&
            (tokens[0] == "requires_tech" || tokens[0] == "requires_focus"))
        {
            if (!wroteRequirements)
                writeRequirements();
            continue;
        }
        if (blockDepth == 1 && !tokens.empty() && tokens[0] == "end")
        {
            if (!wroteRequirements)
                writeRequirements();
            blockDepth = 0;
            output << sourceLine << "\n";
            continue;
        }
        if (blockDepth > 0 && !tokens.empty() &&
            (tokens[0] == "production" || tokens[0] == "recipe" || tokens[0] == "terrain_production"))
            blockDepth++;
        else if (blockDepth > 1 && !tokens.empty() && tokens[0] == "end")
            blockDepth--;
        output << sourceLine << "\n";
    }

    if (!output.good())
        return false;
    buildingUnlocksDirty = false;
    return true;
}

bool TreeDocument::ArePrerequisitesTaken(const TechnologyDefinition& definition) const
{
    return std::all_of(definition.prerequisites.begin(), definition.prerequisites.end(),
        [&](const std::string& prerequisite) { return taken.count(prerequisite) > 0; });
}

std::vector<ResearchNodeView> TreeDocument::BuildNodes() const
{
    std::vector<ResearchNodeView> nodes;
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
        node.researchTime = definition.researchTime;
        node.prerequisites = definition.prerequisites;
        node.costs = definition.costs;
        node.modifiers = definition.modifiers;
        node.unlockedBuildings = GetUnlockedBuildings(definition.id);
        node.tags = definition.tags;
        // Same fallbacks as ResearchCatalog: an unset lane falls back to the
        // category, an unset order to the file position.
        node.layoutLane = definition.layoutLane.empty() ? definition.category : definition.layoutLane;
        node.layoutOrder = definition.layoutOrder == std::numeric_limits<int>::max()
            ? static_cast<int>(i)
            : definition.layoutOrder;
        node.definitionIndex = static_cast<int>(i);

        node.researched = taken.count(definition.id) > 0;
        node.prerequisitesMet = ArePrerequisitesTaken(definition);
        // No economy in the editor: costs are shown, never checked.
        node.canPay = true;
        node.available = node.prerequisitesMet && !node.researched;
        node.active = false;
        node.remainingTime = 0.0;
        node.progress = node.researched ? 1.0 : 0.0;
        node.stateText = node.researched ? "Taken"
                       : node.available ? "Available"
                       : "Locked";
        nodes.push_back(std::move(node));
    }

    return nodes;
}

std::string TreeDocument::AddNode(const std::string& parentId, const std::string& lane, int layoutOrder)
{
    // Generated ids stay stable and unique; the user renames them in the
    // inspector. Prefixed per tree so a focus and a tech id never collide when
    // both files end up open.
    std::string prefix = kind == TreeKind::Focus ? "new_focus_" : "new_tech_";
    int suffix = 1;
    std::string id;
    do
    {
        id = prefix + std::to_string(suffix++);
    } while (Find(id) != nullptr);

    TechnologyDefinition definition;
    definition.id = id;
    definition.name = "New Node";
    definition.description = "";
    definition.layoutLane = lane;
    definition.layoutOrder = layoutOrder;
    definition.researchTime = 60.0;
    if (const auto* parent = Find(parentId))
    {
        definition.prerequisites.push_back(parentId);
        // Inherit the parent's category so the node does not jump lanes when
        // layout_lane is left empty (the view falls back to category).
        definition.category = parent->category;
    }
    else
    {
        definition.category = kind == TreeKind::Focus ? "PRODUCTION" : "SCIENCE";
    }

    definitions.push_back(std::move(definition));
    dirty = true;
    return id;
}

void TreeDocument::DeleteNode(const std::string& id)
{
    definitions.erase(
        std::remove_if(definitions.begin(), definitions.end(),
            [&](const TechnologyDefinition& definition) { return definition.id == id; }),
        definitions.end());

    // A dangling `requires` would silently make the child unreachable, so strip
    // the reference everywhere rather than leaving it to the layout to ignore.
    for (auto& definition : definitions)
    {
        definition.prerequisites.erase(
            std::remove(definition.prerequisites.begin(), definition.prerequisites.end(), id),
            definition.prerequisites.end());
    }

    taken.erase(id);
    dirty = true;
}

void TreeDocument::DeleteNodes(const std::vector<std::string>& ids)
{
    if (ids.empty())
        return;

    std::set<std::string> removed(ids.begin(), ids.end());
    definitions.erase(
        std::remove_if(definitions.begin(), definitions.end(), [&](const TechnologyDefinition& definition)
        {
            return removed.contains(definition.id);
        }),
        definitions.end());
    for (auto& definition : definitions)
    {
        definition.prerequisites.erase(
            std::remove_if(definition.prerequisites.begin(), definition.prerequisites.end(), [&](const std::string& prerequisite)
            {
                return removed.contains(prerequisite);
            }),
            definition.prerequisites.end());
    }
    for (const auto& id : removed)
        taken.erase(id);
    dirty = true;
}

bool TreeDocument::RenameNode(const std::string& oldId, const std::string& newId)
{
    if (newId.empty() || oldId == newId || Find(newId) != nullptr)
        return false;

    auto* definition = Find(oldId);
    if (definition == nullptr)
        return false;

    definition->id = newId;
    for (auto& other : definitions)
        std::replace(other.prerequisites.begin(), other.prerequisites.end(), oldId, newId);

    if (taken.erase(oldId) > 0)
        taken.insert(newId);
    dirty = true;
    return true;
}

void TreeDocument::SetLanePosition(const std::string& id, const std::string& lane, int layoutOrder)
{
    auto* definition = Find(id);
    if (definition == nullptr)
        return;

    definition->layoutLane = lane;
    definition->layoutOrder = layoutOrder;
    dirty = true;
}

TechnologyDefinition* TreeDocument::Find(const std::string& id)
{
    auto it = std::find_if(definitions.begin(), definitions.end(),
        [&](const TechnologyDefinition& definition) { return definition.id == id; });
    return it == definitions.end() ? nullptr : &*it;
}

const TechnologyDefinition* TreeDocument::Find(const std::string& id) const
{
    auto it = std::find_if(definitions.begin(), definitions.end(),
        [&](const TechnologyDefinition& definition) { return definition.id == id; });
    return it == definitions.end() ? nullptr : &*it;
}

std::vector<std::string> TreeDocument::CollectLanes() const
{
    std::vector<std::string> lanes;
    for (const auto& definition : definitions)
    {
        const std::string& lane = definition.layoutLane.empty() ? definition.category : definition.layoutLane;
        if (std::find(lanes.begin(), lanes.end(), lane) == lanes.end())
            lanes.push_back(lane);
    }
    return lanes;
}

SaveResult TreeDocument::Save()
{
    SaveResult result = SaveTree(path, definitions, kind == TreeKind::Focus);
    if (result.ok && !SaveBuildingUnlocks())
        result = {false, "Tree saved, but cannot update building unlocks: " + buildingsPath};
    status = result.message;
    if (result.ok)
        dirty = false;
    return result;
}

void TreeDocument::ToggleTaken(const std::string& id)
{
    if (!taken.insert(id).second)
        taken.erase(id);
}

void TreeDocument::ClearTaken()
{
    taken.clear();
}

bool TreeDocument::IsTaken(const std::string& id) const
{
    return taken.count(id) > 0;
}
