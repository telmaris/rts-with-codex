#ifndef GUI_CONTROLLER_H
#define GUI_CONTROLLER_H

#include "Gui.h"
#include "BuildingConfig.h"
#include "ProductionBuildings.h"
#include "raylib.h"
#include "raymath.h"

#include <deque>
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

    // Switches active interaction system.
    void ChangeSystem(std::string name) {activeSystem = systems[name];}
    // Returns widgets that should be drawn by the renderer.
    inline std::vector<UiWidget*> GetUiWidgets() { return ui; }
    // Adds a widget to the current draw list.
    inline void AddUiWidget(UiWidget* ptr) { ui.push_back(ptr); }

    std::map<std::string, std::shared_ptr<GuiSystem>> systems;
    std::shared_ptr<GuiSystem> activeSystem;

    std::vector<UiWidget*> ui;
    std::unique_ptr<DivisionMapWidget> divisionMapOverlay;
    GameScene *scene{nullptr};
};

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

// A clickable field-battle marker: the screen point of the circle plus the two
// engaged divisions (by player + division id) so the panel can show live details.
struct FieldBattleMarker
{
    Vector2 screenPos{0.0f, 0.0f};
    int playerA{-1};
    int divisionA{-1};
    int playerB{-1};
    int divisionB{-1};
};

// Draws visible military orders and active battle indicators for the local player.
class MilitaryOrderWidget : public UiWidget
{
public:
    void Update(double dt) override;
    // Returns the battle circle hit at a screen point, or nullptr.
    const FieldBattleMarker* HitTest(Vec2i point) const;
    // Opens / closes the battle details panel.
    void SelectBattle(const FieldBattleMarker& m)
    {
        detailsOpen = true;
        selPlayerA = m.playerA; selDivA = m.divisionA;
        selPlayerB = m.playerB; selDivB = m.divisionB;
    }
    void CloseDetails() { detailsOpen = false; }

    GameScene* scene{nullptr};
    std::vector<FieldBattleMarker> battleMarkers;  // rebuilt each frame

    bool detailsOpen{false};
    int selPlayerA{-1}, selDivA{-1}, selPlayerB{-1}, selDivB{-1};
};

// Shows parameters of an active or recently ended battle.
class BattleInfoPanel : public UiWidget
{
public:
    void Update(double dt) override;
    void UpdateSize(Vec2i windowSize) override;
    bool HasBattle() const { return activeBattleId >= 0; }
    void SetBattle(int battleId) { activeBattleId = battleId; }
    void Clear() { activeBattleId = -1; }

    GameScene* scene{nullptr};
    int activeBattleId{-1};
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
    static constexpr float kMarkerHalfW = 16.0f;
    static constexpr float kMarkerHalfH = 11.0f;
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

    GameScene* scene{nullptr};
    MilitaryDivisionBarWidget* bar{nullptr};  // current division selection source

    // Rebuilt each frame in Update() so HandleClick uses the exact drawn layout.
    std::vector<std::pair<int, Rectangle>> cardRects;  // armyId -> rect
    Rectangle plusRect{0, 0, 0, 0};
    Rectangle contentBounds{0, 0, 0, 0};  // bounding box of the whole strip
};

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
    Rectangle GetFilterButtonRect(Rectangle chart, int index) const;
    Rectangle GetAllFilterButtonRect(Rectangle chart) const;

    std::set<ResourceType> selectedResources;
    std::vector<ResourceType> filterResources;
};

class FocusPanelWidget : public UiWidget
{
public:
    void Update(double dt) override;
    void AdjustTreeZoom(Vec2i point, float wheel);

