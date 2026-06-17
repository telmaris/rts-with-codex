#include "../inc/Scenes.h"

namespace
{
    void MoveCamera(GameScene* scene, CameraMovement& cameraMovement)
    {
        if (!cameraMovement.isMoving)
            return;
        if (!IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
        {
            cameraMovement.isMoving = false;
            return;
        }

        Vector2 delta = GetMouseDelta();
        delta.x *= -1;
        auto minPos = Vector2{0.0f, static_cast<float>(-scene->game->tilemap.params.sizeY * TILE_SIZE + 1080)};
        auto maxPos = Vector2{static_cast<float>(scene->game->tilemap.params.sizeX * TILE_SIZE - 1920), 0.0f};
        auto pos = Vector2Clamp(Vector2Add(scene->render.camera.target, delta), minPos, maxPos);
        scene->render.camera.target = pos;
    }

    template <typename T>
    BuildOption MakeBuildOption(GameScene* scene, const std::string& name, const std::string& costText, double buildTime = 0.0)
    {
        static_assert(std::is_base_of<Building, T>::value);

        BuildOption option;
        option.name = name;
        option.costText = costText;
        option.buildTime = buildTime;
        option.previewFactory = []()
        {
            return std::make_unique<T>(0);
        };
        option.buildAt = [scene](Vec2i tilePos)
        {
            auto player = scene->game->playerHandler.players[0].get();
            return player->Build<T>(tilePos);
        };

        return option;
    }

    Rectangle BuildingScreenRect(GameScene* scene, Building* building)
    {
        Vec2i anchor = scene->game->tilemap.GetCoordsFromId(building->positionId);
        Vec2i footprint = building->GetFootprint();
        Vec2f renderTopLeft{
            static_cast<float>(anchor.x * TILE_SIZE - scene->render.camera.target.x),
            static_cast<float>(anchor.y * TILE_SIZE + scene->render.camera.target.y)};
        Vec2f renderBottomRight{
            renderTopLeft.x + footprint.x * TILE_SIZE,
            renderTopLeft.y + footprint.y * TILE_SIZE};
        Vec2f screenTopLeft = scene->render.RenderToScreen(renderTopLeft);
        Vec2f screenBottomRight = scene->render.RenderToScreen(renderBottomRight);
        return Rectangle{
            screenTopLeft.x,
            screenTopLeft.y,
            screenBottomRight.x - screenTopLeft.x,
            screenBottomRight.y - screenTopLeft.y};
    }
}

void InputProcessor::HandleInputs()
{
    if (IsActionPressed(CLOSE_TOP_GUI))
        controller->MakeAction("esc");
    if (IsActionPressed(OPEN_BUILD_GUI))
        controller->MakeAction("q");
    if (IsActionPressed(OPEN_ROAD_BUILD_GUI))
        controller->MakeAction("r");
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
    auto it = activeSystem->actionMap.find(action);
    if (it != activeSystem->actionMap.end())
        it->second();
}

// ================ BASIC CAMERA SYSTEM ====================

void BasicMapViewSystem::Update(double dt)
{
    MoveCamera(scene, cameraMovement);

    if (isBuildingSelected)
    {
        if (!buildingInfoPanel.HasBuilding())
        {
            isBuildingSelected = false;
            return;
        }

        // handling building Gui Panel
        selectedBuildingWidget.building = buildingInfoPanel.GetBuilding();
        owner->AddUiWidget(&selectedBuildingWidget);
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
        buildingInfoPanel.SetBuilding(nullptr);
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
    isBuildingSelected = false;
    buildingInfoPanel.SetBuilding(nullptr);
    owner->ChangeSystem("build");
    Log::Msg("[Input]", "Q pressed");
}

void BasicMapViewSystem::RoadBuildPressed()
{
    isBuildingSelected = false;
    buildingInfoPanel.SetBuilding(nullptr);
    cameraMovement.isMoving = false;
    owner->ChangeSystem("road_build");
    Log::Msg("[Input]", "R pressed");
}

void BasicMapViewSystem::LmbPressed()
{
    auto mousePos = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mousePos.x), static_cast<int>(mousePos.y)};

    if (isBuildingSelected && buildingInfoPanel.ContainsPoint(screenPos))
    {
        Log::Msg("[Input]", "building info panel clicked");
        return;
    }

    auto renderPos = scene->render.ScreenToRender(mousePos);
    if (renderPos.x < 0.0f || renderPos.y < 0.0f)
        return;

    Vector2 pos{renderPos.x, renderPos.y};
    pos.x += scene->render.camera.target.x;
    pos.y -= scene->render.camera.target.y;

    if (pos.x < 0 || pos.y < 0)
        return;

