// Map-layer military widgets: selection highlights, division counters, order
// arrows, field battles and the division/army selection bars.

#include "GuiInternal.h"

#include "../inc/Scenes.h"
#include "../inc/Player.h"
#include "../inc/DivisionSector.h"
#include "../inc/ArmyOrder.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numeric>
#include <set>

namespace
{
    // Returns the screen-space rectangle covering a building's footprint.
    Rectangle BuildingScreenRect(GameScene* scene, Building* building)
    {
        Vec2i anchor = scene->game->tilemap.GetCoordsFromId(building->positionId);
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

    // Returns the screen-space center of a building.
    Vector2 BuildingScreenCenter(GameScene* scene, Building* building)
    {
        Rectangle rect = BuildingScreenRect(scene, building);
        return Vector2{rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f};
    }

    // Returns the arrow/badge color for one military order type.
    Color MilitaryOrderColor(MilitaryOrderType order)
    {
        switch (order)
        {
            case MilitaryOrderType::Attack: return Color{238, 84, 84, 218};
            case MilitaryOrderType::Support: return Color{84, 166, 238, 210};
            case MilitaryOrderType::Defend: return Color{88, 216, 132, 210};
            case MilitaryOrderType::None:
            default: return Color{180, 190, 205, 180};
        }
    }

    // Draws an arrow between two screen-space points.
    void DrawOrderArrow(Vector2 from, Vector2 to, Color color)
    {
        Vector2 delta{to.x - from.x, to.y - from.y};
        float len = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (len < 8.0f)
            return;

        Vector2 dir{delta.x / len, delta.y / len};
        const float head = 16.0f;      // arrowhead length
        const float halfW = 8.0f;      // arrowhead half-width
        Vector2 base{to.x - dir.x * head, to.y - dir.y * head};   // where the head meets the shaft
        DrawLineEx(from, base, 3.0f, color);

        Vector2 perp{-dir.y, dir.x};
        Vector2 left{base.x + perp.x * halfW, base.y + perp.y * halfW};
        Vector2 right{base.x - perp.x * halfW, base.y - perp.y * halfW};
        // raylib back-face-culls clockwise triangles, and the winding of (to,left,
        // right) flips with the arrow's direction — so draw both windings to
        // guarantee the head is always visible regardless of which way it points.
        DrawTriangle(to, left, right, color);
        DrawTriangle(to, right, left, color);
        // Crisp outline so the head reads even against a busy map.
        DrawLineEx(to, left, 2.0f, color);
        DrawLineEx(to, right, 2.0f, color);
    }

    // Shared panel theme — matches GuiPanel so war panels read like every other
    // side panel (same dark fill, cool border, title bar and dim body text).
    namespace PanelTheme
    {
        constexpr Color Bg{28, 32, 38, 238};
        constexpr Color Border{92, 102, 118, 255};
        constexpr Color TitleBar{44, 52, 65, 255};
        constexpr Color Separator{105, 118, 136, 255};
        constexpr Color TextDim{190, 198, 208, 255};
        constexpr Color HpFill{75, 185, 100, 230};
        constexpr Color HpBack{35, 38, 45, 220};
    }

    // Draws a standard themed panel frame (rounded fill + border) and returns its
    // title-bar height. Title text is centred via the shared UI font.
    float DrawThemedPanel(Rectangle bounds, const std::string& title, Color accent)
    {
        float titleBar = std::max(30.0f, std::min(40.0f, bounds.height / 10.0f));
        DrawRectangleRounded(bounds, 0.04f, 8, PanelTheme::Bg);
        DrawRectangleRoundedLines(bounds, 0.04f, 8, 1.0f, PanelTheme::Border);
        Rectangle titleBounds{bounds.x, bounds.y, bounds.width, titleBar};
        DrawRectangleRounded(titleBounds, 0.06f, 8, PanelTheme::TitleBar);
        DrawLine(static_cast<int>(bounds.x), static_cast<int>(bounds.y + titleBar),
                 static_cast<int>(bounds.x + bounds.width), static_cast<int>(bounds.y + titleBar),
                 PanelTheme::Separator);
        int titleFont = std::max(16, std::min(24, static_cast<int>(titleBar) - 12));
        UiText::Draw(title, bounds.x + 14.0f, bounds.y + (titleBar - titleFont) * 0.5f, titleFont, accent);
        return titleBar;
    }

    // A static (non-pulsing) combat indicator: crossed swords inside a themed ring.
    // `count` > 0 adds a small badge with the number of divisions in the fight.
    void DrawCombatBubble(Vector2 center, float radius, int count)
    {
        DrawCircle(static_cast<int>(center.x), static_cast<int>(center.y), radius + 2.0f, Color{24, 28, 34, 235});
        DrawCircleLines(static_cast<int>(center.x), static_cast<int>(center.y), radius, Color{198, 96, 70, 255});
        DrawCircleLines(static_cast<int>(center.x), static_cast<int>(center.y), radius - 1.0f, Color{198, 96, 70, 110});

        float blade = radius * 0.45f;
        Color steel{226, 198, 140, 255};
        DrawLineEx({center.x - blade, center.y - blade}, {center.x + blade, center.y + blade}, 2.0f, steel);
        DrawLineEx({center.x + blade, center.y - blade}, {center.x - blade, center.y + blade}, 2.0f, steel);

        if (count > 1)
        {
            std::string label = std::to_string(count);
            int font = 13;
            float w = static_cast<float>(UiText::Measure(label, font)) + 8.0f;
            float bh = font + 4.0f;
            Rectangle badge{center.x + radius - 5.0f, center.y - radius - bh, w, bh};
            DrawRectangleRounded(badge, 0.5f, 6, PanelTheme::TitleBar);
            DrawRectangleRoundedLines(badge, 0.5f, 6, 1.0f, PanelTheme::Separator);
            UiText::Draw(label, badge.x + 4.0f, badge.y + 2.0f, font, RAYWHITE);
        }
    }

    // Returns a readable equipment label while keeping empty equipment compact.
    std::string EquipmentLabel(ResourceType type)
    {
        return type == ResourceType::Null ? "-" : rt2s(type);
    }

    // Canonical per-unit-class colour used by the division bar so a class always
    // reads the same.
    Color DivisionColor(MilitaryUnitType type)
    {
        switch (type)
        {
            case MilitaryUnitType::Militia:   return Color{220, 200, 110, 255};  // straw
            case MilitaryUnitType::Swordsman: return Color{218,  96,  84, 255};  // red
            case MilitaryUnitType::Archer:    return Color{ 94, 180, 112, 255};  // green
            case MilitaryUnitType::Spearman:  return Color{206, 168,  92, 255};  // amber
            case MilitaryUnitType::Cavalry:   return Color{176, 122, 210, 255};  // purple
            default:                          return Color{150, 160, 175, 255};  // gray fallback
        }
    }

    // Returns a readable military order label for UI.
    const char* MilitaryOrderLabel(MilitaryOrderType order)
    {
        switch (order)
        {
            case MilitaryOrderType::Attack: return "Attack";
            case MilitaryOrderType::Support: return "Support";
            case MilitaryOrderType::Defend: return "Defend";
            case MilitaryOrderType::None:
            default: return "None";
        }
    }

    // World-space position a division is drawn at: where it stands when deployed,
    // otherwise just above its home building (still garrisoned).
    Vec2f DivisionRenderWorldPos(GameScene* scene, Building* building, const SoldierDivision& div)
    {
        if (div.worldPos.x >= 0.0f)
            return div.worldPos;
        Vec2i c = scene->game->tilemap.GetCoordsFromId(building->positionId);
        Vec2i fp = building->GetFootprint();
        return {(c.x + fp.x * 0.5f) * TILE_SIZE,
                (c.y + fp.y * 0.5f) * TILE_SIZE - TILE_SIZE * 0.8f};
    }
}

// ─── SelectedBuildingWidget ──────────────────────────────────────────────────

// Highlights the selected building and its suppliers.
void SelectedBuildingWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr || building == nullptr)
        return;

    for (const auto& supplier : building->GetSupplierViews())
    {
        if (supplier.building == nullptr)
            continue;

        Rectangle supplierDest = BuildingScreenRect(scene, supplier.building);
        DrawRectangleRounded(supplierDest, 0.04f, 8, Color{73, 146, 236, 48});
        DrawRectangleRoundedLines(supplierDest, 0.04f, 8, 1.0f, Color{96, 174, 255, 190});
    }

    Rectangle dest = BuildingScreenRect(scene, building);
    DrawRectangleRounded(dest, 0.04f, 8, Color{88, 196, 124, 55});
    DrawRectangleRoundedLines(dest, 0.04f, 8, 1.0f, Color{112, 230, 150, 185});
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

    for (auto* building : localPlayer->GetTrackedBuildings())
    {
        if (building == nullptr || !building->IsProductionStalled())
            continue;

        Rectangle dest = BuildingScreenRect(scene, building);
        DrawRectangleRounded(dest, 0.04f, 8, Color{236, 184, 62, 42});
        DrawRectangleRoundedLines(dest, 0.04f, 8, 1.0f, Color{255, 211, 84, 210});
    }
}

