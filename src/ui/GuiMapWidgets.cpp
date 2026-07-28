// Map-layer widgets: selection and production-warning highlights.
// TD(etap-1): the division/army/battle widgets that used to live here
// (MilitaryOrderWidget, MilitaryDivisionBarWidget, ArmyBarWidget,
// DivisionMapWidget, MoveTargetWidget, ArmyOrderPanelWidget) were removed along
// with the old war system. The Tower Defense rework adds fresh unit/roster
// widgets starting in ETAP 3-4.

#include "GuiInternal.h"

#include "scenes/Scenes.h"
#include "economy/Player.h"
#include "economy/BuildingComponents.h"
#include "warfare/CombatPipeline.h"

#include <cmath>

namespace
{
    // Returns the screen-space rectangle covering a building's footprint.
    Rectangle BuildingScreenRect(GameScene* scene, Building* building)
    {
        Vec2i anchor = scene->game->GetTileMap().GetCoordsFromId(building->positionId);
        Vec2i footprint = building->GetFootprint();
        Vec2f worldTopLeft{
            static_cast<float>(anchor.x * TILE_SIZE),
            static_cast<float>(anchor.y * TILE_SIZE)};
        Vec2f worldBottomRight{
            worldTopLeft.x + footprint.x * TILE_SIZE,
            worldTopLeft.y + footprint.y * TILE_SIZE};
        Vec2f screenTopLeft = scene->render.WorldToScreen(worldTopLeft);
        Vec2f screenBottomRight = scene->render.WorldToScreen(worldBottomRight);
        return Rectangle{
            screenTopLeft.x,
            screenTopLeft.y,
            screenBottomRight.x - screenTopLeft.x,
            screenBottomRight.y - screenTopLeft.y};
    }

    // T7 (docs/post_pivot_audit_2026-07-12.md): draws a semi-transparent range
    // ring for a selected DefenseTower, centered on its footprint. Screen
    // radius derived from two WorldToScreen calls (center, center+radius)
    // rather than assuming a direct zoom multiplier — same approach
    // BuildingScreenRect already uses for width/height.
    void DrawTowerRangeRing(GameScene* scene, Building* building)
    {
        const auto* combat = building->GetComponent<TowerCombatComponent>();
        if (combat == nullptr)
            return;

        Vec2f worldCenter = ComputeBuildingCenter(scene->game->GetTileMap(), *building);
        double rangePixels = combat->GetModifiedRange(*building) * TILE_SIZE;

        Vec2f screenCenter = scene->render.WorldToScreen(worldCenter);
        Vec2f screenEdge = scene->render.WorldToScreen({worldCenter.x + static_cast<float>(rangePixels), worldCenter.y});
        float screenRadius = std::abs(screenEdge.x - screenCenter.x);

        DrawCircleV({screenCenter.x, screenCenter.y}, screenRadius, Color{236, 92, 74, 28});
        DrawCircleLinesV({screenCenter.x, screenCenter.y}, screenRadius, Color{255, 120, 100, 200});
    }

    // A two-pass animated outline remains sharp above fog and lighting without
    // needing a sprite mask or a full-screen outline shader. It is screen-space
    // UI, so it never alters the cached world render targets.
    void DrawPulsingOutline(Rectangle bounds, Color color, float baseThickness)
    {
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 4.2f);
        Color outer = color;
        outer.a = static_cast<unsigned char>(55.0f + pulse * 65.0f);
        Color inner = color;
        inner.a = static_cast<unsigned char>(175.0f + pulse * 65.0f);
        DrawRectangleRoundedLines(bounds, 0.04f, 8, baseThickness + 2.0f, outer);
        DrawRectangleRoundedLines(bounds, 0.04f, 8, baseThickness, inner);
    }
}

// ─── SelectedBuildingWidget ──────────────────────────────────────────────────

// Highlights the selected building and its suppliers.
void SelectedBuildingWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr || building == nullptr)
        return;

    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 3.6f);
    for (const auto& supplier : building->GetSupplierViews())
    {
        if (supplier.building == nullptr)
            continue;

        Rectangle supplierDest = BuildingScreenRect(scene, supplier.building);
        DrawRectangleRounded(supplierDest, 0.04f, 8,
                             Color{73, 146, 236, static_cast<unsigned char>(28.0f + pulse * 24.0f)});
        DrawPulsingOutline(supplierDest, Color{96, 174, 255, 190}, 1.0f);
    }

    Rectangle dest = BuildingScreenRect(scene, building);
    DrawRectangleRounded(dest, 0.04f, 8,
                         Color{88, 196, 124, static_cast<unsigned char>(32.0f + pulse * 32.0f)});
    DrawPulsingOutline(dest, Color{112, 230, 150, 220}, 1.25f);

    DrawTowerRangeRing(scene, building);
}

// ─── ProductionWarningWidget ─────────────────────────────────────────────────

// Highlights production buildings that cannot currently work.
void ProductionWarningWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* localPlayer = GuiLocalPlayer(scene);
    if (localPlayer == nullptr)
        return;

    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 3.2f);
    for (auto* building : localPlayer->GetTrackedBuildings())
    {
        if (building == nullptr || !building->IsProductionStalled())
            continue;

        Rectangle dest = BuildingScreenRect(scene, building);
        DrawRectangleRounded(dest, 0.04f, 8,
                             Color{236, 184, 62, static_cast<unsigned char>(22.0f + pulse * 30.0f)});
        DrawRectangleRoundedLines(dest, 0.04f, 8, 1.0f,
                                  Color{255, 211, 84, static_cast<unsigned char>(135.0f + pulse * 90.0f)});
    }
}
