// Shared helpers for the GUI translation units. See GuiInternal.h.

#include "GuiInternal.h"

#include "scenes/Scenes.h"
#include "core/Log.h"
#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "ui/Renderer.h"
#include "raymath.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

Player* GuiLocalPlayer(GameScene* scene)
{
    if (scene == nullptr || scene->game == nullptr)
        return nullptr;

    auto it = scene->game->GetPlayerHandler().players.find(scene->game->GetLocalPlayerId());
    return it != scene->game->GetPlayerHandler().players.end() ? it->second.get() : nullptr;
}

bool HasUniversity(GameScene* scene)
{
    Player* player = GuiLocalPlayer(scene);
    return player != nullptr && player->HasTrackedBuilding(BuildingType::University, true);
}

Vec2i GetMapSize(GameScene* scene)
{
    return Vec2i{scene->game->GetTileMap().params.sizeX, scene->game->GetTileMap().params.sizeY};
}

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

namespace
{
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
}

// Grants local debug resources when the current world allows debug helpers.
// Moved out of GuiController (user-directed rework, 2026-07-14): the
// controller is pure system-transition plumbing, concrete actions live in
// the systems' actionMaps.
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

namespace
{
    float StrategicHudHeightForWindow(Vec2i windowSize)
    {
        return std::clamp(windowSize.y * 0.066f, 58.0f, 76.0f);
    }

    float StrategicHudTopPaddingForWindow(Vec2i windowSize)
    {
        return StrategicHudHeightForWindow(windowSize) + 6.0f;
    }
}

void UpdateStrategicHudLayout(StrategicResourceHudWidget& hud, Vec2i windowSize)
{
    hud.ChangePosition(0, 0);
    hud.ChangeSize(windowSize.x, static_cast<int>(StrategicHudHeightForWindow(windowSize)));
}

void ApplyStrategicHudCameraPadding(GameScene* scene)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Vec2i windowSize{GetScreenWidth(), GetScreenHeight()};
    scene->render.SetTopScreenPadding(StrategicHudTopPaddingForWindow(windowSize));
    scene->render.ClampCameraToMap(GetMapSize(scene));
}

namespace
{
    // Net screen-space displacement beyond which an RMB press+release counts
    // as a pan drag rather than a click (A3, docs/work_plan_2026-07-13.md).
    constexpr float kRmbDragThresholdPixels = 4.0f;
}

