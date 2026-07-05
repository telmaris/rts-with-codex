#ifndef GUI_CONTROLLER_H
#define GUI_CONTROLLER_H

#include "Gui.h"
#include "BuildingConfig.h"
#include "raylib.h"

#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class GameScene;
class GuiController;
class DivisionMapWidget;
class ArmyOrderPanelWidget;

// Implementations are split across thematic translation units:
//   src/GuiController.cpp   - controller core, input routing, BasicMapViewSystem
//   src/GuiMapWidgets.cpp   - military/map overlay widgets (divisions, battles)
//   src/GuiHudPanels.cpp    - strategic HUD, statistics panel + StatsGuiSystem
//   src/GuiResearchTree.cpp - research tree panel + Focus/Tech systems
//   src/GuiBuildModes.cpp   - build/road/destroy interaction modes
//   src/GuiCommon.cpp       - helpers shared by the TUs above (src/GuiInternal.h)

// Mutable camera drag state shared by map interaction systems.
struct CameraMovement
{
    bool isMoving = false;
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

    GuiController* owner{nullptr};
    std::map<std::string, std::function<void()>> actionMap;
};

// Routes input actions to the active GUI interaction system.
class GuiController
{
public:
    // Creates interaction systems and attaches the controller to a game scene.
    void Init(GameScene *);
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

    // Switches active interaction system. Refuses to open "tech" without a completed University.
    void ChangeSystem(std::string name);
    // Returns widgets that should be drawn by the renderer.
    inline std::vector<UiWidget*> GetUiWidgets() { return ui; }
    // Adds a widget to the current draw list.
    inline void AddUiWidget(UiWidget* ptr) { ui.push_back(ptr); }

    std::map<std::string, std::shared_ptr<GuiSystem>> systems;
    std::shared_ptr<GuiSystem> activeSystem;

    std::vector<UiWidget*> ui;
    std::unique_ptr<DivisionMapWidget> divisionMapOverlay;
    std::unique_ptr<ArmyOrderPanelWidget> armyOrderPanel;
    GameScene *scene{nullptr};
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

// One engaged division in a field battle, identified by player + division id.
struct FieldBattleParticipant
{
    int playerId{-1};
    int divisionId{-1};
};

// A clickable field-battle marker: every division consolidated into one fight
// (divisions touching the same melee) plus the screen point of its static bubble.
struct FieldBattleMarker
{
    Vector2 screenPos{0.0f, 0.0f};
    float radius{14.0f};
    std::vector<FieldBattleParticipant> participants;

    bool Contains(int playerId, int divisionId) const
    {
        for (const auto& p : participants)
            if (p.playerId == playerId && p.divisionId == divisionId)
                return true;
        return false;
    }
};

// Draws visible military orders and active battle indicators for the local player.
class MilitaryOrderWidget : public UiWidget
{
public:
    void Update(double dt) override;
    // Returns the battle circle hit at a screen point, or nullptr.
    const FieldBattleMarker* HitTest(Vec2i point) const;
    // Opens the battle details panel for a consolidated field battle.
    void SelectBattle(const FieldBattleMarker& m)
    {
        detailsOpen = true;
        selectedParticipants = m.participants;
    }
    void CloseDetails() { detailsOpen = false; selectedParticipants.clear(); }

    // Renders the consolidated field-battle details panel for the open fight.
    void DrawFieldBattlePanel();

    GameScene* scene{nullptr};
    std::vector<FieldBattleMarker> battleMarkers;  // rebuilt each frame

    bool detailsOpen{false};
    // Sticky selection: the set of divisions in the open battle, refreshed each
    // frame against live markers so the panel survives divisions joining/leaving.
    std::vector<FieldBattleParticipant> selectedParticipants;
};

// Pickable map marker for a stack of co-located divisions (HOI4-style counter).
struct DivisionMapMarker
{
    Building* homeBuilding{nullptr};
    Player* owner{nullptr};
    std::vector<int> divisionIds;   // all divisions drawn under this one symbol
    Vec2i tile{-1, -1};
    Vector2 screenPos{0.0f, 0.0f};
    Color color{200, 200, 200, 255};
};

// Rebuilds pick data for division counters drawn on the world layer.
class DivisionMapWidget : public UiWidget
{
public:
    void Update(double dt) override;

    // Returns the marker hit at a screen point, or nullptr if none.
    const DivisionMapMarker* HitTest(Vec2i screenPoint) const;

    GameScene* scene{nullptr};
    std::vector<DivisionMapMarker> markers; // rebuilt each frame in Update()
    static constexpr float kMarkerHalfW = 20.0f;
    static constexpr float kMarkerHalfH = 14.0f;
};

class MilitaryDivisionBarWidget;
class ArmyBarWidget;

// When divisions are selected, highlights the 2x2 quadrant under the cursor (the
// destination for a right-click move order) and rings the selected divisions.
class MoveTargetWidget : public UiWidget
{
public:
    void Update(double dt) override;

