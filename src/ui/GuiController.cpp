// GUI controller core: input routing, system switching and the default map
// interaction mode (BasicMapViewSystem). Widgets and other interaction systems
// live in the sibling Gui*.cpp translation units.

#include "GuiInternal.h"

#include "scenes/Scenes.h"
#include "economy/Player.h"

#include <algorithm>

namespace
{
    // Finds the local player's headquarters building.
    Building* FindLocalHeadquarters(GameScene* scene)
    {
        Player* player = GuiLocalPlayer(scene);
        if (player == nullptr)
            return nullptr;

        for (auto* building : player->GetTrackedBuildings())
        {
            if (building != nullptr && building->owner == player && building->buildingType == BuildingType::Headquarters)
                return building;
        }

        return nullptr;
    }

    // Adds a debug resource package to one storage-like building.
    void GrantResourcesToStorage(StorageComponent* storage, int amount)
    {
        if (storage == nullptr || amount <= 0)
            return;

        for (ResourceType type : resourceTypes)
        {
            auto& buffer = storage->buffers[type];
            if (buffer.type == ResourceType::Null)
                buffer = ResourceBuffer{type, amount};
            buffer.bufferSize = std::max(buffer.bufferSize, static_cast<int>(buffer.buffer.size()) + amount);
            for (int i = 0; i < amount; i++)
                buffer.GenerateResource(type);
        }
    }

    // Grants local debug resources when the current world allows debug helpers.
    void GrantDebugResources(GameScene* scene, int amount)
    {
        if (scene == nullptr || scene->game == nullptr || !scene->game->GetTileMap().params.debugMode)
            return;

        Building* headquarters = FindLocalHeadquarters(scene);
        auto* storage = headquarters != nullptr ? headquarters->GetComponent<StorageComponent>() : nullptr;
        if (storage == nullptr)
            return;

        GrantResourcesToStorage(storage, amount);
        Log::Msg("[Debug]", "granted ", amount, " of every resource to local HQ");
    }
}

// ─── InputProcessor ──────────────────────────────────────────────────────────

// Translates raw input into named controller actions once per frame.
void InputProcessor::HandleInputs()
{
    if (IsActionPressed(CLOSE_TOP_GUI))
        controller->MakeAction("esc");
    if (IsActionPressed(OPEN_BUILD_GUI))
        controller->MakeAction("q");
    if (IsActionPressed(OPEN_ROAD_BUILD_GUI))
        controller->MakeAction("r");
    if (IsActionPressed(OPEN_DESTROY_GUI))
        controller->MakeAction("d");
    if (IsActionPressed(OPEN_HEADQUARTERS_GUI))
        controller->MakeAction("e");
    if (IsActionPressed(OPEN_STATS_GUI))
        controller->MakeAction("s");
    if (IsActionPressed(OPEN_FOCUS_GUI))
        controller->MakeAction("f");
    if (IsActionPressed(OPEN_TECH_GUI))
        controller->MakeAction("t");
    if (IsActionPressed(CENTER_CAMERA_ON_HEADQUARTERS))
        controller->MakeAction("space");
    if (IsActionPressed(DEBUG_GRANT_RESOURCES))
        controller->MakeAction("debug_resources");
    if (IsActionPressed(LEFT_BUTTON_DOWN))
        controller->MakeAction("lmbp");
    if (IsActionReleased(LEFT_BUTTON_DOWN))
        controller->MakeAction("lmbr");
    if (IsActionPressed(RIGHT_BUTTON_DOWN))
        controller->MakeAction("rmbp");
    if (IsActionReleased(RIGHT_BUTTON_DOWN))
        controller->MakeAction("rmbr");
    if (IsActionPressed(MIDDLE_BUTTON_DOWN))
        controller->MakeAction("mmbp");
    if (IsActionReleased(MIDDLE_BUTTON_DOWN))
        controller->MakeAction("mmbr");
    if (InputManager::GetMouseWheelMove() != 0.0f)
        controller->MakeAction("scroll");
}

// ─── GuiController ───────────────────────────────────────────────────────────

// Attaches the controller to a scene.
void GuiController::Init(GameScene *s)
{
    scene = s;
}

