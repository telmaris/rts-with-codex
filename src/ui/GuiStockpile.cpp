// Player-wide stockpile panel (E) + its GuiSystem.
//
// The counterpart to a building's own panel: that one shows only what the
// selected warehouse physically holds, this one shows the totals across the
// whole warehouse network, with a per-warehouse breakdown on hover (user
// request, 2026-07-25). Both read StockpileIndex, so they can never disagree.

#include "GuiInternal.h"

#include "scenes/Scenes.h"
#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "raygui.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float PanelMargin = 24.0f;
    constexpr float HeaderHeight = 54.0f;
    constexpr float SummaryHeight = 96.0f;
    constexpr int PreferredColumns = 5;
    constexpr float CardGap = 12.0f;
    // Leave permanent space for raygui's vertical scrollbar so a long list
    // never causes a surprise horizontal scrollbar.
    constexpr float ScrollbarReserve = 24.0f;

    // One card per resource, sized so the amount stays readable at a glance
    // and the capacity bar has room underneath it.
    void DrawStockpileCard(Rectangle cell, ResourceType type, const StockpileTotals& totals, bool hovered)
    {
        DrawRectangleRounded(cell, 0.10f, 8, hovered ? Color{62, 46, 32, 242} : Color{40, 29, 21, 232});
        DrawRectangleRoundedLines(cell, 0.10f, 8, 1.0f, hovered ? UiTheme::Gold : Color{112, 88, 58, 255});

        float iconSize = std::clamp(cell.height * 0.48f, 48.0f, 70.0f);
        float iconY = cell.y + 14.0f;
        GuiPanel::DrawResourceIcon(type, Rectangle{cell.x + 14.0f, iconY, iconSize, iconSize});

        float textX = cell.x + iconSize + 26.0f;
        float textWidth = cell.width - iconSize - 40.0f;
        UiText::DrawFit(ResourceDisplayName(type),
                        Rectangle{textX, cell.y + 15.0f, textWidth, 23.0f}, 18, UiTheme::ParchmentDim);
        UiText::DrawFit(std::to_string(totals.amount),
                        Rectangle{textX, cell.y + 42.0f, textWidth, 32.0f}, 28, UiTheme::Parchment);

        // Fill bar: how close the network is to running out of room for this
        // type — the thing that silently stalls a production chain.
        Rectangle track{cell.x + 14.0f, cell.y + cell.height - 17.0f, cell.width - 28.0f, 5.0f};
        DrawRectangleRounded(track, 0.6f, 6, Color{24, 17, 12, 200});
        if (totals.capacity > 0 && totals.amount > 0)
        {
            float fill = std::clamp(static_cast<float>(totals.amount) / totals.capacity, 0.0f, 1.0f);
            Rectangle filled{track.x, track.y, track.width * fill, track.height};
            DrawRectangleRounded(filled, 0.6f, 6, fill > 0.9f ? UiTheme::AmberBright : Color{150, 180, 98, 230});
        }

        // Split across more than one warehouse — worth surfacing on the card
        // itself so the hover tooltip is a detail view, not a discovery tool.
        if (totals.holdings.size() > 1)
        {
            std::string badge = std::to_string(totals.holdings.size()) + "x";
            int width = UiText::Measure(badge, 12);
            Rectangle chip{cell.x + cell.width - width - 16.0f, cell.y + 12.0f, static_cast<float>(width + 8), 16.0f};
            DrawRectangleRounded(chip, 0.3f, 6, Color{24, 17, 12, 230});
            UiText::Draw(badge, chip.x + 4.0f, chip.y + 2.0f, 12, UiTheme::ParchmentDim);
        }
    }
}

// ─── StockpilePanelWidget ────────────────────────────────────────────────────

Rectangle StockpilePanelWidget::GetGridRect() const
{
    return Rectangle{
        pos.x + PanelMargin,
        pos.y + HeaderHeight + SummaryHeight,
        size.x - PanelMargin * 2.0f,
        size.y - HeaderHeight - SummaryHeight - PanelMargin};
}

