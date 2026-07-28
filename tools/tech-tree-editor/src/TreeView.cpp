#include "TreeView.h"

#include "economy/BalanceStatDisplay.h"

#include "ui/UiText.h"
#include "TreeModel.h"

#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <sstream>

namespace
{
    bool HasNodeTag(const ResearchNodeView& node, const std::string& tag)
    {
        return tag.empty() || std::find(node.tags.begin(), node.tags.end(), tag) != node.tags.end();
    }

    std::vector<std::string> CollectVisibleTags(const std::vector<ResearchNodeView>& nodes)
    {
        std::vector<std::string> tags;
        for (const auto& node : nodes)
        {
            for (const auto& tag : node.tags)
            {
                if (std::find(tags.begin(), tags.end(), tag) == tags.end())
                    tags.push_back(tag);
            }
        }
        std::sort(tags.begin(), tags.end());
        if (tags.size() > 10)
            tags.resize(10);
        return tags;
    }

    void DrawTagFilterBar(Rectangle bounds, const std::vector<std::string>& tags, std::string& selectedTag)
    {
        Vector2 mouse = GetMousePosition();
        float x = bounds.x;
        auto drawButton = [&](const std::string& label, const std::string& value)
        {
            float width = std::min(112.0f, std::max(54.0f, static_cast<float>(MeasureText(label.c_str(), 14) + 22)));
            Rectangle rect{x, bounds.y, width, bounds.height};
            bool selected = selectedTag == value;
            bool hover = CheckCollisionPointRec(mouse, rect);
            DrawRectangleRounded(rect, 0.20f, 6, selected ? Color{92, 74, 38, 235} : hover ? Color{69, 55, 42, 235} : Color{40, 29, 21, 220});
            DrawRectangleRoundedLines(rect, 0.20f, 6, 1.0f, selected ? UiTheme::Gold : Color{112, 92, 66, 230});
            UiText::DrawFit(label, Rectangle{rect.x + 8.0f, rect.y + 4.0f, rect.width - 16.0f, rect.height - 8.0f}, 14, selected ? UiTheme::Parchment : UiTheme::ParchmentDim);
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                selectedTag = value;
            x += width + 8.0f;
            return x < bounds.x + bounds.width - 44.0f;
        };

        if (!drawButton("All", ""))
            return;
        for (const auto& tag : tags)
            if (!drawButton(tag, tag))
                break;
    }

    void DrawUiTextWrappedCentered(const std::string& text, Rectangle bounds, int fontSize, Color color, int maxLines = 2)
    {
        std::vector<std::string> lines = UiText::Wrap(text, fontSize, bounds.width);
        if (static_cast<int>(lines.size()) > maxLines)
        {
            lines.resize(maxLines);
            while (!lines.back().empty() && UiText::Measure(lines.back() + "...", fontSize) > bounds.width)
                lines.back().pop_back();
            lines.back() += "...";
        }

        float lineH = static_cast<float>(fontSize) + 2.0f;
        float totalH = lineH * static_cast<float>(lines.size());
        float y = bounds.y + (bounds.height - totalH) * 0.5f;
        for (const auto& line : lines)
        {
            int measured = UiText::Measure(line, fontSize);
            UiText::Draw(line, bounds.x + (bounds.width - measured) * 0.5f, y, fontSize, color);
            y += lineH;
        }
    }




    std::string TooltipBonusLine(const std::string& text)
    {
        return "{bonus}" + text;
    }

    std::string TooltipPenaltyLine(const std::string& text)
    {
        return "{penalty}" + text;
    }

    std::string TooltipSeparatorLine()
    {
        return "{separator}";
    }



    std::string FormatDuration(double seconds)
    {
        int total = std::max(0, static_cast<int>(std::round(seconds)));
        int minutes = total / 60;
        int remainingSeconds = total % 60;
        std::ostringstream stream;
        stream << minutes << ":";
        if (remainingSeconds < 10)
            stream << "0";
        stream << remainingSeconds;
        return stream.str();
    }