    Vec2i tilePos{static_cast<int>(pos.x / TILE_SIZE), static_cast<int>(pos.y / TILE_SIZE)};
    if (tilePos.x < 0 || tilePos.x >= scene->game->tilemap.params.sizeX ||
        tilePos.y < 0 || tilePos.y >= scene->game->tilemap.params.sizeY)
    {
        return;
    }

    auto &tile = scene->game->tilemap[tilePos];

    Log::Msg("[Input]", "Tile ID: ", tile.id, " clicked!");

    auto building = tile.GetBuilding();
    if (building != nullptr)
    {
        isBuildingSelected = true;
        buildingInfoPanel.SetBuilding(building);
        Log::Msg("[Input]", building->name, " selected!");
    }
    else
    {
        isBuildingSelected = false;
        buildingInfoPanel.SetBuilding(nullptr);
    }
}

void BasicMapViewSystem::LmbReleased()
{
}

void BasicMapViewSystem::RmbPressed()
{
    if (isBuildingSelected && buildingInfoPanel.HasBuilding())
    {
        auto mousePos = GetMousePosition();
        auto renderPos = scene->render.ScreenToRender(mousePos);
        if (renderPos.x >= 0.0f && renderPos.y >= 0.0f)
        {
            Vector2 worldPos{renderPos.x, renderPos.y};
            worldPos.x += scene->render.camera.target.x;
            worldPos.y -= scene->render.camera.target.y;
            Vec2i tilePos{static_cast<int>(worldPos.x / TILE_SIZE), static_cast<int>(worldPos.y / TILE_SIZE)};

            if (scene->game->tilemap.IsInside(tilePos))
            {
                auto* selected = buildingInfoPanel.GetBuilding();
                auto* receiver = scene->game->tilemap.GetBuilding(tilePos);
                if (selected != nullptr && receiver != nullptr && selected != receiver)
                {
                    scene->game->tilemap.ConnectReceiver(selected, receiver);
                    Log::Msg("[Input]", receiver->name, " set as receiver for ", selected->name);
                    return;
                }
            }
        }
    }

    cameraMovement.isMoving = true;
}

void BasicMapViewSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

// ============== BUILD SYSTEM ===================

void SelectedBuildingWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr || building == nullptr)
        return;

    Rectangle dest = BuildingScreenRect(scene, building);
    DrawRectangleRounded(dest, 0.04f, 8, Color{88, 196, 124, 55});
    DrawRectangleRoundedLines(dest, 0.04f, 8, Color{112, 230, 150, 185});
}

void BuildPanelWidget::Update(double dt)
{
    if (scene == nullptr || options == nullptr)
        return;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    DrawRectangleRounded(bounds, 0.025f, 8, Color{28, 32, 38, 238});
    DrawRectangleRoundedLines(bounds, 0.025f, 8, Color{92, 102, 118, 255});

    int margin = std::max(10, size.x / 24);
    int titleBar = std::max(34, size.y / 12);
    Rectangle titleBounds{bounds.x, bounds.y, bounds.width, static_cast<float>(titleBar)};
    DrawRectangleRounded(titleBounds, 0.025f, 8, Color{44, 52, 65, 255});
    int titleFont = std::max(17, std::min(24, titleBar / 2));
    const char* titleText = title.c_str();
    int titleWidth = MeasureText(titleText, titleFont);
    DrawText(titleText, static_cast<int>(bounds.x + (bounds.width - titleWidth) * 0.5f), static_cast<int>(bounds.y + (titleBar - titleFont) * 0.5f), titleFont, RAYWHITE);

    int columns = 3;
    float gap = 7.0f;
    float cardW = (bounds.width - margin * 2 - gap * (columns - 1)) / columns;
    float cardH = std::max(58.0f, cardW * 0.62f);
    float startY = bounds.y + titleBar + margin;

    auto atlasIt = scene->render.atlasMap.find(1);
    for (int i = 0; i < options->size(); i++)
    {
        int col = i % columns;
        int row = i / columns;
        Rectangle card{
            bounds.x + margin + col * (cardW + gap),
            startY + row * (cardH + gap),
            cardW,
            cardH};
        if (card.y + card.height > bounds.y + bounds.height - margin)
            break;

        bool selected = static_cast<size_t>(i) == selectedIndex;
        DrawRectangleRounded(card, 0.06f, 8, selected ? Color{48, 68, 58, 245} : Color{36, 41, 49, 235});
        DrawRectangleRoundedLines(card, 0.06f, 8, selected ? Color{112, 230, 150, 210} : Color{88, 98, 114, 255});

        auto preview = (*options)[i].previewFactory();
        if (preview != nullptr && atlasIt != scene->render.atlasMap.end())
        {
            Rectangle src = atlasIt->second.GetRectFromId(preview->GetTextureId());
            float icon = std::min(card.width * 0.50f, card.height - 12.0f);
            Rectangle dst{card.x + 8.0f, card.y + (card.height - icon) * 0.5f, icon, icon};
            DrawTexturePro(atlasIt->second.tex, src, dst, {0.0f, 0.0f}, 0.0f, WHITE);
        }

        int nameFont = 12;
        DrawText((*options)[i].name.c_str(), static_cast<int>(card.x + card.width * 0.48f), static_cast<int>(card.y + 10.0f), nameFont, RAYWHITE);
        DrawText((*options)[i].costText.c_str(), static_cast<int>(card.x + card.width * 0.48f), static_cast<int>(card.y + 28.0f), 10, Color{188, 197, 208, 255});

        if (CheckCollisionPointRec(GetMousePosition(), card))
        {
            std::string tooltip = (*options)[i].name + "\n" + (*options)[i].costText + "\nBuild time: " + std::to_string(static_cast<int>((*options)[i].buildTime)) + "s";
            Vector2 mouse = GetMousePosition();
            Rectangle tip{mouse.x + 14.0f, mouse.y + 14.0f, 190.0f, 66.0f};
            DrawRectangleRounded(tip, 0.06f, 8, Color{18, 22, 28, 245});
            DrawRectangleRoundedLines(tip, 0.06f, 8, Color{110, 125, 146, 255});
            DrawText((*options)[i].name.c_str(), static_cast<int>(tip.x + 10.0f), static_cast<int>(tip.y + 8.0f), 15, RAYWHITE);
            DrawText((*options)[i].costText.c_str(), static_cast<int>(tip.x + 10.0f), static_cast<int>(tip.y + 30.0f), 12, Color{195, 205, 216, 255});
            DrawText("Instant build", static_cast<int>(tip.x + 10.0f), static_cast<int>(tip.y + 47.0f), 12, Color{195, 205, 216, 255});
        }
    }
}