// Switches active interaction system. Refuses to open "tech" without a completed University.
void GuiController::ChangeSystem(std::string name)
{
    if (name == "tech" && !HasUniversity(scene))
        return;

    auto it = systems.find(name);
    if (it == systems.end())
    {
        Log::Msg("[Gui]", "unknown interaction system requested: ", name);
        return;
    }

    if (activeSystem == it->second)
        return;

    if (activeSystem != nullptr)
        activeSystem->OnDeactivate();
    activeSystem = it->second;
    activeSystem->OnActivate();
}

// Rebuilds the widget draw list through the active system every frame.
void GuiController::Update(double dt)
{
    ui.clear();
    activeSystem->Update(dt);
}

// Dispatches a named UI action to the active GUI system.
void GuiController::MakeAction(std::string action)
{
    if (action == "debug_resources")
    {
        GrantDebugResources(scene, 50);
        return;
    }

    if (action == "lmbp" && scene != nullptr && scene->audioSystem != nullptr)
        scene->audioSystem->PlaySound("click", 1.0f);

    auto it = activeSystem->actionMap.find(action);
    if (it != activeSystem->actionMap.end())
        it->second();
}

// ─── BasicMapViewSystem ──────────────────────────────────────────────────────

BasicMapViewSystem::BasicMapViewSystem(GuiController* con)
    : GuiSystem(con)
{
    scene = owner->scene;

    WireCommonSystemActions(*this, cameraMovement);
    actionMap["space"] = [this] { CenterOnHeadquartersPressed(); };

    buildingInfoPanel.ChangePositionAnchor({0.66f, 0.08f});
    buildingInfoPanel.ChangeSizeAnchor({0.31f, 0.82f});
    selectedBuildingWidget.scene = scene;
    productionWarningWidget.scene = scene;
    SetupStrategicHud(strategicHudWidget, scene);
}

// Returns the research panel when it holds a building, else the info panel.
GuiPanel* BasicMapViewSystem::ActivePanel()
{
    return researchPanel.HasBuilding()
        ? static_cast<GuiPanel*>(&researchPanel)
        : &buildingInfoPanel;
}

// Clears building selection and both side panels.
void BasicMapViewSystem::ClearBuildingSelection()
{
    isBuildingSelected = false;
    buildingInfoPanel.SetBuilding(nullptr);
    researchPanel.SetBuilding(nullptr);
}

// Runs whenever GuiController switches to a different system. The existing
// Pressed() handlers (BuildPressed, StatsPressed, ...) already call
// ClearBuildingSelection() before switching away, but that made the guarantee
// dependent on every call site remembering to do it. Doing it here too makes
// it a property of leaving this system, not of how you left it — closing the
// gap InputEventSubscriber-based bindings need (see GuiPanel::escClose):
// once this system is inactive, its panels report HasBuilding() == false, so
// their ESC subscriber becomes a no-op even though it's still registered.
void BasicMapViewSystem::OnDeactivate()
{
    ClearBuildingSelection();
}

// Advances this object's state for one frame.
void BasicMapViewSystem::Update(double dt)
{
    if (!researchPanel.researchRequested)
    {
        researchPanel.researchRequested = [this](const std::string& technologyId, Building* university)
        {
            if (scene == nullptr || scene->game == nullptr || university == nullptr)
                return;

            scene->SubmitLocalCommand(GameCommand::StartTechnologyResearch(
                scene->game->GetLocalPlayerId(),
                technologyId,
                university->positionId));
        };
    }

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    owner->AddUiWidget(&productionWarningWidget);

    if (isBuildingSelected)
    {
        GuiPanel* activePanel = ActivePanel();

        if (!activePanel->HasBuilding())
        {
            isBuildingSelected = false;
            return;
        }

        selectedBuildingWidget.building = activePanel->GetBuilding();
        if (activePanel->ConsumeDestroyRequest())
        {
            Building* building = activePanel->GetBuilding();
            if (building != nullptr && building->CanBeManuallyDestroyed())
                scene->SubmitLocalCommand(GameCommand::DestroyBuilding(scene->game->GetLocalPlayerId(), building->positionId));
            ClearBuildingSelection();
            selectedBuildingWidget.building = nullptr;
            return;
        }

        owner->AddUiWidget(&selectedBuildingWidget);
        owner->AddUiWidget(activePanel);
    }
    owner->AddUiWidget(&strategicHudWidget);
}

// Applies window size changes to widgets owned by this mode.
void BasicMapViewSystem::UpdateUiWidgets(Vec2i size)
{
    buildingInfoPanel.UpdateSize(size);
    researchPanel.UpdateSize(size);
    strategicHudWidget.UpdateSize(size);
}