// ─── MilitaryOrderWidget ─────────────────────────────────────────────────────

// Draws order arrows and consolidates engaged divisions into field battles.
void MilitaryOrderWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* localPlayer = GuiLocalPlayer(scene);
    if (localPlayer == nullptr)
        return;

    // One arrow per local division that has an order OR is marching, FROM its own
    // position TO its target/destination — so a grouped stack splitting several
    // ways draws one arrow each (some attack, some move, some stay = no arrow).
    for (auto* building : localPlayer->GetTrackedBuildingsWithComponent<GarrisonComponent>())
    {
        auto* garrison = building != nullptr ? building->GetComponent<GarrisonComponent>() : nullptr;
        if (garrison == nullptr)
            continue;

        for (const auto& divisionPtr : garrison->divisions)
        {
            const auto& division = *divisionPtr;
            bool hasOrder = division.currentOrder != MilitaryOrderType::None && division.orderTargetPositionId >= 0;
            if (!hasOrder && !division.inTransit)
                continue;  // idle, in place → no arrow

            // Origin: where the division actually is right now.
            Vector2 from;
            if (division.worldPos.x >= 0.0f)
            {
                Vec2f s = scene->render.WorldToScreen(division.worldPos);
                from = {s.x, s.y};
            }
            else
            {
                from = BuildingScreenCenter(scene, building);
            }

            // Target: an ordered division points at its order target (enemy building
            // centre, or the targeted tile centre); a plain move points at its travel
            // destination (the quadrant centre it is heading to).
            Vector2 to;
            if (hasOrder)
            {
                if (Building* target = scene->game->tilemap.GetBuilding(division.orderTargetPositionId))
                    to = BuildingScreenCenter(scene, target);
                else
                {
                    Vec2i t = scene->game->tilemap.GetCoordsFromId(division.orderTargetPositionId);
                    Vec2f s = scene->render.WorldToScreen({(t.x + 0.5f) * TILE_SIZE, (t.y + 0.5f) * TILE_SIZE});
                    to = {s.x, s.y};
                }
            }
            else
            {
                Vec2f s = scene->render.WorldToScreen(division.travelToPos);
                to = {s.x, s.y};
            }

            Color col = hasOrder ? MilitaryOrderColor(division.currentOrder)
                                 : Color{150, 220, 255, 230};  // move = cyan
            DrawOrderArrow(from, to, col);
        }
    }

    // Field battles: consolidate every engaged division into fights. Divisions
    // touching the same melee (Chebyshev-adjacent, any owner) belong to one fight,
    // so a pile-up in a quadrant draws a single static bubble — not one per pair.
    battleMarkers.clear();
    // `siegeTarget` is the position id of the building this division is besieging
    // (Attack order on a military target), else -1. All attackers of the same
    // building share one battle bubble even when spread around a large footprint —
    // otherwise a big building like the HQ (4x4) split the assault into several
    // stray bubbles at its corners, none of them centred on the fight.
    struct EngagedUnit { int playerId; int divId; Vec2i tile; Vec2f world; int siegeTarget; };
    std::vector<EngagedUnit> engaged;
    for (auto& [pid, player] : scene->game->playerHandler.players)
    {
        if (player == nullptr) continue;
        for (Building* b : player->GetTrackedBuildingsWithComponent<GarrisonComponent>())
        {
            auto* g = b != nullptr ? b->GetComponent<GarrisonComponent>() : nullptr;
            if (g == nullptr) continue;
            for (const auto& d : g->divisions)
                if (d->engaged && d->occupiedTile.x >= 0 && d->worldPos.x >= 0.0f)
                {
                    int siege = -1;
                    if (d->currentOrder == MilitaryOrderType::Attack && d->orderTargetPositionId >= 0 &&
                        scene->game->tilemap.GetBuilding(d->orderTargetPositionId) != nullptr)
                        siege = d->orderTargetPositionId;
                    engaged.push_back({pid, d->id, d->occupiedTile, d->worldPos, siege});
                }
        }
    }

    // Union-Find: merge engaged units within combat adjacency into one cluster, and
    // also merge every attacker besieging the same building so a surrounded
    // structure shows a single battle bubble on it (not one per side).
    std::vector<int> parent(engaged.size());
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> root = [&](int x)
    {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    for (size_t i = 0; i < engaged.size(); i++)
        for (size_t j = i + 1; j < engaged.size(); j++)
        {
            bool adjacent = std::abs(engaged[i].tile.x - engaged[j].tile.x) <= 1 &&
                            std::abs(engaged[i].tile.y - engaged[j].tile.y) <= 1;
            bool sameSiege = engaged[i].siegeTarget >= 0 &&
                             engaged[i].siegeTarget == engaged[j].siegeTarget;
            if (adjacent || sameSiege)
                parent[root(i)] = root(j);
        }

    // Group members by cluster root.
    std::map<int, std::vector<int>> clusters;
    for (size_t i = 0; i < engaged.size(); i++)
        clusters[root(static_cast<int>(i))].push_back(static_cast<int>(i));

    for (const auto& [r, members] : clusters)
    {
        // Every engaged cluster is a battle: two-sided clusters are field fights,
        // single-sided ones are sieges of a manned garrison (the sim disengages
        // units with nothing left to fight, so `engaged` always means combat).
        // A siege (all members besiege the same building) is anchored ON that
        // building's centre so the icon sits on the target, not drifting to
        // whichever side the attackers happen to stand — important for the HQ,
        // whose large footprint otherwise pulled the bubble off to a corner.
        int commonSiege = engaged[members.front()].siegeTarget;
        for (int idx : members)
            if (engaged[idx].siegeTarget != commonSiege) { commonSiege = -1; break; }

        Vector2 mid;
        Building* siegeBuilding = commonSiege >= 0 ? scene->game->tilemap.GetBuilding(commonSiege) : nullptr;
        if (siegeBuilding != nullptr)
        {
            mid = BuildingScreenCenter(scene, siegeBuilding);
        }
        else
        {
            Vec2f sum{0.0f, 0.0f};
            for (int idx : members) { sum.x += engaged[idx].world.x; sum.y += engaged[idx].world.y; }
            Vec2f centre{sum.x / members.size(), sum.y / members.size()};
            Vec2f screen = scene->render.WorldToScreen(centre);
            mid = {screen.x, screen.y};
        }

        float radius = std::min(24.0f, 13.0f + 1.5f * static_cast<float>(members.size() - 2));
        DrawCombatBubble(mid, radius, static_cast<int>(members.size()));

        FieldBattleMarker marker;
        marker.screenPos = mid;
        marker.radius = radius;
        for (int idx : members)
            marker.participants.push_back({engaged[idx].playerId, engaged[idx].divId});
        battleMarkers.push_back(std::move(marker));
    }

    // Battle details panel (opened by clicking a bubble). Selection is sticky:
    // re-bind to whichever live marker still shares divisions with the open fight.
    if (detailsOpen)
        DrawFieldBattlePanel();
}