int BuildPanelWidget::GetOptionAt(Vec2i point) const
{
    if (options == nullptr)
        return -1;

    int margin = std::max(10, size.x / 24);
    int titleBar = std::max(34, size.y / 12);
    int columns = 3;
    float gap = 8.0f;
    float cardW = (size.x - margin * 2 - gap * (columns - 1)) / columns;
    float cardH = std::max(72.0f, cardW * 0.72f);
    float startY = pos.y + titleBar + margin;

    for (int i = 0; i < options->size(); i++)
    {
        int col = i % columns;
        int row = i / columns;
        Rectangle card{
            static_cast<float>(pos.x + margin) + col * (cardW + gap),
            startY + row * (cardH + gap),
            cardW,
            cardH};
        if (CheckCollisionPointRec(Vector2{static_cast<float>(point.x), static_cast<float>(point.y)}, card))
            return i;
    }
    return -1;
}

void BuildGhostWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr || selectedOption == nullptr)
        return;
    if (tilePos.x < 0 || tilePos.y < 0)
        return;

    auto preview = selectedOption->previewFactory();
    if (preview == nullptr)
        return;

    Vec2i footprint = preview->GetFootprint();
    Vec2f renderTopLeft{
        static_cast<float>(tilePos.x * TILE_SIZE - scene->render.camera.target.x),
        static_cast<float>(tilePos.y * TILE_SIZE + scene->render.camera.target.y)};
    Vec2f renderBottomRight{
        renderTopLeft.x + footprint.x * TILE_SIZE,
        renderTopLeft.y + footprint.y * TILE_SIZE};

    Vec2f screenTopLeft = scene->render.RenderToScreen(renderTopLeft);
    Vec2f screenBottomRight = scene->render.RenderToScreen(renderBottomRight);
    Rectangle dest{
        screenTopLeft.x,
        screenTopLeft.y,
        screenBottomRight.x - screenTopLeft.x,
        screenBottomRight.y - screenTopLeft.y};

    Color tint = canBuild ? Color{88, 196, 124, 62} : Color{220, 80, 80, 70};
    DrawRectangleRounded(dest, 0.04f, 8, tint);
    DrawRectangleRoundedLines(dest, 0.04f, 8, canBuild ? Color{112, 230, 150, 180} : Color{240, 110, 110, 190});

    auto atlasIt = scene->render.atlasMap.find(1);
    if (atlasIt == scene->render.atlasMap.end())
        return;

    auto& atlas = atlasIt->second;
    Rectangle src = atlas.GetRectFromId(preview->GetTextureId());
    DrawTexturePro(atlas.tex, src, dest, {0.0f, 0.0f}, 0.0f, Color{255, 255, 255, static_cast<unsigned char>(canBuild ? 170 : 120)});
}

