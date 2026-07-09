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

// Attaches the controller to a scene and creates the shared map overlay.
void GuiController::Init(GameScene *s)
{
    scene = s;
    divisionMapOverlay = std::make_unique<DivisionMapWidget>();
    divisionMapOverlay->scene = scene;
    armyOrderPanel = std::make_unique<ArmyOrderPanelWidget>();
    armyOrderPanel->scene = scene;
    // Connection to armyBarWidget will be done in BasicMapViewSystem after it's created
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
    if (divisionMapOverlay != nullptr)
        AddUiWidget(divisionMapOverlay.get());
    if (armyOrderPanel != nullptr)
        AddUiWidget(armyOrderPanel.get());
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
    militaryOrderWidget.scene = scene;
    // Division panel sits top-left (under the resource HUD) as a vertical
    // HOI4-style list; the bottom of the screen is reserved for the army bar.
    militaryDivisionBarWidget.ChangePositionAnchor({0.012f, 0.085f});
    militaryDivisionBarWidget.ChangeSizeAnchor({0.23f, 0.58f});
    SetupStrategicHud(strategicHudWidget, scene);
    armyBarWidget.scene = scene;
    armyBarWidget.bar = &militaryDivisionBarWidget;
    // Bottom-center HOI4-style army strip.
    armyBarWidget.ChangePositionAnchor({0.30f, 0.88f});
    armyBarWidget.ChangeSizeAnchor({0.40f, 0.10f});
    moveTargetWidget.scene = scene;
    moveTargetWidget.bar = &militaryDivisionBarWidget;
    moveTargetWidget.armyBar = &armyBarWidget;

    // Connect army order panel to army bar widget.
    if (owner != nullptr && owner->armyOrderPanel != nullptr)
    {
        owner->armyOrderPanel->armyBar = &armyBarWidget;
    }

    buildingInfoPanel.recruitRequested = [this](Building* building, MilitaryUnitType unitType)
    {
        SubmitRecruitCommand(building, unitType);
    };
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
    owner->AddUiWidget(&militaryOrderWidget);

    // Track drag-box selection.
    moveTargetWidget.drawBox = false;
    if (pendingBox && InputManager::IsMouseButtonDown(MOUSE_LEFT_BUTTON))
    {
        Vector2 m = GetMousePosition();
        boxEnd = {static_cast<int>(m.x), static_cast<int>(m.y)};
        int dx = boxEnd.x - boxStart.x;
        int dy = boxEnd.y - boxStart.y;
        if (dx * dx + dy * dy > 36)
            boxActive = true;
        if (boxActive)
        {
            moveTargetWidget.drawBox = true;
            moveTargetWidget.boxRect = {
                static_cast<float>(std::min(boxStart.x, boxEnd.x)),
                static_cast<float>(std::min(boxStart.y, boxEnd.y)),
                static_cast<float>(std::abs(dx)),
                static_cast<float>(std::abs(dy))};
        }
    }

    owner->AddUiWidget(&armyBarWidget);   // persistent bottom army strip

    bool hasSelection = militaryDivisionBarWidget.building != nullptr &&
                        !militaryDivisionBarWidget.selectedDivisionIds.empty();
    if (hasSelection || moveTargetWidget.drawBox)
        owner->AddUiWidget(&moveTargetWidget);

    if (isDivisionOnlyMode && !isBuildingSelected)
    {
        // Show only the garrison bar — triggered by clicking a map division marker.
        // Clear when the home building disappears or user clicks elsewhere.
        auto* garrison = militaryDivisionBarWidget.building != nullptr
            ? militaryDivisionBarWidget.building->GetComponent<GarrisonComponent>() : nullptr;
        if (garrison == nullptr || garrison->divisions.empty())
        {
            isDivisionOnlyMode = false;
            militaryDivisionBarWidget.building = nullptr;
        }
        else
        {
            owner->AddUiWidget(&militaryDivisionBarWidget);
        }
    }

    if (isBuildingSelected)
    {
        isDivisionOnlyMode = false;
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
        Building* selected = activePanel->GetBuilding();
        // Only defensive works (tower/fortress/castle) present as garrisons.
        // The Barracks factory and the HQ are neutral buildings — their homed
        // field divisions are managed through map counters and the army bar.
        if (selected != nullptr && IsDefensiveGarrisonBuilding(*selected))
        {
            militaryDivisionBarWidget.building = selected;
            owner->AddUiWidget(&militaryDivisionBarWidget);
        }
        else
        {
            militaryDivisionBarWidget.building = nullptr;
        }
    }
    owner->AddUiWidget(&strategicHudWidget);
}

