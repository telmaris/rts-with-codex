#ifndef TREE_MODEL_H
#define TREE_MODEL_H

// Editor-side replacement for ResearchCatalog::BuildView/BuildFocusView.
//
// The game decorates each node with player state (researched, affordable, which
// University is working on it). The editor has no economy, so node state comes
// from one editor-owned set: which nodes the user has "taken". That set also
// becomes the selection the bonus summary will aggregate over.
//
// Definitions themselves are NOT parsed here — they come from the game's real
// LoadTechnologyDefinitionsFromFile/LoadFocusDefinitionsFromFile.

#include "TreeSerializer.h"

#include "research/ResearchCatalog.h"
#include "research/Technology.h"

#include <set>
#include <string>
#include <vector>

enum class TreeKind
{
    Technology,
    Focus,
};

class TreeDocument
{
public:
    TreeDocument(TreeKind kind, std::string path);

    // (Re)reads the file from disk. Keeps the taken-set for ids that still
    // exist, so a reload after an edit does not wipe the current selection.
    void Reload();

    // Builds render-ready nodes in file order, mirroring ResearchCatalog's
    // field mapping (including its layoutOrder / layoutLane fallbacks).
    std::vector<ResearchNodeView> BuildNodes() const;

    void ToggleTaken(const std::string& id);
    void ClearTaken();
    bool IsTaken(const std::string& id) const;

    // --- editing ---------------------------------------------------------

    // Creates a node, optionally as a child of `parentId`, at the given lane
    // and layout_order. Returns the new node's generated id.
    std::string AddNode(const std::string& parentId, const std::string& lane, int layoutOrder);
    // Removes the node and drops it from every other node's prerequisites.
    void DeleteNode(const std::string& id);
    // Renames a node, rewriting references to it in other nodes' prerequisites.
    // Returns false when the new id is empty or already taken.
    bool RenameNode(const std::string& oldId, const std::string& newId);
    void SetLanePosition(const std::string& id, const std::string& lane, int layoutOrder);

    TechnologyDefinition* Find(const std::string& id);
    const TechnologyDefinition* Find(const std::string& id) const;
    // Every distinct lane currently in use, in display order.
    std::vector<std::string> CollectLanes() const;

    // Marks the model as changed since the last save/reload.
    void MarkDirty() { dirty = true; }
    bool IsDirty() const { return dirty; }

    // Serializes to GetPath() and verifies the round trip; see TreeSerializer.
    SaveResult Save();

    TreeKind GetKind() const { return kind; }
    const std::string& GetPath() const { return path; }
    // Short "loaded N nodes" / failure text for the status line.
    const std::string& GetStatus() const { return status; }
    size_t GetNodeCount() const { return definitions.size(); }
    const std::vector<TechnologyDefinition>& GetDefinitions() const { return definitions; }

private:
    bool ArePrerequisitesTaken(const TechnologyDefinition& definition) const;

    TreeKind kind;
    std::string path;
    std::vector<TechnologyDefinition> definitions;
    std::set<std::string> taken;
    std::string status;
    bool dirty{false};
};

#endif