// Closes open panels or opens the in-game menu.
void BasicMapViewSystem::EscPressed()
{
    if (isBuildingSelected)
    {
        ClearBuildingSelection();
        return;
    }

    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = scene;
    msg->sceneName = "GameMenuScene";
    scene->broker->Broadcast(msg);

    Log::Msg("[Input]", "escape pressed");
}

// Enters building placement mode.
void BasicMapViewSystem::BuildPressed()
{
    ClearBuildingSelection();
    owner->ChangeSystem("build");
    Log::Msg("[Input]", "Q pressed");
}

// Enters road placement mode.
void BasicMapViewSystem::RoadBuildPressed()
{
    ClearBuildingSelection();
    cameraMovement.isMoving = false;
    owner->ChangeSystem("road_build");
    Log::Msg("[Input]", "R pressed");
}

// Enters destroy mode.
void BasicMapViewSystem::DestroyPressed()
{
    ClearBuildingSelection();
    cameraMovement.isMoving = false;
    owner->ChangeSystem("destroy");
    Log::Msg("[Input]", "D pressed");
}

// Toggles the local headquarters storage panel.
void BasicMapViewSystem::HeadquartersPressed()
{
    Building* headquarters = FindLocalHeadquarters(scene);
    if (headquarters == nullptr)
        return;

    if (isBuildingSelected && ActivePanel()->GetBuilding() == headquarters)
    {
        ClearBuildingSelection();
        selectedBuildingWidget.building = nullptr;
        Log::Msg("[Input]", "E pressed - headquarters panel closed");
        return;
    }

    OpenHeadquartersPanel();
}

// Opens the full-screen statistics panel.
void BasicMapViewSystem::StatsPressed()
{
    ClearBuildingSelection();
    selectedBuildingWidget.building = nullptr;
    owner->ChangeSystem("stats");
    Log::Msg("[Input]", "S pressed - stats panel opened");
}

void BasicMapViewSystem::FocusPressed()
{
    ClearBuildingSelection();
    selectedBuildingWidget.building = nullptr;
    owner->ChangeSystem("focus");
    Log::Msg("[Input]", "F pressed - focus panel opened");
}

void BasicMapViewSystem::TechPressed()
{
    if (!HasUniversity(scene))
        return;
    ClearBuildingSelection();
    selectedBuildingWidget.building = nullptr;
    owner->ChangeSystem("tech");
    Log::Msg("[Input]", "T pressed - technology panel opened");
}

// Opens the local headquarters panel without toggling it closed.
void BasicMapViewSystem::OpenHeadquartersPanel()
{
    Building* headquarters = FindLocalHeadquarters(scene);
    if (headquarters == nullptr)
        return;

    isBuildingSelected = true;
    researchPanel.SetBuilding(nullptr);
    buildingInfoPanel.SetBuilding(headquarters);
    Log::Msg("[Input]", "E pressed - headquarters panel opened");
}

// Centers the camera on the local headquarters.
void BasicMapViewSystem::CenterOnHeadquartersPressed()
{
    Building* headquarters = FindLocalHeadquarters(scene);
    if (headquarters == nullptr)
        return;

    Vec2i anchor = scene->game->GetTileMap().GetCoordsFromId(headquarters->positionId);
    Vec2i footprint = headquarters->GetFootprint();
    Vec2f hqWorldCenter{
        static_cast<float>((anchor.x + footprint.x * 0.5f) * TILE_SIZE),
        static_cast<float>((anchor.y + footprint.y * 0.5f) * TILE_SIZE)};
    ApplyStrategicHudCameraPadding(scene);
    scene->render.CenterCameraOnWorld(hqWorldCenter, GetMapSize(scene));
    Log::Msg("[Input]", "space pressed - camera centered on headquarters");
}