void StockpilePanelWidget::Update(double dt)
{
    (void)dt;
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* player = GuiLocalPlayer(scene);
    if (player == nullptr)
        return;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y),
                     static_cast<float>(size.x), static_cast<float>(size.y)};
    DrawRectangleRounded(bounds, 0.025f, 8, Color{30, 22, 16, 244});
    DrawRectangleRoundedLines(bounds, 0.025f, 8, 1.0f, UiTheme::Bronze);

    Rectangle title{bounds.x, bounds.y, bounds.width, HeaderHeight};
    DrawRectangleRounded(title, 0.025f, 8, UiTheme::Oak);
    UiText::DrawTitleBar(title, "Stockpile", PanelTitleCloseReserve(bounds));
    DrawCloseButton(bounds);

    auto warehouses = StockpileIndex::Warehouses(*player);
    auto snapshot = StockpileIndex::Snapshot(*player);

    int distinctTypes = 0;
    int totalUnits = 0;
    int totalCapacity = 0;
    for (const auto& [type, totals] : snapshot)
    {
        totalUnits += totals.amount;
        totalCapacity += totals.capacity;
        if (totals.amount > 0)
            distinctTypes++;
    }

    float summaryY = bounds.y + HeaderHeight + 8.0f;
    UiText::DrawFit("Warehouses: " + std::to_string(warehouses.size()) +
                        "   Resource types held: " + std::to_string(distinctTypes) +
                        "   Total units: " + std::to_string(totalUnits) +
                        " / " + std::to_string(totalCapacity),
                    Rectangle{bounds.x + PanelMargin, summaryY, bounds.width - PanelMargin * 2.0f, 24.0f},
                    20, UiTheme::Parchment);
    UiText::DrawFit("Each warehouse keeps its own stock — this is the sum. Hover a resource to see where it sits.",
                    Rectangle{bounds.x + PanelMargin, summaryY + 28.0f, bounds.width - PanelMargin * 2.0f, 20.0f},
                    16, UiTheme::ParchmentDim);

    if (warehouses.empty())
    {
        UiText::DrawFit("No warehouses", Rectangle{bounds.x + PanelMargin, bounds.y + bounds.height * 0.45f,
                                                   bounds.width - PanelMargin * 2.0f, 26.0f},
                        22, UiTheme::ParchmentDim);
        return;
    }

    // Only types the network actually holds — a grid of ~45 empty cells is
    // noise, and the capacity a warehouse "could" hold of everything is not
    // information the player acts on.
    std::vector<std::pair<ResourceType, StockpileTotals>> cards;
    for (const auto& entry : snapshot)
        if (entry.second.amount > 0)
            cards.push_back(entry);

    Rectangle grid = GetGridRect();
    if (cards.empty())
    {
        maxScrollOffset = 0.0f;
        scrollOffset = 0.0f;
        UiText::DrawFit("Every warehouse is empty",
                        Rectangle{grid.x, grid.y + 12.0f, grid.width, 26.0f}, 22, UiTheme::ParchmentDim);
        return;
    }

    const int columns = grid.width >= 720.0f ? PreferredColumns : 4;
    const float contentWidth = std::max(120.0f, grid.width - ScrollbarReserve);
    const float cardWidth = (contentWidth - CardGap * (columns - 1)) / columns;
    const float cardHeight = std::clamp(cardWidth * 0.56f, 112.0f, 156.0f);
    int rows = static_cast<int>((cards.size() + columns - 1) / columns);
    float contentHeight = rows * cardHeight + std::max(0, rows - 1) * CardGap;
    Rectangle content{0.0f, 0.0f, contentWidth, contentHeight};
    Vector2 scroll{0.0f, -scrollOffset};
    Rectangle view{};
    GuiScrollPanel(grid, nullptr, content, &scroll, &view);
    scrollOffset = std::max(0.0f, -scroll.y);
    maxScrollOffset = std::max(0.0f, contentHeight - view.height);

    int hoveredIndex = -1;
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y),
                     static_cast<int>(view.width), static_cast<int>(view.height));
    for (int i = 0; i < static_cast<int>(cards.size()); i++)
    {
        float x = view.x + scroll.x + (i % columns) * (cardWidth + CardGap);
        float y = view.y + scroll.y + (i / columns) * (cardHeight + CardGap);
        if (y > view.y + view.height)
            break;
        if (y + cardHeight < view.y)
            continue;

        Rectangle cell{x, y, cardWidth, cardHeight};
        bool hovered = CheckCollisionPointRec(GetMousePosition(), cell);
        DrawStockpileCard(cell, cards[i].first, cards[i].second, hovered);
        if (hovered)
            hoveredIndex = i;
    }
    EndScissorMode();

    // Drawn last so it is never clipped by the scroll viewport or overdrawn by
    // a neighbouring card.
    if (hoveredIndex >= 0)
        DrawResourceTooltip(cards[hoveredIndex].first,
                            StockpileTooltipLines(player, cards[hoveredIndex].first), 280.0f);
}

