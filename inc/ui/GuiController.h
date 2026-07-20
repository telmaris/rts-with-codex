#ifndef GUI_CONTROLLER_H
#define GUI_CONTROLLER_H

#include "ui/Gui.h"
#include "ui/UiTheme.h"
#include "economy/BuildingConfig.h"
#include "raylib.h"

#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class Scene;
class GameScene;
class GuiController;

// Implementations are split across thematic translation units:
//   src/GuiController.cpp   - controller core, input routing, BasicMapViewSystem
//   src/GuiMapWidgets.cpp   - map overlay widgets (selection/warning highlights)
//   src/GuiHudPanels.cpp    - strategic HUD, statistics panel + StatsGuiSystem
//   src/GuiResearchTree.cpp - research tree panel + Focus/Tech systems
//   src/GuiBuildModes.cpp   - build/road/destroy interaction modes
//   src/GuiCommon.cpp       - helpers shared by the TUs above (src/GuiInternal.h)

// Mutable camera drag state shared by map interaction systems.
struct CameraMovement
{
    bool isMoving = false;
    // A3 (docs/work_plan_2026-07-13.md): in the default map view RMB does
    // double duty as both the pan button and a click action (assign
    // logistics receiver), so BeginCameraDrag/EndCameraDragWasClick (see
    // GuiInternal.h) track cumulative screen displacement since the press to
    // tell a real click (no drag) from a pan gesture. Unused by systems that
    // only ever pan on RMB (no competing click action there).
    Vec2i rmbPressScreenPos{-1, -1};
    bool rmbDragged = false;
};

// One selectable item in the build panel.
struct BuildOption
{
    std::string name;
    std::string costText;
    std::vector<ResourceAmountDefinition> buildCosts;
    std::vector<std::string> lockReasons;
    BuildingType buildingType{BuildingType::Building};
    Vec2i footprint{1, 1};
    double buildTime{0.0};
    std::string category{"OTHER"};
    std::function<std::unique_ptr<Building>()> previewFactory;
    std::function<void(Vec2i)> buildAt;
};

// Interaction mode owned by GuiController.
class GuiSystem
{
public:
    virtual ~GuiSystem() = default;
    explicit GuiSystem(GuiController* con) : owner(con) {}
    GuiSystem() = delete;

    // Advances this interaction mode by one frame.
    virtual void Update(double dt) = 0;

    // Rebuilds widgets owned by this interaction mode after layout changes.
    virtual void UpdateUiWidgets(Vec2i) = 0;

    // Called by GuiController::ChangeSystem when this system becomes/stops
    // being the active one. actionMap dispatch (MakeAction) is naturally
    // scoped already — it only ever looks in the *active* system's map — but
    // InputEventSubscriber-based bindings (ETAP 4) register globally for the
    // subscriber's whole lifetime, so a system that owns one (e.g. a panel
    // with an ESC-close subscriber) must use these hooks to stop reacting
    // while some other system is active. See BasicMapViewSystem::OnDeactivate.
    virtual void OnActivate() {}
    virtual void OnDeactivate() {}

    // Domain gate for ChangeSystem: a system that has preconditions (e.g.
    // TechGuiSystem needs a completed University) overrides this. Keeps the
    // controller itself free of any game-domain logic — it only manages
    // transitions between systems (user-directed rework, 2026-07-14).
    virtual bool CanActivate() { return true; }

    GuiController* owner{nullptr};
    // Owning scene — hoisted to the base (2026-07-14): every interaction
    // system needs SOME scene (at minimum for render/audioSystem, both
    // declared on the Scene base itself), and the shared action wiring
    // (WireCommonSystemActions) reads it uniformly regardless of which
    // concrete scene type owns the controller. A4 (docs/work_plan_2026-07-13.md):
    // typed as the generic Scene base rather than GameScene — the controller
    // itself is scene-agnostic. Concrete game interaction systems (which need
    // GameScene-specific members: game, SubmitLocalCommand, ...) redeclare
    // their OWN same-named `GameScene* scene` member, which shadows this one
    // within their scope — every existing `scene->game` etc. call site in
    // those systems' .cpp files keeps compiling unchanged, since name lookup
    // finds the nearer (derived) declaration first. See BasicMapViewSystem
    // etc. below for the pattern; a menu-scene system would instead use this
    // base member directly (or its own typed shadow of its own scene type).
    Scene* scene{nullptr};
    std::map<std::string, std::function<void()>> actionMap;
};