BuildGuiSystem::BuildGuiSystem(GuiController* con, bool roadModeParam)
    : GuiSystem(con)
{
    scene = owner->scene;
    roadMode = roadModeParam;

    actionMap["esc"]  = [this] { EscPressed(); };
    actionMap["q"]    = [this] { BuildPressed(); };
    actionMap["r"]    = [this] { RoadBuildPressed(); };
    actionMap["lmbp"] = [this] { LmbPressed(); };
    actionMap["lmbr"] = [this] { LmbReleased(); };
    actionMap["rmbp"] = [this] { RmbPressed(); };
    actionMap["rmbr"] = [this] { RmbReleased(); };

    buildPanel.ChangePositionAnchor({0.69f, 0.08f});
    buildPanel.ChangeSizeAnchor({0.28f, 0.82f});
    buildPanel.scene = scene;
    buildPanel.options = &options;
    buildPanel.title = roadMode ? "Roads" : "Build";
    buildPanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});

    if (roadMode)
    {
        options.push_back(MakeBuildOption<Road>(scene, "Road", "Cost TBD"));
    }
    else
    {
        options.push_back(MakeBuildOption<Woodcutter>(scene, "Woodcutter", "Cost TBD"));
        options.push_back(MakeBuildOption<LumberMill>(scene, "Lumber Mill", "Cost TBD"));
        options.push_back(MakeBuildOption<Mine>(scene, "Mine", "Cost TBD"));
        options.push_back(MakeBuildOption<Foundry>(scene, "Foundry", "Cost TBD"));
        options.push_back(MakeBuildOption<StorageBuilding>(scene, "Storage", "Cost TBD"));
    }

    ghostWidget.scene = scene;
    SelectOption(0);
}

void BuildGuiSystem::UpdateUiWidgets(Vec2i size)
{
    buildPanel.UpdateSize(size);
}

void BuildGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    MoveCamera(scene, cameraMovement);
    RefreshGhost();
    owner->AddUiWidget(&ghostWidget);
    owner->AddUiWidget(&buildPanel);
}

void BuildGuiSystem::EscPressed()
{
    ReturnToMapView();
}

void BuildGuiSystem::BuildPressed()
{
    ReturnToMapView();
}

void BuildGuiSystem::RoadBuildPressed()
{
    if (roadMode)
        ReturnToMapView();
    else
        owner->ChangeSystem("road_build");
}

void BuildGuiSystem::LmbPressed()
{
    auto mousePos = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mousePos.x), static_cast<int>(mousePos.y)};
    if (buildPanel.ContainsPoint(screenPos))
    {
        int option = buildPanel.GetOptionAt(screenPos);
        if (option >= 0)
            SelectOption(static_cast<size_t>(option));
        return;
    }

    Vec2i tilePos = GetHoveredTile();
    if (!CanPlaceSelected(tilePos))
        return;

    if (selectedIndex < options.size())
    {
        buildTimePlaceholder = options[selectedIndex].buildTime;
        options[selectedIndex].buildAt(tilePos);
        if (!roadMode)
            ReturnToMapView();
    }
}

void BuildGuiSystem::LmbReleased()
{
}

void BuildGuiSystem::RmbPressed()
{
    cameraMovement.isMoving = true;
}

void BuildGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

void BuildGuiSystem::ReturnToMapView()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}

Vec2i BuildGuiSystem::GetHoveredTile() const
{
    auto mousePos = GetMousePosition();
    auto renderPos = scene->render.ScreenToRender(mousePos);
    if (renderPos.x < 0.0f || renderPos.y < 0.0f)
        return {-1, -1};

    Vector2 worldPos{renderPos.x, renderPos.y};
    worldPos.x += scene->render.camera.target.x;
    worldPos.y -= scene->render.camera.target.y;
    if (worldPos.x < 0.0f || worldPos.y < 0.0f)
        return {-1, -1};

    return Vec2i{
        static_cast<int>(worldPos.x / TILE_SIZE),
        static_cast<int>(worldPos.y / TILE_SIZE)};
}

bool BuildGuiSystem::CanPlaceSelected(Vec2i tilePos) const
{
    if (selectedPreview == nullptr || scene->game == nullptr)
        return false;

    auto player = scene->game->playerHandler.players[0].get();
    return scene->game->tilemap.CanBuildFootprint(tilePos, selectedPreview->GetFootprint(), player);
}

void BuildGuiSystem::SelectOption(size_t index)
{
    if (index >= options.size())
        return;

    selectedIndex = index;
    selectedPreview = options[selectedIndex].previewFactory();
    buildPanel.selectedIndex = selectedIndex;

    if (scene->game != nullptr)
        RefreshGhost();
}

void BuildGuiSystem::RefreshGhost()
{
    ghostWidget.selectedOption = selectedIndex < options.size() ? &options[selectedIndex] : nullptr;
    ghostWidget.tilePos = GetHoveredTile();
    ghostWidget.canBuild = CanPlaceSelected(ghostWidget.tilePos);
}