// ─── StockpileGuiSystem ──────────────────────────────────────────────────────

StockpileGuiSystem::StockpileGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    // A4 (docs/work_plan_2026-07-13.md): shadows GuiSystem::scene (Scene*).
    scene = dynamic_cast<GameScene*>(owner->scene);

    WireCommonSystemActions(*this, cameraMovement);

    stockpilePanel.scene = scene;
    stockpilePanel.ChangePositionAnchor({0.06f, 0.10f});
    stockpilePanel.ChangeSizeAnchor({0.88f, 0.82f});
    stockpilePanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    SetupStrategicHud(strategicHudWidget, scene);
}

void StockpileGuiSystem::UpdateUiWidgets(Vec2i size)
{
    stockpilePanel.UpdateSize(size);
    strategicHudWidget.UpdateSize(size);
}

void StockpileGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    owner->AddUiWidget(&stockpilePanel);
    owner->AddUiWidget(&strategicHudWidget);
}

void StockpileGuiSystem::EscPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}

void StockpileGuiSystem::BuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("build");
}

void StockpileGuiSystem::RoadBuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("road_build");
}

void StockpileGuiSystem::DestroyPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("destroy");
}

void StockpileGuiSystem::StockpilePressed()
{
    EscPressed();
}

void StockpileGuiSystem::StatsPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stats");
}

void StockpileGuiSystem::FocusPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("focus");
}

void StockpileGuiSystem::TechPressed()
{
    if (!HasUniversity(scene))
        return;
    cameraMovement.isMoving = false;
    owner->ChangeSystem("tech");
}

void StockpileGuiSystem::RosterPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("roster");
}

void StockpileGuiSystem::LmbPressed()
{
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    Rectangle panelBounds{
        static_cast<float>(stockpilePanel.pos.x),
        static_cast<float>(stockpilePanel.pos.y),
        static_cast<float>(stockpilePanel.size.x),
        static_cast<float>(stockpilePanel.size.y)};
    if (CheckCollisionPointRec(GetMousePosition(), PanelCloseButtonRect(panelBounds)))
        EscPressed();
}

void StockpileGuiSystem::LmbReleased()
{
}

void StockpileGuiSystem::RmbPressed()
{
    Rectangle panelBounds{
        static_cast<float>(stockpilePanel.pos.x),
        static_cast<float>(stockpilePanel.pos.y),
        static_cast<float>(stockpilePanel.size.x),
        static_cast<float>(stockpilePanel.size.y)};
    if (CheckCollisionPointRec(GetMousePosition(), panelBounds))
    {
        cameraMovement.isMoving = false;
        return;
    }
    cameraMovement.isMoving = true;
}

void StockpileGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

void StockpileGuiSystem::Scroll()
{
    // GuiScrollPanel handles both wheel input and dragging its real scrollbar
    // while the pointer is over the panel. Elsewhere retain camera zoom.
    Rectangle panelBounds{
        static_cast<float>(stockpilePanel.pos.x),
        static_cast<float>(stockpilePanel.pos.y),
        static_cast<float>(stockpilePanel.size.x),
        static_cast<float>(stockpilePanel.size.y)};
    if (!CheckCollisionPointRec(GetMousePosition(), panelBounds))
    {
        ZoomCamera(scene);
        return;
    }

}