// Routes input actions to the active GUI interaction system.
class GuiController
{
public:
    // Creates interaction systems and attaches the controller to a scene
    // (any Scene, not just GameScene — A4, docs/work_plan_2026-07-13.md).
    void Init(Scene *);
    // Updates the active system.
    void Update(double);
    // Executes an action in the active system when it is registered.
    void MakeAction(std::string);

    // Registers an interaction system by name.
    template <typename T> void AddSystem(std::string name)
    {
        static_assert(std::is_base_of<GuiSystem, T>::value);

        systems[name] = std::make_shared<T>(this);
    }

    // Switches active interaction system (refuses when the target system's
    // own CanActivate() precondition isn't met, e.g. TechGuiSystem).
    void ChangeSystem(std::string name);
    // Returns widgets that should be drawn by the renderer.
    inline std::vector<UiWidget*> GetUiWidgets() { return ui; }
    // Adds a widget to the current draw list.
    inline void AddUiWidget(UiWidget* ptr) { ui.push_back(ptr); }

    std::map<std::string, std::shared_ptr<GuiSystem>> systems;
    std::shared_ptr<GuiSystem> activeSystem;

    std::vector<UiWidget*> ui;
    Scene *scene{nullptr};
};

// ─── Map overlay widgets ─────────────────────────────────────────────────────

// Draws the ghost preview for the currently selected build option.
class BuildGhostWidget : public UiWidget
{
public:
    BuildGhostWidget() = default;
    // Draws build preview and validity tint under the cursor.
    void Update(double dt) override;

    GameScene* scene{nullptr};
    const BuildOption* selectedOption{nullptr};
    bool canBuild{false};
    Vec2i tilePos{0, 0};
};

// Draws selection highlight for the currently selected building.
class SelectedBuildingWidget : public UiWidget
{
public:
    // Draws selected building footprint highlight.
    void Update(double dt) override;

    GameScene* scene{nullptr};
    Building* building{nullptr};
};

// Draws warning highlights over production buildings that cannot currently work.
class ProductionWarningWidget : public UiWidget
{
public:
    void Update(double dt) override;

    GameScene* scene{nullptr};
};

// ─── HUD and full-screen panels ──────────────────────────────────────────────

// Top-screen strategic resource summary for the local player.
class StrategicResourceHudWidget : public UiWidget
{
public:
    void UpdateSize(Vec2i windowSize) override;
    void Update(double dt) override;

    GameScene* scene{nullptr};
};

// Full-screen economy and strategic statistics overview.
class StatsPanelWidget : public UiWidget
{
public:
    void Update(double dt) override;
    bool HandleClick(Vec2i point);

    GameScene* scene{nullptr};
    int selectedWindowIndex{0};
    int selectedFlowMode{0};

private:
    // Layout rectangles shared by Update (drawing) and HandleClick (hit tests)
    // so both always agree on where the controls are.
    Rectangle GetWindowSpinnerRect() const;
    Rectangle GetFlowModeToggleRect() const;
    Rectangle GetChartRect() const;
    Rectangle GetFilterButtonRect(Rectangle chart, int index) const;
    Rectangle GetAllFilterButtonRect(Rectangle chart) const;

    std::set<ResourceType> selectedResources;
    std::vector<ResourceType> filterResources;
};

// Which research tree a ResearchTreePanelWidget renders.
enum class ResearchTreeKind
{
    Focus,
    Technology,
};

// Full-screen research tree (shared by political focuses and technologies).
// The two trees differ only in data source, the focus-only state side panel and
// what happens when an available node is clicked.
class ResearchTreePanelWidget : public UiWidget
{
public:
    explicit ResearchTreePanelWidget(ResearchTreeKind kind) : kind(kind) {}
    void Update(double dt) override;
    void AdjustTreeZoom(Vec2i point, float wheel);

    GameScene* scene{nullptr};
    ResearchTreeKind kind{ResearchTreeKind::Technology};
    float scrollOffset{0.0f};
    float maxScrollOffset{0.0f};
    Vec2f panOffset{0.0f, 0.0f};
    float zoom{0.78f};
    bool panning{false};
    Vec2f lastPanMouse{0.0f, 0.0f};
    std::string selectedTagFilter;

private:
    // Scrollable tree viewport; the focus tree reserves space for the state panel.
    Rectangle GetTreeArea(Rectangle bounds) const;
};