void MoveCamera(GameScene* scene, CameraMovement& cameraMovement)
{
    if (!cameraMovement.isMoving)
        return;
    if (!InputManager::IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && !InputManager::IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    {
        cameraMovement.isMoving = false;
        return;
    }

    // Only set (via BeginCameraDrag) for systems where RMB also has a
    // competing click action — MMB-only drags never populate this, so this
    // is a no-op for them.
    if (cameraMovement.rmbPressScreenPos.x >= 0)
    {
        Vector2 mouse = GetMousePosition();
        float dx = mouse.x - static_cast<float>(cameraMovement.rmbPressScreenPos.x);
        float dy = mouse.y - static_cast<float>(cameraMovement.rmbPressScreenPos.y);
        if (dx * dx + dy * dy > kRmbDragThresholdPixels * kRmbDragThresholdPixels)
            cameraMovement.rmbDragged = true;
    }

    Vector2 delta = GetMouseDelta();
    delta.x *= -1;
    delta.x /= scene->render.camera.zoom;
    delta.y /= scene->render.camera.zoom;
    scene->render.camera.target = Vector2Add(scene->render.camera.target, delta);
    ApplyStrategicHudCameraPadding(scene);
    scene->render.ClampCameraToMap(GetMapSize(scene));
}

void BeginCameraDrag(CameraMovement& cameraMovement)
{
    cameraMovement.isMoving = true;
    Vector2 mouse = GetMousePosition();
    cameraMovement.rmbPressScreenPos = {static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
    cameraMovement.rmbDragged = false;
}

bool EndCameraDragWasClick(CameraMovement& cameraMovement)
{
    bool wasClick = cameraMovement.rmbPressScreenPos.x >= 0 && !cameraMovement.rmbDragged;
    cameraMovement.isMoving = false;
    cameraMovement.rmbPressScreenPos = {-1, -1};
    cameraMovement.rmbDragged = false;
    return wasClick;
}

void ZoomCamera(GameScene* scene)
{
    float wheel = InputManager::GetMouseWheelMove();
    if (wheel == 0.0f)
        return;

    ApplyStrategicHudCameraPadding(scene);
    scene->render.ZoomAtScreenPoint(GetMousePosition(), wheel, GetMapSize(scene));
}

Vec2i ScreenToTile(GameScene* scene, Vector2 screen)
{
    Vec2f world = scene->render.ScreenToWorld(screen);
    if (world.x < 0.0f || world.y < 0.0f)
        return {-1, -1};

    Vec2i tilePos{
        static_cast<int>(world.x / TILE_SIZE),
        static_cast<int>(world.y / TILE_SIZE)};
    if (!scene->game->GetTileMap().IsInside(tilePos))
        return {-1, -1};

    return tilePos;
}

Rectangle PanelCloseButtonRect(Rectangle panel)
{
    return Rectangle{panel.x + panel.width - 44.0f, panel.y + 10.0f, 30.0f, 30.0f};
}

void DrawCloseButton(Rectangle panel)
{
    Rectangle close = PanelCloseButtonRect(panel);
    bool hover = CheckCollisionPointRec(GetMousePosition(), close);
    DrawRectangleRounded(close, 0.18f, 8, hover ? Color{132, 58, 42, 245} : Color{46, 30, 24, 230});
    DrawRectangleRoundedLines(close, 0.18f, 8, 1.0f, hover ? Color{214, 128, 92, 255} : Color{118, 92, 70, 235});
    UiText::DrawFit("X", Rectangle{close.x + 6.0f, close.y + 4.0f, close.width - 12.0f, close.height - 8.0f}, 20, UiTheme::Parchment);
}

float PanelTitleCloseReserve(Rectangle panel)
{
    Rectangle close = PanelCloseButtonRect(panel);
    return (panel.x + panel.width) - close.x + 10.0f;
}

std::string FormatOneDecimal(double value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    return stream.str();
}

std::vector<std::string> StockpileTooltipLines(Player* player, ResourceType type)
{
    if (player == nullptr)
        return {};

    auto holdings = StockpileIndex::GetHoldings(*player, type);
    int total = 0;
    for (const auto& holding : holdings)
        total += holding.amount;

    std::vector<std::string> lines{"Total: " + std::to_string(total)};
    if (holdings.empty())
    {
        lines.push_back("Not stored anywhere");
        return lines;
    }

    lines.push_back("Stored in:");
    for (const auto& holding : holdings)
    {
        const char* label = holding.building != nullptr &&
                            holding.building->buildingType == BuildingType::Headquarters
            ? "HQ" : "Storage";
        lines.push_back("  " + std::string(label) + " #" + std::to_string(holding.buildingId) +
                        ": " + std::to_string(holding.amount) + " / " + std::to_string(holding.capacity));
    }
    return lines;
}

void DrawResourceTooltip(ResourceType type, const std::vector<std::string>& lines, float preferredWidth)
{
    Tooltip::Draw(ResourceDisplayName(type), lines, preferredWidth,
                  [type](Rectangle icon) { GuiPanel::DrawResourceIcon(type, icon); });
}

Rectangle StatsHudButtonRect(const StrategicResourceHudWidget& hud)
{
    float height = static_cast<float>(hud.size.y);
    float buttonH = std::max(40.0f, height - 14.0f);
    float buttonW = 132.0f;
    return Rectangle{
        static_cast<float>(hud.pos.x + hud.size.x) - buttonW - 18.0f,
        static_cast<float>(hud.pos.y) + (height - buttonH) * 0.5f,
        buttonW,
        buttonH};
}

Rectangle FocusHudButtonRect(const StrategicResourceHudWidget& hud)
{
    Rectangle stats = StatsHudButtonRect(hud);
    return Rectangle{stats.x - 166.0f - 10.0f, stats.y, 166.0f, stats.height};
}

Rectangle TechHudButtonRect(const StrategicResourceHudWidget& hud)
{
    Rectangle focus = FocusHudButtonRect(hud);
    return Rectangle{focus.x - 130.0f - 10.0f, focus.y, 130.0f, focus.height};
}

Rectangle DestroyHudButtonRect(const StrategicResourceHudWidget& hud)
{
    Rectangle tech = TechHudButtonRect(hud);
    return Rectangle{tech.x - 110.0f - 10.0f, tech.y, 110.0f, tech.height};
}

Rectangle RoadHudButtonRect(const StrategicResourceHudWidget& hud)
{
    Rectangle destroy = DestroyHudButtonRect(hud);
    return Rectangle{destroy.x - 92.0f - 10.0f, destroy.y, 92.0f, destroy.height};
}

Rectangle BuildHudButtonRect(const StrategicResourceHudWidget& hud)
{
    Rectangle road = RoadHudButtonRect(hud);
    return Rectangle{road.x - 92.0f - 10.0f, road.y, 92.0f, road.height};
}

Rectangle RosterHudButtonRect(const StrategicResourceHudWidget& hud)
{
    Rectangle build = BuildHudButtonRect(hud);
    return Rectangle{build.x - 108.0f - 10.0f, build.y, 108.0f, build.height};
}

Rectangle LogisticsHudButtonRect(const StrategicResourceHudWidget& hud)
{
    Rectangle roster = RosterHudButtonRect(hud);
    return Rectangle{roster.x - 42.0f - 8.0f, roster.y, 42.0f, roster.height};
}

namespace
{
    bool TutorialDestroyLocked(const StrategicResourceHudWidget& hud)
    {
        return hud.scene != nullptr && hud.scene->AreTutorialDestroyLocked();
    }

    bool TutorialDecisionsLocked(const StrategicResourceHudWidget& hud)
    {
        return hud.scene != nullptr && hud.scene->AreTutorialDecisionsLocked();
    }

    bool TutorialStatisticsLocked(const StrategicResourceHudWidget& hud)
    {
        return hud.scene != nullptr && hud.scene->AreTutorialStatisticsLocked();
    }
}

bool IsStatsHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return !hud.tutorialDisableStatistics && !TutorialStatisticsLocked(hud) &&
           CheckCollisionPointRec(GetMousePosition(), StatsHudButtonRect(hud));
}

bool IsFocusHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return !hud.tutorialDisableDecisions && !TutorialDecisionsLocked(hud) &&
           CheckCollisionPointRec(GetMousePosition(), FocusHudButtonRect(hud));
}

