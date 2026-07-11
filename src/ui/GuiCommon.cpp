// Shared helpers for the GUI translation units. See GuiInternal.h.

#include "GuiInternal.h"

#include "scenes/Scenes.h"
#include "economy/Player.h"
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

void MoveCamera(GameScene* scene, CameraMovement& cameraMovement)
{
    if (!cameraMovement.isMoving)
        return;
    if (!InputManager::IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
    {
        cameraMovement.isMoving = false;
        return;
    }

    Vector2 delta = GetMouseDelta();
    delta.x *= -1;
    delta.x /= scene->render.camera.zoom;
    delta.y /= scene->render.camera.zoom;
    scene->render.camera.target = Vector2Add(scene->render.camera.target, delta);
    ApplyStrategicHudCameraPadding(scene);
    scene->render.ClampCameraToMap(GetMapSize(scene));
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
    DrawRectangleRounded(close, 0.18f, 8, hover ? Color{110, 58, 64, 245} : Color{54, 42, 48, 230});
    DrawRectangleRoundedLines(close, 0.18f, 8, 1.0f, hover ? Color{244, 132, 142, 255} : Color{156, 104, 114, 235});
    UiText::DrawFit("X", Rectangle{close.x + 6.0f, close.y + 4.0f, close.width - 12.0f, close.height - 8.0f}, 20, RAYWHITE);
}

std::string FormatOneDecimal(double value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    return stream.str();
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

bool IsStatsHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return CheckCollisionPointRec(GetMousePosition(), StatsHudButtonRect(hud));
}

bool IsFocusHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return CheckCollisionPointRec(GetMousePosition(), FocusHudButtonRect(hud));
}

bool IsTechHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return CheckCollisionPointRec(GetMousePosition(), TechHudButtonRect(hud)) && HasUniversity(hud.scene);
}

bool IsDestroyHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return CheckCollisionPointRec(GetMousePosition(), DestroyHudButtonRect(hud));
}

bool IsRoadHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return CheckCollisionPointRec(GetMousePosition(), RoadHudButtonRect(hud));
}

bool IsBuildHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return CheckCollisionPointRec(GetMousePosition(), BuildHudButtonRect(hud));
}

bool IsAnyHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return IsBuildHudButtonHovered(hud) || IsRoadHudButtonHovered(hud) ||
           IsDestroyHudButtonHovered(hud) || IsStatsHudButtonHovered(hud) ||
           IsFocusHudButtonHovered(hud) || IsTechHudButtonHovered(hud);
}

bool DispatchHudButtonClick(GuiSystem& system, const StrategicResourceHudWidget& hud)
{
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

void SwitchToMapViewAndOpenHeadquarters(GuiController* owner)
{
    owner->ChangeSystem("default");
    auto mapSystem = std::dynamic_pointer_cast<BasicMapViewSystem>(owner->systems["default"]);
    if (mapSystem != nullptr)
        mapSystem->OpenHeadquartersPanel();
}

