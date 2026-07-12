// Research tree UI shared by political focuses and technologies: the merged
// ResearchTreePanelWidget plus the Focus/Tech interaction systems.
//
// Both trees render through one layout + draw pass; the differences (data
// source, focus-only state side panel, node click command) branch on
// ResearchTreeKind.

#include "GuiInternal.h"

#include "scenes/Scenes.h"
#include "economy/Player.h"
#include "research/ResearchCatalog.h"
#include "research/Technology.h"
#include "research/StateDevelopment.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
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
            DrawRectangleRounded(rect, 0.20f, 6, selected ? Color{64, 94, 128, 235} : hover ? Color{45, 55, 69, 235} : Color{31, 37, 47, 220});
            DrawRectangleRoundedLines(rect, 0.20f, 6, 1.0f, selected ? Color{140, 185, 240, 255} : Color{82, 96, 116, 230});
            UiText::DrawFit(label, Rectangle{rect.x + 8.0f, rect.y + 4.0f, rect.width - 16.0f, rect.height - 8.0f}, 14, selected ? RAYWHITE : Color{188, 198, 212, 255});
            if (hover && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
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

    std::vector<std::string> WrapUiText(const std::string& text, int fontSize, float maxWidth)
    {
        std::vector<std::string> wrapped;
        std::istringstream words(text);
        std::string word;
        std::string line;
        while (words >> word)
        {
            std::string candidate = line.empty() ? word : line + " " + word;
            if (UiText::Measure(candidate, fontSize) <= maxWidth || line.empty())
            {
                line = candidate;
                continue;
            }
            wrapped.push_back(line);
            line = word;
        }
        if (!line.empty())
            wrapped.push_back(line);
        if (wrapped.empty())
            wrapped.push_back("");
        return wrapped;
    }

    void DrawUiTextWrappedCentered(const std::string& text, Rectangle bounds, int fontSize, Color color, int maxLines = 2)
    {
        std::vector<std::string> lines = WrapUiText(text, fontSize, bounds.width);
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

    // Finds the first idle (not researching) University owned by the local player.
    Building* FindIdleUniversity(GameScene* scene)
    {
        Player* player = GuiLocalPlayer(scene);
        if (player == nullptr)
            return nullptr;
        for (auto* building : player->GetTrackedBuildingsWithComponent<ResearchComponent>())
        {
            if (building == nullptr || building->owner != player ||
                building->buildingType != BuildingType::University ||
                building->IsUnderConstruction())
                continue;
            const auto* research = building->GetComponent<ResearchComponent>();
            if (research != nullptr && research->technologyId.empty())
                return building;
        }
        return nullptr;
    }

    const char* FocusStatLabel(BalanceStat stat)
    {
        switch (stat)
        {
            case BalanceStat::BuildTime: return "Build time";
            case BalanceStat::BuildCost: return "Build cost";
            case BalanceStat::ProductionCycleTime: return "Production cycle time";
            case BalanceStat::ProductionOutputAmount: return "Production output";
            case BalanceStat::WorkerCapacity: return "Worker capacity";
            case BalanceStat::TransportTime: return "Transport time";
            case BalanceStat::RoadCapacity: return "Road capacity";
            case BalanceStat::RoadSpeed: return "Road speed";
            case BalanceStat::ManpowerRate: return "Manpower growth";
            case BalanceStat::PopulationCap: return "Population cap";
            case BalanceStat::BuilderAmount: return "Builders";
            default: return "Effect";
        }
    }

    bool LowerValueIsBetter(BalanceStat stat)
    {
        switch (stat)
        {
            case BalanceStat::BuildTime:
            case BalanceStat::BuildCost:
            case BalanceStat::ProductionCycleTime:
            case BalanceStat::TransportTime:
                return true;
            default:
                return false;
        }
    }

    bool IsPositiveModifier(const BalanceModifier& modifier)
    {
        bool lowerIsBetter = LowerValueIsBetter(modifier.stat);
        if (std::abs(modifier.additive) > 0.001)
            return lowerIsBetter ? modifier.additive < 0.0 : modifier.additive > 0.0;
        if (std::abs(modifier.multiplier - 1.0) > 0.001)
            return lowerIsBetter ? modifier.multiplier < 1.0 : modifier.multiplier > 1.0;
        return true;
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

    const char* ImprovedRateLabel(BalanceStat stat)
    {
        switch (stat)
        {
            case BalanceStat::BuildTime: return "Build speed";
            case BalanceStat::ProductionCycleTime: return "Production speed";
            case BalanceStat::TransportTime: return "Transport speed";
            default: return FocusStatLabel(stat);
        }
    }

    const char* FocusBuildingLabel(BuildingType type)
    {
        switch (type)
        {
            case BuildingType::Headquarters: return "Headquarters";
            case BuildingType::Village: return "Village";
            case BuildingType::StorageBuilding: return "Storage";
            case BuildingType::Woodcutter: return "Woodcutter";
            case BuildingType::HuntersHut: return "Hunters Hut";
            case BuildingType::LumberMill: return "Lumber Mill";
            case BuildingType::Mine: return "Mine";
            case BuildingType::Foundry: return "Foundry";
            case BuildingType::Well: return "Well";
            case BuildingType::WheatFarm: return "Wheat Farm";
            case BuildingType::Windmill: return "Windmill";
            case BuildingType::Bakery: return "Bakery";
            case BuildingType::Inn: return "Inn";
            case BuildingType::Paperworks: return "Paperworks";
            case BuildingType::Smith: return "Smith";
            case BuildingType::Mint: return "Mint";
            case BuildingType::Glassworks: return "Glassworks";
            case BuildingType::Powderworks: return "Powderworks";
            case BuildingType::University: return "University";
            case BuildingType::Barracks: return "Barracks";
            case BuildingType::Road: return "Road";
            default: return "Building";
        }
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
        stream << (showAsRate ? ImprovedRateLabel(modifier.stat) : FocusStatLabel(modifier.stat));
        if (modifier.buildingType.has_value())
            stream << " for " << FocusBuildingLabel(modifier.buildingType.value());
        if (modifier.resourceType.has_value())
            stream << " producing " << rt2s(modifier.resourceType.value());
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

    // Display order of tree lanes; unknown lanes go last, alphabetically.
    int LaneRank(const std::string& lane)
    {
        if (lane == "PRODUCTION" || lane == "ECONOMY") return 0;
        if (lane == "MILITARY" || lane == "WARFARE") return 1;
        if (lane == "SOCIAL" || lane == "LOGISTICS") return 2;
        if (lane == "POLITICS" || lane == "GOVERNANCE") return 3;
        return 10;
    }
}

// ─── ResearchTreePanelWidget ─────────────────────────────────────────────────

Rectangle ResearchTreePanelWidget::GetTreeArea(Rectangle bounds) const
{
    float top = bounds.y + 104.0f;
    float bottom = bounds.y + bounds.height - 20.0f;
    if (kind == ResearchTreeKind::Focus)
    {
        // Reserve room for the state overview side panel on the right.
        float sideW = std::min(260.0f, std::max(218.0f, bounds.width * 0.22f));
        float sideGap = 18.0f;
        return Rectangle{bounds.x + 24.0f, top, bounds.width - sideW - sideGap - 48.0f, bottom - top};
    }
    return Rectangle{bounds.x + 24.0f, top, bounds.width - 48.0f, bottom - top};
}

void ResearchTreePanelWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* player = GuiLocalPlayer(scene);
    if (player == nullptr)
        return;

    const bool isFocus = kind == ResearchTreeKind::Focus;
    const char* panelTitle = isFocus ? "Political Focus Tree" : "Technology Research";

    Vector2 mouse = GetMousePosition();
    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    DrawRectangleRounded(bounds, 0.025f, 8, Color{26, 30, 37, 244});
    DrawRectangleRoundedLines(bounds, 0.025f, 8, 1.0f, Color{92, 102, 118, 255});
    Rectangle title{bounds.x, bounds.y, bounds.width, 52.0f};
    DrawRectangleRounded(title, 0.025f, 8, Color{42, 50, 62, 255});
    DrawCloseButton(bounds);

    bool debugMode = scene->game->GetTileMap().params.debugMode;
    if (debugMode)
    {
        Rectangle reloadBtn{bounds.x + bounds.width - 44.0f - 8.0f - 110.0f, bounds.y + 11.0f, 110.0f, 30.0f};
        bool reloadHov = CheckCollisionPointRec(mouse, reloadBtn);
        DrawRectangleRounded(reloadBtn, 0.18f, 8, reloadHov ? Color{80, 100, 60, 245} : Color{46, 60, 38, 230});
        DrawRectangleRoundedLines(reloadBtn, 0.18f, 8, 1.0f, reloadHov ? Color{160, 220, 100, 255} : Color{100, 148, 72, 230});
        UiText::DrawFit("[D] Reload", Rectangle{reloadBtn.x + 8.0f, reloadBtn.y + 4.0f, reloadBtn.width - 16.0f, reloadBtn.height - 8.0f}, 17, RAYWHITE);
        if (reloadHov && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (isFocus)
                ReloadFocusDefinitions();
            else
                ReloadTechnologyDefinitions();
            player->ResetResearchState();
        }
        UiText::DrawFit(std::string(panelTitle) + " [DEBUG]", Rectangle{title.x + 18.0f, title.y + 10.0f, title.width - 200.0f, 30.0f}, 26, RAYWHITE);
    }
    else
    {
        UiText::DrawFit(panelTitle, Rectangle{title.x + 18.0f, title.y + 10.0f, title.width - 86.0f, 30.0f}, 28, RAYWHITE);
    }

    auto nodes = isFocus ? ResearchCatalog::BuildFocusView(*player) : ResearchCatalog::BuildView(*player);
    std::map<std::string, ResearchNodeView*> byId;
    for (auto& node : nodes)
        byId[node.id] = &node;

    auto visibleTags = CollectVisibleTags(nodes);
    Rectangle tagBar{bounds.x + 24.0f, bounds.y + 62.0f, bounds.width - 48.0f, 30.0f};
    DrawTagFilterBar(tagBar, visibleTags, selectedTagFilter);

    Rectangle treeArea = GetTreeArea(bounds);

    if (CheckCollisionPointRec(mouse, treeArea) && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
    {
        panning = true;
        lastPanMouse = {mouse.x, mouse.y};
    }
    if (panning && InputManager::IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    {
        panOffset.x += mouse.x - lastPanMouse.x;
        panOffset.y += mouse.y - lastPanMouse.y;
        lastPanMouse = {mouse.x, mouse.y};
    }
    if (panning && InputManager::IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
        panning = false;

    // ── Layout: lanes → depth rows → per-row horizontal placement ────────────
    float nodeW = 128.0f * zoom;
    float nodeH = 122.0f * zoom;
    float colGap = 118.0f * zoom;
    float laneGap = 250.0f * zoom;
    float rowGap = 176.0f * zoom;
    float laneHeaderH = 38.0f * zoom;
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

            std::vector<float> desiredX(rowNodes.size(), laneX);
            float laneCenter = laneX + laneWidth * 0.5f;
            for (size_t i = 0; i < rowNodes.size(); i++)
            {
                float parentCenterSum = 0.0f;
                int parentCount = 0;
                for (const auto& prerequisite : rowNodes[i]->prerequisites)
                {
                    auto parentIt = nodeRects.find(prerequisite);
                    if (parentIt == nodeRects.end())
                        continue;
                    parentCenterSum += parentIt->second.x + parentIt->second.width * 0.5f;
                    parentCount++;
                }
                float rowOffset = (static_cast<float>(i) - (static_cast<float>(rowNodes.size()) - 1.0f) * 0.5f) * (nodeW + colGap * 1.35f);
                int orderWithinLayer = ((rowNodes[i]->layoutOrder % 1000) + 1000) % 1000;
                float orderNorm = static_cast<float>(orderWithinLayer) / 999.0f;
                float laneMargin = std::min(laneWidth * 0.28f, 230.0f * zoom);
                float orderTarget = laneX + laneMargin + orderNorm * std::max(0.0f, laneWidth - laneMargin * 2.0f - nodeW) + nodeW * 0.5f;
                if (parentCount > 0)
                {
                    float parentCenter = parentCenterSum / parentCount;
                    float orderInfluence = orderWithinLayer <= 80 || orderWithinLayer >= 920 ? 0.62f : 0.42f;
                    float spreadTarget = laneCenter + rowOffset;
                    float center = parentCenter * (1.0f - orderInfluence) + orderTarget * orderInfluence;
                    center = center * 0.72f + spreadTarget * 0.28f;
                    desiredX[i] = center - nodeW * 0.5f;
                }
                else
                {
                    desiredX[i] = laneCenter + rowOffset - nodeW * 0.5f;
                }
            }

            std::vector<size_t> order(rowNodes.size());
            for (size_t i = 0; i < order.size(); i++)
                order[i] = i;
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
            {
                return desiredX[a] < desiredX[b];
            });

            std::vector<float> placed(order.size(), laneX);
            for (size_t orderIndex = 0; orderIndex < order.size(); orderIndex++)
            {
                size_t i = order[orderIndex];
                placed[orderIndex] = std::clamp(desiredX[i], laneX, laneX + laneWidth - nodeW);
            }
            float minStep = nodeW + colGap;
            for (int pass = 0; pass < 2; pass++)
            {
                for (size_t i = 1; i < placed.size(); i++)
                    placed[i] = std::max(placed[i], placed[i - 1] + minStep);
                for (int i = static_cast<int>(placed.size()) - 2; i >= 0; i--)
                    placed[i] = std::min(placed[i], placed[i + 1] - minStep);
            }
            if (!placed.empty())
            {
                float rowMin = placed.front();
                float rowMax = placed.back();
                float rowCenter = (rowMin + rowMax + nodeW) * 0.5f;
                float targetCenter = laneCenter;
                for (float x : desiredX)
                    targetCenter += x + nodeW * 0.5f;
                targetCenter /= static_cast<float>(desiredX.size() + 1);
                float shift = std::clamp(targetCenter - rowCenter, laneX - rowMin, laneX + laneWidth - nodeW - rowMax);
                for (auto& x : placed)
                    x += shift;
            }

            for (size_t orderIndex = 0; orderIndex < order.size(); orderIndex++)
            {
                size_t i = order[orderIndex];
                Rectangle rect{
                    placed[orderIndex],
                    treeArea.y + panOffset.y + laneHeaderH + 28.0f + depth * (nodeH + rowGap) - scrollOffset,
                    nodeW,
                    nodeH};
                nodeRects[rowNodes[i]->id] = rect;
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

    // ── Draw: lane headers, prerequisite edges, nodes ─────────────────────────
    BeginScissorMode(static_cast<int>(treeArea.x), static_cast<int>(treeArea.y), static_cast<int>(treeArea.width), static_cast<int>(treeArea.height));
    for (const auto& [lane, header] : laneHeaders)
    {
        DrawRectangleRounded(header, 0.14f, 8, Color{36, 43, 54, 215});
        UiText::DrawFit(lane, Rectangle{header.x + 12.0f, header.y + 4.0f, header.width - 24.0f, header.height - 8.0f}, std::max(20, static_cast<int>(27 * zoom)), Color{208, 220, 238, 255});
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
            Color edgeColor = highlighted ? Color{232, 202, 104, 255} : Color{74, 86, 104, 150};
            float edgeWidth = highlighted ? 4.0f : 1.5f;
            Vector2 midA{parentAnchor.x, parentAnchor.y + rowGap * 0.34f};
            Vector2 midB{childAnchor.x, childAnchor.y - rowGap * 0.34f};
            DrawLineEx(parentAnchor, midA, edgeWidth, edgeColor);
            DrawLineEx(midA, midB, edgeWidth, edgeColor);
            DrawLineEx(midB, childAnchor, edgeWidth, edgeColor);
        }
    }

    for (const auto& node : nodes)
    {
        Rectangle rect = nodeRects[node.id];
        bool hover = CheckCollisionPointRec(mouse, rect);
        bool tagMatched = HasNodeTag(node, selectedTagFilter);
        Color fill = node.researched ? Color{45, 86, 63, 245}
                   : node.active ? Color{70, 65, 38, 245}
                   : node.available ? Color{47, 66, 88, 245}
                   : Color{34, 38, 46, 230};
        Color line = node.researched ? Color{87, 176, 113, 255}
                   : node.active ? Color{214, 178, 84, 255}
                   : node.available ? Color{94, 134, 188, 255}
                   : Color{78, 86, 100, 220};
        if (!selectedTagFilter.empty() && !tagMatched)
        {
            fill.a = 110;
            line.a = 120;
        }
        DrawRectangleRounded(rect, 0.06f, 8, fill);
        DrawRectangleRoundedLines(rect, 0.06f, 8, 1.0f, tagMatched && !selectedTagFilter.empty() ? Color{112, 208, 172, 255} : highlightedPath.contains(node.id) ? Color{232, 202, 104, 255} : (hover ? Color{190, 215, 255, 255} : line));
        DrawUiTextWrappedCentered(node.name, Rectangle{rect.x + 10.0f * zoom, rect.y + 7.0f * zoom, rect.width - 20.0f * zoom, 45.0f * zoom}, std::max(15, static_cast<int>(24 * zoom)), RAYWHITE, 2);
        UiText::DrawFit(node.stateText, Rectangle{rect.x + 12.0f * zoom, rect.y + 53.0f * zoom, rect.width - 24.0f * zoom, 21.0f * zoom}, std::max(11, static_cast<int>(18 * zoom)),
            node.researched ? Color{145, 230, 160, 255} : node.available ? Color{190, 215, 255, 255} : Color{180, 186, 196, 255});

        // Focus nodes can promote the state class; draw the government chip and
        // shift the time row below it. Tech nodes have no government unlock.
        float timeTextY = 76.0f;
        if (isFocus)
        {
            timeTextY = 94.0f;
            if (const StateDevelopmentDefinition* unlockedState = StateDevelopment::FindDefinition(node.governmentId))
            {
                Color governmentFill = unlockedState->color;
                governmentFill.a = 210;
                DrawRectangleRounded(Rectangle{rect.x + 12.0f * zoom, rect.y + 76.0f * zoom, rect.width - 24.0f * zoom, 22.0f * zoom}, 0.16f, 6, governmentFill);
                UiText::DrawFit("State class: " + unlockedState->name, Rectangle{rect.x + 20.0f * zoom, rect.y + 78.0f * zoom, rect.width - 40.0f * zoom, 18.0f * zoom}, std::max(10, static_cast<int>(16 * zoom)), Color{205, 224, 255, 255});
            }
        }
        std::string timeText = node.active ? FormatDuration(node.remainingTime) + " left" : FormatDuration(node.researchTime);
        UiText::DrawFit(timeText, Rectangle{rect.x + 12.0f * zoom, rect.y + timeTextY * zoom, rect.width - 24.0f * zoom, 16.0f * zoom}, std::max(10, static_cast<int>(15 * zoom)), Color{180, 190, 205, 255});
        if (node.active || node.researched)
        {
            Rectangle progress{rect.x + 12.0f, rect.y + rect.height - 11.0f, rect.width - 24.0f, 5.0f};
            DrawRectangleRounded(progress, 0.5f, 4, Color{17, 20, 25, 230});
            Rectangle fillBar = progress;
            fillBar.width *= static_cast<float>(std::clamp(node.progress, 0.0, 1.0));
            DrawRectangleRounded(fillBar, 0.5f, 4, node.researched ? Color{95, 190, 116, 255} : Color{214, 178, 84, 255});
        }
        if (hover && node.available && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (isFocus)
            {
                scene->SubmitLocalCommand(GameCommand::StartFocus(scene->game->GetLocalPlayerId(), node.id));
            }
            else if (Building* university = FindIdleUniversity(scene))
            {
                scene->SubmitLocalCommand(GameCommand::StartTechnologyResearch(scene->game->GetLocalPlayerId(), node.id, university->positionId));
            }
        }
    }
    EndScissorMode();

    if (maxScrollOffset > 0.0f)
    {
        Rectangle track{treeArea.x + treeArea.width + 6.0f, treeArea.y, 5.0f, treeArea.height};
        DrawRectangleRounded(track, 0.5f, 4, Color{18, 22, 28, 190});
        float thumbH = std::max(32.0f, track.height * (track.height / (track.height + maxScrollOffset)));
        float thumbY = track.y + (track.height - thumbH) * (scrollOffset / maxScrollOffset);
        DrawRectangleRounded(Rectangle{track.x, thumbY, track.width, thumbH}, 0.5f, 4, Color{116, 132, 154, 230});
    }

    // ── Focus-only state overview side panel ──────────────────────────────────
    Rectangle stateClassRow{};
    const StateDevelopmentDefinition* stateDefinition = nullptr;
    if (isFocus)
    {
        float sideW = std::min(260.0f, std::max(218.0f, bounds.width * 0.22f));
        Rectangle statePanel{treeArea.x + treeArea.width + 18.0f, treeArea.y, sideW, treeArea.height};

        DrawRectangleRounded(statePanel, 0.04f, 8, Color{31, 36, 44, 236});
        DrawRectangleRoundedLines(statePanel, 0.04f, 8, 1.0f, Color{88, 100, 118, 235});
        UiText::DrawFit("State Overview", Rectangle{statePanel.x + 14.0f, statePanel.y + 14.0f, statePanel.width - 28.0f, 26.0f}, 23, RAYWHITE);
        stateDefinition = &player->stateDevelopment.GetDefinition();
        std::vector<std::pair<std::string, std::string>> placeholders{
            {"Class", stateDefinition->name},
            {"Administration", "TBD"},
            {"Stability", "TBD"},
            {"Legitimacy", "TBD"},
            {"War Support", "TBD"},
            {"Treasury", "TBD"}};
        float rowY = statePanel.y + 54.0f;
        for (const auto& [label, value] : placeholders)
        {
            Rectangle row{statePanel.x + 14.0f, rowY, statePanel.width - 28.0f, 30.0f};
            Color rowFill = Color{24, 29, 36, 225};
            if (label == "Class")
            {
                rowFill = stateDefinition->color;
                rowFill.a = 210;
            }
            DrawRectangleRounded(row, 0.08f, 6, rowFill);
            if (label == "Class")
            {
                stateClassRow = row;
                UiText::DrawFit(label, Rectangle{row.x + 10.0f, row.y + 5.0f, 52.0f, 20.0f}, 17, Color{184, 196, 214, 255});
                UiText::DrawFit(value, Rectangle{row.x + 70.0f, row.y + 5.0f, row.width - 80.0f, 20.0f}, 17, RAYWHITE);
            }
            else
            {
                UiText::DrawFit(label, Rectangle{row.x + 10.0f, row.y + 5.0f, row.width * 0.62f, 20.0f}, 17, Color{184, 196, 214, 255});
                UiText::DrawFit(value, Rectangle{row.x + row.width - 62.0f, row.y + 5.0f, 52.0f, 20.0f}, 17, Color{138, 151, 170, 255});
            }
            rowY += 38.0f;
        }
        Rectangle stateDescription{
            statePanel.x + 14.0f,
            rowY + 4.0f,
            statePanel.width - 28.0f,
            66.0f};
        DrawRectangleRounded(stateDescription, 0.08f, 6, Color{24, 29, 36, 205});
        UiText::DrawFit(stateDefinition->description,
            Rectangle{stateDescription.x + 10.0f, stateDescription.y + 8.0f, stateDescription.width - 20.0f, stateDescription.height - 16.0f},
            16,
            Color{172, 184, 202, 255});
        const std::string& activeId = player->focuses.GetActiveFocusId();
        const TechnologyDefinition* activeFocus = activeId.empty() ? nullptr : FindFocusDefinition(activeId);
        Rectangle activeBox{statePanel.x + 14.0f, statePanel.y + statePanel.height - 86.0f, statePanel.width - 28.0f, 68.0f};
        DrawRectangleRounded(activeBox, 0.08f, 6, Color{25, 31, 39, 230});
        UiText::DrawFit("Active Focus", Rectangle{activeBox.x + 10.0f, activeBox.y + 8.0f, activeBox.width - 20.0f, 20.0f}, 17, Color{184, 196, 214, 255});
        UiText::DrawFit(activeFocus != nullptr ? activeFocus->name : "None", Rectangle{activeBox.x + 10.0f, activeBox.y + 32.0f, activeBox.width - 20.0f, 24.0f}, 20, activeFocus != nullptr ? RAYWHITE : Color{138, 151, 170, 255});
    }

    if (isFocus && stateDefinition != nullptr && CheckCollisionPointRec(mouse, stateClassRow))
    {
        std::vector<std::string> lines{stateDefinition->description};
        lines.push_back(TooltipSeparatorLine());
        if (stateDefinition->modifiers.empty())
        {
            lines.push_back("No fixed effects yet");
        }
        else
        {
            for (const auto& modifier : stateDefinition->modifiers)
                lines.push_back(FormatModifierForFocusTooltip(modifier));
        }
        Tooltip::Draw(stateDefinition->name, lines, 460.0f);
    }

    if (hovered != nullptr)
    {
        std::vector<std::string> lines{hovered->description};
        lines.push_back(TooltipSeparatorLine());
        lines.push_back("Time: " + FormatDuration(hovered->researchTime) + " | " + hovered->stateText);
        if (hovered->active)
            lines.push_back("Remaining: " + FormatDuration(hovered->remainingTime));
        if (isFocus)
        {
            lines.push_back(TooltipSeparatorLine());
            if (const auto* unlockedState = StateDevelopment::FindDefinition(hovered->governmentId))
                lines.push_back("State class change: " + unlockedState->name);
        }
        else if (!hovered->available && !hovered->researched && !hovered->active)
        {
            lines.push_back(TooltipSeparatorLine());
            lines.push_back("Prerequisites not met");
        }
        for (const auto& modifier : hovered->modifiers)
            lines.push_back(FormatModifierForFocusTooltip(modifier));
        Tooltip::Draw(hovered->name, lines, 460.0f);
    }
}

void ResearchTreePanelWidget::AdjustTreeZoom(Vec2i point, float wheel)
{
    if (wheel == 0.0f)
        return;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    Rectangle treeArea = GetTreeArea(bounds);
    Vector2 mouse{static_cast<float>(point.x), static_cast<float>(point.y)};
    if (!CheckCollisionPointRec(mouse, treeArea))
        return;

    float oldZoom = zoom;
    float newZoom = std::clamp(zoom + wheel * 0.08f, 0.42f, 1.15f);
    if (std::abs(newZoom - oldZoom) < 0.001f)
        return;

    float localX = mouse.x - treeArea.x;
    float localY = mouse.y - treeArea.y;
    panOffset.x = localX - (localX - panOffset.x) * (newZoom / oldZoom);
    panOffset.y = localY - (localY - panOffset.y) * (newZoom / oldZoom);
    zoom = newZoom;
}

// ─── FocusGuiSystem ──────────────────────────────────────────────────────────

FocusGuiSystem::FocusGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    scene = owner->scene;

    WireCommonSystemActions(*this, cameraMovement);

    focusPanel.scene = scene;
    focusPanel.ChangePositionAnchor({0.06f, 0.10f});
    focusPanel.ChangeSizeAnchor({0.88f, 0.82f});
    focusPanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    SetupStrategicHud(strategicHudWidget, scene);
}

void FocusGuiSystem::UpdateUiWidgets(Vec2i size)
{
    focusPanel.UpdateSize(size);
    strategicHudWidget.UpdateSize(size);
}

void FocusGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    owner->AddUiWidget(&focusPanel);
    owner->AddUiWidget(&strategicHudWidget);
}

void FocusGuiSystem::EscPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}

void FocusGuiSystem::BuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("build");
}