// Resolves the open field battle against the live markers and renders its panel
// using the shared panel theme + UI font. Closes itself when the fight is over.
void MilitaryOrderWidget::DrawFieldBattlePanel()
{
    // Re-bind the sticky selection to the marker with the most shared divisions.
    const FieldBattleMarker* active = nullptr;
    int bestOverlap = 0;
    for (const auto& m : battleMarkers)
    {
        int overlap = 0;
        for (const auto& p : selectedParticipants)
            if (m.Contains(p.playerId, p.divisionId)) overlap++;
        if (overlap > bestOverlap) { bestOverlap = overlap; active = &m; }
    }
    if (active == nullptr)
    {
        CloseDetails();
        return;
    }
    selectedParticipants = active->participants;

    auto find = [&](int playerId, int divId, Player*& ownerOut) -> const SoldierDivision*
    {
        auto pit = scene->game->playerHandler.players.find(playerId);
        Player* p = pit != scene->game->playerHandler.players.end() ? pit->second.get() : nullptr;
        if (p == nullptr) return nullptr;
        for (Building* bb : p->GetTrackedBuildingsWithComponent<GarrisonComponent>())
        {
            auto* g = bb != nullptr ? bb->GetComponent<GarrisonComponent>() : nullptr;
            if (g == nullptr) continue;
            for (auto* d : g->divisions)
                if (d->id == divId) { ownerOut = p; return d; }
        }
        return nullptr;
    };

    // Group live participants by player (one column per side).
    struct Side { Player* owner{nullptr}; std::vector<const SoldierDivision*> divs; };
    std::vector<Side> sides;
    for (const auto& part : selectedParticipants)
    {
        Player* owner = nullptr;
        const SoldierDivision* d = find(part.playerId, part.divisionId, owner);
        if (d == nullptr || owner == nullptr) continue;
        Side* side = nullptr;
        for (auto& s : sides) if (s.owner == owner) { side = &s; break; }
        if (side == nullptr) { sides.push_back({owner, {}}); side = &sides.back(); }
        side->divs.push_back(d);
    }
    // When only one player's divisions are present, check if they are besieging a building.
    Building* targetBuilding = nullptr;
    if (sides.size() == 1)
    {
        for (const auto* d : sides[0].divs)
        {
            if (d->currentOrder == MilitaryOrderType::Attack && d->orderTargetPositionId >= 0)
            {
                Building* b = scene->game->tilemap.GetBuilding(d->orderTargetPositionId);
                if (b != nullptr && b->HasComponent<TerritoryComponent>())
                { targetBuilding = b; break; }
            }
        }
        if (targetBuilding == nullptr) { CloseDetails(); return; }
    }
    else if (sides.size() < 2)
    {
        CloseDetails();
        return;
    }

    // Layout: a column per side (plus one extra column when a building is the defender).
    size_t totalCols = sides.size() + (targetBuilding != nullptr ? 1 : 0);
    size_t maxRows = 0;
    for (const auto& s : sides) maxRows = std::max(maxRows, s.divs.size());
    maxRows = std::min<size_t>(maxRows, 6);
    if (targetBuilding != nullptr) maxRows = std::max(maxRows, (size_t)2);

    float colW = 188.0f;
    float rowH = 40.0f;
    float headerH = 34.0f;
    float w = colW * static_cast<float>(totalCols) + 12.0f * (static_cast<float>(totalCols) + 1.0f);
    float h = 44.0f + headerH + rowH * static_cast<float>(maxRows) + 12.0f;
    Rectangle panel{GetScreenWidth() * 0.5f - w * 0.5f, GetScreenHeight() * 0.12f, w, h};

    float titleBar = DrawThemedPanel(panel, "Field Battle", Color{226, 150, 110, 255});

    for (size_t si = 0; si < sides.size(); si++)
    {
        const Side& side = sides[si];
        float cx = panel.x + 12.0f + (colW + 12.0f) * static_cast<float>(si);
        float cy = panel.y + titleBar + 8.0f;

        // Side header: player colour swatch + name + division count.
        Color sideCol = side.owner != nullptr ? side.owner->color : WHITE;
        DrawRectangleRounded(Rectangle{cx, cy, colW, headerH - 6.0f}, 0.18f, 6, Color{36, 41, 50, 235});
        DrawRectangleRec(Rectangle{cx + 6.0f, cy + 6.0f, 12.0f, headerH - 18.0f}, sideCol);
        std::string sideName = side.owner != nullptr ? side.owner->name : "Unknown";
        UiText::DrawFit(sideName, Rectangle{cx + 24.0f, cy + 4.0f, colW - 70.0f, 20.0f}, 17, sideCol);
        UiText::DrawFit(std::to_string(side.divs.size()) + " div",
                        Rectangle{cx + colW - 46.0f, cy + 6.0f, 40.0f, 18.0f}, 15, PanelTheme::TextDim);

        float ry = cy + headerH;
        for (size_t i = 0; i < side.divs.size() && i < maxRows; i++)
        {
            const SoldierDivision* d = side.divs[i];
            std::string label = "#" + std::to_string(d->id) + " " + MilitaryUnitLabel(d->type);
            UiText::Draw(label, cx + 4.0f, ry, 15, RAYWHITE);

            float ratio = d->maxHealth > 0 ? std::clamp(d->health / static_cast<float>(d->maxHealth), 0.0f, 1.0f) : 0.0f;
            Rectangle bar{cx + 4.0f, ry + 19.0f, colW - 70.0f, 8.0f};
            DrawRectangleRec(bar, PanelTheme::HpBack);
            Rectangle fill = bar; fill.width *= ratio;
            DrawRectangleRec(fill, PanelTheme::HpFill);
            UiText::DrawFit("HP " + std::to_string(std::max(0, d->health)),
                            Rectangle{cx + colW - 62.0f, ry + 3.0f, 58.0f, 16.0f}, 13, PanelTheme::TextDim);
            // Supply readiness (combat effectiveness after logistics) as a percent.
            int readiness = static_cast<int>(std::lround(DivisionSupplyEfficiency(*d) * 100.0f));
            UiText::DrawFit("Rdy " + std::to_string(readiness) + "%",
                            Rectangle{cx + colW - 62.0f, ry + 19.0f, 58.0f, 14.0f}, 12,
                            readiness < 60 ? Color{238, 160, 96, 255} : PanelTheme::TextDim);
            ry += rowH;
        }
        if (side.divs.size() > maxRows)
            UiText::Draw("+" + std::to_string(side.divs.size() - maxRows) + " more",
                         cx + 4.0f, ry, 13, PanelTheme::TextDim);
    }

    // Defender building column (when divisions are besieging a structure).
    if (targetBuilding != nullptr)
    {
        float cx = panel.x + 12.0f + (colW + 12.0f) * static_cast<float>(sides.size());
        float cy = panel.y + titleBar + 8.0f;

        Color sideCol = targetBuilding->owner != nullptr ? targetBuilding->owner->color : GRAY;
        DrawRectangleRounded(Rectangle{cx, cy, colW, headerH - 6.0f}, 0.18f, 6, Color{36, 41, 50, 235});
        DrawRectangleRec(Rectangle{cx + 6.0f, cy + 6.0f, 12.0f, headerH - 18.0f}, sideCol);
        UiText::DrawFit(targetBuilding->name, Rectangle{cx + 24.0f, cy + 4.0f, colW - 30.0f, 20.0f}, 17, sideCol);

        float ry = cy + headerH;
        auto* territory = targetBuilding->GetComponent<TerritoryComponent>();
        if (territory != nullptr)
        {
            int hp = territory->hp;
            int maxHp = territory->GetMaxHp(*targetBuilding);
            float ratio = maxHp > 0 ? std::clamp(hp / static_cast<float>(maxHp), 0.0f, 1.0f) : 0.0f;
            UiText::Draw("HP " + std::to_string(hp) + "/" + std::to_string(maxHp), cx + 4.0f, ry, 15, RAYWHITE);
            ry += 18.0f;
            Rectangle bar{cx + 4.0f, ry, colW - 8.0f, 8.0f};
            DrawRectangleRec(bar, PanelTheme::HpBack);
            Rectangle fill = bar; fill.width *= ratio;
            DrawRectangleRec(fill, PanelTheme::HpFill);
            ry += 16.0f;
        }
        auto* garrison = targetBuilding->GetComponent<GarrisonComponent>();
        if (garrison != nullptr)
        {
            int strength = garrison->GetEffectiveStrength(*targetBuilding);
            UiText::Draw("Garrison: " + std::to_string(strength), cx + 4.0f, ry, 14, Color{210, 170, 100, 255});
        }
    }
}

