// Roster/deploy panel (TD etap-8.1) + its GuiSystem.

#include "GuiInternal.h"

#include "ui/ControlIcons.h"
#include "scenes/Scenes.h"
#include "economy/Player.h"
#include "warfare/UnitDefinition.h"
#include "core/GameCommand.h"
#include "simulation/MilitaryRoadNetwork.h"
#include "simulation/PathingService.h"

#include <algorithm>
#include <set>

namespace
{
    // Every player the local player could currently deploy against: direct
    // ring neighbors, or transitively reachable through a conquered HQ
    // (PathingService::AreHqsConnected already covers both — see etap-6.3).
    std::vector<int> ReachableEnemyPlayerIds(GameScene* scene, int localPlayerId)
    {
        std::vector<int> result;
        if (scene == nullptr || scene->game == nullptr)
            return result;

        auto& playerHandler = scene->game->GetPlayerHandler();
        auto isEliminated = [&](int playerId)
        {
            auto it = playerHandler.players.find(playerId);
            return it != playerHandler.players.end() && it->second != nullptr && it->second->defeated;
        };

        for (const auto& [id, player] : playerHandler.players)
        {
            if (id == localPlayerId || player == nullptr || player->defeated)
                continue;
            if (PathingService::AreHqsConnected(scene->game->GetMilitaryRoads(), localPlayerId, id, isEliminated))
                result.push_back(id);
        }
        std::sort(result.begin(), result.end());
        return result;
    }
}

