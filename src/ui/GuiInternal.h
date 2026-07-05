#ifndef GUI_INTERNAL_H
#define GUI_INTERNAL_H

// Helpers shared by the GUI translation units (GuiController, GuiMapWidgets,
// GuiHudPanels, GuiResearchTree, GuiBuildModes). Implemented in GuiCommon.cpp.
// This header is internal to src/ — nothing outside the GUI code includes it.

#include "ui/GuiController.h"

#include <string>

class GameScene;
class Player;

// ─── Player and scene lookups ────────────────────────────────────────────────

// Returns the local player controlled by this client, or nullptr.
Player* GuiLocalPlayer(GameScene* scene);

// Returns true once the local player has a completed University, gating the
// Technology panel.
bool HasUniversity(GameScene* scene);

// Returns current map size in tiles for camera clamping helpers.
Vec2i GetMapSize(GameScene* scene);

// ─── Camera and coordinate helpers ───────────────────────────────────────────

// Reserves screen space under the strategic HUD and clamps the camera.
void ApplyStrategicHudCameraPadding(GameScene* scene);

// Applies the standard full-width top-bar layout to the strategic HUD.
void UpdateStrategicHudLayout(StrategicResourceHudWidget& hud, Vec2i windowSize);

// Middle-mouse camera drag shared by all interaction systems.
void MoveCamera(GameScene* scene, CameraMovement& cameraMovement);

// Mouse-wheel zoom around the cursor.
void ZoomCamera(GameScene* scene);

// Converts a screen point to a map tile, or {-1,-1} when outside the map.
Vec2i ScreenToTile(GameScene* scene, Vector2 screen);

// ─── Shared panel chrome ─────────────────────────────────────────────────────

// Returns the close ("X") button area of a full-screen panel.
Rectangle PanelCloseButtonRect(Rectangle panel);

// Draws the close button for a full-screen panel.
void DrawCloseButton(Rectangle panel);

// Formats a compact fixed-point HUD value with one decimal place.
std::string FormatOneDecimal(double value);

// ─── Strategic HUD buttons ───────────────────────────────────────────────────

// Clickable mode-button areas in the strategic HUD (right-aligned row).
Rectangle StatsHudButtonRect(const StrategicResourceHudWidget& hud);
Rectangle FocusHudButtonRect(const StrategicResourceHudWidget& hud);
Rectangle TechHudButtonRect(const StrategicResourceHudWidget& hud);
Rectangle DestroyHudButtonRect(const StrategicResourceHudWidget& hud);
Rectangle RoadHudButtonRect(const StrategicResourceHudWidget& hud);
Rectangle BuildHudButtonRect(const StrategicResourceHudWidget& hud);

bool IsStatsHudButtonHovered(const StrategicResourceHudWidget& hud);
bool IsFocusHudButtonHovered(const StrategicResourceHudWidget& hud);
// Hover counts only when the tech tree is unlocked (completed University).
bool IsTechHudButtonHovered(const StrategicResourceHudWidget& hud);
bool IsDestroyHudButtonHovered(const StrategicResourceHudWidget& hud);
bool IsRoadHudButtonHovered(const StrategicResourceHudWidget& hud);
bool IsBuildHudButtonHovered(const StrategicResourceHudWidget& hud);

// True when the cursor is over any strategic HUD mode button.
bool IsAnyHudButtonHovered(const StrategicResourceHudWidget& hud);

// Routes an LMB press on a strategic HUD mode button to the system's matching
// keyboard action ("q"/"r"/"d"/"s"/"f"/"t"). Returns true when the click was
// consumed by a HUD button.
bool DispatchHudButtonClick(GuiSystem& system, const StrategicResourceHudWidget& hud);

// ─── System construction helpers ─────────────────────────────────────────────

// Applies the standard scene/anchor setup for a system-owned strategic HUD.
void SetupStrategicHud(StrategicResourceHudWidget& hud, GameScene* scene);

// Switches to the default map view and opens the headquarters panel there.
void SwitchToMapViewAndOpenHeadquarters(GuiController* owner);

// Wires the input actions shared by every interaction system to the system's
// handler methods. Systems with extra actions add them after this call.
template <typename SystemT>
void WireCommonSystemActions(SystemT& system, CameraMovement& cameraMovement)
{
    system.actionMap["esc"]  = [&system] { system.EscPressed(); };
    system.actionMap["q"]    = [&system] { system.BuildPressed(); };
    system.actionMap["r"]    = [&system] { system.RoadBuildPressed(); };
    system.actionMap["d"]    = [&system] { system.DestroyPressed(); };
    system.actionMap["e"]    = [&system] { system.HeadquartersPressed(); };
    system.actionMap["s"]    = [&system] { system.StatsPressed(); };
    system.actionMap["f"]    = [&system] { system.FocusPressed(); };
    system.actionMap["t"]    = [&system] { system.TechPressed(); };
    system.actionMap["lmbp"] = [&system] { system.LmbPressed(); };
    system.actionMap["lmbr"] = [&system] { system.LmbReleased(); };
    system.actionMap["rmbp"] = [&system] { system.RmbPressed(); };
    system.actionMap["rmbr"] = [&system] { system.RmbReleased(); };
    system.actionMap["mmbp"] = [&cameraMovement] { cameraMovement.isMoving = true; };
    system.actionMap["mmbr"] = [&cameraMovement] { cameraMovement.isMoving = false; };
    system.actionMap["scroll"] = [&system] { system.Scroll(); };
}

// ─── Frontier and military helpers ───────────────────────────────────────────

// Returns true if a tile is on the frontier: on the edge between owned and enemy territory.
bool IsFrontierTile(Vec2i tile, const class TileMap& map, const class Player& owner);

// Walks RMB drag segment and collects all frontier tiles touched.
std::vector<Vec2i> CollectFrontierSegment(Vec2i start, Vec2i end, const class TileMap& map, const class Player& owner);

#endif