const FieldBattleMarker* MilitaryOrderWidget::HitTest(Vec2i point) const
{
    for (const auto& m : battleMarkers)
    {
        float dx = point.x - m.screenPos.x;
        float dy = point.y - m.screenPos.y;
        float hit = std::max(18.0f, m.radius + 4.0f);
        if (dx * dx + dy * dy <= hit * hit)
            return &m;
    }
    return nullptr;
}

// ─── MilitaryDivisionBarWidget ───────────────────────────────────────────────

// Returns true if a division is in the current selection group.
bool MilitaryDivisionBarWidget::IsSelected(int divId) const
{
    for (int id : selectedDivisionIds)
        if (id == divId) return true;
    return false;
}

// Selects or group-toggles a division card in the bottom military strip.
// Ctrl+LMB adds/removes from group; plain LMB replaces selection.
bool MilitaryDivisionBarWidget::HandleClick(Vec2i point)
{
    auto* garrison = building != nullptr ? building->GetComponent<GarrisonComponent>() : nullptr;
    if (building == nullptr || garrison == nullptr || garrison->divisions.empty() || !ContainsPoint(point))
        return false;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    const float stripH = 28.0f;
    const float gap = 4.0f;
    float y = bounds.y + 36.0f;
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    for (const auto& divisionPtr : garrison->divisions)
    {
        const auto& division = *divisionPtr;
        Rectangle strip{bounds.x + 8.0f, y, bounds.width - 16.0f, stripH};
        if (CheckCollisionPointRec(Vector2{static_cast<float>(point.x), static_cast<float>(point.y)}, strip))
        {
            if (ctrl)
            {
                auto it = std::find(selectedDivisionIds.begin(), selectedDivisionIds.end(), division.id);
                if (it != selectedDivisionIds.end())
                    selectedDivisionIds.erase(it);
                else
                    selectedDivisionIds.push_back(division.id);
            }
            else
            {
                selectedDivisionIds = {division.id};
            }
            return true;
        }
        y += stripH + gap;
        if (y + stripH > bounds.y + bounds.height - 8.0f)
            break;
    }

    return false;
}

