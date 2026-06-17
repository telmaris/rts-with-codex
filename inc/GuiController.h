#ifndef GUI_CONTROLLER_H
#define GUI_CONTROLLER_H

#include "Gui.h"
#include "ProductionBuildings.h"
#include "raylib.h"
#include "raymath.h"

class GameScene;
class GuiController;

struct CameraMovement
{
    bool isMoving = false;
};

struct BuildOption
{
    std::string name;
    std::string costText;
    double buildTime{0.0};
    std::function<std::unique_ptr<Building>()> previewFactory;
    std::function<Building*(Vec2i)> buildAt;
};

class GuiSystem
{
public:
    virtual ~GuiSystem() = default;
    explicit GuiSystem(GuiController* con) : owner(con) {}
    GuiSystem() = delete;

    virtual void Update(double dt) = 0;

    virtual void UpdateUiWidgets(Vec2i) = 0;

    GuiController* owner;
    std::map<std::string, std::function<void()>> actionMap;
};

class GuiController
{
public:
    void Init(GameScene *);
    void Update(double);
    void MakeAction(std::string);

    template <typename T> void AddSystem(std::string name)
    {
        static_assert(std::is_base_of<GuiSystem, T>::value);

        systems[name] = std::make_shared<T>(this);
    }

    void ChangeSystem(std::string name) {activeSystem = systems[name];}
    inline std::vector<UiWidget*> GetUiWidgets() { return ui; }
    inline void AddUiWidget(UiWidget* ptr) { ui.push_back(ptr); }

    std::map<std::string, std::shared_ptr<GuiSystem>> systems;
    std::shared_ptr<GuiSystem> activeSystem;

    std::vector<UiWidget*> ui;
    GameScene *scene;
};

class BuildGhostWidget : public UiWidget
{
public:
    BuildGhostWidget() = default;
    void Update(double dt) override;

    GameScene* scene{nullptr};
    const BuildOption* selectedOption{nullptr};
    bool canBuild{false};
    Vec2i tilePos{0, 0};
};

class SelectedBuildingWidget : public UiWidget
{
public:
    void Update(double dt) override;

    GameScene* scene{nullptr};
    Building* building{nullptr};
};

class BuildPanelWidget : public UiWidget
{
public:
    void Update(double dt) override;
    int GetOptionAt(Vec2i point) const;

    GameScene* scene{nullptr};
    std::vector<BuildOption>* options{nullptr};
    size_t selectedIndex{0};
    std::string title{"Build"};
};

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
        actionMap["lmbp"] = [this] { LmbPressed(); };
        actionMap["lmbr"] = [this] { LmbReleased(); };
        actionMap["rmbp"] = [this] { RmbPressed(); };
        actionMap["rmbr"] = [this] { RmbReleased(); };

        buildingInfoPanel.ChangePositionAnchor({0.69f, 0.08f});
        buildingInfoPanel.ChangeSizeAnchor({0.28f, 0.82f});
        selectedBuildingWidget.scene = scene;
        // buildingInfoPanel.UpdateSize();
    }

    BasicMapViewSystem() = delete;

    void UpdateUiWidgets(Vec2i) override;

    void EscPressed();
    void BuildPressed();
    void RoadBuildPressed();

    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();

    void Update(double dt) override;

    GameScene* scene;
    CameraMovement cameraMovement;

    // gui

    GuiPanel buildingInfoPanel;
    SelectedBuildingWidget selectedBuildingWidget;

    // building details panel
    bool isBuildingSelected{false};
};

class BuildGuiSystem : public GuiSystem
{
public:
    explicit BuildGuiSystem(GuiController* con, bool roadMode = false);
    BuildGuiSystem() = delete;

    void UpdateUiWidgets(Vec2i) override;
    void Update(double dt) override;

    void EscPressed();
    void BuildPressed();
    void RoadBuildPressed();
    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();

protected:
    void ReturnToMapView();
    Vec2i GetHoveredTile() const;
    bool CanPlaceSelected(Vec2i tilePos) const;
    void SelectOption(size_t index);
    void RefreshGhost();

    GameScene* scene{nullptr};
    CameraMovement cameraMovement;
    BuildPanelWidget buildPanel;
    std::vector<BuildOption> options;
    size_t selectedIndex{0};
    std::unique_ptr<Building> selectedPreview;
    BuildGhostWidget ghostWidget;
    double buildTimePlaceholder{0.0};
    bool roadMode{false};
};

class RoadBuildSystem : public BuildGuiSystem
{
public:
    explicit RoadBuildSystem(GuiController* con) : BuildGuiSystem(con, true) {}
    RoadBuildSystem() = delete;
};

#endif