// Applies window size changes to widgets owned by this mode.
void BasicMapViewSystem::UpdateUiWidgets(Vec2i size)
{
    buildingInfoPanel.UpdateSize(size);
    researchPanel.UpdateSize(size);
    militaryDivisionBarWidget.UpdateSize(size);
    armyBarWidget.UpdateSize(size);
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

    // Close the division / army selection panel before opening the game menu.
    if (isDivisionOnlyMode || !militaryDivisionBarWidget.selectedDivisionIds.empty())
    {
        isDivisionOnlyMode = false;
        militaryDivisionBarWidget.selectedDivisionIds.clear();
        militaryDivisionBarWidget.building = nullptr;
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
    if ((isBuildingSelected || isDivisionOnlyMode) && militaryDivisionBarWidget.building != nullptr &&
        militaryDivisionBarWidget.ContainsPoint(screenPos))
    {
        militaryDivisionBarWidget.HandleClick(screenPos);
        Log::Msg("[Input]", "military division bar clicked");
        return;
    }

    // Army strip (bottom) handles its own clicks: form / select armies. Only the
    // cards and the "+" consume the click — empty space falls through to the map.
    if (armyBarWidget.HandleClick(screenPos))
    {
        // Selecting an army opens the division side panel so its units are visible.
        if (militaryDivisionBarWidget.building != nullptr &&
            !militaryDivisionBarWidget.selectedDivisionIds.empty())
        {
            ClearBuildingSelection();
            isDivisionOnlyMode = true;
        }
        return;
    }

    // Army order panel (right side) handles order button clicks.
    if (owner->armyOrderPanel != nullptr && owner->armyOrderPanel->HandleClick(screenPos))
    {
        Log::Msg("[Input]", "army order panel clicked");
        return;
    }

    // Check division map markers before tile selection
    {
        bool ctrl = InputManager::IsKeyDown(KEY_LEFT_CONTROL) || InputManager::IsKeyDown(KEY_RIGHT_CONTROL);
        const DivisionMapMarker* hit = owner->divisionMapOverlay != nullptr
            ? owner->divisionMapOverlay->HitTest(screenPos)
            : nullptr;
        Player* localPlayer = GuiLocalPlayer(scene);
        if (hit != nullptr && hit->homeBuilding != nullptr && hit->owner == localPlayer)
        {
            Building* home = hit->homeBuilding;
            // Division-only mode: show only garrison bar, not full building panel
            ClearBuildingSelection();
            isDivisionOnlyMode = true;
            // Switching to a different home drops the old selection immediately
            // (and keeps prevBuilding in sync so Update() doesn't wipe the new one).
            if (militaryDivisionBarWidget.building != home)
                militaryDivisionBarWidget.SetSelection(home, {});
            // A counter is a stack of divisions — selecting it takes the whole stack.
            if (ctrl)
            {
                for (int id : hit->divisionIds)
                {
                    auto it = std::find(militaryDivisionBarWidget.selectedDivisionIds.begin(),
                                        militaryDivisionBarWidget.selectedDivisionIds.end(), id);
                    if (it != militaryDivisionBarWidget.selectedDivisionIds.end())
                        militaryDivisionBarWidget.selectedDivisionIds.erase(it);
                    else
                        militaryDivisionBarWidget.selectedDivisionIds.push_back(id);
                }
            }
            else
            {
                militaryDivisionBarWidget.selectedDivisionIds = hit->divisionIds;
            }
            Log::Msg("[Input]", hit->divisionIds.size(), " division(s) selected via map counter");
            return;
        }
    }

    // Field-battle circle → open its details panel.
    if (const FieldBattleMarker* fb = militaryOrderWidget.HitTest(screenPos))
    {
        militaryOrderWidget.SelectBattle(*fb);
        return;
    }

    // Clicking on the map clears division-only selection
    isDivisionOnlyMode = false;
    militaryOrderWidget.CloseDetails();

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
        // Begin a potential drag-box selection on open ground.
        pendingBox = true;
        boxActive = false;
        boxStart = screenPos;
        boxEnd = screenPos;
    }
}

