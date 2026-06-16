#include "../inc/Scenes.h"

void InputProcessor::HandleInputs()
{
    if (IsActionPressed(CLOSE_TOP_GUI))
        controller->MakeAction("esc");
    if (IsActionPressed(OPEN_BUILD_GUI))
        controller->MakeAction("q");
    if (IsActionPressed(LEFT_BUTTON_DOWN))
        controller->MakeAction("lmbp");
    if (IsActionReleased(LEFT_BUTTON_DOWN))
        controller->MakeAction("lmbr");
    if (IsActionPressed(RIGHT_BUTTON_DOWN))
        controller->MakeAction("rmbp");
    if (IsActionReleased(RIGHT_BUTTON_DOWN))
        controller->MakeAction("rmbr");
}

void GuiController::Init(GameScene *s)
{
    scene = s;
}

void GuiController::Update(double dt)
{
    ui.clear();
    activeSystem->Update(dt);
}

void GuiController::MakeAction(std::string action)
{
    activeSystem->actionMap[action]();
}

// ================ BASIC CAMERA SYSTEM ====================

void BasicMapViewSystem::Update(double dt)
{
    if (cameraMovement.isMoving)
    {
        Vector2 delta = GetMouseDelta();
        delta.x *= -1;
        auto pos = Vector2Clamp(Vector2Add(scene->render.camera.target, delta), {0, -scene->game->tilemap.params.sizeY * 64 + 1080}, {scene->game->tilemap.params.sizeX * 64 - 1920, 0});
        scene->render.camera.target = pos;
        Log::Msg("[GuiController]", "mouse pos: ", scene->render.camera.target.x, " ", scene->render.camera.target.y);
    }

    if (isBuildingSelected)
    {
        // handling building Gui Panel
        owner->AddUiWidget(&buildingInfoPanel);
    }
}

void BasicMapViewSystem::UpdateUiWidgets(Vec2i size)
{
    buildingInfoPanel.UpdateSize(size);
}

void BasicMapViewSystem::EscPressed()
{
    if(isBuildingSelected)
    {
        isBuildingSelected = false;
        return;
    }

    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = scene;
    msg->sceneName = "GameMenuScene";
    scene->broker->Broadcast(msg);

    Log::Msg("[Input]", "escape pressed");
}

void BasicMapViewSystem::BuildPressed()
{
    // owner->ChangeSystem("build");
    Log::Msg("[Input]", "Q pressed");
}

void BasicMapViewSystem::LmbPressed()
{
    auto pos = GetMousePosition();
    pos.x += scene->render.camera.target.x;
    pos.y -= scene->render.camera.target.y;

    Vec2i tilePos{pos.x / 64, pos.y / 64};
    auto &tile = scene->game->tilemap[tilePos];

    Log::Msg("[Input]", "Tile ID: ", tile.id, " clicked!");

    // if the given tile has a building on
    if (tile.building != nullptr)
    {
        isBuildingSelected = true;
        Log::Msg("[Input]", tile.building->name, " selected!");
    }
    else
        isBuildingSelected = false;
}

void BasicMapViewSystem::LmbReleased()
{
}

void BasicMapViewSystem::RmbPressed()
{
    cameraMovement.isMoving = true;
}

void BasicMapViewSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

// ============== BUILD SYSTEM ===================

