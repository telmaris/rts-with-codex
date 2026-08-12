#ifndef TREE_VIEW_H
#define TREE_VIEW_H

// Port of ResearchTreePanelWidget from the game's src/ui/GuiResearchTree.cpp.
//
// The layout pass (lanes -> depth rows -> horizontal placement), the edge
// routing and the node card drawing are copied verbatim so the tree reads
// identically to the in-game panel. What changed, and only this:
//   * data comes from TreeDocument instead of Player/GameScene,
//   * clicking a node toggles the editor's taken-set instead of submitting a
//     GameCommand,
//   * input goes straight to raylib instead of through InputManager.
// Everything the editor adds later (selection handles, node dragging, insert
// ghosts) belongs here, which is why this file is a copy and not shared.

#include "research/ResearchCatalog.h"

#include "raylib.h"

#include <map>
#include <set>
#include <string>
#include <vector>

class TreeDocument;

// Where a node is being placed, in FILE coordinates rather than pixels: the
// lane string and the layout_order that encodes layer and horizontal slot
// (layout_order = (layer + 1) * 1000 + order, with order in 0..999). Dragging
// edits these, and the layout recomputes from them every frame — so the preview
// is the real layout, not an approximation of it.
struct PlacementTarget
{
    std::string lane;
    int layer{0};
    int order{500};

    int ToLayoutOrder() const { return (layer + 1) * 1000 + order; }
};

class TreeView
{
public:
    // Draws the tree inside `bounds` and handles pan/zoom/click within it.
    void Draw(Rectangle bounds, TreeDocument& document);

    // Id of the node shown in the inspector; empty when nothing is selected.
    std::string selectedNodeId;
    // Independent editor selection. Ctrl+drag adds every node touched by the
    // marquee; a subsequent drag on any selected node moves the whole group.
    std::set<std::string> selectedNodeIds;
    // Set while a node follows the mouse (freshly added, or being dragged).
    std::string placingNodeId;
    PlacementTarget placement;

    // True while a placement is in progress — the caller suppresses shortcuts
    // that would otherwise fight with it.
    bool IsPlacing() const { return !placingNodeId.empty(); }
    void CancelPlacement(TreeDocument& document);

    // Mouse-wheel zoom anchored at `point`; ignored outside the tree area.
    void AdjustZoom(Rectangle bounds, Vector2 point, float wheel);
    // Ctrl+wheel vertical scroll.
    void AdjustScroll(float wheel);
    // Recenters the view on the first lane (used after a reload or tree switch).
    void ResetCamera();

    bool ContainsTreeArea(Rectangle bounds, Vector2 point) const;
    void ClearNodeSelection();

    float zoom{0.78f};
    std::string selectedTagFilter;

private:
    Rectangle GetTreeArea(Rectangle bounds) const;

    // Lane rectangles from the last layout pass, used to map a mouse position
    // back to (lane, layer, order). Rebuilt every frame.
    struct LaneBand
    {
        std::string lane;
        float x{0.0f};
        float width{0.0f};
        // Lane width minus one node width: the same span the placement pass
        // maps order 0..999 onto, so the drop lands under the cursor.
        float usableWidth{0.0f};
    };

    // Converts a mouse position into file coordinates using the bands and row
    // geometry of the frame currently being drawn.
    PlacementTarget ResolvePlacement(Vector2 mouse, float rowTop, float rowStep) const;

    std::vector<LaneBand> laneBands;
    float scrollOffset{0.0f};
    float maxScrollOffset{0.0f};
    Vector2 panOffset{0.0f, 0.0f};
    bool panning{false};
    Vector2 lastPanMouse{0.0f, 0.0f};

    // Drag bookkeeping: a press only becomes a drag past a small threshold, so
    // a plain click still toggles the node instead of nudging its position.
    std::string pressedNodeId;
    Vector2 pressOrigin{0.0f, 0.0f};
    bool dragging{false};
    // Restores lane/order if a placement is cancelled with Esc.
    std::string placementUndoLane;
    int placementUndoOrder{0};
    bool placementIsNew{false};
    std::map<std::string, std::pair<std::string, int>> groupPlacementUndo;
    bool groupSharesLane{true};

    bool marqueeSelecting{false};
    Vector2 marqueeOrigin{0.0f, 0.0f};
    Rectangle marqueeRect{};
};

#endif
