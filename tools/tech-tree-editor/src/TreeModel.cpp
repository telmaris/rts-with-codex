#include "TreeModel.h"

#include "data/RtsDataFile.h"

#include <algorithm>
#include <filesystem>
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

    // Drop selections whose node disappeared from the file.
    for (auto it = taken.begin(); it != taken.end();)
    {
        bool stillExists = std::any_of(definitions.begin(), definitions.end(),
            [&](const TechnologyDefinition& definition) { return definition.id == *it; });
        it = stillExists ? std::next(it) : taken.erase(it);
    }
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
