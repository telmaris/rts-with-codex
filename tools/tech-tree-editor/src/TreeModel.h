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

#include <map>
#include <optional>
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
    void DeleteNodes(const std::vector<std::string>& ids);

    TechnologyDefinition* Find(const std::string& id);
    const TechnologyDefinition* Find(const std::string& id) const;
    // Every distinct lane currently in use, in display order.
    std::vector<std::string> CollectLanes() const;

    // Marks the model as changed since the last save/reload.
    void MarkDirty() { dirty = true; }
    bool IsDirty() const { return dirty; }

    // Serializes to GetPath() and verifies the round trip; see TreeSerializer.
    SaveResult Save();

    // Command history. A command stores the full editable document state,
    // including pending building-unlock relations, so undo is safe across
    // coupled technologies.rtsdata / buildings.rtsdata edits.
    void BeginHistoryFrame();
    void CommitHistoryFrame();
    void BeginHistoryTransaction();
    void CommitHistoryTransaction();
    void CancelHistoryTransaction();
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !undoHistory.empty(); }
    bool CanRedo() const { return !redoHistory.empty(); }
    void ClearHistory();

    TreeKind GetKind() const { return kind; }
    const std::string& GetPath() const { return path; }
    // Short "loaded N nodes" / failure text for the status line.
    const std::string& GetStatus() const { return status; }
    size_t GetNodeCount() const { return definitions.size(); }
    const std::vector<TechnologyDefinition>& GetDefinitions() const { return definitions; }
    // Display names of buildings whose construction lists this technology as a
    // requirement. This is the same relationship the game exposes as
    // "Unlocks <building>" in technology tooltips.
    const std::vector<std::string>& GetUnlockedBuildings(const std::string& technologyId) const;
    // Editor labels for buildings that are not technology-gated yet, plus
    // buildings already unlocked by `technologyId`.
    std::vector<std::string> GetBuildingUnlockOptions(const std::string& technologyId) const;
    // Stable building ids corresponding to the selected technology's Unlocks
    // rows. Used by the inspector; display labels are intentionally separate.
    std::vector<std::string> GetUnlockedBuildingIds(const std::string& technologyId) const;
    std::string GetBuildingUnlockLabel(const std::string& buildingId) const;
    std::string GetBuildingUnlockIdForLabel(const std::string& label) const;
    // Replaces this technology's building-unlock rows. The relation is stored
    // as top-level `requires_tech` in buildings.rtsdata, not as a faux stat
    // modifier inside technologies.rtsdata.
    void SetBuildingUnlocks(const std::string& technologyId, const std::vector<std::string>& buildingIds);

private:
    bool ArePrerequisitesTaken(const TechnologyDefinition& definition) const;
    void LoadBuildingUnlocks();
    bool SaveBuildingUnlocks();

    TreeKind kind;
    std::string path;
    std::vector<TechnologyDefinition> definitions;
    std::map<std::string, std::vector<std::string>> buildingUnlocks;
    struct BuildingUnlockDefinition
    {
        std::string id;
        std::string name;
        std::vector<std::string> requiredTechnologies;
    };
    std::vector<BuildingUnlockDefinition> buildings;
    std::string buildingsPath;
    std::set<std::string> taken;
    std::string status;
    bool dirty{false};
    bool buildingUnlocksDirty{false};

    struct EditSnapshot
    {
        std::vector<TechnologyDefinition> definitions;
        std::map<std::string, std::vector<std::string>> buildingUnlocks;
        std::vector<BuildingUnlockDefinition> buildings;
        std::set<std::string> taken;
        bool dirty{false};
        bool buildingUnlocksDirty{false};
    };
    // A reversible editor command: before/after snapshots make every command
    // deterministic even when it spans both data files.
    struct EditCommand
    {
        EditSnapshot before;
        EditSnapshot after;
    };
    EditSnapshot CaptureSnapshot() const;
    void RestoreSnapshot(const EditSnapshot& snapshot);
    bool MatchesSnapshot(const EditSnapshot& snapshot) const;
    void CommitPendingHistory();

    static constexpr size_t HistoryCapacity = 128;
    std::vector<EditCommand> undoHistory;
    std::vector<EditCommand> redoHistory;
    std::optional<EditSnapshot> pendingHistory;
    bool historyTransaction{false};
};

#endif