// Right-side build panel with selectable building cards.
class BuildPanelWidget : public UiWidget
{
public:
    // Draws available build options and handles hover visuals.
    void Update(double dt) override;
    // Scrolls the build option list.
    void Scroll(float wheel);
    // Returns option index under a point, or -1 when none is hit.
    int GetOptionAt(Vec2i point) const;

    GameScene* scene{nullptr};
    std::vector<BuildOption>* options{nullptr};
    size_t selectedIndex{std::numeric_limits<size_t>::max()};
    Vec2i hoveredTile{-1, -1};
    std::string title{"Build"};
    float scrollOffset{0.0f};
    float maxScrollOffset{0.0f};
    bool dragging{false};
    Vec2i dragOffset{0, 0};
};

// Roster/deploy panel (TD etap-8.1): composes an ordered attack group
// (spearhead first) from the local player's recruited-but-undeployed roster
// and submits GameCommand::DeployUnits. Interaction is entirely via
// temporary, per-frame UiButtons created inside Update() (same approach as
// GuiPanel's recruitment branch) rather than a separate HandleClick — no
// per-row state needs to persist between frames.
class RosterPanelWidget : public UiWidget
{
public:
    void Update(double dt) override;

    GameScene* scene{nullptr};
    // Ordered attack group (index 0 = spearhead) — instance ids currently
    // staged from the local roster. Cleared after a successful Deploy, and
    // pruned of any id that stopped being a valid InRoster unit (e.g. it was
    // deployed through some other path) every frame.
    std::vector<int> selectedGroup;
    // -1 = no target chosen yet; auto-filled when exactly one reachable
    // enemy exists (the plan's "w 1vs1 auto" rule generalized to "only one
    // choice").
    int selectedTargetPlayerId{-1};
    float scrollOffset{0.0f};
    float maxScrollOffset{0.0f};
};

// ─── Interaction systems ─────────────────────────────────────────────────────

// Default map interaction mode for selection, camera and logistics assignment.
class BasicMapViewSystem : public GuiSystem
{
public:
    explicit BasicMapViewSystem(GuiController* con);
    BasicMapViewSystem() = delete;

    // Rebuilds map-view widget list.
    void UpdateUiWidgets(Vec2i) override;

    // Opens or closes the in-game menu.
    void EscPressed();
    // Enters building placement mode.
    void BuildPressed();
    // Enters road placement mode.
    void RoadBuildPressed();
    void DestroyPressed();
    void HeadquartersPressed();
    void StatsPressed();
    void FocusPressed();
    void TechPressed();
    void RosterPressed();
    void CenterOnHeadquartersPressed();
    void OpenHeadquartersPanel();

    // Handles map selection and panel interactions.
    void LmbPressed();
    // Stops camera drag initiated by left mouse button when relevant.
    void LmbReleased();
    // Starts camera drag or assigns selected building receiver.
    void RmbPressed();
    // Stops camera drag.
    void RmbReleased();
    // Zooms camera around cursor.
    void Scroll();

    // Updates camera drag and visible widgets.
    void Update(double dt) override;

    // Clears building selection so the info/research panels (and any
    // InputEventSubscriber they own, e.g. GuiPanel::escClose) stop reacting
    // the moment another GuiSystem becomes active — see GuiSystem::OnDeactivate.
    void OnDeactivate() override;

    // Shadows GuiSystem::scene with the concrete type this system actually
    // needs (game, SubmitLocalCommand, ...) — A4, docs/work_plan_2026-07-13.md.
    GameScene* scene{nullptr};
    CameraMovement cameraMovement;

    BuildingInfoPanel buildingInfoPanel;
    ResearchPanel researchPanel;
    SelectedBuildingWidget selectedBuildingWidget;
    ProductionWarningWidget productionWarningWidget;
    StrategicResourceHudWidget strategicHudWidget;

    bool isBuildingSelected{false};