void RosterPanelWidget::Update(double dt)
{
    (void)dt;
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* player = GuiLocalPlayer(scene);
    if (player == nullptr)
        return;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y),
                     static_cast<float>(size.x), static_cast<float>(size.y)};
    if (!UiControlIcons::DrawPixelHudFrame(bounds))
    {
        DrawRectangleRounded(bounds, 0.03f, 8, UiTheme::Panel);
        DrawRectangleRoundedLines(bounds, 0.03f, 8, 1.5f, UiTheme::Iron);
    }
    const float chromeInset = UiControlIcons::PixelHudFrameInset(bounds);
    Rectangle title{bounds.x + chromeInset + 2.0f, bounds.y + 4.0f,
                    bounds.width - (chromeInset + 2.0f) * 2.0f, 42.0f};
    UiText::DrawTitleBar(title, "Roster & Deploy", PanelTitleCloseReserve(bounds));
    DrawCloseButton(bounds);

    // Drop anything no longer a valid InRoster unit (deployed some other way,
    // or the roster changed under us) before drawing.
    selectedGroup.erase(std::remove_if(selectedGroup.begin(), selectedGroup.end(), [&](int id)
    {
        const BattleUnit* unit = player->roster.FindUnit(id);
        return unit == nullptr || unit->state != BattleUnitState::InRoster;
    }), selectedGroup.end());

    float margin = 24.0f;
    float columnGap = 24.0f;
    float columnW = (bounds.width - margin * 2.0f - columnGap) / 2.0f;
    float top = bounds.y + 60.0f;
    float targetAreaH = 110.0f;
    float listBottom = bounds.y + bounds.height - targetAreaH;

    Rectangle availableArea{bounds.x + margin, top, columnW, listBottom - top};
    Rectangle groupArea{bounds.x + margin + columnW + columnGap, top, columnW, listBottom - top};

    UiText::Draw("Available", static_cast<int>(availableArea.x), static_cast<int>(availableArea.y), 20, UiTheme::AmberBright);
    UiText::Draw("Attack group (spearhead first)", static_cast<int>(groupArea.x), static_cast<int>(groupArea.y), 20, UiTheme::AmberBright);

    std::set<int> inGroup(selectedGroup.begin(), selectedGroup.end());
    const int rowH = 44;

    // --- Available units: click a row to append it to the tail of the group ---
    float rowY = availableArea.y + 28.0f;
    for (const auto& [instanceId, unit] : player->roster.units)
    {
        if (unit.state != BattleUnitState::InRoster || inGroup.count(instanceId) > 0)
            continue;
        if (rowY + rowH > availableArea.y + availableArea.height)
            break;

        const UnitDefinition* def = FindUnitDefinition(unit.unitDefId);
        std::string label = (def != nullptr ? def->displayName : unit.unitDefId) + " - HP " +
            std::to_string(static_cast<int>(std::round(unit.GetEffectiveMaxHp(*player))));

        Rectangle row{availableArea.x, rowY, availableArea.width, static_cast<float>(rowH - 6)};
        bool hovered = CheckCollisionPointRec(GetMousePosition(), row);
        UiControlIcons::DrawPixelHudWidgetFrame(row, hovered);
        UiText::DrawFit(label, Rectangle{row.x + 10.0f, row.y + 4.0f, row.width - 60.0f, row.height - 8.0f}, 17, UiTheme::Parchment);
        UiText::DrawFit("+ Add", Rectangle{row.x + row.width - 56.0f, row.y + 4.0f, 48.0f, row.height - 8.0f}, 15, UiTheme::SageBright);

        if (hovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            selectedGroup.push_back(instanceId);

        rowY += rowH;
    }
    if (rowY == availableArea.y + 28.0f)
    {
        UiText::DrawFit("No units ready to deploy — recruit some at a Barracks",
                        Rectangle{availableArea.x, rowY, availableArea.width, 20.0f}, 15, UiTheme::ParchmentDim);
    }

    // --- Attack group: ordered, with reorder (up) and remove (x) controls ---
    rowY = groupArea.y + 28.0f;
    for (size_t i = 0; i < selectedGroup.size(); i++)
    {
        if (rowY + rowH > groupArea.y + groupArea.height)
            break;

        int instanceId = selectedGroup[i];
        const BattleUnit* unit = player->roster.FindUnit(instanceId);
        const UnitDefinition* def = unit != nullptr ? FindUnitDefinition(unit->unitDefId) : nullptr;
        std::string label = std::to_string(i + 1) + ". " + (def != nullptr ? def->displayName :
            (unit != nullptr ? unit->unitDefId : "?"));
        if (i == 0)
            label += " (spearhead)";

        Rectangle row{groupArea.x, rowY, groupArea.width, static_cast<float>(rowH - 6)};
        UiControlIcons::DrawPixelHudWidgetFrame(row);
        UiText::DrawFit(label, Rectangle{row.x + 10.0f, row.y + 4.0f, row.width - 96.0f, row.height - 8.0f}, 16, UiTheme::Parchment);

        Rectangle upRect{row.x + row.width - 84.0f, row.y + 4.0f, 34.0f, row.height - 8.0f};
        Rectangle removeRect{row.x + row.width - 44.0f, row.y + 4.0f, 34.0f, row.height - 8.0f};
        bool upHovered = i > 0 && CheckCollisionPointRec(GetMousePosition(), upRect);
        bool removeHovered = CheckCollisionPointRec(GetMousePosition(), removeRect);

        if (i > 0)
        {
            UiControlIcons::DrawPixelHudWidgetFrame(upRect, upHovered);
            UiText::DrawFit("Up", Rectangle{upRect.x + 2.0f, upRect.y + 3.0f, upRect.width - 4.0f, upRect.height - 6.0f}, 13, UiTheme::Parchment);
            if (upHovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                std::swap(selectedGroup[i], selectedGroup[i - 1]);
        }

        UiControlIcons::DrawPixelHudWidgetFrame(removeRect, removeHovered,
                                                Color{180, 100, 96, 255});
        UiText::DrawFit("X", Rectangle{removeRect.x + 2.0f, removeRect.y + 3.0f, removeRect.width - 4.0f, removeRect.height - 6.0f}, 14, UiTheme::Parchment);
        if (removeHovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            selectedGroup.erase(selectedGroup.begin() + static_cast<long>(i));
            break; // indices shifted; finish drawing the rest next frame
        }

        rowY += rowH;
    }
    if (selectedGroup.empty())
    {
        UiText::DrawFit("Click units on the left to add them to the attack group",
                        Rectangle{groupArea.x, rowY, groupArea.width, 20.0f}, 15, UiTheme::ParchmentDim);
    }

    // --- Target picker + Deploy ---
    float targetY = listBottom + 12.0f;
    UiText::Draw("Target", static_cast<int>(bounds.x + margin), static_cast<int>(targetY), 18, UiTheme::AmberBright);
    targetY += 24.0f;

    std::vector<int> reachable = ReachableEnemyPlayerIds(scene, player->id);
    if (selectedTargetPlayerId != -1 &&
        std::find(reachable.begin(), reachable.end(), selectedTargetPlayerId) == reachable.end())
        selectedTargetPlayerId = -1; // previous target no longer reachable (e.g. it was just eliminated)
    if (selectedTargetPlayerId == -1 && reachable.size() == 1)
        selectedTargetPlayerId = reachable.front(); // "w 1vs1 auto" generalized to "only one choice"

    float targetX = bounds.x + margin;
    if (reachable.empty())
    {
        UiText::DrawFit("No reachable enemy — build/hold the military road ring",
                        Rectangle{targetX, targetY, bounds.width - margin * 2.0f, 20.0f}, 15, Color{238, 184, 84, 255});
    }
    for (int targetId : reachable)
    {
        auto& playerHandler = scene->game->GetPlayerHandler();
        auto it = playerHandler.players.find(targetId);
        std::string name = (it != playerHandler.players.end() && it->second != nullptr) ? it->second->name : "?";
        Rectangle chip{targetX, targetY, 150.0f, 30.0f};
        bool selected = targetId == selectedTargetPlayerId;
        bool hovered = CheckCollisionPointRec(GetMousePosition(), chip);
        UiControlIcons::DrawPixelHudWidgetFrame(chip, hovered,
                                                selected ? Color{176, 210, 150, 255} : WHITE);
        UiText::DrawFit(name, Rectangle{chip.x + 8.0f, chip.y + 4.0f, chip.width - 16.0f, chip.height - 8.0f}, 16, UiTheme::Parchment);
        if (hovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            selectedTargetPlayerId = targetId;
        targetX += chip.width + 10.0f;
    }

    Rectangle deployRect{bounds.x + bounds.width - margin - 160.0f, bounds.y + bounds.height - 56.0f, 160.0f, 40.0f};
    bool canDeploy = !selectedGroup.empty() && selectedTargetPlayerId != -1;
    bool deployHovered = canDeploy && CheckCollisionPointRec(GetMousePosition(), deployRect);
    UiControlIcons::DrawPixelHudWidgetFrame(
        deployRect, deployHovered,
        canDeploy ? Color{176, 210, 150, 255} : Color{135, 135, 135, 190});
    UiText::DrawFit("Deploy", Rectangle{deployRect.x + 10.0f, deployRect.y + 6.0f, deployRect.width - 20.0f, deployRect.height - 12.0f},
                    20, !canDeploy ? UiTheme::ParchmentDim : UiTheme::Parchment);

    if (deployHovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        scene->SubmitLocalCommand(GameCommand::DeployUnits(player->id, selectedTargetPlayerId, selectedGroup));
        selectedGroup.clear();
    }
}

// ─── RosterGuiSystem ─────────────────────────────────────────────────────────

RosterGuiSystem::RosterGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    // A4 (docs/work_plan_2026-07-13.md): shadows GuiSystem::scene (Scene*).
    scene = dynamic_cast<GameScene*>(owner->scene);

    WireCommonSystemActions(*this, cameraMovement);

    rosterPanel.scene = scene;
    rosterPanel.ChangePositionAnchor({0.06f, 0.15f});
    rosterPanel.ChangeSizeAnchor({0.88f, 0.82f});
    rosterPanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    SetupStrategicHud(strategicHudWidget, scene);
}

void RosterGuiSystem::UpdateUiWidgets(Vec2i size)
{
    rosterPanel.UpdateSize(size);
    strategicHudWidget.UpdateSize(size);
}

void RosterGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    owner->AddUiWidget(&rosterPanel);
    owner->AddUiWidget(&strategicHudWidget);
}