// Finalizes a drag-box division selection, or clears selection on a plain click.
void BasicMapViewSystem::LmbReleased()
{
    if (!pendingBox)
        return;

    if (boxActive && owner->divisionMapOverlay != nullptr)
    {
        float x0 = static_cast<float>(std::min(boxStart.x, boxEnd.x));
        float x1 = static_cast<float>(std::max(boxStart.x, boxEnd.x));
        float y0 = static_cast<float>(std::min(boxStart.y, boxEnd.y));
        float y1 = static_cast<float>(std::max(boxStart.y, boxEnd.y));

        // Group-select boxed markers. Divisions all share one home building (v1),
        // matching the move command's single-source model. Only the local player's
        // divisions are selectable — never enemy or allied stacks.
        Player* localPlayer = GuiLocalPlayer(scene);
        Building* home = nullptr;
        std::vector<int> ids;
        for (const auto& marker : owner->divisionMapOverlay->markers)
        {
            if (marker.homeBuilding == nullptr || marker.owner != localPlayer)
                continue;
            // Overlap test against the badge RECTANGLE (its drawn footprint), not
            // just the centre point — a drag that visibly touches a counter takes it.
            if (marker.screenPos.x + DivisionMapWidget::kMarkerHalfW < x0 ||
                marker.screenPos.x - DivisionMapWidget::kMarkerHalfW > x1 ||
                marker.screenPos.y + DivisionMapWidget::kMarkerHalfH < y0 ||
                marker.screenPos.y - DivisionMapWidget::kMarkerHalfH > y1)
                continue;
            if (home == nullptr)
                home = marker.homeBuilding;
            if (marker.homeBuilding == home)
                for (int id : marker.divisionIds)
                    ids.push_back(id);
        }

        if (home != nullptr && !ids.empty())
        {
            militaryDivisionBarWidget.SetSelection(home, ids);
            isDivisionOnlyMode = true;
            isBuildingSelected = false;
            Log::Msg("[Input]", "Box-selected ", ids.size(), " divisions");
        }
        else
        {
            militaryDivisionBarWidget.selectedDivisionIds.clear();
            isDivisionOnlyMode = false;
        }
    }
    else
    {
        // Plain click on open ground clears the division selection.
        militaryDivisionBarWidget.selectedDivisionIds.clear();
        isDivisionOnlyMode = false;
    }

    pendingBox = false;
    boxActive = false;
    moveTargetWidget.drawBox = false;
}