    GameScene* scene{nullptr};
    MilitaryDivisionBarWidget* bar{nullptr};  // source of the current selection
    ArmyBarWidget* armyBar{nullptr};          // suppress highlight over this panel
    bool drawBox{false};                      // draw the drag-selection rectangle
    Rectangle boxRect{0, 0, 0, 0};
};

// Bottom army strip listing divisions stationed in the selected military building.
class MilitaryDivisionBarWidget : public UiWidget
{
public:
    void Update(double dt) override;
    bool HandleClick(Vec2i point);
    bool IsSelected(int divId) const;

    // Replaces the selection (and home building) atomically. Update() wipes the
    // selection when it sees the building change (user clicked another building);
    // programmatic selection must sync prevBuilding or its fresh ids get wiped on
    // the very next frame (the "first drag-select does nothing" bug).
    void SetSelection(Building* home, std::vector<int> ids)
    {
        building = home;
        prevBuilding = home;
        selectedDivisionIds = std::move(ids);
    }

    Building* building{nullptr};
    Building* prevBuilding{nullptr};
    std::vector<int> selectedDivisionIds;
};

// Bottom army strip (HOI4-style): floating cards, one per army, that grow with
// the army count — no fixed background panel. A trailing "+" always groups the
// currently selected divisions into a new army.
class ArmyBarWidget : public UiWidget
{
public:
    void Update(double dt) override;
    // Returns true only when the click actually hit a card or the "+" button.
    bool HandleClick(Vec2i point);
    // True when the point is over an army card or the "+" button (for highlight
    // suppression) — not the whole nominal widget rectangle.
    bool IsOverContent(Vec2i point) const;
    // Returns the army id whose card contains the point, or -1 if none. Used by the
    // RMB handler to transfer the current selection into that army.
    int ArmyIdAt(Vec2i point) const;

    GameScene* scene{nullptr};
    MilitaryDivisionBarWidget* bar{nullptr};  // current division selection source

    // Rebuilt each frame in Update() so HandleClick uses the exact drawn layout.
    std::vector<std::pair<int, Rectangle>> cardRects;  // armyId -> rect
    Rectangle plusRect{0, 0, 0, 0};
    Rectangle contentBounds{0, 0, 0, 0};  // bounding box of the whole strip
};

// Right-side panel with army order buttons; appears when an army is selected via ArmyBarWidget.
// Allows issuing strategic commands like "Border Deploy", "Attack", etc.
class ArmyOrderPanelWidget : public UiWidget
{
public:
    void Update(double dt) override;
    bool HandleClick(Vec2i point);
    // Returns the screen rectangle occupied by this panel (or {0,0,0,0} if not visible).
    Rectangle GetBounds() const;

    GameScene* scene{nullptr};
    ArmyBarWidget* armyBar{nullptr};  // reference to detect which army is selected

    // Layout cache for button hit-testing
    std::vector<std::pair<std::string, Rectangle>> buttonRects;  // action name -> rect
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
    void SubmitRecruitCommand(Building* building, MilitaryUnitType unitType);

    // Updates camera drag and visible widgets.
    void Update(double dt) override;

    GameScene* scene;
    CameraMovement cameraMovement;

    BuildingInfoPanel buildingInfoPanel;
    ResearchPanel researchPanel;
    SelectedBuildingWidget selectedBuildingWidget;
    ProductionWarningWidget productionWarningWidget;
    MilitaryOrderWidget militaryOrderWidget;
    MilitaryDivisionBarWidget militaryDivisionBarWidget;
    ArmyBarWidget armyBarWidget;
    MoveTargetWidget moveTargetWidget;
    StrategicResourceHudWidget strategicHudWidget;

    bool isBuildingSelected{false};
    bool isDivisionOnlyMode{false}; // garrison bar only, no building info panel

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

    GameScene* scene{nullptr};
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

    Vec2i lastRoadDragTile{-9999, -9999};
};

class DestroyGuiSystem : public GuiSystem
{
public:
    explicit DestroyGuiSystem(GuiController* con);
    DestroyGuiSystem() = delete;

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
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    void ReturnToMapView();
    // Drops the hover target before leaving destroy mode.
    void ClearHoverTarget();

    GameScene* scene{nullptr};
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
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    GameScene* scene{nullptr};
    CameraMovement cameraMovement;
    StatsPanelWidget statsPanel;
    StrategicResourceHudWidget strategicHudWidget;
};

class FocusGuiSystem : public GuiSystem
{
public:
    explicit FocusGuiSystem(GuiController* con);
    FocusGuiSystem() = delete;

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
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    GameScene* scene{nullptr};
    CameraMovement cameraMovement;
    ResearchTreePanelWidget focusPanel{ResearchTreeKind::Focus};
    StrategicResourceHudWidget strategicHudWidget;
};

class TechGuiSystem : public GuiSystem
{
public:
    explicit TechGuiSystem(GuiController* con);
    TechGuiSystem() = delete;

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
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    GameScene* scene{nullptr};
    CameraMovement cameraMovement;
    ResearchTreePanelWidget techPanel{ResearchTreeKind::Technology};
    StrategicResourceHudWidget strategicHudWidget;
};

#endif