// Handles map selection and panel interactions.
void BasicMapViewSystem::LmbPressed()
{
    auto mousePos = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mousePos.x), static_cast<int>(mousePos.y)};
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    if (isBuildingSelected && ActivePanel()->ContainsPoint(screenPos))
    {
        Log::Msg("[Input]", "building info panel clicked");
        return;
    }

    Vec2i tilePos = ScreenToTile(scene, mousePos);
    if (tilePos.x < 0 || tilePos.y < 0)
        return;

    auto &tile = scene->game->GetTileMap()[tilePos];

    Log::Msg("[Input]", "Tile ID: ", tile.id, " clicked!");

    auto building = tile.GetBuilding();
    if (building != nullptr)
    {
        // Circular click hitbox: only register a building hit near its footprint
        // centre (diameter 0.85 of the footprint side), so corner clicks feel like
        // clicking the ground rather than the building.
        Vec2i anchor = scene->game->GetTileMap().GetCoordsFromId(building->positionId);
        Vec2i fp = building->GetFootprint();
        Vec2f centerWorld{(anchor.x + fp.x * 0.5f) * TILE_SIZE, (anchor.y + fp.y * 0.5f) * TILE_SIZE};
        Vec2f centerScreen = scene->render.WorldToScreen(centerWorld);
        float radiusScreen = 0.425f * std::min(fp.x, fp.y) * TILE_SIZE * scene->render.camera.zoom;
        float ddx = mousePos.x - centerScreen.x;
        float ddy = mousePos.y - centerScreen.y;
        if (ddx * ddx + ddy * ddy > radiusScreen * radiusScreen)
            building = nullptr;  // outside the circle → treat as an empty-ground click
    }

    if (building != nullptr)
    {
        if (building->owner == GuiLocalPlayer(scene))
        {
            isBuildingSelected = true;
            if (building->buildingType == BuildingType::University)
            {
                buildingInfoPanel.SetBuilding(nullptr);
                researchPanel.SetBuilding(building);
            }
            else
            {
                researchPanel.SetBuilding(nullptr);
                buildingInfoPanel.SetBuilding(building);
            }
            Log::Msg("[Input]", building->name, " selected!");
        }
        else
        {
            ClearBuildingSelection();
            Log::Msg("[Input]", "enemy building clicked - intel unavailable");
        }
    }
    else
    {
        ClearBuildingSelection();
    }
}

// Left mouse button does not drive a drag interaction in the default map view.
void BasicMapViewSystem::LmbReleased()
{
}

// Assigns logistics receivers through the right button.
void BasicMapViewSystem::RmbPressed()
{
    auto mousePos = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mousePos.x), static_cast<int>(mousePos.y)};
    if (isBuildingSelected && ActivePanel()->ContainsPoint(screenPos))
    {
        cameraMovement.isMoving = false;
        return;
    }

    if (isBuildingSelected && ActivePanel()->HasBuilding())
    {
        Vec2i tilePos = ScreenToTile(scene, mousePos);
        if (tilePos.x >= 0 && tilePos.y >= 0)
        {
            auto* selected = ActivePanel()->GetBuilding();
            auto* receiver = scene->game->GetTileMap().GetBuilding(tilePos);
            if (selected != nullptr && receiver != nullptr && selected != receiver)
            {
                bool alternativeReceiver = InputManager::IsKeyDown(KEY_LEFT_CONTROL) || InputManager::IsKeyDown(KEY_RIGHT_CONTROL);
                scene->SubmitLocalCommand(GameCommand::SetReceiver(
                    scene->game->GetLocalPlayerId(), selected->positionId,
                    receiver->positionId, alternativeReceiver));
                Log::Msg("[Input]", receiver->name,
                         alternativeReceiver ? " set as alternative receiver for " : " set as receiver for ",
                         selected->name);
                return;
            }
        }
    }

    // Camera panning lives on the middle mouse button (see mmbp/mmbr), so the
    // right button is free for movement and combat orders.
}

// Stops camera drag.
void BasicMapViewSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

// Scrolls panels under the cursor or zooms the camera.
void BasicMapViewSystem::Scroll()
{
    auto mouse = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
    GuiPanel* activePanel = ActivePanel();
    if (isBuildingSelected && activePanel->ContainsPoint(screenPos))
    {
        if (researchPanel.HasBuilding() && (InputManager::IsKeyDown(KEY_LEFT_CONTROL) || InputManager::IsKeyDown(KEY_RIGHT_CONTROL)))
        {
            researchPanel.treePanOffset.y += InputManager::GetMouseWheelMove() * 64.0f;
            return;
        }
        if (researchPanel.HasBuilding())
        {
            researchPanel.AdjustTreeZoom(screenPos, InputManager::GetMouseWheelMove());
            return;
        }
        activePanel->ScrollContent(InputManager::GetMouseWheelMove());
        return;
    }

    ZoomCamera(scene);
}