void FocusGuiSystem::RoadBuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("road_build");
}

void FocusGuiSystem::DestroyPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("destroy");
}

void FocusGuiSystem::HeadquartersPressed()
{
    cameraMovement.isMoving = false;
    SwitchToMapViewAndOpenHeadquarters(owner);
}

void FocusGuiSystem::StatsPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stats");
}

void FocusGuiSystem::FocusPressed()
{
    EscPressed();
}

void FocusGuiSystem::TechPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("tech");
}

void FocusGuiSystem::RosterPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("roster");
}

void FocusGuiSystem::LmbPressed()
{
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    Vector2 mouse = GetMousePosition();
    Rectangle panelBounds{
        static_cast<float>(focusPanel.pos.x),
        static_cast<float>(focusPanel.pos.y),
        static_cast<float>(focusPanel.size.x),
        static_cast<float>(focusPanel.size.y)};
    if (CheckCollisionPointRec(mouse, PanelCloseButtonRect(panelBounds)))
    {
        EscPressed();
        return;
    }
}

void FocusGuiSystem::LmbReleased()
{
}

void FocusGuiSystem::RmbPressed()
{
    Vector2 mouse = GetMousePosition();
    if (focusPanel.ContainsPoint(Vec2i{static_cast<int>(mouse.x), static_cast<int>(mouse.y)}))
    {
        cameraMovement.isMoving = false;
        return;
    }
    cameraMovement.isMoving = true;
}

void FocusGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

void FocusGuiSystem::Scroll()
{
    Vector2 mouse = GetMousePosition();
    Vec2i point{static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
    if (focusPanel.ContainsPoint(point))
    {
        if (InputManager::IsKeyDown(KEY_LEFT_CONTROL) || InputManager::IsKeyDown(KEY_RIGHT_CONTROL))
        {
            focusPanel.scrollOffset = std::clamp(focusPanel.scrollOffset - InputManager::GetMouseWheelMove() * 48.0f, 0.0f, focusPanel.maxScrollOffset);
            return;
        }
        focusPanel.AdjustTreeZoom(point, InputManager::GetMouseWheelMove());
        return;
    }
    ZoomCamera(scene);
}

// ─── TechGuiSystem ───────────────────────────────────────────────────────────

TechGuiSystem::TechGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    scene = owner->scene;

    WireCommonSystemActions(*this, cameraMovement);

    techPanel.scene = scene;
    techPanel.ChangePositionAnchor({0.06f, 0.10f});
    techPanel.ChangeSizeAnchor({0.88f, 0.82f});
    techPanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    SetupStrategicHud(strategicHudWidget, scene);
}

void TechGuiSystem::UpdateUiWidgets(Vec2i windowSize)
{
    techPanel.UpdateSize(windowSize);
    strategicHudWidget.UpdateSize(windowSize);
}

void TechGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    owner->AddUiWidget(&techPanel);
    owner->AddUiWidget(&strategicHudWidget);
}

void TechGuiSystem::EscPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}

void TechGuiSystem::BuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("build");
}

void TechGuiSystem::RoadBuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("road_build");
}

void TechGuiSystem::DestroyPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("destroy");
}

void TechGuiSystem::HeadquartersPressed()
{
    cameraMovement.isMoving = false;
    SwitchToMapViewAndOpenHeadquarters(owner);
}

void TechGuiSystem::StatsPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stats");
}

void TechGuiSystem::FocusPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("focus");
}

void TechGuiSystem::TechPressed()
{
    EscPressed();
}

void TechGuiSystem::RosterPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("roster");
}

void TechGuiSystem::LmbPressed()
{
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    Vector2 mouse = GetMousePosition();
    Rectangle panelBounds{
        static_cast<float>(techPanel.pos.x),
        static_cast<float>(techPanel.pos.y),
        static_cast<float>(techPanel.size.x),
        static_cast<float>(techPanel.size.y)};
    if (CheckCollisionPointRec(mouse, PanelCloseButtonRect(panelBounds)))
    {
        EscPressed();
        return;
    }
}

void TechGuiSystem::LmbReleased()
{
}

void TechGuiSystem::RmbPressed()
{
    Vector2 mouse = GetMousePosition();
    if (techPanel.ContainsPoint(Vec2i{static_cast<int>(mouse.x), static_cast<int>(mouse.y)}))
    {
        cameraMovement.isMoving = false;
        return;
    }
    cameraMovement.isMoving = true;
}

void TechGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

void TechGuiSystem::Scroll()
{
    Vector2 mouse = GetMousePosition();
    Vec2i point{static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
    if (techPanel.ContainsPoint(point))
    {
        if (InputManager::IsKeyDown(KEY_LEFT_CONTROL) || InputManager::IsKeyDown(KEY_RIGHT_CONTROL))
        {
            techPanel.scrollOffset = std::clamp(techPanel.scrollOffset - InputManager::GetMouseWheelMove() * 48.0f, 0.0f, techPanel.maxScrollOffset);
            return;
        }
        techPanel.AdjustTreeZoom(point, InputManager::GetMouseWheelMove());
        return;
    }
    ZoomCamera(scene);
}