    // Drag-box selection state (box-select divisions on the map).
    bool pendingBox{false};   // LMB pressed on open ground, may become a box drag
    bool boxActive{false};    // dragged far enough to be a box selection
    Vec2i boxStart{0, 0};
    Vec2i boxEnd{0, 0};

private:
    // Returns the research panel when it holds a building, else the info panel.
    GuiPanel* ActivePanel();
    // Clears building selection and both side panels.
    void ClearBuildingSelection();
};

// Build interaction mode for placeable buildings.
class BuildGuiSystem : public GuiSystem
{
public:
    explicit BuildGuiSystem(GuiController* con);
    BuildGuiSystem() = delete;

    // Shadows GuiSystem::scene (A4, docs/work_plan_2026-07-13.md) — public so
    // the free WireCommonSystemActions template (GuiInternal.h) can read it;
    // also inherited as-is by RoadBuildSystem below.
    GameScene* scene{nullptr};

    // Rebuilds build-mode widget list.
    void UpdateUiWidgets(Vec2i) override;
    // Updates camera drag, build panel and ghost preview.
    void Update(double dt) override;

    // Cancels build mode and returns to map view.
    virtual void EscPressed();
    // Toggles build mode.
    virtual void BuildPressed();
    // Toggles road build mode.
    virtual void RoadBuildPressed();
    virtual void DestroyPressed();
    virtual void HeadquartersPressed();
    virtual void StatsPressed();
    virtual void FocusPressed();
    virtual void TechPressed();
    virtual void RosterPressed();
    // Selects build option or places selected building.
    virtual void LmbPressed();
    // Stops left-button action.
    virtual void LmbReleased();
    // Starts camera drag.
    virtual void RmbPressed();
    // Stops camera drag.
    virtual void RmbReleased();
    // Zooms camera around cursor.
    virtual void Scroll();

protected:
    // Switches controller back to default map view.
    void ReturnToMapView();
    void OpenHeadquartersAndReturn();
    // Returns map tile currently under cursor.
    Vec2i GetHoveredTile() const;
    // Returns true when selected building can be placed at tile.
    bool CanPlaceSelected(Vec2i tilePos) const;
    // Selects a build option by index and refreshes preview.
    void SelectOption(size_t index);
    // Rebuilds ghost preview for the selected option.
    void RefreshGhost();
    // Places selected option under cursor when placement is valid.
    bool TryPlaceSelectedAtHovered(bool returnAfterBuild);

    CameraMovement cameraMovement;
    BuildPanelWidget buildPanel;
    StrategicResourceHudWidget strategicHudWidget;
    std::vector<BuildOption> options;
    size_t selectedIndex{std::numeric_limits<size_t>::max()};
    std::unique_ptr<Building> selectedPreview;
    BuildGhostWidget ghostWidget;
};

// Specialized build mode that only places roads.
class RoadBuildSystem : public BuildGuiSystem
{
public:
    explicit RoadBuildSystem(GuiController* con);
    RoadBuildSystem() = delete;

    // Updates camera drag, road panel, ghost preview and drag placement.
    void Update(double dt) override;

    // Switches back to building placement mode.
    void BuildPressed() override;
    // Cancels road placement mode.
    void RoadBuildPressed() override;
    // Selects road option or starts placing roads.
    void LmbPressed() override;
    // Ends road drag placement.
    void LmbReleased() override;
    void Scroll() override;

private:
    bool TryPlaceRoadAtHovered();
    // Road mode has no visible option panel (Update never draws buildPanel),
    // so the Road/Bridge choice can't be a manual selection — instead it
    // follows the cursor: hovering an isMilitaryRoad tile selects Bridge,
    // anywhere else selects Road. Dragging a road across the unit track
    // therefore inserts the bridge automatically (B6 follow-up #2, playtest
    // 2026-07-14 — both "can't build roads" and "can't build bridges" were
    // this same invisible, unswitchable selection stuck on one type).
    void SyncSelectionToHoveredTile();

    Vec2i lastRoadDragTile{-9999, -9999};
};

class DestroyGuiSystem : public GuiSystem
{
public:
    explicit DestroyGuiSystem(GuiController* con);
    DestroyGuiSystem() = delete;

    // Shadows GuiSystem::scene (A4, docs/work_plan_2026-07-13.md) — public so
    // the free WireCommonSystemActions template (GuiInternal.h) can read it.
    GameScene* scene{nullptr};

    void UpdateUiWidgets(Vec2i) override;
    void Update(double dt) override;