// Draws stationed divisions for the selected military building.
void MilitaryDivisionBarWidget::Update(double dt)
{
    if (building == nullptr)
        return;

    auto* garrison = building->GetComponent<GarrisonComponent>();
    if (garrison == nullptr)
        return;

    // Clear selection when building changes
    if (building != prevBuilding)
    {
        selectedDivisionIds.clear();
        prevBuilding = building;
    }

    // Remove ids for divisions that no longer exist
    selectedDivisionIds.erase(
        std::remove_if(selectedDivisionIds.begin(), selectedDivisionIds.end(),
            [&garrison](int id) {
                for (const auto& div : garrison->divisions)
                    if (div->id == id) return false;
                return true;
            }),
        selectedDivisionIds.end());

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    DrawRectangleRounded(bounds, 0.04f, 8, Color{20, 24, 30, 232});
    DrawRectangleRoundedLines(bounds, 0.04f, 8, 1.0f, Color{86, 98, 116, 235});
    UiText::DrawFit("Divisions — " + building->name,
        Rectangle{bounds.x + 12.0f, bounds.y + 8.0f, bounds.width - 24.0f, 22.0f}, 18, RAYWHITE);

    if (garrison->divisions.empty())
    {
        UiText::DrawFit("No divisions",
            Rectangle{bounds.x + 12.0f, bounds.y + 40.0f, bounds.width - 24.0f, 24.0f}, 16, Color{190, 198, 208, 255});
        return;
    }

    // HOI4-style vertical list: one thin strip per division.
    Vector2 mouse = GetMousePosition();
    const MilitaryDivision* hovered = nullptr;
    const float stripH = 28.0f;
    const float gap = 4.0f;
    float y = bounds.y + 36.0f;
    for (const auto& divisionPtr : garrison->divisions)
    {
        const auto& division = *divisionPtr;
        if (y + stripH > bounds.y + bounds.height - 8.0f)
            break;

        Rectangle strip{bounds.x + 8.0f, y, bounds.width - 16.0f, stripH};
        bool isHovered = CheckCollisionPointRec(mouse, strip);
        bool isSelected = IsSelected(division.id);
        if (isHovered)
            hovered = &division;

        Color accent = DivisionColor(division.type);
        DrawRectangleRounded(strip, 0.25f, 6, isSelected ? Color{46, 58, 74, 250} : Color{30, 36, 45, 242});
        DrawRectangleRoundedLines(strip, 0.25f, 6, 1.0f,
                                  isHovered || isSelected ? accent : Color{70, 80, 96, 230});
        DrawRectangleRec({strip.x, strip.y + 3.0f, 4.0f, strip.height - 6.0f}, accent);  // unit-type tab

        std::string label = "#" + std::to_string(division.id) + " " + MilitaryUnitLabel(division.type);
        UiText::DrawFit(label, Rectangle{strip.x + 12.0f, strip.y + 3.0f, strip.width * 0.52f, 22.0f}, 15, RAYWHITE);

        // March / order badge.
        if (division.inTransit)
            UiText::DrawFit("MARCH", Rectangle{strip.x + strip.width * 0.52f, strip.y + 6.0f, 46.0f, 14.0f},
                            12, Color{130, 200, 255, 255});
        else if (division.currentOrder != MilitaryOrderType::None)
            UiText::DrawFit("ORDER", Rectangle{strip.x + strip.width * 0.52f, strip.y + 6.0f, 46.0f, 14.0f},
                            12, MilitaryOrderColor(division.currentOrder));

        // HP + supply mini-bars on the right.
        float barW = strip.width * 0.24f;
        float barX = strip.x + strip.width - barW - 8.0f;
        Rectangle hpBar{barX, strip.y + 5.0f, barW, 6.0f};
        Rectangle supBar{barX, strip.y + 15.0f, barW, 6.0f};
        DrawRectangleRounded(hpBar, 0.4f, 4, Color{18, 22, 28, 255});
        DrawRectangleRounded(supBar, 0.4f, 4, Color{18, 22, 28, 255});
        Rectangle hpFill = hpBar;
        hpFill.width *= division.HealthRatio();
        DrawRectangleRounded(hpFill, 0.4f, 4, Color{89, 197, 121, 255});
        Rectangle supFill = supBar;
        supFill.width *= division.weaponSupplyCapacity > 0
            ? std::clamp(division.weaponSupply / static_cast<float>(division.weaponSupplyCapacity), 0.0f, 1.0f) : 0.0f;
        DrawRectangleRounded(supFill, 0.4f, 4, Color{126, 142, 162, 255});

        y += stripH + gap;
    }

    if (hovered != nullptr)
    {
        auto pct = [](int value, int capacity) {
            int p = capacity > 0
                ? static_cast<int>(std::lround(100.0 * value / capacity)) : 0;
            return std::to_string(std::clamp(p, 0, 100)) + "%";
        };
        Tooltip::Draw(std::string(MilitaryUnitLabel(hovered->type)) + " division #" + std::to_string(hovered->id), {
            "Health: " + std::to_string(hovered->health) + "/" + std::to_string(hovered->maxHealth),
            "Strength: " + std::to_string(hovered->strength),
            "Endurance: " + std::to_string(hovered->endurance),
            "Morale: " + std::to_string(hovered->morale),
            "Experience: " + std::to_string(hovered->experience),
            "Speed: " + FormatOneDecimal(hovered->speedTilesPerMinute) + " tiles/min",
            "Food supply: " + pct(hovered->foodSupply, hovered->foodSupplyCapacity),
            "Weapon supply: " + pct(hovered->weaponSupply, hovered->weaponSupplyCapacity),
            "Materiel supply: " + pct(hovered->materielSupply, hovered->materielSupplyCapacity),
            "Combat readiness: " +
                std::to_string(static_cast<int>(std::lround(DivisionSupplyEfficiency(*hovered) * 100.0f))) + "%",
            "Order: " + std::string(MilitaryOrderLabel(hovered->currentOrder)),
            "Weapon: " + EquipmentLabel(hovered->equipment.weapon),
            "Armor: " + EquipmentLabel(hovered->equipment.armor),
            "Ranged: " + EquipmentLabel(hovered->equipment.rangedWeapon),
            "Ammo: " + EquipmentLabel(hovered->equipment.ammo)
        }, 310.0f);
    }
}

// ─── ArmyBarWidget ────────────────────────────────────────────────────────────

