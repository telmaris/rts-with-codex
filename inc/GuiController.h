#ifndef GUI_CONTROLLER_H
#define GUI_CONTROLLER_H

#include "raylib.h"
#include "raymath.h"

class GameScene;
class GuiController;

struct CameraMovement
{
    bool isMoving = false;
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

class BasicMapViewSystem : public GuiSystem
{
public:
    explicit BasicMapViewSystem(GuiController* con)
    : GuiSystem(con)
    {
        scene = owner->scene;

        actionMap["esc"]  = [this] { EscPressed(); };
        actionMap["q"]    = [this] { BuildPressed(); };
        actionMap["lmbp"] = [this] { LmbPressed(); };
        actionMap["lmbr"] = [this] { LmbReleased(); };
        actionMap["rmbp"] = [this] { RmbPressed(); };
        actionMap["rmbr"] = [this] { RmbReleased(); };

        buildingInfoPanel.ChangePositionAnchor({0.75f, 0.2f});
        buildingInfoPanel.ChangeSizeAnchor({0.2f, 0.4f});
        // buildingInfoPanel.UpdateSize();
    }

    BasicMapViewSystem() = delete;

    void UpdateUiWidgets(Vec2i) override;

    void EscPressed();
    void BuildPressed();

    void LmbPressed();
    void LmbReleased();
    void RmbPressed();
    void RmbReleased();

    void Update(double dt) override;

    GameScene* scene;
    CameraMovement cameraMovement;

    // gui

    GuiPanel buildingInfoPanel;

    // building details panel
    bool isBuildingSelected{false};
};

#endif