// Issues military orders and logistics assignments through the right button.
void BasicMapViewSystem::RmbPressed()
{
    auto mousePos = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mousePos.x), static_cast<int>(mousePos.y)};
    if (isBuildingSelected && ActivePanel()->ContainsPoint(screenPos))
    {
        cameraMovement.isMoving = false;
        return;
    }

    // ── Battle marker RMB ────────────────────────────────────────────────────
    // If divisions are selected: attack the enemy participant (support the battle).
    // If no divisions selected: open the battle details panel (same as LMB).
    if (const FieldBattleMarker* fb = militaryOrderWidget.HitTest(screenPos))
    {
        const auto& divIds = militaryDivisionBarWidget.selectedDivisionIds;
        Building* home = militaryDivisionBarWidget.building;
        if (!divIds.empty() && home != nullptr)
        {
            Player* lp = GuiLocalPlayer(scene);
            Vec2i enemyTile{-1, -1};
            for (const auto& p : fb->participants)
            {
                if (enemyTile.x >= 0) break;
                if (lp != nullptr && p.playerId == lp->id) continue;
                auto plit = scene->game->GetPlayerHandler().players.find(p.playerId);
                if (plit == scene->game->GetPlayerHandler().players.end() || !plit->second) continue;
                for (Building* b : plit->second->GetTrackedBuildingsWithComponent<GarrisonComponent>())
                {
                    if (enemyTile.x >= 0) break;
                    auto* g = b ? b->GetComponent<GarrisonComponent>() : nullptr;
                    if (!g) continue;
                    for (const auto& div : g->divisions)
                        if (div->id == p.divisionId && div->occupiedTile.x >= 0)
                        { enemyTile = div->occupiedTile; break; }
                }
            }
            if (enemyTile.x >= 0)
            {
                int targetId = scene->game->GetTileMap().GetIdFromCoords(enemyTile);
                for (int divId : divIds)
                    scene->SubmitLocalCommand(GameCommand::AttackTile(
                        scene->game->GetLocalPlayerId(), home->positionId, divId, targetId));
                Log::Msg("[Input]", "battle support: attacking enemy at ", enemyTile.x, ",", enemyTile.y);
            }
        }
        else
        {
            militaryOrderWidget.SelectBattle(*fb);
        }
        cameraMovement.isMoving = false;
        return;
    }

    // ── RMB on an army card → transfer the current selection into that army ────
    {
        const auto& divIds = militaryDivisionBarWidget.selectedDivisionIds;
        Building* home = militaryDivisionBarWidget.building;
        if (!divIds.empty() && home != nullptr)
        {
            int targetArmyId = armyBarWidget.ArmyIdAt(screenPos);
            if (targetArmyId >= 0)
            {
                scene->SubmitLocalCommand(GameCommand::AssignToArmy(
                    scene->game->GetLocalPlayerId(), targetArmyId, home->positionId, divIds));
                Log::Msg("[Input]", "transfer ", divIds.size(), " divisions -> army #", targetArmyId);
                cameraMovement.isMoving = false;
                return;
            }
        }
    }

    // ── Division bar handler ──────────────────────────────────────────────────
    // When divisions are selected in the bar, RMB drives all military actions:
    //   enemy military building → attack, own garrison building → defend/enter,
    //   enemy army counter → attack tile, empty ground → move.
    {
        const auto& divIds = militaryDivisionBarWidget.selectedDivisionIds;
        Building* home = militaryDivisionBarWidget.building;
        if (!divIds.empty() && home != nullptr &&
            !militaryDivisionBarWidget.ContainsPoint(screenPos))
        {
            Player* localPlayer = GuiLocalPlayer(scene);

            // Enemy army counter → attack that tile.
            const DivisionMapMarker* hit = owner->divisionMapOverlay != nullptr
                ? owner->divisionMapOverlay->HitTest(screenPos)
                : nullptr;
            if (hit != nullptr && hit->owner != nullptr && hit->owner != localPlayer && hit->tile.x >= 0)
            {
                int targetTileId = scene->game->GetTileMap().GetIdFromCoords(hit->tile);
                for (int divId : divIds)
                {
                    scene->SubmitLocalCommand(GameCommand::AttackTile(
                        scene->game->GetLocalPlayerId(), home->positionId, divId, targetTileId));
                    Log::Msg("[Input]", "division #", divId, " attack tile at ",
                             hit->tile.x, ",", hit->tile.y);
                }
                cameraMovement.isMoving = false;
                return;
            }

            Vec2i tilePos = ScreenToTile(scene, mousePos);
            if (tilePos.x >= 0 && tilePos.y >= 0)
            {
                Building* clicked = scene->game->GetTileMap().GetBuilding(tilePos);
                // A road is traversable terrain, not an order target — treat clicking
                // it as clicking open ground so divisions can be moved onto/through it.
                if (clicked != nullptr && clicked->buildingType == BuildingType::Road)
                    clicked = nullptr;

                if (clicked != nullptr && clicked != home)
                {
                    bool isEnemy = (clicked->owner != nullptr && clicked->owner != localPlayer);
                    // Armies besiege only military targets (defensive works + HQ)
                    // and garrison only defensive works. Civil buildings like the
                    // Barracks factory fall through to the logistics handler.
                    bool attackTarget = IsMilitaryAttackTarget(*clicked);
                    bool defensiveGarrison = IsDefensiveGarrisonBuilding(*clicked);

                    if (attackTarget && isEnemy)
                    {
                        // Attack enemy military building with all selected divisions.
                        for (int divId : divIds)
                        {
                            scene->SubmitLocalCommand(GameCommand::IssueMilitaryOrder(
                                scene->game->GetLocalPlayerId(), MilitaryOrderType::Attack,
                                home->positionId, clicked->positionId, divId));
                            Log::Msg("[Input]", "division #", divId, " attack order -> ", clicked->name);
                        }
                        cameraMovement.isMoving = false;
                        return;
                    }

                    if (defensiveGarrison && !isEnemy)
                    {
                        // Defend/enter own garrison.
                        for (int divId : divIds)
                        {
                            scene->SubmitLocalCommand(GameCommand::IssueMilitaryOrder(
                                scene->game->GetLocalPlayerId(), MilitaryOrderType::Defend,
                                home->positionId, clicked->positionId, divId));
                            Log::Msg("[Input]", "division #", divId, " defend order -> ", clicked->name);
                        }
                        cameraMovement.isMoving = false;
                        return;
                    }
                    // Clicked a non-military building: fall through to logistics handler.
                }
                else if (clicked == nullptr)
                {
                    // Empty ground → move divisions to this sector.
                    int tileId = scene->game->GetTileMap().GetIdFromCoords(tilePos);
                    for (int divId : divIds)
                    {
                        scene->SubmitLocalCommand(GameCommand::MoveDivision(
                            scene->game->GetLocalPlayerId(), home->positionId, divId, tileId));
                        Log::Msg("[Input]", "division #", divId, " move to sector at ",
                                 tilePos.x, ",", tilePos.y);
                    }
                    cameraMovement.isMoving = false;
                    return;
                }
            }
        }
    }

    // ── Building panel — logistics only ──────────────────────────────────────
    // Military actions are handled above; this path only assigns transport receivers.
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

// Submits a recruitment command to the simulation.
void BasicMapViewSystem::SubmitRecruitCommand(Building* building, MilitaryUnitType unitType)
{
    if (scene == nullptr || scene->game == nullptr || building == nullptr)
        return;

    scene->SubmitLocalCommand(GameCommand::RecruitUnit(scene->game->GetLocalPlayerId(), building->positionId, unitType));
}