bool IsTechHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return CheckCollisionPointRec(GetMousePosition(), TechHudButtonRect(hud)) && HasUniversity(hud.scene);
}

bool IsDestroyHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return !hud.tutorialDisableDestroy && !TutorialDestroyLocked(hud) &&
           CheckCollisionPointRec(GetMousePosition(), DestroyHudButtonRect(hud));
}

bool IsRoadHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return CheckCollisionPointRec(GetMousePosition(), RoadHudButtonRect(hud));
}

bool IsBuildHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return CheckCollisionPointRec(GetMousePosition(), BuildHudButtonRect(hud));
}

bool IsRosterHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return CheckCollisionPointRec(GetMousePosition(), RosterHudButtonRect(hud));
}

bool IsLogisticsHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return CheckCollisionPointRec(GetMousePosition(), LogisticsHudButtonRect(hud));
}

bool IsAnyHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    const Vector2 mouse = GetMousePosition();
    const bool disabledButtonHovered =
        ((hud.tutorialDisableDestroy || TutorialDestroyLocked(hud)) && CheckCollisionPointRec(mouse, DestroyHudButtonRect(hud))) ||
        ((hud.tutorialDisableDecisions || TutorialDecisionsLocked(hud)) && CheckCollisionPointRec(mouse, FocusHudButtonRect(hud))) ||
        ((hud.tutorialDisableStatistics || TutorialStatisticsLocked(hud)) && CheckCollisionPointRec(mouse, StatsHudButtonRect(hud)));
    return disabledButtonHovered || IsBuildHudButtonHovered(hud) || IsRoadHudButtonHovered(hud) ||
           IsDestroyHudButtonHovered(hud) || IsStatsHudButtonHovered(hud) ||
           IsFocusHudButtonHovered(hud) || IsTechHudButtonHovered(hud) ||
           IsRosterHudButtonHovered(hud) || IsLogisticsHudButtonHovered(hud);
}

bool DispatchHudButtonClick(GuiSystem& system, const StrategicResourceHudWidget& hud)
{
    // Disabled tutorial controls still consume the click so it cannot fall
    // through to map selection or camera interaction underneath the HUD.
    const Vector2 mouse = GetMousePosition();
    if (((hud.tutorialDisableDestroy || TutorialDestroyLocked(hud)) && CheckCollisionPointRec(mouse, DestroyHudButtonRect(hud))) ||
        ((hud.tutorialDisableDecisions || TutorialDecisionsLocked(hud)) && CheckCollisionPointRec(mouse, FocusHudButtonRect(hud))) ||
        ((hud.tutorialDisableStatistics || TutorialStatisticsLocked(hud)) && CheckCollisionPointRec(mouse, StatsHudButtonRect(hud))))
        return true;

    if (IsLogisticsHudButtonHovered(hud))
    {
        SetLogisticsOverlayPreferenceEnabled(!IsLogisticsOverlayPreferenceEnabled());
        return true;
    }

    const char* action = nullptr;
    if (IsBuildHudButtonHovered(hud))
        action = "q";
    else if (IsRoadHudButtonHovered(hud))
        action = "r";
    else if (IsDestroyHudButtonHovered(hud))
        action = "d";
    else if (IsStatsHudButtonHovered(hud))
        action = "s";
    else if (IsFocusHudButtonHovered(hud))
        action = "f";
    else if (IsTechHudButtonHovered(hud))
        action = "t";
    else if (IsRosterHudButtonHovered(hud))
        action = "u";

    if (action == nullptr)
        return false;

    auto it = system.actionMap.find(action);
    if (it != system.actionMap.end())
        it->second();
    return true;
}

void SetupStrategicHud(StrategicResourceHudWidget& hud, GameScene* scene)
{
    hud.scene = scene;
    hud.ChangePositionAnchor({0.012f, 0.012f});
    hud.ChangeSizeAnchor({0.42f, 0.055f});
    hud.UpdateSize({GetScreenWidth(), GetScreenHeight()});
}