    GameScene* scene{nullptr};
    float scrollOffset{0.0f};
    float maxScrollOffset{0.0f};
    Vec2f panOffset{0.0f, 0.0f};
    float zoom{0.78f};
    bool panning{false};
    Vec2f lastPanMouse{0.0f, 0.0f};
    std::string selectedTagFilter;
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

// Default map interaction mode for selection, camera and logistics assignment.
class BasicMapViewSystem : public GuiSystem
{
public:
    explicit BasicMapViewSystem(GuiController* con)
    : GuiSystem(con)
    {
        scene = owner->scene;

        actionMap["esc"]  = [this] { EscPressed(); };
        actionMap["q"]    = [this] { BuildPressed(); };
        actionMap["r"]    = [this] { RoadBuildPressed(); };
        actionMap["d"]    = [this] { DestroyPressed(); };
        actionMap["e"]    = [this] { HeadquartersPressed(); };
        actionMap["s"]    = [this] { StatsPressed(); };
        actionMap["f"]    = [this] { FocusPressed(); };
        actionMap["space"] = [this] { CenterOnHeadquartersPressed(); };
        actionMap["lmbp"] = [this] { LmbPressed(); };
        actionMap["lmbr"] = [this] { LmbReleased(); };
        actionMap["rmbp"] = [this] { RmbPressed(); };
        actionMap["rmbr"] = [this] { RmbReleased(); };
        actionMap["mmbp"] = [this] { cameraMovement.isMoving = true; };
        actionMap["mmbr"] = [this] { cameraMovement.isMoving = false; };
        actionMap["scroll"] = [this] { Scroll(); };

        buildingInfoPanel.ChangePositionAnchor({0.66f, 0.08f});
        buildingInfoPanel.ChangeSizeAnchor({0.31f, 0.82f});
        selectedBuildingWidget.scene = scene;
        productionWarningWidget.scene = scene;
        militaryOrderWidget.scene = scene;
        // Division panel sits top-left (under the resource HUD) as a vertical
        // HOI4-style list; the bottom of the screen is reserved for the future
        // army (grouped-divisions) bar.
        militaryDivisionBarWidget.ChangePositionAnchor({0.012f, 0.085f});
        militaryDivisionBarWidget.ChangeSizeAnchor({0.23f, 0.58f});
        strategicHudWidget.scene = scene;
        strategicHudWidget.ChangePositionAnchor({0.012f, 0.012f});
        strategicHudWidget.ChangeSizeAnchor({0.42f, 0.055f});
        statsPanel.scene = scene;
        statsPanel.ChangePositionAnchor({0.06f, 0.10f});
        statsPanel.ChangeSizeAnchor({0.88f, 0.82f});
        battleInfoPanel.scene = scene;
        battleInfoPanel.ChangePositionAnchor({0.66f, 0.08f});
        battleInfoPanel.ChangeSizeAnchor({0.31f, 0.82f});
        divisionMapWidget.scene = scene;
        armyBarWidget.scene = scene;
        armyBarWidget.bar = &militaryDivisionBarWidget;
        // Bottom-center HOI4-style army strip.
        armyBarWidget.ChangePositionAnchor({0.30f, 0.88f});
        armyBarWidget.ChangeSizeAnchor({0.40f, 0.10f});
        moveTargetWidget.scene = scene;
        moveTargetWidget.bar = &militaryDivisionBarWidget;
        moveTargetWidget.armyBar = &armyBarWidget;
        buildingInfoPanel.recruitRequested = [this](Building* building, MilitaryUnitType unitType)
        {
            SubmitRecruitCommand(building, unitType);
        };
    }

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
    BattleInfoPanel battleInfoPanel;
    SelectedBuildingWidget selectedBuildingWidget;
    ProductionWarningWidget productionWarningWidget;
    MilitaryOrderWidget militaryOrderWidget;
    DivisionMapWidget divisionMapWidget;
    MilitaryDivisionBarWidget militaryDivisionBarWidget;
    ArmyBarWidget armyBarWidget;
    MoveTargetWidget moveTargetWidget;
    StrategicResourceHudWidget strategicHudWidget;
    StatsPanelWidget statsPanel;
    FocusPanelWidget focusPanel;

    bool isBuildingSelected{false};
    bool isDivisionOnlyMode{false}; // garrison bar only, no building info panel
    int selectedBattleId{-1};

    // Drag-box selection state (box-select divisions on the map).
    bool pendingBox{false};   // LMB pressed on open ground, may become a box drag
    bool boxActive{false};    // dragged far enough to be a box selection
    Vec2i boxStart{0, 0};
    Vec2i boxEnd{0, 0};
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
    double buildTimePlaceholder{0.0};
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
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    void ReturnToMapView();

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
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();
    void Scroll();

private:
    GameScene* scene{nullptr};
    CameraMovement cameraMovement;
    FocusPanelWidget focusPanel;
    StrategicResourceHudWidget strategicHudWidget;
};

#endif