    std::string FormatModifierForFocusTooltip(const BalanceModifier& modifier)
    {
        std::ostringstream stream;
        bool lowerIsBetter = LowerValueIsBetter(modifier.stat);
        bool showAsRate = lowerIsBetter && std::abs(modifier.multiplier - 1.0) > 0.001 &&
                          (modifier.stat == BalanceStat::BuildTime ||
                           modifier.stat == BalanceStat::ProductionCycleTime ||
                           modifier.stat == BalanceStat::TransportTime);
        const bool isUniversityResearch = modifier.stat == BalanceStat::ProductionCycleTime &&
                                          modifier.buildingType == BuildingType::University;
        stream << (isUniversityResearch ? "Research speed" :
                   (showAsRate ? ImprovedRateLabel(modifier.stat) : BalanceStatLabel(modifier.stat)));
        if (modifier.buildingType.has_value())
            stream << " for {building}" << BalanceBuildingLabel(modifier.buildingType.value()) << "{/building}";
        if (modifier.resourceType.has_value())
            stream << " producing {resource}" << rt2s(modifier.resourceType.value()) << "{/resource}";
        // Divergence from the game: it resolves the unit's display name via
        // FindUnitDefinition(). That catalog loads from a path relative to the
        // game's working directory and would drag warfare/ into this tool, so
        // the raw unit id from the data file is shown instead.
        if (modifier.unitDefId.has_value())
            stream << " for " << modifier.unitDefId.value();
        if (modifier.resourceCategory.has_value())
            stream << " ({category}" << ResourceCategoryLabel(modifier.resourceCategory.value()) << "{/category})";
        stream << ": ";

        bool hasValue = false;
        if (std::abs(modifier.additive) > 0.001)
        {
            stream << (modifier.additive > 0.0 ? "+" : "") << modifier.additive;
            hasValue = true;
        }
        if (std::abs(modifier.multiplier - 1.0) > 0.001)
        {
            double percent = showAsRate
                ? (1.0 / modifier.multiplier - 1.0) * 100.0
                : (modifier.multiplier - 1.0) * 100.0;
            if (hasValue)
                stream << ", ";
            stream << (percent > 0.0 ? "+" : "") << static_cast<int>(std::round(percent)) << "%";
            hasValue = true;
        }
        if (!hasValue)
            stream << "No numeric modifier";
        return IsPositiveModifier(modifier) ? TooltipBonusLine(stream.str()) : TooltipPenaltyLine(stream.str());
    }

    std::string FormatResearchCosts(const std::vector<ResourceAmountDefinition>& costs)
    {
        std::ostringstream stream;
        stream << "Research cost: ";
        for (size_t index = 0; index < costs.size(); index++)
        {
            if (index > 0)
                stream << "  |  ";
            stream << "{resource}" << rt2s(costs[index].type) << "{/resource} x" << costs[index].amount;
        }
        return stream.str();
    }

    // Display order of tree lanes; unknown lanes go last, alphabetically.
    int LaneRank(const std::string& lane)
    {
        if (lane == "Biology") return 0;
        if (lane == "Mathematics") return 1;
        if (lane == "Humanities") return 2;
        if (lane == "PRODUCTION" || lane == "ECONOMY") return 0;
        if (lane == "MILITARY" || lane == "WARFARE") return 1;
        if (lane == "SOCIAL" || lane == "LOGISTICS") return 2;
        if (lane == "POLITICS" || lane == "GOVERNANCE") return 3;
        return 10;
    }
}

Rectangle TreeView::GetTreeArea(Rectangle bounds) const
{
    // Flatter than the game's panel: no title bar (the toolbar already says
    // which tree is open), so the canvas starts right below the tag filter.
    float top = bounds.y + 44.0f;
    float bottom = bounds.y + bounds.height - 10.0f;
    return Rectangle{bounds.x + 10.0f, top, bounds.width - 20.0f, bottom - top};
}

bool TreeView::ContainsTreeArea(Rectangle bounds, Vector2 point) const
{
    return CheckCollisionPointRec(point, GetTreeArea(bounds));
}

void TreeView::ResetCamera()
{
    panOffset = {0.0f, 0.0f};
    scrollOffset = 0.0f;
    panning = false;
}

void TreeView::AdjustScroll(float wheel)
{
    scrollOffset = std::clamp(scrollOffset - wheel * 48.0f, 0.0f, maxScrollOffset);
}