void RosterGuiSystem::EscPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}

void RosterGuiSystem::BuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("build");
}

void RosterGuiSystem::RoadBuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("road_build");
}

void RosterGuiSystem::DestroyPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("destroy");
}

void RosterGuiSystem::StockpilePressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stockpile");
}

void RosterGuiSystem::StatsPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stats");
}

void RosterGuiSystem::FocusPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("focus");
}

void RosterGuiSystem::TechPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("tech");
}

void RosterGuiSystem::RosterPressed()
{
    EscPressed();
}

void RosterGuiSystem::LmbPressed()
{
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    Vector2 mouse = GetMousePosition();
    Rectangle panelBounds{
        static_cast<float>(rosterPanel.pos.x),
        static_cast<float>(rosterPanel.pos.y),
        static_cast<float>(rosterPanel.size.x),
        static_cast<float>(rosterPanel.size.y)};
    if (CheckCollisionPointRec(mouse, PanelCloseButtonRect(panelBounds)))
        EscPressed();

    // Row/button clicks inside the panel are handled directly in
    // RosterPanelWidget::Update via IsMouseButtonPressed — no separate
    // HandleClick dispatch needed (same approach as GuiPanel's recruitment
    // branch), so there is nothing else to do here.
}

void RosterGuiSystem::LmbReleased()
{
}

void RosterGuiSystem::RmbPressed()
{
    Vector2 mouse = GetMousePosition();
    Rectangle panelBounds{
        static_cast<float>(rosterPanel.pos.x),
        static_cast<float>(rosterPanel.pos.y),
        static_cast<float>(rosterPanel.size.x),
        static_cast<float>(rosterPanel.size.y)};
    if (CheckCollisionPointRec(mouse, panelBounds))
    {
        cameraMovement.isMoving = false;
        return;
    }
    cameraMovement.isMoving = true;
}

void RosterGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

void RosterGuiSystem::Scroll()
{
    ZoomCamera(scene);
}