void ArmyBarWidget::Update(double dt)
{
    cardRects.clear();
    plusRect = {0, 0, 0, 0};
    contentBounds = {0, 0, 0, 0};

    Player* localPlayer = GuiLocalPlayer(scene);
    if (localPlayer == nullptr)
        return;

    // No fixed background panel — a screen-centered HBox of floating cards that
    // grows symmetrically as armies are added, plus a trailing "+".
    Vector2 mouse = GetMousePosition();

    const float cardW = 132.0f;
    const float cardH = static_cast<float>(size.y) - 8.0f;
    const float gap = 8.0f;
    const float plusW = 44.0f;
    const float y = static_cast<float>(pos.y) + 4.0f;

    const auto& armies = localPlayer->armyGroups.GetArmies();
    float screenW = static_cast<float>(GetScreenWidth());
    int maxCards = std::max(0, static_cast<int>((screenW - 80.0f - plusW) / (cardW + gap)));
    int shown = std::min(static_cast<int>(armies.size()), maxCards);

    float contentW = shown * (cardW + gap) + plusW;
    float x = screenW * 0.5f - contentW * 0.5f;
    contentBounds = {x - 6.0f, y - 4.0f, contentW + 12.0f, cardH + 8.0f};

    for (int i = 0; i < shown; i++)
    {
        ArmyGroup& army = const_cast<ArmyGroup&>(armies[i]);
        Rectangle card{x, y, cardW, cardH};
        bool hovered = CheckCollisionPointRec(mouse, card);
        bool selected = army.selectedForUI >= 0;
        Color bgColor = selected ? Color{56, 76, 100, 252} : (hovered ? Color{46, 56, 70, 252} : Color{28, 34, 43, 244});
        Color borderColor = selected ? Color{200, 220, 255, 255} : Color{182, 160, 100, 220};
        DrawRectangleRounded(card, 0.18f, 6, bgColor);
        DrawRectangleRoundedLines(card, 0.18f, 6, 1.0f, borderColor);
        UiText::DrawFit(army.name, Rectangle{card.x + 8.0f, card.y + 5.0f, card.width - 16.0f, 18.0f}, 15, RAYWHITE);
        UiText::DrawFit(std::to_string(army.divisions.size()) + " divisions",
                        Rectangle{card.x + 8.0f, card.y + 25.0f, card.width - 16.0f, 16.0f}, 13,
                        Color{190, 198, 208, 255});

        // Aggregate supply readiness across the army's divisions, shown as
        // percentages (Food / Weapon / Materiel — the three supply streams).
        int fS = 0, fC = 0, wS = 0, wC = 0, mS = 0, mC = 0;
        for (const auto& fptr : localPlayer->forces)
        {
            if (fptr == nullptr || !army.HasDivision(fptr->id)) continue;
            fS += fptr->foodSupply;     fC += fptr->foodSupplyCapacity;
            wS += fptr->weaponSupply;   wC += fptr->weaponSupplyCapacity;
            mS += fptr->materielSupply; mC += fptr->materielSupplyCapacity;
        }
        auto apct = [](int v, int c) {
            return std::to_string(c > 0 ? std::clamp(static_cast<int>(std::lround(100.0 * v / c)), 0, 100) : 0);
        };
        UiText::DrawFit("F" + apct(fS, fC) + " W" + apct(wS, wC) + " M" + apct(mS, mC) + "%",
                        Rectangle{card.x + 8.0f, card.y + 42.0f, card.width - 16.0f, 14.0f}, 12,
                        Color{150, 176, 140, 255});
        cardRects.emplace_back(army.id, card);
        x += cardW + gap;
    }

    // Trailing "+" — always present so more armies can be created.
    bool canForm = bar != nullptr && bar->building != nullptr && !bar->selectedDivisionIds.empty();
    plusRect = {x, y, plusW, cardH};
    bool hovered = CheckCollisionPointRec(mouse, plusRect);
    DrawRectangleRounded(plusRect, 0.2f, 6, hovered ? Color{44, 54, 68, 252} : Color{26, 32, 41, 240});
    DrawRectangleRoundedLines(plusRect, 0.2f, 6, 1.0f,
                              canForm ? Color{120, 220, 150, 235} : Color{96, 108, 124, 190});
    UiText::DrawFit("+", plusRect, 26, canForm ? Color{150, 235, 175, 255} : Color{130, 140, 152, 255});
}

bool ArmyBarWidget::IsOverContent(Vec2i point) const
{
    Vector2 p{static_cast<float>(point.x), static_cast<float>(point.y)};
    return CheckCollisionPointRec(p, contentBounds);
}

int ArmyBarWidget::ArmyIdAt(Vec2i point) const
{
    Vector2 p{static_cast<float>(point.x), static_cast<float>(point.y)};
    for (const auto& [armyId, rect] : cardRects)
        if (CheckCollisionPointRec(p, rect))
            return armyId;
    return -1;
}

bool ArmyBarWidget::HandleClick(Vec2i point)
{
    Player* localPlayer = GuiLocalPlayer(scene);
    if (localPlayer == nullptr)
        return false;

    Vector2 click{static_cast<float>(point.x), static_cast<float>(point.y)};

    // Clicks anywhere on the strip are consumed (so they don't deselect via the
    // map underneath); only cards / "+" trigger an action.
    if (!CheckCollisionPointRec(click, contentBounds))
        return false;

    // "+" groups the current selection into a new army.
    if (CheckCollisionPointRec(click, plusRect))
    {
        if (bar != nullptr && bar->building != nullptr && !bar->selectedDivisionIds.empty())
        {
            scene->SubmitLocalCommand(GameCommand::FormArmy(
                scene->game->GetLocalPlayerId(), bar->building->positionId, bar->selectedDivisionIds));
            Log::Msg("[Input]", "Form-army requested for ", bar->selectedDivisionIds.size(), " divisions");
        }
        return true;
    }

    // Clicking an army card selects its divisions and sets it as the active army for orders.
    for (const auto& [armyId, rect] : cardRects)
    {
        if (!CheckCollisionPointRec(click, rect))
            continue;
        ArmyGroup* army = localPlayer->armyGroups.FindArmy(armyId);
        if (army != nullptr)
        {
            // Set as selected for UI (ArmyOrderPanelWidget will show).
            for (auto& a : localPlayer->armyGroups.GetArmies())
                a.selectedForUI = -1;  // Deselect all first.
            army->selectedForUI = 0;   // Select this one.

            // Also select its divisions if we have a bar reference.
            if (bar != nullptr && !army->divisions.empty())
            {
                Building* home = scene->game->tilemap.GetBuilding(army->divisions.front().homeTileId);
                if (home != nullptr)
                {
                    std::vector<int> ids;
                    for (const auto& ref : army->divisions)
                        if (ref.homeTileId == home->positionId)
                            ids.push_back(ref.divisionId);
                    // Atomic so the bar's next Update doesn't wipe the fresh selection.
                    bar->SetSelection(home, std::move(ids));
                }
            }
        }
        return true;
    }

    // Clicked the strip but not a card/"+" — still consume so it doesn't deselect.
    return true;
}

// ─── DivisionMapWidget ────────────────────────────────────────────────────────

// Builds the marker list for division counters drawn on the world layer.
void DivisionMapWidget::Update(double dt)
{
    markers.clear();
    if (scene == nullptr || scene->game == nullptr) return;

    struct Stack
    {
        Building* home{nullptr};
        Player* owner{nullptr};
        Vec2f world{0.0f, 0.0f};
        Vec2i tile{-1, -1};
        std::vector<int> ids;
        MilitaryUnitType type{MilitaryUnitType::Militia};
        bool moving{false};
    };

    for (auto& [playerId, player] : scene->game->playerHandler.players)
    {
        if (player == nullptr) continue;
        for (auto* building : player->GetTrackedBuildingsWithComponent<GarrisonComponent>())
        {
            if (building == nullptr) continue;
            auto* garrison = building->GetComponent<GarrisonComponent>();
            if (garrison == nullptr) continue;

            std::map<long long, Stack> stacks;
            for (const auto& divPtr : garrison->divisions)
            {
                const auto& div = *divPtr;
                bool deployed = div.occupiedTile.x >= 0;
                Vec2f world = DivisionRenderWorldPos(scene, building, div);
                long long key = -(static_cast<long long>(building->positionId) + 1);
                if (deployed)
                {
                    // The badge sits at the CENTRE of the occupied quadrant, always
                    // derived from occupiedTile — so the marker, the combat cell and
                    // the selection outline agree on which province the unit holds.
                    Vec2i cell = SectorCellOf(div.occupiedTile);
                    if (!div.inTransit)
                        world = {(cell.x * 2 + 1) * static_cast<float>(TILE_SIZE),
                                 (cell.y * 2 + 1) * static_cast<float>(TILE_SIZE)};
                    key = div.inTransit
                        ? (1000000000000LL + static_cast<long long>(cell.x) * 100000 + cell.y)
                        : (static_cast<long long>(cell.x) * 100000 + cell.y);
                }

                Stack& stack = stacks[key];
                if (stack.ids.empty())
                {
                    stack.home = building;
                    stack.owner = player.get();
                    stack.world = world;
                    stack.tile = div.occupiedTile;
                    stack.type = div.type;
                }
                else if (div.inTransit)
                {
                    float n = static_cast<float>(stack.ids.size());
                    stack.world = {
                        (stack.world.x * n + world.x) / (n + 1.0f),
                        (stack.world.y * n + world.y) / (n + 1.0f)};
                }
                stack.ids.push_back(div.id);
                if (div.inTransit)
                    stack.moving = true;
            }

            for (auto& [key, stack] : stacks)
            {
                Vec2f screen = scene->render.WorldToScreen(stack.world);
                // Badge colour follows the OWNING PLAYER, not the unit type: the
                // type of a mixed stack's "first" division is unstable frame to
                // frame (map iteration / re-stacking), which made badges flicker
                // between colours. Player colour is stable and reads as "whose army".
                Color ownerColor = stack.owner != nullptr ? stack.owner->color : Color{220, 220, 220, 255};
                markers.push_back({stack.home, stack.owner, stack.ids, stack.tile, {screen.x, screen.y}, ownerColor});

                Rectangle rect{screen.x - kMarkerHalfW, screen.y - kMarkerHalfH,
                               kMarkerHalfW * 2.0f, kMarkerHalfH * 2.0f};
                DrawRectangleRec(rect, Color{18, 22, 30, 225});
                DrawRectangleRec({rect.x, rect.y, 4.0f, rect.height}, ownerColor);
                DrawRectangleLinesEx(rect, stack.moving ? 2.0f : 1.0f,
                                     stack.moving ? Color{255, 255, 255, 210} : ownerColor);
                UiText::DrawFit(std::to_string(stack.ids.size()),
                                Rectangle{rect.x + 4.0f, rect.y + 2.0f, rect.width - 6.0f, rect.height - 4.0f},
                                14, RAYWHITE);
            }
        }
    }
}