    void EscPressed();
    void BuildPressed();
    void RoadBuildPressed();
    void DestroyPressed();
    void HeadquartersPressed();
    void StatsPressed();
    void FocusPressed();
    void TechPressed();
    void RosterPressed();
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    void ReturnToMapView();
    // Drops the hover target before leaving destroy mode.
    void ClearHoverTarget();

    CameraMovement cameraMovement;
    SelectedBuildingWidget destroyTargetWidget;
    Building* hoveredBuilding{nullptr};
    StrategicResourceHudWidget strategicHudWidget;
};

class StatsGuiSystem : public GuiSystem
{
public:
    explicit StatsGuiSystem(GuiController* con);
    StatsGuiSystem() = delete;

    // Shadows GuiSystem::scene (A4, docs/work_plan_2026-07-13.md) — public so
    // the free WireCommonSystemActions template (GuiInternal.h) can read it.
    GameScene* scene{nullptr};

    void UpdateUiWidgets(Vec2i) override;
    void Update(double dt) override;

    void EscPressed();
    void BuildPressed();
    void RoadBuildPressed();
    void DestroyPressed();
    void HeadquartersPressed();
    void StatsPressed();
    void FocusPressed();
    void TechPressed();
    void RosterPressed();
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    CameraMovement cameraMovement;
    StatsPanelWidget statsPanel;
    StrategicResourceHudWidget strategicHudWidget;
};

class FocusGuiSystem : public GuiSystem
{
public:
    explicit FocusGuiSystem(GuiController* con);
    FocusGuiSystem() = delete;

    // Shadows GuiSystem::scene (A4, docs/work_plan_2026-07-13.md) — public so
    // the free WireCommonSystemActions template (GuiInternal.h) can read it.
    GameScene* scene{nullptr};

    void UpdateUiWidgets(Vec2i) override;
    void Update(double dt) override;

    void EscPressed();
    void BuildPressed();
    void RoadBuildPressed();
    void DestroyPressed();
    void HeadquartersPressed();
    void StatsPressed();
    void FocusPressed();
    void TechPressed();
    void RosterPressed();
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    CameraMovement cameraMovement;
    ResearchTreePanelWidget focusPanel{ResearchTreeKind::Focus};
    StrategicResourceHudWidget strategicHudWidget;
};

class TechGuiSystem : public GuiSystem
{
public:
    explicit TechGuiSystem(GuiController* con);
    TechGuiSystem() = delete;

    // Shadows GuiSystem::scene (A4, docs/work_plan_2026-07-13.md) — public so
    // the free WireCommonSystemActions template (GuiInternal.h) can read it.
    GameScene* scene{nullptr};

    void UpdateUiWidgets(Vec2i) override;
    void Update(double dt) override;

    // The tech tree only opens once the local player has a completed
    // University — domain gate moved here from GuiController::ChangeSystem.
    bool CanActivate() override;

    void EscPressed();
    void BuildPressed();
    void RoadBuildPressed();
    void DestroyPressed();
    void HeadquartersPressed();
    void StatsPressed();
    void FocusPressed();
    void TechPressed();
    void RosterPressed();
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    CameraMovement cameraMovement;
    ResearchTreePanelWidget techPanel{ResearchTreeKind::Technology};
    StrategicResourceHudWidget strategicHudWidget;
};

// Roster/deploy full-screen mode (TD etap-8.1) — opened via the strategic
// HUD's Roster button or the U key, following the same recipe as
// FocusGuiSystem/TechGuiSystem.
class RosterGuiSystem : public GuiSystem
{
public:
    explicit RosterGuiSystem(GuiController* con);
    RosterGuiSystem() = delete;

    // Shadows GuiSystem::scene (A4, docs/work_plan_2026-07-13.md) — public so
    // the free WireCommonSystemActions template (GuiInternal.h) can read it.
    GameScene* scene{nullptr};

    void UpdateUiWidgets(Vec2i) override;
    void Update(double dt) override;

    void EscPressed();
    void BuildPressed();
    void RoadBuildPressed();
    void DestroyPressed();
    void HeadquartersPressed();
    void StatsPressed();
    void FocusPressed();
    void TechPressed();
    void RosterPressed();
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    CameraMovement cameraMovement;
    RosterPanelWidget rosterPanel;
    StrategicResourceHudWidget strategicHudWidget;
};

#endif