void TreeView::AdjustZoom(Rectangle bounds, Vector2 point, float wheel)
{
    if (wheel == 0.0f)
        return;

    Rectangle treeArea = GetTreeArea(bounds);
    if (!CheckCollisionPointRec(point, treeArea))
        return;

    float oldZoom = zoom;
    float newZoom = std::clamp(zoom + wheel * 0.08f, 0.42f, 1.15f);
    if (std::abs(newZoom - oldZoom) < 0.001f)
        return;

    float localX = point.x - treeArea.x;
    float localY = point.y - treeArea.y;
    panOffset.x = localX - (localX - panOffset.x) * (newZoom / oldZoom);
    panOffset.y = localY - (localY - panOffset.y) * (newZoom / oldZoom);
    zoom = newZoom;
}

void TreeView::Draw(Rectangle bounds, TreeDocument& document)
{
    Vector2 mouse = GetMousePosition();
    // Flat canvas instead of the game's framed panel: this is a work surface,
    // not an in-game window.
    DrawRectangleRec(bounds, Color{24, 19, 15, 255});
    DrawRectangleLinesEx(bounds, 1.0f, Color{62, 50, 38, 255});

    auto nodes = document.BuildNodes();
    std::map<std::string, ResearchNodeView*> byId;
    for (auto& node : nodes)
        byId[node.id] = &node;

    auto visibleTags = CollectVisibleTags(nodes);
    Rectangle tagBar{bounds.x + 10.0f, bounds.y + 9.0f, bounds.width - 20.0f, 26.0f};
    DrawTagFilterBar(tagBar, visibleTags, selectedTagFilter);

    Rectangle treeArea = GetTreeArea(bounds);

    // A right press either adds a child (when it lands on a node) or pans (when
    // it does not). Which one is only known after the node rects exist, so the
    // decision is deferred to the node loop below.
    bool rightPressedInTree = CheckCollisionPointRec(mouse, treeArea) && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    if (panning && IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    {
        panOffset.x += mouse.x - lastPanMouse.x;
        panOffset.y += mouse.y - lastPanMouse.y;
        lastPanMouse = {mouse.x, mouse.y};
    }
    if (panning && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
        panning = false;

    // -- Layout: lanes -> depth rows -> per-row horizontal placement -----------
    // Shorter cards than the game's: the time bar and progress bar are gone
    // (nothing is ever "in progress" here) so the box only needs name + state.
    float nodeW = 132.0f * zoom;
    float nodeH = 76.0f * zoom;
    float colGap = 118.0f * zoom;
    float laneGap = 220.0f * zoom;
    float rowGap = 118.0f * zoom;
    float laneHeaderH = 30.0f * zoom;
    std::map<std::string, Rectangle> nodeRects;
    std::map<std::string, int> depthById;
    std::map<std::string, std::map<int, std::vector<ResearchNodeView*>>> nodesByLaneDepth;
    std::vector<std::pair<std::string, Rectangle>> laneHeaders;
    std::vector<std::string> lanes;

    auto preferredDepth = [](const ResearchNodeView& node)
    {
        return node.layoutOrder >= 1000 ? node.layoutOrder / 1000 - 1 : 0;
    };

    std::function<int(const ResearchNodeView&)> depthOf = [&](const ResearchNodeView& node)
    {
        auto cached = depthById.find(node.id);
        if (cached != depthById.end())
            return cached->second;
        int depth = preferredDepth(node);
        for (const auto& prerequisite : node.prerequisites)
        {
            auto it = byId.find(prerequisite);
            if (it != byId.end())
                depth = std::max(depth, depthOf(*it->second) + 1);
        }
        depthById[node.id] = depth;
        return depth;
    };

    for (auto& node : nodes)
    {
        int depth = depthOf(node);
        std::string lane = node.layoutLane.empty() ? node.category : node.layoutLane;
        nodesByLaneDepth[lane][depth].push_back(&node);
        if (std::find(lanes.begin(), lanes.end(), lane) == lanes.end())
            lanes.push_back(lane);
    }

    std::sort(lanes.begin(), lanes.end(), [](const std::string& a, const std::string& b)
    {
        int rankA = LaneRank(a);
        int rankB = LaneRank(b);
        if (rankA != rankB)
            return rankA < rankB;
        return a < b;
    });

    laneBands.clear();
    float laneX = treeArea.x + 28.0f + panOffset.x;
    for (const auto& lane : lanes)
    {
        auto& rows = nodesByLaneDepth[lane];
        size_t maxColumns = 1;
        for (const auto& [depth, rowNodes] : rows)
            maxColumns = std::max(maxColumns, rowNodes.size());

        float laneWidth = std::max(680.0f * zoom, maxColumns * nodeW + (maxColumns - 1) * colGap + 360.0f * zoom);
        Rectangle laneHeader{laneX, treeArea.y + panOffset.y - scrollOffset, laneWidth, laneHeaderH};
        laneHeaders.push_back({lane, laneHeader});
        laneBands.push_back({lane, laneX, laneWidth, std::max(1.0f, laneWidth - nodeW)});

        for (auto& [depth, rowNodes] : rows)
        {
            std::stable_sort(rowNodes.begin(), rowNodes.end(), [&](const ResearchNodeView* a, const ResearchNodeView* b)
            {
                auto parentOrder = [&](const ResearchNodeView* node)
                {
                    int order = node->layoutOrder;
                    for (const auto& prerequisite : node->prerequisites)
                    {
                        auto it = byId.find(prerequisite);
                        if (it != byId.end())
                            order = std::min(order, it->second->layoutOrder);
                    }
                    return order;
                };

                int parentA = parentOrder(a);
                int parentB = parentOrder(b);
                if (parentA != parentB)
                    return parentA < parentB;
                if (a->layoutOrder != b->layoutOrder)
                    return a->layoutOrder < b->layoutOrder;
                return a->definitionIndex < b->definitionIndex;
            });

            float rowY = treeArea.y + panOffset.y + laneHeaderH + 24.0f + depth * (nodeH + rowGap) - scrollOffset;
            float usableWidth = std::max(1.0f, laneWidth - nodeW);

            // Placement is deterministic: layout_order IS the horizontal
            // position. The game blends it with the parent's centre, a spread
            // term and a collision pass, which is why order barely moved a node
            // there and why adding one shoved its neighbours. Here a node with
            // an explicit layout_order lands exactly on its slot and never
            // moves because something else appeared next to it — that is what
            // "hold hard unless I drag it" requires. Only nodes with no
            // layout_order in the file are auto-spread.
            std::vector<ResearchNodeView*> autoNodes;
            for (auto* rowNode : rowNodes)
            {
                const auto* definition = document.Find(rowNode->id);
                bool explicitOrder = definition != nullptr &&
                                     definition->layoutOrder != std::numeric_limits<int>::max();
                if (!explicitOrder)
                {
                    autoNodes.push_back(rowNode);
                    continue;
                }

                int slot = ((rowNode->layoutOrder % 1000) + 1000) % 1000;
                float x = laneX + (static_cast<float>(slot) / 999.0f) * usableWidth;
                nodeRects[rowNode->id] = Rectangle{x, rowY, nodeW, nodeH};
            }

            // Nodes with no layout_order pack from the LEFT edge of the lane
            // rather than spreading around its centre, so an unpositioned node
            // has an obvious, stable home instead of drifting as siblings come
            // and go.
            for (size_t i = 0; i < autoNodes.size(); i++)
            {
                float x = laneX + static_cast<float>(i) * (nodeW + colGap * 0.5f);
                nodeRects[autoNodes[i]->id] = Rectangle{std::min(x, laneX + usableWidth), rowY, nodeW, nodeH};
            }
        }
        laneX += laneWidth + laneGap;
    }

    float contentBottom = treeArea.y;
    for (const auto& [id, rect] : nodeRects)
        contentBottom = std::max(contentBottom, rect.y + rect.height + scrollOffset);
    maxScrollOffset = std::max(0.0f, contentBottom - (treeArea.y + treeArea.height));
    scrollOffset = std::clamp(scrollOffset, 0.0f, maxScrollOffset);

    const ResearchNodeView* hovered = nullptr;
    for (const auto& node : nodes)
    {
        auto it = nodeRects.find(node.id);
        if (it != nodeRects.end() && CheckCollisionPointRec(mouse, it->second))
            hovered = &node;
    }

    std::set<std::string> highlightedPath;
    std::function<void(const std::string&)> collectParents = [&](const std::string& id)
    {
        if (!highlightedPath.insert(id).second)
            return;
        auto it = byId.find(id);
        if (it == byId.end())
            return;
        for (const auto& prerequisite : it->second->prerequisites)
            collectParents(prerequisite);
    };
    if (hovered != nullptr)
        collectParents(hovered->id);

    // -- Draw: lane headers, prerequisite edges, nodes -------------------------
    BeginScissorMode(static_cast<int>(treeArea.x), static_cast<int>(treeArea.y), static_cast<int>(treeArea.width), static_cast<int>(treeArea.height));
    // Lane headers: a left-aligned label over a rule, instead of the game's
    // filled bar — less ink for the same information.
    for (const auto& [lane, header] : laneHeaders)
    {
        int laneFont = std::max(16, static_cast<int>(21 * zoom));
        UiText::Draw(lane, header.x, header.y, laneFont, Color{216, 194, 156, 255});
        float ruleY = header.y + header.height - 4.0f;
        DrawLineEx({header.x, ruleY}, {header.x + header.width, ruleY}, 1.0f, Color{72, 58, 43, 255});
    }

    for (const auto& node : nodes)
    {
        auto child = nodeRects[node.id];
        Vector2 childAnchor{child.x + child.width * 0.5f, child.y};
        for (const auto& prerequisite : node.prerequisites)
        {
            auto parentIt = nodeRects.find(prerequisite);
            if (parentIt == nodeRects.end())
                continue;
            Rectangle parent = parentIt->second;
            Vector2 parentAnchor{parent.x + parent.width * 0.5f, parent.y + parent.height};
            bool highlighted = highlightedPath.contains(node.id) && highlightedPath.contains(prerequisite);
            Color edgeColor = highlighted ? Color{232, 202, 104, 255} : Color{104, 88, 66, 190};
            float edgeWidth = highlighted ? 3.0f : 1.5f;

            // Orthogonal routing: a straight drop when the columns line up,
            // otherwise down / across / down with right angles only. The game's
            // version used the child's x for the second point, which made the
            // middle segment a diagonal whenever the columns differed.
            if (std::abs(parentAnchor.x - childAnchor.x) < 1.5f)
            {
                DrawLineEx(parentAnchor, childAnchor, edgeWidth, edgeColor);
            }
            else
            {
                float midY = (parentAnchor.y + childAnchor.y) * 0.5f;
                Vector2 corner1{parentAnchor.x, midY};
                Vector2 corner2{childAnchor.x, midY};
                DrawLineEx(parentAnchor, corner1, edgeWidth, edgeColor);
                DrawLineEx(corner1, corner2, edgeWidth, edgeColor);
                DrawLineEx(corner2, childAnchor, edgeWidth, edgeColor);
            }
        }
    }

    std::string clickedNodeId;
    std::string rightClickedNodeId;
    for (const auto& node : nodes)
    {
        Rectangle rect = nodeRects[node.id];
        bool hover = CheckCollisionPointRec(mouse, rect);
        bool tagMatched = HasNodeTag(node, selectedTagFilter);
        Color fill = node.researched ? Color{52, 74, 40, 245}
                   : node.active ? Color{70, 65, 38, 245}
                   : node.available ? Color{74, 56, 34, 245}
                   : Color{34, 26, 19, 230};
        Color line = node.researched ? Color{140, 176, 96, 255}
                   : node.active ? Color{214, 178, 84, 255}
                   : node.available ? Color{176, 132, 68, 255}
                   : Color{100, 84, 64, 220};
        if (!selectedTagFilter.empty() && !tagMatched)
        {
            fill.a = 110;
            line.a = 120;
        }
        // Plain rectangles, not the game's rounded cards: no research timer and
        // no progress bar, because nothing here is ever mid-research. Just the
        // name, a state word, and a thin status stripe down the left edge.
        Color border = tagMatched && !selectedTagFilter.empty() ? Color{150, 210, 130, 255}
                     : highlightedPath.contains(node.id) ? Color{232, 202, 104, 255}
                     : hover ? UiTheme::AmberBright
                     : line;
        DrawRectangleRec(rect, fill);
        DrawRectangleRec(Rectangle{rect.x, rect.y, 3.0f, rect.height}, line);
        DrawRectangleLinesEx(rect, 1.0f, border);

        DrawUiTextWrappedCentered(node.name,
            Rectangle{rect.x + 10.0f * zoom, rect.y + 6.0f * zoom, rect.width - 18.0f * zoom, 40.0f * zoom},
            std::max(14, static_cast<int>(20 * zoom)), UiTheme::Parchment, 2);
        UiText::DrawFit(node.stateText,
            Rectangle{rect.x + 10.0f * zoom, rect.y + rect.height - 22.0f * zoom, rect.width - 18.0f * zoom, 18.0f * zoom},
            std::max(10, static_cast<int>(15 * zoom)),
            node.researched ? Color{162, 214, 122, 255} : node.available ? UiTheme::AmberBright : Color{150, 132, 104, 255});

        // Selection ring for whichever node the inspector is editing.
        if (node.id == selectedNodeId)
            DrawRectangleLinesEx(Rectangle{rect.x - 3.0f, rect.y - 3.0f, rect.width + 6.0f, rect.height + 6.0f},
                                 2.0f, UiTheme::Gold);
        // The node currently following the mouse is drawn ghosted.
        if (node.id == placingNodeId)
        {
            DrawRectangleRec(rect, Fade(UiTheme::Gold, 0.18f));
            DrawRectangleLinesEx(rect, 2.0f, UiTheme::Gold);
        }

        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            clickedNodeId = node.id;
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
            rightClickedNodeId = node.id;
    }
    EndScissorMode();

    if (maxScrollOffset > 0.0f)
    {
        Rectangle track{treeArea.x + treeArea.width + 6.0f, treeArea.y, 5.0f, treeArea.height};
        DrawRectangleRounded(track, 0.5f, 4, Color{24, 17, 12, 190});
        float thumbH = std::max(32.0f, track.height * (track.height / (track.height + maxScrollOffset)));
        float thumbY = track.y + (track.height - thumbH) * (scrollOffset / maxScrollOffset);
        DrawRectangleRounded(Rectangle{track.x, thumbY, track.width, thumbH}, 0.5f, 4, Color{150, 108, 58, 230});
    }

    // Row geometry needed to map a mouse position back to a layer.
    float rowTop = treeArea.y + panOffset.y + laneHeaderH + 28.0f - scrollOffset;
    float rowStep = nodeH + rowGap;

    // --- Placement in progress -----------------------------------------------
    if (IsPlacing())
    {
        placement = ResolvePlacement(mouse, rowTop, rowStep);
        document.SetLanePosition(placingNodeId, placement.lane, placement.ToLayoutOrder());

        std::string hint = placement.lane + "  |  layer " + std::to_string(placement.layer + 1) +
                           "  order " + std::to_string(placement.order) +
                           "  (layout_order " + std::to_string(placement.ToLayoutOrder()) + ")";
        Rectangle badge{mouse.x + 18.0f, mouse.y - 30.0f, static_cast<float>(UiText::Measure(hint, 17)) + 20.0f, 26.0f};
        badge.x = std::min(badge.x, static_cast<float>(GetScreenWidth()) - badge.width - 8.0f);
        DrawRectangleRounded(badge, 0.2f, 6, Fade(UiTheme::Bark, 0.96f));
        DrawRectangleRoundedLines(badge, 0.2f, 6, 1.0f, UiTheme::Gold);
        UiText::Draw(hint, badge.x + 10.0f, badge.y + 4.0f, 17, UiTheme::Parchment);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            placingNodeId.clear();
            placementIsNew = false;
        }
        else if (IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
        {
            CancelPlacement(document);
        }
        return;
    }

    // --- Left button: click selects/toggles, drag repositions -----------------
    if (!clickedNodeId.empty())
    {
        pressedNodeId = clickedNodeId;
        pressOrigin = mouse;
        dragging = false;
    }
    if (!pressedNodeId.empty() && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !dragging)
    {
        float dx = mouse.x - pressOrigin.x;
        float dy = mouse.y - pressOrigin.y;
        if (dx * dx + dy * dy > 36.0f)
        {
            // Past the threshold this is a move, not a click.
            dragging = true;
            const auto* definition = document.Find(pressedNodeId);
            placementUndoLane = definition != nullptr ? definition->layoutLane : std::string();
            placementUndoOrder = definition != nullptr ? definition->layoutOrder : 0;
            placementIsNew = false;
            placingNodeId = pressedNodeId;
            selectedNodeId = pressedNodeId;
        }
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        if (!pressedNodeId.empty() && !dragging && CheckCollisionPointRec(mouse, treeArea))
        {
            // Plain click: select for the inspector and toggle the calculator
            // selection in one gesture.
            selectedNodeId = pressedNodeId;
            document.ToggleTaken(pressedNodeId);
        }
        pressedNodeId.clear();
        dragging = false;
    }

    // --- Right button: on a node adds a child, on empty space pans -----------
    if (!rightClickedNodeId.empty())
    {
        panning = false;
        const auto* parent = document.Find(rightClickedNodeId);
        std::string lane = parent != nullptr && !parent->layoutLane.empty()
            ? parent->layoutLane
            : (parent != nullptr ? parent->category : std::string());

        PlacementTarget target = ResolvePlacement(mouse, rowTop, rowStep);
        target.lane = lane.empty() ? target.lane : lane;
        // A child must sit at least one layer below its prerequisite, otherwise
        // depthOf() overrides the layout_order and the node visibly snaps away
        // from where it was dropped.
        auto parentDepth = depthById.find(rightClickedNodeId);
        if (parentDepth != depthById.end())
            target.layer = std::max(target.layer, parentDepth->second + 1);

        std::string newId = document.AddNode(rightClickedNodeId, target.lane, target.ToLayoutOrder());
        placingNodeId = newId;
        selectedNodeId = newId;
        placement = target;
        placementIsNew = true;
    }
    else if (rightPressedInTree && !panning)
    {
        panning = true;
        lastPanMouse = {mouse.x, mouse.y};
    }

    if (hovered != nullptr && !IsPlacing())
    {
        std::vector<std::string> lines{hovered->description};
        lines.push_back(TooltipSeparatorLine());
        lines.push_back("Time: " + FormatDuration(hovered->researchTime) + " | " + hovered->stateText);
        if (!hovered->costs.empty())
            lines.push_back(FormatResearchCosts(hovered->costs));
        lines.push_back("id: " + hovered->id + "  |  layout_order " + std::to_string(hovered->layoutOrder));
        if (!hovered->available && !hovered->researched)
        {
            lines.push_back(TooltipSeparatorLine());
            lines.push_back("Prerequisites not met");
        }
        for (const auto& modifier : hovered->modifiers)
            lines.push_back(FormatModifierForFocusTooltip(modifier));
        Tooltip::Draw(hovered->name, lines, 460.0f);
    }
}

PlacementTarget TreeView::ResolvePlacement(Vector2 mouse, float rowTop, float rowStep) const
{
    PlacementTarget target;

    // Lane: whichever band contains the cursor, else the nearest one.
    float bestDistance = std::numeric_limits<float>::max();
    for (const auto& band : laneBands)
    {
        float distance = mouse.x < band.x ? band.x - mouse.x
                       : mouse.x > band.x + band.width ? mouse.x - (band.x + band.width)
                       : 0.0f;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            target.lane = band.lane;
            // Horizontal slot: position within the band mapped onto 0..999,
            // snapped to 25 so the values stay readable in the file. Uses the
            // same usable span as the placement pass so the node lands where
            // the cursor is, not offset by half a card.
            float t = (mouse.x - band.x) / band.usableWidth;
            int order = static_cast<int>(std::round(std::clamp(t, 0.0f, 1.0f) * 999.0f / 25.0f)) * 25;
            target.order = std::clamp(order, 0, 975);
        }
        if (distance == 0.0f)
            break;
    }

    // Layer: which row band the cursor is in.
    target.layer = rowStep > 1.0f
        ? std::max(0, static_cast<int>(std::floor((mouse.y - rowTop + rowStep * 0.5f) / rowStep)))
        : 0;
    return target;
}

void TreeView::CancelPlacement(TreeDocument& document)
{
    if (placingNodeId.empty())
        return;

    if (placementIsNew)
    {
        document.DeleteNode(placingNodeId);
        if (selectedNodeId == placingNodeId)
            selectedNodeId.clear();
    }
    else
    {
        document.SetLanePosition(placingNodeId, placementUndoLane, placementUndoOrder);
    }
    placingNodeId.clear();
    placementIsNew = false;
    dragging = false;
    pressedNodeId.clear();
}