// Returns the marker stack at a screen point, or nullptr.
const DivisionMapMarker* DivisionMapWidget::HitTest(Vec2i screenPoint) const
{
    for (const auto& m : markers)
    {
        if (std::abs(screenPoint.x - m.screenPos.x) <= kMarkerHalfW + 3.0f &&
            std::abs(screenPoint.y - m.screenPos.y) <= kMarkerHalfH + 3.0f)
            return &m;
    }
    return nullptr;
}

// ─── MoveTargetWidget ─────────────────────────────────────────────────────────

// Highlights the quadrant under the cursor (move destination) and rings the
// currently selected divisions.
void MoveTargetWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr || bar == nullptr)
        return;

    // Drag-selection rectangle.
    if (drawBox)
    {
        DrawRectangleRec(boxRect, Color{120, 200, 255, 40});
        DrawRectangleLinesEx(boxRect, 1.5f, Color{150, 215, 255, 220});
    }

    if (bar->building == nullptr || bar->selectedDivisionIds.empty())
        return;

    auto* garrison = bar->building->GetComponent<GarrisonComponent>();
    if (garrison == nullptr)
        return;

    Player* localPlayer = GuiLocalPlayer(scene);

    // Draws a subtle outline (+ optional tint) around a single map tile.
    auto drawTileOutline = [&](Vec2i pos, Color lineCol, Color fillCol, float thick)
    {
        if (!scene->game->tilemap.IsInside(pos))
            return;
        Vec2f sTL = scene->render.WorldToScreen({pos.x * static_cast<float>(TILE_SIZE),
                                                 pos.y * static_cast<float>(TILE_SIZE)});
        Vec2f sBR = scene->render.WorldToScreen({(pos.x + 1) * static_cast<float>(TILE_SIZE),
                                                 (pos.y + 1) * static_cast<float>(TILE_SIZE)});
        Rectangle rect{sTL.x, sTL.y, sBR.x - sTL.x, sBR.y - sTL.y};
        if (fillCol.a > 0)
            DrawRectangleRec(rect, fillCol);
        DrawRectangleLinesEx(rect, thick, lineCol);
    };

    auto drawSectorOutline = [&](const DivisionSector& sector, Color lineCol, Color fillCol, float thick)
    {
        if (!sector.IsValid())
            return;
        for (int tileId : sector.TileIds(scene->game->tilemap))
            drawTileOutline(scene->game->tilemap.GetCoordsFromId(tileId), lineCol, fillCol, thick);
    };

    // Subtly outline the QUADRANT each selected division is physically in.
    // Use the live position (worldPos) while marching, not occupiedTile — the
    // latter is set to the destination the instant the order is issued, which made
    // the highlight teleport to the target before the units got there.
    for (int divId : bar->selectedDivisionIds)
    {
        for (const auto& divPtr : garrison->divisions)
        {
            const auto& div = *divPtr;
            if (div.id != divId)
                continue;

            Vec2i physTile{-1, -1};
            if (div.worldPos.x >= 0.0f)
                physTile = {std::clamp(static_cast<int>(div.worldPos.x / TILE_SIZE), 0, scene->game->tilemap.params.sizeX - 1),
                            std::clamp(static_cast<int>(div.worldPos.y / TILE_SIZE), 0, scene->game->tilemap.params.sizeY - 1)};
            else if (div.occupiedTile.x >= 0)
                physTile = div.occupiedTile;

            if (physTile.x >= 0)
            {
                drawSectorOutline(ResolveDivisionSector(scene->game->tilemap, physTile, nullptr),
                                  Color{135, 228, 158, 165}, Color{0, 0, 0, 0}, 1.5f);
            }
            else
            {
                Vec2f sc = scene->render.WorldToScreen(DivisionRenderWorldPos(scene, bar->building, div));
                DrawRectangleLinesEx({sc.x - 17.0f, sc.y - 12.0f, 34.0f, 24.0f}, 1.5f,
                                     Color{135, 228, 158, 170});
            }
            break;
        }
    }

    // Move target under the cursor — single tile, kept subtle.
    Vector2 mouse = GetMousePosition();
    Vec2i mouseScreen{static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
    if (bar->ContainsPoint(mouseScreen) ||
        (armyBar != nullptr && armyBar->IsOverContent(mouseScreen)))
        return;
    // Also suppress highlight when hovering over army order panel.
    if (scene != nullptr && scene->controller != nullptr && scene->controller->armyOrderPanel != nullptr)
    {
        Rectangle panelBounds = scene->controller->armyOrderPanel->GetBounds();
        Vector2 mpos{static_cast<float>(mouseScreen.x), static_cast<float>(mouseScreen.y)};
        if (panelBounds.width > 0 && CheckCollisionPointRec(mpos, panelBounds))
            return;
    }
    Vec2i tile = ScreenToTile(scene, mouse);
    if (tile.x < 0 || tile.y < 0)
        return;

    // Hovering an enemy military building with a selection: highlight the whole
    // building so an attack target is visible instead of being aimed at blind.
    Building* hoveredBuilding = scene->game->tilemap.GetBuilding(tile);
    if (hoveredBuilding != nullptr && hoveredBuilding->owner != localPlayer &&
        IsMilitaryAttackTarget(*hoveredBuilding))
    {
        Vec2i anchor = scene->game->tilemap.GetCoordsFromId(hoveredBuilding->positionId);
        Vec2i fp = hoveredBuilding->GetFootprint();
        for (int yy = 0; yy < fp.y; yy++)
            for (int xx = 0; xx < fp.x; xx++)
                drawTileOutline({anchor.x + xx, anchor.y + yy},
                                Color{235, 110, 90, 210}, Color{220, 80, 60, 40}, 2.0f);
        return;
    }

    // Target quadrant under the cursor. Resolve WITHOUT the owner restriction so
    // it shows on enemy/neutral ground too — you push into enemy land, so the
    // target must be visible there (the move itself isn't territory-locked).
    DivisionSector targetSector = ResolveDivisionSector(scene->game->tilemap, tile, nullptr);
    bool valid = targetSector.IsValid();
    bool intoEnemy = scene->game->tilemap.IsInside(tile) &&
                     scene->game->tilemap.tilemap[scene->game->tilemap.GetIdFromCoords(tile)].owner != localPlayer;

    // Green over own ground, amber when pushing into enemy/neutral ground.
    Color fill = !valid ? Color{220, 70, 60, 16}
               : intoEnemy ? Color{232, 150, 80, 26}
                           : Color{90, 220, 120, 20};
    Color line = !valid ? Color{235, 110, 100, 150}
               : intoEnemy ? Color{240, 175, 95, 175}
                           : Color{140, 235, 165, 165};

    if (valid)
        drawSectorOutline(targetSector, line, fill, 1.5f);
    else
        drawTileOutline(tile, line, fill, 1.5f);
}

// ─── ArmyOrderPanelWidget ───────────────────────────────────────────────────────

void ArmyOrderPanelWidget::Update(double dt)
{
    buttonRects.clear();

    if (scene == nullptr || scene->game == nullptr || armyBar == nullptr)
        return;

    Player* localPlayer = GuiLocalPlayer(scene);
    if (localPlayer == nullptr)
        return;

    // Find which army (if any) is selected in the ArmyBarWidget.
    int selectedArmyId = -1;
    for (const auto& [armyId, rect] : armyBar->cardRects)
    {
        // Check if this army has selectedForUI set, or just use the first visible army for now.
        ArmyGroup* army = localPlayer->armyGroups.FindArmy(armyId);
        if (army != nullptr && army->selectedForUI >= 0)
        {
            selectedArmyId = armyId;
            break;
        }
    }

    // If no army is explicitly selected, don't draw the panel.
    if (selectedArmyId < 0)
        return;

    ArmyGroup* army = localPlayer->armyGroups.FindArmy(selectedArmyId);
    if (army == nullptr || army->divisions.empty())
        return;

    // Panel positioned to the right side of the screen, above the army bar.
    const float panelW = 180.0f;
    const float panelH = 200.0f;
    const float panelX = static_cast<float>(GetScreenWidth()) - panelW - 12.0f;
    const float panelY = static_cast<float>(GetScreenHeight()) - 100.0f - panelH;

    // Draw themed panel background.
    Rectangle panelBounds{panelX, panelY, panelW, panelH};
    float titleBar = DrawThemedPanel(panelBounds, army->name + " Orders", Color{150, 210, 255, 255});

    // Button layout: vertical stack of order buttons.
    const float btnW = panelW - 16.0f;
    const float btnH = 32.0f;
    const float gap = 6.0f;
    float btnY = panelY + titleBar + 8.0f;
    Vector2 mouse = GetMousePosition();

    // Define available orders (for MVP: only BorderDeploy, others grayed).
    struct OrderButton
    {
        std::string label;
        ArmyOrderType type;
        bool enabled{true};
    };
    std::vector<OrderButton> buttons = {
        {"Border Deploy", ArmyOrderType::BorderDeploy, true},
        {"Hold Position", ArmyOrderType::Hold, false},
        {"Attack", ArmyOrderType::Attack, false},
    };

    for (const auto& btn : buttons)
    {
        Rectangle btnRect{panelX + 8.0f, btnY, btnW, btnH};
        bool hovered = CheckCollisionPointRec(mouse, btnRect) && btn.enabled;
        Color bgColor = btn.enabled
                       ? (hovered ? Color{60, 100, 140, 220} : Color{40, 60, 90, 200})
                       : Color{30, 35, 45, 160};
        Color borderColor = btn.enabled
                           ? (hovered ? Color{150, 200, 255, 255} : Color{100, 150, 200, 200})
                           : Color{70, 80, 95, 150};
        Color textColor = btn.enabled
                         ? (hovered ? RAYWHITE : Color{180, 200, 220, 255})
                         : Color{100, 110, 125, 180};

        DrawRectangleRounded(btnRect, 0.08f, 5, bgColor);
        DrawRectangleRoundedLines(btnRect, 0.08f, 5, 1.0f, borderColor);
        UiText::Draw(btn.label, btnRect.x + 8.0f, btnRect.y + (btnH - 16.0f) * 0.5f, 14, textColor);

        if (btn.enabled)
            buttonRects.emplace_back(btn.label, btnRect);

        btnY += btnH + gap;
    }
}

Rectangle ArmyOrderPanelWidget::GetBounds() const
{
    if (scene == nullptr || armyBar == nullptr)
        return {0, 0, 0, 0};

    // Find which army (if any) is selected.
    Player* localPlayer = GuiLocalPlayer(scene);
    if (localPlayer == nullptr)
        return {0, 0, 0, 0};

    bool hasSelection = false;
    for (const auto& [armyId, rect] : armyBar->cardRects)
    {
        ArmyGroup* army = localPlayer->armyGroups.FindArmy(armyId);
        if (army != nullptr && army->selectedForUI >= 0)
        {
            hasSelection = true;
            break;
        }
    }

    if (!hasSelection)
        return {0, 0, 0, 0};

    // Return the panel bounds (consistent with Update layout).
    const float panelW = 180.0f;
    const float panelH = 200.0f;
    const float panelX = static_cast<float>(GetScreenWidth()) - panelW - 12.0f;
    const float panelY = static_cast<float>(GetScreenHeight()) - 100.0f - panelH;
    return {panelX, panelY, panelW, panelH};
}

bool ArmyOrderPanelWidget::HandleClick(Vec2i point)
{
    if (scene == nullptr || scene->game == nullptr || armyBar == nullptr)
        return false;

    Vector2 click{static_cast<float>(point.x), static_cast<float>(point.y)};

    // Check if click hit any button.
    for (const auto& [label, rect] : buttonRects)
    {
        if (!CheckCollisionPointRec(click, rect))
            continue;

        // For now, just log what was clicked. Actual order dispatch happens in GuiSystem.
        Log::Msg("[ArmyOrder]", "Order button clicked: ", label);

        if (label == "Border Deploy")
        {
            // Transition to BorderDeployMode (will be handled by GuiController system switch).
            if (scene->controller != nullptr)
                scene->controller->ChangeSystem("borderDeploy");
        }
        return true;
    }

    return false;
}
