#include "ui/Gui.h"
#include "ui/UiTheme.h"
#include "economy/Building.h"
#include "economy/BuildingConfig.h"
#include "economy/Player.h"
#include "research/ResearchCatalog.h"
#include "research/Technology.h"
#include "warfare/UnitDefinition.h"
#include "core/GameCommand.h"
#include "scenes/Scenes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

#include "raylib.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

namespace
{
    ResourceIconAtlas resourceIconAtlas;
    Font uiFont{};
    bool uiFontLoaded{false};

    void DrawTextFit(const std::string& text, Rectangle bounds, int fontSize, Color color);

    struct PendingTooltip
    {
        bool visible{false};
        std::string title;
        std::vector<std::string> lines;
        float preferredWidth{0.0f};
    };

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
            DrawTextFit(label, Rectangle{rect.x + 8.0f, rect.y + 4.0f, rect.width - 16.0f, rect.height - 8.0f}, 14, selected ? UiTheme::Parchment : UiTheme::ParchmentDim);
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

    PendingTooltip pendingTooltip;

    // Returns the current rectangle occupied by a UI widget.
    Rectangle WidgetBounds(const UiWidget& widget)
    {
        return Rectangle{
            static_cast<float>(widget.pos.x),
            static_cast<float>(widget.pos.y),
            static_cast<float>(widget.size.x),
            static_cast<float>(widget.size.y)};
    }

    // Measures text using the configured UI font when available.
    int MeasureUiText(const std::string& text, int fontSize)
    {
        if (!uiFontLoaded)
            return MeasureText(text.c_str(), fontSize);

        return static_cast<int>(std::ceil(MeasureTextEx(uiFont, text.c_str(), static_cast<float>(fontSize), 0.0f).x));
    }

    // Draws text using the configured UI font when available.
    void DrawUiText(const std::string& text, float x, float y, int fontSize, Color color)
    {
        if (uiFontLoaded)
            DrawTextEx(uiFont, text.c_str(), {x, y}, static_cast<float>(fontSize), 0.0f, color);
        else
            DrawText(text.c_str(), static_cast<int>(x), static_cast<int>(y), fontSize, color);
    }

    // Returns a fallback swatch color for a resource type.
    Color ResourceColor(ResourceType type)
    {
        switch (type)
        {
            case ResourceType::COPPER_ORE: return Color{148, 92, 62, 255};
            case ResourceType::COPPER: return Color{191, 111, 68, 255};
            case ResourceType::WOOD: return Color{126, 87, 54, 255};
            case ResourceType::IRON_ORE: return Color{122, 126, 133, 255};
            case ResourceType::SILVER_ORE: return Color{156, 166, 174, 255};
            case ResourceType::SILVER: return Color{205, 214, 220, 255};
            case ResourceType::GOLD_ORE: return Color{160, 132, 60, 255};
            case ResourceType::GOLD: return Color{228, 181, 65, 255};
            case ResourceType::COAL: return Color{42, 43, 45, 255};
            case ResourceType::IRON: return Color{175, 176, 180, 255};
            case ResourceType::PLANKS: return Color{190, 139, 78, 255};
            case ResourceType::STONE: return Color{126, 124, 116, 255};
            case ResourceType::LEATHER: return Color{128, 75, 45, 255};
            case ResourceType::MEAT: return Color{176, 77, 69, 255};
            case ResourceType::WHEAT: return Color{214, 179, 83, 255};
            case ResourceType::FLOUR: return Color{224, 214, 184, 255};
            case ResourceType::BREAD: return Color{180, 120, 55, 255};
            case ResourceType::WATER: return Color{75, 146, 214, 255};
            case ResourceType::BEER: return Color{184, 128, 48, 255};
            case ResourceType::COINS: return Color{230, 190, 82, 255};
            case ResourceType::FOOD_PROVISIONS: return Color{113, 162, 92, 255};
            case ResourceType::PAPER: return Color{213, 211, 190, 255};
            case ResourceType::TOOLS: return Color{120, 136, 145, 255};
            case ResourceType::COPPER_SWORD: return Color{178, 105, 70, 255};
            case ResourceType::IRON_SWORD: return Color{153, 160, 170, 255};
            case ResourceType::STEEL_SWORD: return Color{185, 198, 205, 255};
            case ResourceType::BOW: return Color{137, 91, 48, 255};
            case ResourceType::ARROWS: return Color{174, 144, 92, 255};
            case ResourceType::HORSE: return Color{130, 91, 67, 255};
            default: return Color{88, 92, 98, 255};
        }
    }

    // Returns a short fallback label for a resource icon.
    const char* ResourceShortName(ResourceType type)
    {
        switch (type)
        {
            case ResourceType::COPPER_ORE: return "CuO";
            case ResourceType::COPPER: return "Cu";
            case ResourceType::WOOD: return "W";
            case ResourceType::IRON_ORE: return "Ore";
            case ResourceType::SILVER_ORE: return "AgO";
            case ResourceType::SILVER: return "Ag";
            case ResourceType::GOLD_ORE: return "AuO";
            case ResourceType::GOLD: return "Au";
            case ResourceType::COAL: return "C";
            case ResourceType::IRON: return "Fe";
            case ResourceType::PLANKS: return "Pl";
            case ResourceType::STONE: return "St";
            case ResourceType::LEATHER: return "Le";
            case ResourceType::MEAT: return "Mt";
            case ResourceType::WHEAT: return "Wh";
            case ResourceType::FLOUR: return "Fl";
            case ResourceType::BREAD: return "Br";
            case ResourceType::WATER: return "Wa";
            case ResourceType::BEER: return "Be";
            case ResourceType::COINS: return "$";
            case ResourceType::FOOD_PROVISIONS: return "Fd";
            case ResourceType::PAPER: return "Pa";
            case ResourceType::TOOLS: return "Tl";
            case ResourceType::COPPER_SWORD: return "CuS";
            case ResourceType::IRON_SWORD: return "Sw";
            case ResourceType::STEEL_SWORD: return "StS";
            case ResourceType::BOW: return "Bw";
            case ResourceType::ARROWS: return "Arr";
            case ResourceType::HORSE: return "Ho";
            default: return "?";
        }
    }

    // Draws text that shrinks until it fits inside the target rectangle.
    void DrawTextFit(const std::string& text, Rectangle bounds, int fontSize, Color color)
    {
        int measured = MeasureUiText(text, fontSize);
        while (fontSize > 8 && measured > bounds.width)
        {
            fontSize--;
            measured = MeasureUiText(text, fontSize);
        }

        DrawUiText(text,
            bounds.x + (bounds.width - measured) * 0.5f,
            bounds.y + (bounds.height - fontSize) * 0.5f,
            fontSize,
            color);
    }

    void RemoveLastUtf8Codepoint(std::string& value)
    {
        if (value.empty())
            return;

        size_t index = value.size() - 1;
        while (index > 0 && (static_cast<unsigned char>(value[index]) & 0xC0) == 0x80)
            index--;
        value.erase(index);
    }

    std::string EncodeUtf8Codepoint(int codepoint)
    {
        std::string encoded;
        if (codepoint <= 0x7F)
        {
            encoded.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint <= 0x7FF)
        {
            encoded.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
            encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else if (codepoint <= 0xFFFF)
        {
            encoded.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
            encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else if (codepoint <= 0x10FFFF)
        {
            encoded.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
            encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        return encoded;
    }

    void SyncTextBoxBuffer(const std::string& value, char* buffer, size_t bufferSize)
    {
        if (bufferSize == 0)
            return;

        std::snprintf(buffer, bufferSize, "%s", value.c_str());
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

    // Wraps prose to fit a fixed width while keeping the requested font size.
    std::vector<std::string> WrapText(const std::string& text, int fontSize, float maxWidth)
    {
        std::vector<std::string> wrapped;
        std::istringstream words(text);
        std::string word;
        std::string line;

        while (words >> word)
        {
            std::string candidate = line.empty() ? word : line + " " + word;
            if (MeasureUiText(candidate, fontSize) <= maxWidth || line.empty())
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

    // Draws centered wrapped text without shrinking the requested font size.
    void DrawTextWrappedCentered(const std::string& text, Rectangle bounds, int fontSize, Color color, int maxLines = 2)
    {
        std::vector<std::string> lines = WrapText(text, fontSize, bounds.width);
        if (static_cast<int>(lines.size()) > maxLines)
        {
            lines.resize(maxLines);
            while (!lines.back().empty() && MeasureUiText(lines.back() + "...", fontSize) > bounds.width)
                lines.back().pop_back();
            lines.back() += "...";
        }

        float lineH = static_cast<float>(fontSize) + 2.0f;
        float totalH = lineH * static_cast<float>(lines.size());
        float y = bounds.y + (bounds.height - totalH) * 0.5f;
        for (const auto& line : lines)
        {
            int measured = MeasureUiText(line, fontSize);
            DrawUiText(line, bounds.x + (bounds.width - measured) * 0.5f, y, fontSize, color);
            y += lineH;
        }
    }

    // Queues a tooltip for the final overlay pass.
    void QueueTooltip(const std::string& title, std::vector<std::string> lines, float preferredWidth = 0.0f)
    {
        pendingTooltip.visible = true;
        pendingTooltip.title = title;
        pendingTooltip.lines = std::move(lines);
        pendingTooltip.preferredWidth = preferredWidth;
    }

    // Draws the queued tooltip above all panel content.
    void DrawPendingTooltip()
    {
        if (!pendingTooltip.visible)
            return;

        Tooltip::Draw(pendingTooltip.title, pendingTooltip.lines, pendingTooltip.preferredWidth);
    }

    // Draws one resource buffer as a wide card with amount and capacity.
    void DrawResourceCard(const ResourceBufferView& view, Rectangle bounds)
    {
        DrawRectangleRounded(bounds, 0.08f, 6, Color{46, 34, 24, 230});
        DrawRectangleRoundedLines(bounds, 0.08f, 6, 1.0f, Color{112, 88, 58, 255});

        float iconSize = std::min(bounds.width * 0.38f, bounds.height * 0.64f);
        Rectangle icon{
            bounds.x + 8.0f,
            bounds.y + (bounds.height - iconSize) * 0.5f,
            iconSize,
            iconSize};

        if (resourceIconAtlas.IsLoaded())
        {
            Rectangle src = resourceIconAtlas.GetRect(view.type);
            DrawTexturePro(resourceIconAtlas.texture, src, icon, {0.0f, 0.0f}, 0.0f, WHITE);
        }
        else
        {
            DrawRectangleRounded(icon, 0.16f, 6, ResourceColor(view.type));
            DrawTextFit(ResourceShortName(view.type), {icon.x + 5.0f, icon.y + icon.height * 0.32f, icon.width - 10.0f, 20.0f}, 18, WHITE);
        }

        std::string amount = std::to_string(view.amount) + "/" + std::to_string(view.capacity);
        DrawTextFit(amount, {bounds.x + iconSize + 18.0f, bounds.y + 8.0f, bounds.width - iconSize - 26.0f, 20.0f}, 16, UiTheme::Parchment);

        if (view.recipeAmount > 0)
        {
            std::string recipe = "x" + std::to_string(view.recipeAmount);
            int fontSize = 14;
            int textWidth = UiText::Measure(recipe, fontSize);
            DrawRectangleRounded({icon.x + icon.width - textWidth - 8.0f, icon.y + icon.height - 18.0f, static_cast<float>(textWidth + 8), 18.0f}, 0.25f, 4, Color{24, 17, 12, 230});
            UiText::Draw(recipe, icon.x + icon.width - textWidth - 4.0f, icon.y + icon.height - 16.0f, fontSize, UiTheme::Parchment);
        }
    }

    // Draws one compact resource icon with an amount badge.
    void DrawResourceIcon(const ResourceBufferView& view, Rectangle bounds)
    {
        DrawRectangleRounded(bounds, 0.10f, 8, Color{40, 29, 21, 230});
        DrawRectangleRoundedLines(bounds, 0.10f, 8, 1.0f, Color{112, 88, 58, 255});

        float padding = std::max(6.0f, bounds.width * 0.12f);
        Rectangle icon{
            bounds.x + padding,
            bounds.y + padding,
            bounds.width - padding * 2.0f,
            bounds.height - padding * 2.0f};

        if (resourceIconAtlas.IsLoaded())
        {
            Rectangle src = resourceIconAtlas.GetRect(view.type);
            DrawTexturePro(resourceIconAtlas.texture, src, icon, {0.0f, 0.0f}, 0.0f, WHITE);
        }
        else
        {
            DrawRectangleRounded(icon, 0.16f, 6, ResourceColor(view.type));
            DrawTextFit(ResourceShortName(view.type), {icon.x + 5.0f, icon.y + icon.height * 0.34f, icon.width - 10.0f, 20.0f}, 18, WHITE);
        }

        std::string amount = view.capacity > 0
            ? std::to_string(view.amount) + "/" + std::to_string(view.capacity)
            : std::to_string(view.amount);
        int fontSize = std::max(10, static_cast<int>(bounds.height * 0.16f));
        int textWidth = MeasureUiText(amount, fontSize);
        Rectangle badge{
            bounds.x + bounds.width - textWidth - 12.0f,
            bounds.y + bounds.height - fontSize - 8.0f,
            static_cast<float>(textWidth + 8),
            static_cast<float>(fontSize + 4)};

        DrawRectangleRounded(badge, 0.25f, 6, Color{18, 13, 10, 230});
        DrawUiText(amount, badge.x + 4.0f, badge.y + 2.0f, fontSize, UiTheme::Parchment);

        if (view.recipeAmount > 0)
        {
            std::string recipe = "x" + std::to_string(view.recipeAmount);
            int recipeFont = std::max(10, static_cast<int>(bounds.height * 0.15f));
            int recipeWidth = MeasureUiText(recipe, recipeFont);
            Rectangle recipeBadge{
                bounds.x + 5.0f,
                bounds.y + 5.0f,
                static_cast<float>(recipeWidth + 8),
                static_cast<float>(recipeFont + 4)};
            DrawRectangleRounded(recipeBadge, 0.25f, 6, Color{24, 17, 12, 232});
            DrawUiText(recipe, recipeBadge.x + 4.0f, recipeBadge.y + 2.0f, recipeFont, UiTheme::Parchment);
        }
    }

    // Draws resource cards in a fixed-column grid.
    void DrawResourceGrid(const std::vector<ResourceBufferView>& views, Rectangle bounds, int columns)
    {
        if (views.empty())
        {
            DrawTextFit("Empty", {bounds.x, bounds.y + 6.0f, bounds.width, 20.0f}, 16, UiTheme::ParchmentDim);
            return;
        }

        columns = std::max(1, columns);
        int rows = static_cast<int>((views.size() + columns - 1) / columns);
        float gap = 6.0f;
        float cardW = (bounds.width - gap * (columns - 1)) / columns;
        float cardH = std::max(34.0f, (bounds.height - gap * (rows - 1)) / std::max(1, rows));

        for (int i = 0; i < views.size(); i++)
        {
            int col = i % columns;
            int row = i / columns;
            Rectangle card{
                bounds.x + col * (cardW + gap),
                bounds.y + row * (cardH + gap),
                cardW,
                std::min(cardH, 58.0f)};
            DrawResourceCard(views[i], card);
        }
    }

    // Draws compact resource icons in a fixed-column grid.
    void DrawResourceIconGrid(const std::vector<ResourceBufferView>& views, Rectangle bounds, int columns, float scrollOffset = 0.0f, float* maxScrollOffset = nullptr)
    {
        if (maxScrollOffset != nullptr)
            *maxScrollOffset = 0.0f;

        if (views.empty())
        {
            DrawTextFit("Empty storage", {bounds.x, bounds.y + 6.0f, bounds.width, 22.0f}, 17, UiTheme::ParchmentDim);
            return;
        }

        columns = std::max(1, columns);
        if (views.size() > 16)
            columns = std::max(columns, 5);
        if (views.size() > 25)
            columns = std::max(columns, 6);
        float gap = 8.0f;
        float cellW = (bounds.width - gap * (columns - 1)) / columns;
        int rows = static_cast<int>((views.size() + columns - 1) / columns);
        float cellH = std::min(cellW, (bounds.height - gap * std::max(0, rows - 1)) / std::max(1, rows));
        cellH = std::max(28.0f, cellH);
        cellW = std::min(cellW, cellH);
        float contentHeight = rows * cellH + std::max(0, rows - 1) * gap;
        if (maxScrollOffset != nullptr)
            *maxScrollOffset = std::max(0.0f, contentHeight - bounds.height);
        int hoveredIndex = -1;

        BeginScissorMode(static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.width), static_cast<int>(bounds.height));
        for (int i = 0; i < views.size(); i++)
        {
            int col = i % columns;
            int row = i / columns;
            float x = bounds.x + col * (cellW + gap);
            float y = bounds.y + row * (cellH + gap) - scrollOffset;

            if (y > bounds.y + bounds.height)
                break;
            if (y + cellH < bounds.y)
                continue;

            Rectangle cell{x, y, cellW, cellH};
            DrawResourceIcon(views[i], cell);
            if (CheckCollisionPointRec(GetMousePosition(), cell))
                hoveredIndex = i;
        }
        EndScissorMode();

        if (hoveredIndex >= 0)
        {
            const auto& view = views[hoveredIndex];
            QueueTooltip(rt2s(view.type), {"Amount: " + std::to_string(view.amount)});
        }
    }

    // Returns remaining terrain richness under a building footprint.
    int GetLocalTerrainRichness(const Building* building)
    {
        const auto* production = building != nullptr ? building->GetComponent<ProductionComponent>() : nullptr;
        if (building == nullptr || production == nullptr || building->owner == nullptr ||
            production->terrainType == TileType::GRASS || !production->ingredients.empty())
            return -1;

        const TileMap& tilemap = building->owner->tilemap;
        Vec2i anchor = tilemap.GetCoordsFromId(building->positionId);
        Vec2i footprint = building->GetFootprint();
        int richness = 0;
        for (int y = 0; y < footprint.y; y++)
        {
            for (int x = 0; x < footprint.x; x++)
            {
                Vec2i pos{anchor.x + x, anchor.y + y};
                if (!tilemap.IsInside(pos))
                    continue;

                const Tile& tile = tilemap.tilemap[tilemap.GetIdFromCoords(pos)];
                if (tile.tileType == production->terrainType)
                    richness += tile.resourceRichness;
            }
        }
        return richness;
    }

    // Returns a readable label for one modifier stat.
    const char* BalanceStatLabel(BalanceStat stat)
    {
        switch (stat)
        {
            case BalanceStat::BuildTime: return "Build time";
            case BalanceStat::BuildCost: return "Build cost";
            case BalanceStat::ProductionCycleTime: return "Cycle time";
            case BalanceStat::ProductionOutputAmount: return "Output";
            case BalanceStat::WorkerCapacity: return "Workers";
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
        if (std::abs(modifier.additive) > 0.0001)
            return lowerIsBetter ? modifier.additive < 0.0 : modifier.additive > 0.0;
        if (std::abs(modifier.multiplier - 1.0) > 0.0001)
            return lowerIsBetter ? modifier.multiplier < 1.0 : modifier.multiplier > 1.0;
        return true;
    }

    const char* ImprovedRateLabel(BalanceStat stat)
    {
        switch (stat)
        {
            case BalanceStat::BuildTime: return "Build speed";
            case BalanceStat::ProductionCycleTime: return "Production speed";
            case BalanceStat::TransportTime: return "Transport speed";
            default: return BalanceStatLabel(stat);
        }
    }

    // Returns a readable label for one building type.
    const char* BuildingTypeLabel(BuildingType type)
    {
        switch (type)
        {
            case BuildingType::Headquarters: return "HQ";
            case BuildingType::Village: return "Village";
            case BuildingType::StorageBuilding: return "Storage";
            case BuildingType::Woodcutter: return "Woodcutter";
            case BuildingType::HuntersHut: return "Hunter";
            case BuildingType::LumberMill: return "Lumber mill";
            case BuildingType::Mine: return "Mine";
            case BuildingType::Foundry: return "Foundry";
            case BuildingType::Well: return "Well";
            case BuildingType::WheatFarm: return "Wheat farm";
            case BuildingType::Windmill: return "Windmill";
            case BuildingType::Bakery: return "Bakery";
            case BuildingType::Inn: return "Inn";
            case BuildingType::Paperworks: return "Paperworks";
            case BuildingType::Smith: return "Smith";
            case BuildingType::University: return "University";
            case BuildingType::Barracks: return "Barracks";
            case BuildingType::Road: return "Road";
            case BuildingType::Bridge: return "Bridge";
            default: return "Building";
        }
    }

    // Formats one technology modifier for tooltip display.
    std::string FormatTechnologyEffect(const BalanceModifier& modifier)
    {
        std::string text;
        if (modifier.buildingType.has_value())
            text += std::string(BuildingTypeLabel(modifier.buildingType.value())) + ": ";
        if (modifier.resourceType.has_value() && modifier.resourceType.value() != ResourceType::Null)
            text += rt2s(modifier.resourceType.value()) + " ";

        bool lowerIsBetter = LowerValueIsBetter(modifier.stat);
        bool showAsRate = lowerIsBetter && std::abs(modifier.multiplier - 1.0) > 0.0001 &&
                          (modifier.stat == BalanceStat::BuildTime ||
                           modifier.stat == BalanceStat::ProductionCycleTime ||
                           modifier.stat == BalanceStat::TransportTime);
        text += showAsRate ? ImprovedRateLabel(modifier.stat) : BalanceStatLabel(modifier.stat);
        if (std::abs(modifier.additive) > 0.0001)
        {
            text += modifier.additive > 0.0 ? " +" : " ";
            text += std::to_string(static_cast<int>(modifier.additive));
        }
        if (std::abs(modifier.multiplier - 1.0) > 0.0001)
        {
            double displayedPercent = showAsRate
                ? (1.0 / modifier.multiplier - 1.0) * 100.0
                : (modifier.multiplier - 1.0) * 100.0;
            int deltaPercent = static_cast<int>(std::round(displayedPercent));
            text += deltaPercent > 0 ? " +" : " ";
            text += std::to_string(deltaPercent) + "%";
        }
        return IsPositiveModifier(modifier) ? TooltipBonusLine(text) : TooltipPenaltyLine(text);
    }

    // Returns display depth in the research tree based on prerequisite chains.
    int TechnologyDepth(const ResearchNodeView& technology, const std::map<std::string, const ResearchNodeView*>& nodesById, std::map<std::string, int>& cache)
    {
        auto cached = cache.find(technology.id);
        if (cached != cache.end())
            return cached->second;

        int depth = technology.layoutOrder >= 1000 ? technology.layoutOrder / 1000 - 1 : 0;
        for (const auto& prerequisite : technology.prerequisites)
        {
            auto parent = nodesById.find(prerequisite);
            if (parent != nodesById.end())
                depth = std::max(depth, TechnologyDepth(*parent->second, nodesById, cache) + 1);
        }

        cache[technology.id] = depth;
        return depth;
    }

    // Draws a compact row of resource icons representing technology cost.
    void DrawTechnologyCost(const std::vector<ResourceAmountDefinition>& costs, Rectangle bounds)
    {
        if (costs.empty())
        {
            DrawTextFit("Free", bounds, 12, UiTheme::ParchmentDim);
            return;
        }

        float iconSize = std::min(18.0f, bounds.height);
        float x = bounds.x;
        for (const auto& cost : costs)
        {
            if (x + iconSize + 22.0f > bounds.x + bounds.width)
                break;

            Rectangle icon{x, bounds.y + (bounds.height - iconSize) * 0.5f, iconSize, iconSize};
            GuiPanel::DrawResourceIcon(cost.type, icon);
            std::string amount = std::to_string(cost.amount);
            UiText::Draw(amount, x + iconSize + 3.0f, bounds.y + 2.0f, 11, Color{224, 208, 178, 255});
            x += iconSize + 25.0f;
        }
    }

    // Draws a tooltip describing one technology after the tree itself is drawn.
    void DrawTechnologyTooltip(const ResearchNodeView& technology)
    {
        std::vector<std::string> lines;
        lines.push_back(technology.description);
        lines.push_back(TooltipSeparatorLine());
        lines.push_back(std::string("Time: ") + std::to_string(static_cast<int>(technology.researchTime)) + "s | " + technology.stateText);
        if (technology.active)
            lines.push_back("Remaining: " + std::to_string(static_cast<int>(std::round(technology.remainingTime))) + "s");
        lines.push_back(TooltipSeparatorLine());
        for (const auto& modifier : technology.modifiers)
            lines.push_back(FormatTechnologyEffect(modifier));

        Tooltip::Draw(technology.name, lines, 330.0f);
    }

    // Draws one categorized technology tree and returns the hovered technology, if any.
    const ResearchNodeView* DrawResearchCategory(
        Player* player,
        const std::vector<const ResearchNodeView*>& technologies,
        const std::map<std::string, const ResearchNodeView*>& nodesById,
        Rectangle bounds)
    {
        if (player == nullptr || technologies.empty())
        {
            DrawTextFit("No research", bounds, 14, UiTheme::ParchmentDim);
            return nullptr;
        }

        std::map<std::string, int> depthCache;
        std::map<int, int> rowByDepth;
        std::map<std::string, Rectangle> nodeBounds;
        int maxDepth = 0;
        for (const auto* technology : technologies)
            maxDepth = std::max(maxDepth, TechnologyDepth(*technology, nodesById, depthCache));

        float gapX = 34.0f;
        float gapY = 54.0f;
        float nodeW = 122.0f;
        float nodeH = 108.0f;
        int maxRowsAtDepth = 1;
        for (const auto* technology : technologies)
            maxRowsAtDepth = std::max(maxRowsAtDepth, ++rowByDepth[depthCache[technology->id]]);
        rowByDepth.clear();

        for (const auto* technology : technologies)
        {
            int depth = depthCache[technology->id];
            int row = rowByDepth[depth]++;
            Rectangle node{
                bounds.x + row * (nodeW + gapX),
                bounds.y + depth * (nodeH + gapY),
                nodeW,
                nodeH};
            nodeBounds[technology->id] = node;
        }

        for (const auto* technology : technologies)
        {
            auto childIt = nodeBounds.find(technology->id);
            if (childIt == nodeBounds.end())
                continue;

            Rectangle child = childIt->second;
            for (const auto& prerequisite : technology->prerequisites)
            {
                auto parentIt = nodeBounds.find(prerequisite);
                if (parentIt == nodeBounds.end())
                    continue;

                Rectangle parent = parentIt->second;
                Vector2 a{parent.x + parent.width * 0.5f, parent.y + parent.height};
                Vector2 b{child.x + child.width * 0.5f, child.y};
                float midY = (a.y + b.y) * 0.5f;
                DrawLineEx(a, Vector2{a.x, midY}, 2.0f, Color{104, 84, 58, 255});
                DrawLineEx(Vector2{a.x, midY}, Vector2{b.x, midY}, 2.0f, Color{104, 84, 58, 255});
                DrawLineEx(Vector2{b.x, midY}, b, 2.0f, Color{104, 84, 58, 255});
            }
        }

        Vector2 mouse = GetMousePosition();
        const ResearchNodeView* hoveredTechnology = nullptr;
        for (const auto* technology : technologies)
        {
            Rectangle node = nodeBounds[technology->id];
            bool hovered = CheckCollisionPointRec(mouse, node);

            Color fill = technology->researched ? Color{52, 74, 40, 245}
                       : technology->available ? Color{74, 56, 34, 245}
                       : technology->prerequisitesMet ? Color{72, 60, 42, 245}
                       : Color{34, 26, 19, 235};
            Color line = hovered ? Color{214, 178, 96, 255}
                       : technology->researched ? Color{140, 176, 96, 255}
                       : technology->available ? Color{176, 132, 68, 255}
                       : Color{96, 82, 66, 255};

            DrawRectangleRounded(node, 0.08f, 8, fill);
            DrawRectangleRoundedLines(node, 0.08f, 8, 1.0f, line);
            DrawTextFit(technology->name, Rectangle{node.x + 8.0f, node.y + 8.0f, node.width - 16.0f, 26.0f}, 17, UiTheme::Parchment);

            Color stateColor = technology->researched ? Color{162, 214, 122, 255}
                             : technology->available ? UiTheme::AmberBright
                             : technology->prerequisitesMet ? Color{238, 184, 84, 255}
                             : Color{160, 142, 112, 255};
            DrawTextFit(technology->stateText, Rectangle{node.x + 8.0f, node.y + 38.0f, node.width - 16.0f, 19.0f}, 15, stateColor);
            std::string timeText = std::to_string(static_cast<int>(technology->researchTime)) + "s";
            DrawTextFit(timeText, Rectangle{node.x + 8.0f, node.y + 62.0f, node.width - 16.0f, 18.0f}, 14, Color{190, 172, 140, 255});
            DrawTechnologyCost(technology->costs, Rectangle{node.x + 8.0f, node.y + 84.0f, node.width - 16.0f, 18.0f});

            if (hovered)
                hoveredTechnology = technology;

            if (hovered && technology->available && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                player->UnlockTechnology(technology->id);
        }

        return hoveredTechnology;
    }

    // Draws the selected university worker assignment above the research tree.
    void DrawResearchWorkerPanel(const Building* university, Rectangle bounds)
    {
        const auto* workers = university != nullptr ? university->GetComponent<WorkerComponent>() : nullptr;
        if (university == nullptr || workers == nullptr)
            return;

        DrawRectangleRounded(bounds, 0.045f, 8, Color{30, 22, 16, 224});
        DrawRectangleRoundedLines(bounds, 0.045f, 8, 1.0f, Color{130, 98, 60, 245});

        std::string label = "Workers: " + std::to_string(workers->assigned) + "/" + std::to_string(workers->GetModifiedCapacity(*university));
        int workerPct = static_cast<int>(std::round(workers->GetRatio() * 100.0f));
        std::string output = "Worker output: " + std::to_string(workerPct) + "%";
        float y = bounds.y + (bounds.height - 21.0f) * 0.5f;
        UiText::Draw(label, bounds.x + 14.0f, y, 20, UiTheme::Parchment);
        UiText::Draw(output, bounds.x + 220.0f, y, 20, UiTheme::AmberBright);
    }

    // Formats seconds with two decimal places for compact stat labels.
    std::string FormatSeconds(double seconds)
    {
        if (!std::isfinite(seconds))
            return "Paused";

        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2) << seconds << "s";
        return stream.str();
    }

    // Formats a decimal value with one fractional digit (e.g. tower attack
    // speed, "1.2") — plain std::to_string on a double affected by
    // BalanceModifiers would otherwise show a misleadingly truncated integer.
    std::string FormatDecimal(double value, int precision = 1)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(precision) << value;
        return stream.str();
    }

    // Draws all categorized university research trees.
    void DrawResearchTree(Player* player,
                          Building* university,
                          Rectangle bounds,
                          Vec2f panOffset,
                          float zoom,
                          std::string& selectedTagFilter,
                          const std::function<void(const std::string&, Building*)>& researchRequested)
    {
        if (player == nullptr)
        {
            DrawTextFit("No research available", bounds, 16, UiTheme::ParchmentDim);
            return;
        }

        auto nodes = ResearchCatalog::BuildView(*player);
        if (nodes.empty())
        {
            DrawTextFit("No research available", bounds, 16, UiTheme::ParchmentDim);
            return;
        }

        Rectangle manpowerPanel{bounds.x, bounds.y, bounds.width, 46.0f};
        DrawResearchWorkerPanel(university, manpowerPanel);
        auto visibleTags = CollectVisibleTags(nodes);
        Rectangle tagBar{bounds.x, bounds.y + 52.0f, bounds.width, 30.0f};
        DrawTagFilterBar(tagBar, visibleTags, selectedTagFilter);
        Rectangle treeArea{bounds.x, bounds.y + 92.0f, bounds.width, bounds.height - 92.0f};

        std::map<std::string, const ResearchNodeView*> nodesById;
        for (const auto& node : nodes)
            nodesById[node.id] = &node;

        auto laneRank = [](const std::string& lane)
        {
            if (lane == "Core Sciences") return 0;
            if (lane == "Natural Sciences") return 1;
            if (lane == "Engineering") return 2;
            if (lane == "Medicine") return 3;
            if (lane == "Social Sciences") return 4;
            if (lane == "Military Science") return 5;
            return 10;
        };

        std::map<std::string, int> depthCache;
        std::map<std::string, Rectangle> nodeBounds;
        std::map<std::string, std::map<int, std::vector<const ResearchNodeView*>>> nodesByLaneDepth;
        std::vector<std::string> lanes;

        for (const auto& node : nodes)
        {
            int depth = TechnologyDepth(node, nodesById, depthCache);
            std::string lane = node.layoutLane.empty() ? node.category : node.layoutLane;
            nodesByLaneDepth[lane][depth].push_back(&node);
            if (std::find(lanes.begin(), lanes.end(), lane) == lanes.end())
                lanes.push_back(lane);
        }

        std::sort(lanes.begin(), lanes.end(), [&](const std::string& a, const std::string& b)
        {
            int rankA = laneRank(a);
            int rankB = laneRank(b);
            if (rankA != rankB)
                return rankA < rankB;
            return a < b;
        });

        float nodeW = 122.0f * zoom;
        float nodeH = 108.0f * zoom;
        float colGap = 108.0f * zoom;
        float laneGap = 230.0f * zoom;
        float rowGap = 150.0f * zoom;
        float laneHeaderH = 38.0f * zoom;
        std::vector<std::pair<std::string, Rectangle>> laneHeaders;
        float laneX = treeArea.x + 28.0f + panOffset.x;

        for (const auto& lane : lanes)
        {
            auto& rows = nodesByLaneDepth[lane];
            size_t maxColumns = 1;
            for (const auto& [depth, rowNodes] : rows)
                maxColumns = std::max(maxColumns, rowNodes.size());

            float laneWidth = std::max(640.0f * zoom, maxColumns * nodeW + (maxColumns - 1) * colGap + 330.0f * zoom);
            laneHeaders.push_back({lane, Rectangle{laneX, treeArea.y + panOffset.y, laneWidth, laneHeaderH}});

            for (auto& [depth, rowNodes] : rows)
            {
                std::stable_sort(rowNodes.begin(), rowNodes.end(), [&](const ResearchNodeView* a, const ResearchNodeView* b)
                {
                    auto parentOrder = [&](const ResearchNodeView* node)
                    {
                        int order = node->layoutOrder;
                        for (const auto& prerequisite : node->prerequisites)
                        {
                            auto it = nodesById.find(prerequisite);
                            if (it != nodesById.end())
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
                        auto parentIt = nodeBounds.find(prerequisite);
                        if (parentIt == nodeBounds.end())
                            continue;
                        parentCenterSum += parentIt->second.x + parentIt->second.width * 0.5f;
                        parentCount++;
                    }
                    float rowOffset = (static_cast<float>(i) - (static_cast<float>(rowNodes.size()) - 1.0f) * 0.5f) * (nodeW + colGap * 1.35f);
                    int orderWithinLayer = ((rowNodes[i]->layoutOrder % 1000) + 1000) % 1000;
                    float orderNorm = static_cast<float>(orderWithinLayer) / 999.0f;
                    float laneMargin = std::min(laneWidth * 0.28f, 210.0f * zoom);
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
                    nodeBounds[rowNodes[i]->id] = Rectangle{
                        placed[orderIndex],
                        treeArea.y + panOffset.y + laneHeaderH + 28.0f + depth * (nodeH + rowGap),
                        nodeW,
                        nodeH};
                }
            }

            laneX += laneWidth + laneGap;
        }

        Vector2 mouse = GetMousePosition();
        const ResearchNodeView* hoveredTechnology = nullptr;
        for (const auto& technology : nodes)
        {
            auto it = nodeBounds.find(technology.id);
            if (it != nodeBounds.end() && CheckCollisionPointRec(mouse, it->second))
                hoveredTechnology = &technology;
        }

        std::set<std::string> highlightedPath;
        std::function<void(const std::string&)> collectParents = [&](const std::string& id)
        {
            if (!highlightedPath.insert(id).second)
                return;
            auto it = nodesById.find(id);
            if (it == nodesById.end())
                return;
            for (const auto& prerequisite : it->second->prerequisites)
                collectParents(prerequisite);
        };
        if (hoveredTechnology != nullptr)
            collectParents(hoveredTechnology->id);

        BeginScissorMode(static_cast<int>(treeArea.x), static_cast<int>(treeArea.y), static_cast<int>(treeArea.width), static_cast<int>(treeArea.height));
        for (const auto& [lane, header] : laneHeaders)
        {
            DrawRectangleRounded(header, 0.14f, 8, Color{44, 33, 23, 215});
            DrawRectangleRoundedLines(header, 0.14f, 8, 1.0f, Color{120, 92, 58, 235});
            DrawTextFit(lane, Rectangle{header.x + 12.0f, header.y + 4.0f, header.width - 24.0f, header.height - 8.0f}, std::max(20, static_cast<int>(27 * zoom)), Color{224, 204, 168, 255});
        }

        for (const auto& technology : nodes)
        {
            auto childIt = nodeBounds.find(technology.id);
            if (childIt == nodeBounds.end())
                continue;

            Rectangle child = childIt->second;
            for (const auto& prerequisite : technology.prerequisites)
            {
                auto parentIt = nodeBounds.find(prerequisite);
                if (parentIt == nodeBounds.end())
                    continue;

                Rectangle parent = parentIt->second;
                Vector2 a{parent.x + parent.width * 0.5f, parent.y + parent.height};
                Vector2 b{child.x + child.width * 0.5f, child.y};
                bool highlighted = highlightedPath.contains(technology.id) && highlightedPath.contains(prerequisite);
                Color edgeColor = highlighted ? Color{232, 202, 104, 255} : Color{96, 80, 60, 150};
                float edgeWidth = highlighted ? 4.0f : 1.5f;
                Vector2 midA{a.x, a.y + rowGap * 0.34f};
                Vector2 midB{b.x, b.y - rowGap * 0.34f};
                DrawLineEx(a, midA, edgeWidth, edgeColor);
                DrawLineEx(midA, midB, edgeWidth, edgeColor);
                DrawLineEx(midB, b, edgeWidth, edgeColor);
            }
        }

        for (const auto& technology : nodes)
        {
            Rectangle node = nodeBounds[technology.id];
            bool hovered = CheckCollisionPointRec(mouse, node);
            bool tagMatched = HasNodeTag(technology, selectedTagFilter);
            const auto* research = university != nullptr ? university->GetComponent<ResearchComponent>() : nullptr;
            bool selectedUniversityBusy = research != nullptr && !research->technologyId.empty();
            bool localAvailable = technology.available && !selectedUniversityBusy;

            Color fill = technology.researched ? Color{52, 74, 40, 245}
                       : technology.active ? Color{70, 65, 38, 245}
                       : localAvailable ? Color{74, 56, 34, 245}
                       : technology.available && selectedUniversityBusy ? Color{40, 32, 26, 235}
                       : technology.prerequisitesMet ? Color{72, 60, 42, 245}
                       : Color{34, 26, 19, 235};
            Color line = hovered ? Color{214, 178, 96, 255}
                       : technology.researched ? Color{140, 176, 96, 255}
                       : technology.active ? Color{214, 178, 84, 255}
                       : localAvailable ? Color{176, 132, 68, 255}
                       : Color{96, 82, 66, 255};
            if (!selectedTagFilter.empty() && !tagMatched)
            {
                fill.a = 110;
                line.a = 120;
            }

            DrawRectangleRounded(node, 0.08f, 8, fill);
            DrawRectangleRoundedLines(node, 0.08f, 8, 1.0f, tagMatched && !selectedTagFilter.empty() ? Color{150, 210, 130, 255} : highlightedPath.contains(technology.id) ? Color{232, 202, 104, 255} : line);
            DrawTextWrappedCentered(technology.name, Rectangle{node.x + 8.0f * zoom, node.y + 7.0f * zoom, node.width - 16.0f * zoom, 39.0f * zoom}, std::max(13, static_cast<int>(20 * zoom)), UiTheme::Parchment, 2);

            Color stateColor = technology.researched ? Color{162, 214, 122, 255}
                             : technology.available ? UiTheme::AmberBright
                             : technology.prerequisitesMet ? Color{238, 184, 84, 255}
                             : Color{160, 142, 112, 255};
            std::string localState = technology.available && selectedUniversityBusy && !technology.active
                ? "University busy"
                : technology.stateText;
            DrawTextFit(localState, Rectangle{node.x + 8.0f * zoom, node.y + 48.0f * zoom, node.width - 16.0f * zoom, 19.0f * zoom}, std::max(10, static_cast<int>(15 * zoom)), stateColor);
            std::string timeText = technology.active
                ? std::to_string(static_cast<int>(std::round(technology.remainingTime))) + "s left"
                : std::to_string(static_cast<int>(technology.researchTime)) + "s";
            DrawTextFit(timeText, Rectangle{node.x + 8.0f * zoom, node.y + 70.0f * zoom, node.width - 16.0f * zoom, 18.0f * zoom}, std::max(9, static_cast<int>(14 * zoom)), Color{190, 172, 140, 255});
            DrawTechnologyCost(technology.costs, Rectangle{node.x + 8.0f * zoom, node.y + 90.0f * zoom, node.width - 16.0f * zoom, 18.0f * zoom});
            if (technology.active || technology.researched)
            {
                Rectangle progress{node.x + 10.0f, node.y + node.height - 9.0f, node.width - 20.0f, 5.0f};
                DrawRectangleRounded(progress, 0.5f, 4, Color{22, 16, 12, 230});
                Rectangle fillBar = progress;
                fillBar.width *= static_cast<float>(std::clamp(technology.progress, 0.0, 1.0));
                DrawRectangleRounded(fillBar, 0.5f, 4, technology.researched ? Color{140, 176, 96, 255} : Color{214, 178, 84, 255});
            }

            if (hovered && localAvailable && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && researchRequested)
                researchRequested(technology.id, university);
        }
        EndScissorMode();

        if (hoveredTechnology != nullptr)
            DrawTechnologyTooltip(*hoveredTechnology);
    }
}

// Advances this object's state for one frame.
void UiButton::Update(double dt)
{
    Rectangle bounds = WidgetBounds(*this);
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, bounds);
    bool pressed = hovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    if ((hovered && hasHoverTexture) || (!hovered && hasNormalTexture))
    {
        Texture2D tex = (hovered && hasHoverTexture) ? hoverTexture : normalTexture;
        Rectangle src{0.0f, 0.0f, static_cast<float>(tex.width), static_cast<float>(tex.height)};
        DrawTexturePro(tex, src, bounds, {0.0f, 0.0f}, 0.0f, WHITE);
    }
    else
    {
        Color fill = hovered ? Color{78, 62, 44, 255} : Color{40, 29, 21, 255};
        Color line = hovered ? Color{176, 138, 82, 255} : Color{96, 82, 66, 255};
        DrawRectangleRounded(bounds, 0.08f, 8, fill);
        DrawRectangleRoundedLines(bounds, 0.08f, 8, 1.0f, line);
    }

    if (drawText)
    {
        int fontSize = std::max(14, std::min(22, size.y / 3 + 2));
        int textWidth = UiText::Measure(text, fontSize);
        int maxTextWidth = std::max(20, size.x - 18);
        while (fontSize > 9 && textWidth > maxTextWidth)
        {
            fontSize--;
            textWidth = UiText::Measure(text, fontSize);
        }

        UiText::Draw(text,
                     bounds.x + (bounds.width - textWidth) * 0.5f,
                     bounds.y + (bounds.height - fontSize) * 0.5f,
                     fontSize,
                     UiTheme::Parchment);
    }

    if (pressed)
        // Handles the UI action represented by OnClick.
        OnClick();
}

// Initializes UiButton::UiButton.
UiButton::UiButton()
{
    LoadTextures("assets/ui/button_plain.png", "assets/ui/button_hover.png");
}

// Loads the requested data into runtime state.
void UiButton::LoadTextures(const std::string& normalPath, const std::string& hoverPath)
{
    if (FileExists(normalPath.c_str()))
    {
        normalTexture = LoadTexture(normalPath.c_str());
        hasNormalTexture = normalTexture.id != 0;
    }

    if (FileExists(hoverPath.c_str()))
    {
        hoverTexture = LoadTexture(hoverPath.c_str());
        hasHoverTexture = hoverTexture.id != 0;
    }
}

// Advances this object's state for one frame.
void CheckBox::Update(double dt)
{
    GuiCheckBox(WidgetBounds(*this), text.c_str(), &currentState);
}

// Advances this object's state for one frame.
void SliderBar::Update(double dt)
{
    GuiSliderBar(WidgetBounds(*this), text.c_str(), nullptr, &currentValue, 0.0f, 1.0f);
}

// Advances this object's state for one frame.
void ProgressBar::Update(double dt)
{
    Rectangle bounds = WidgetBounds(*this);
    DrawRectangleRounded(bounds, 0.12f, 6, Color{34, 26, 19, 255});

    Rectangle fill = bounds;
    fill.width *= value;
    DrawRectangleRounded(fill, 0.12f, 6, Color{140, 176, 96, 255});
    DrawRectangleRoundedLines(bounds, 0.12f, 6, 1.0f, Color{112, 92, 68, 255});

    std::string label = text + " " + std::to_string(static_cast<int>(value * 100.0f)) + "%";
    int fontSize = std::max(12, std::min(17, static_cast<int>(bounds.height) - 3));
    int textWidth = UiText::Measure(label, fontSize);
    UiText::Draw(label,
                 bounds.x + (bounds.width - textWidth) * 0.5f,
                 bounds.y + (bounds.height - fontSize) * 0.5f,
                 fontSize,
                 UiTheme::Parchment);
}

// Advances this object's state for one frame.
void VBox::Update(double dt)
{
    for(auto& child : children)
    {
        child->Update(dt);
    }
}

// Advances this object's state for one frame.
void HBox::Update(double dt)
{
    for(auto& child : children)
    {
        child->Update(dt);
    }
}

// Advances this object's state for one frame.
void TextBox::Update(double dt)
{
    static TextBox* activeTextBox = nullptr;

    Rectangle bounds = WidgetBounds(*this);
    if (InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        if (CheckCollisionPointRec(GetMousePosition(), bounds))
            activeTextBox = this;
        else if (activeTextBox == this)
            activeTextBox = nullptr;
    }
    if (activeTextBox == this && InputManager::IsKeyPressed(KEY_ESCAPE))
        activeTextBox = nullptr;

    bool active = activeTextBox == this;
    if (active)
    {
        int key = GetCharPressed();
        while (key > 0)
        {
            std::string encoded = EncodeUtf8Codepoint(key);
            if (!encoded.empty() && text.size() + encoded.size() < sizeof(textOutput))
                text += encoded;
            key = GetCharPressed();
        }

        if (InputManager::IsKeyPressed(KEY_BACKSPACE) || InputManager::IsKeyPressedRepeat(KEY_BACKSPACE))
            RemoveLastUtf8Codepoint(text);

        SyncTextBoxBuffer(text, textOutput, sizeof(textOutput));
    }

    DrawRectangleRec(bounds, active ? Color{46, 34, 24, 245} : Color{34, 26, 19, 235});
    DrawRectangleLinesEx(bounds, active ? 2.0f : 1.0f, active ? UiTheme::Gold : Color{96, 82, 66, 255});
    Rectangle textBounds{bounds.x + 10.0f, bounds.y + 7.0f, bounds.width - 20.0f, bounds.height - 14.0f};
    int textWidth = UiText::Measure(text, 22);
    float textOffset = std::max(0.0f, static_cast<float>(textWidth) - textBounds.width + 8.0f);
    BeginScissorMode(static_cast<int>(textBounds.x), static_cast<int>(textBounds.y), static_cast<int>(textBounds.width), static_cast<int>(textBounds.height));
    UiText::Draw(text, textBounds.x - textOffset, textBounds.y + (textBounds.height - 22.0f) * 0.5f, 22, text.empty() ? Color{138, 120, 96, 255} : UiTheme::Parchment);
    EndScissorMode();
    if (active && (static_cast<int>(GetTime() * 2.0) % 2 == 0))
    {
        float cursorX = std::min(textBounds.x + textBounds.width - 2.0f, textBounds.x + static_cast<float>(textWidth) - textOffset + 3.0f);
        DrawLineEx({cursorX, textBounds.y + 4.0f}, {cursorX, textBounds.y + textBounds.height - 4.0f}, 2.0f, UiTheme::Parchment);
    }
}

// Replaces the current editable value.
void TextBox::SetValue(const std::string& value)
{
    text = value;
    SyncTextBoxBuffer(text, textOutput, sizeof(textOutput));
    text = textOutput;
}

// Advances this object's state for one frame.
void UiLabel::Update(double dt)
{
    UiText::DrawFit(text, WidgetBounds(*this), fontSize, color);
}

// Loads the requested data into runtime state.
bool UiImage::LoadTextureFromFile(const std::string& path)
{
    if (!FileExists(path.c_str()))
        return false;

    texture = LoadTexture(path.c_str());
    hasTexture = texture.id != 0;
    return hasTexture;
}

// Advances this object's state for one frame.
void UiImage::Update(double dt)
{
    Rectangle bounds = WidgetBounds(*this);
    if (hasTexture)
    {
        Rectangle src{0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)};
        if (cover)
        {
            float imageRatio = texture.width / static_cast<float>(texture.height);
            float targetRatio = bounds.width / bounds.height;

            if (imageRatio > targetRatio)
            {
                float cropW = texture.height * targetRatio;
                src.x = (texture.width - cropW) * 0.5f;
                src.width = cropW;
            }
            else
            {
                float cropH = texture.width / targetRatio;
                src.y = (texture.height - cropH) * 0.5f;
                src.height = cropH;
            }
        }
        DrawTexturePro(texture, src, bounds, {0.0f, 0.0f}, 0.0f, WHITE);
    }
    else
    {
        DrawRectangleRounded(bounds, 0.04f, 8, Color{30, 22, 16, 255});
        DrawRectangleRoundedLines(bounds, 0.04f, 8, 1.0f, Color{96, 82, 66, 255});
    }
}

// Loads the requested data into runtime state.
void ResourceIconAtlas::Load(const std::string& path, Vec2i iconSize)
{
    if (!FileExists(path.c_str()))
        return;

    texture = LoadTexture(path.c_str());
    loaded = texture.id != 0;
    size = iconSize;
}

// Returns atlas source rectangle for one resource icon.
Rectangle ResourceIconAtlas::GetRect(ResourceType type) const
{
    int index = type == ResourceType::Null ? 0 : std::max(0, static_cast<int>(type));
    int columns = std::max(1, texture.width / size.x);
    return Rectangle{
        static_cast<float>((index % columns) * size.x),
        static_cast<float>((index / columns) * size.y),
        static_cast<float>(size.x),
        static_cast<float>(size.y)};
}

// Measures text using the shared UI font.
int UiText::Measure(const std::string& text, int fontSize)
{
    return MeasureUiText(text, fontSize);
}

// Draws text using the shared UI font.
void UiText::Draw(const std::string& text, float x, float y, int fontSize, Color color)
{
    DrawUiText(text, x, y, fontSize, color);
}

// Draws text that shrinks until it fits within bounds.
void UiText::DrawFit(const std::string& text, Rectangle bounds, int fontSize, Color color)
{
    DrawTextFit(text, bounds, fontSize, color);
}

void UiText::DrawTitleBar(Rectangle titleBar, const std::string& text, float closeButtonReserve)
{
    int titleFont = std::max(21, std::min(30, static_cast<int>(titleBar.height) / 2 + 4));
    int titleWidth = Measure(text, titleFont);
    while (titleFont > 14 && titleWidth > titleBar.width - closeButtonReserve)
    {
        titleFont--;
        titleWidth = Measure(text, titleFont);
    }
    Draw(text,
         titleBar.x + (titleBar.width - titleWidth) * 0.5f,
         titleBar.y + (titleBar.height - titleFont) * 0.5f,
         titleFont,
         UiTheme::Parchment);
}

// Renders a tooltip near the mouse using the shared UI style.
void Tooltip::Draw(const std::string& title, const std::vector<std::string>& lines, float preferredWidth)
{
    int titleFont = 24;
    int lineFont = 20;
    float padding = 14.0f;
    float lineH = 27.0f;
    float paragraphGap = 5.0f;

    float width = std::max(240.0f, preferredWidth);
    width = std::max(width, std::min(520.0f, static_cast<float>(MeasureUiText(title, titleFont)) + padding * 2.0f));
    width = std::min(width, 520.0f);
    float textWidth = width - padding * 2.0f;

    std::vector<std::vector<std::string>> wrappedLines;
    wrappedLines.reserve(lines.size());
    int visualLineCount = 0;
    for (const auto& line : lines)
    {
        if (line == "{separator}")
        {
            wrappedLines.push_back({line});
            visualLineCount++;
            continue;
        }

        std::string displayLine = line;
        if (displayLine.rfind("{bonus}", 0) == 0)
            displayLine = displayLine.substr(7);
        else if (displayLine.rfind("{penalty}", 0) == 0)
            displayLine = displayLine.substr(9);

        wrappedLines.push_back(WrapText(displayLine, lineFont, textWidth));
        visualLineCount += static_cast<int>(wrappedLines.back().size());
    }

    float height = padding * 2.0f + titleFont + 8.0f +
                   std::max(1, visualLineCount) * lineH +
                   std::max(0, static_cast<int>(wrappedLines.size()) - 1) * paragraphGap;
    Vector2 mouse = GetMousePosition();
    Rectangle bounds{mouse.x + 14.0f, mouse.y + 14.0f, width, height};
    bounds.x = std::min(bounds.x, static_cast<float>(GetScreenWidth()) - bounds.width - 8.0f);
    bounds.y = std::min(bounds.y, static_cast<float>(GetScreenHeight()) - bounds.height - 8.0f);
    bounds.x = std::max(8.0f, bounds.x);
    bounds.y = std::max(8.0f, bounds.y);

    DrawRectangleRounded(bounds, 0.05f, 8, Fade(UiTheme::Bark, 0.98f));
    DrawRectangleRoundedLines(bounds, 0.05f, 8, 1.0f, UiTheme::Bronze);
    DrawUiText(title, bounds.x + padding, bounds.y + padding - 1.0f, titleFont, UiTheme::Parchment);

    float y = bounds.y + padding + titleFont + 6.0f;
    for (size_t paragraphIndex = 0; paragraphIndex < wrappedLines.size(); paragraphIndex++)
    {
        const auto& paragraph = wrappedLines[paragraphIndex];
        if (paragraph.size() == 1 && paragraph.front() == "{separator}")
        {
            y += 5.0f;
            DrawLineEx(Vector2{bounds.x + padding, y}, Vector2{bounds.x + bounds.width - padding, y}, 1.0f, Fade(UiTheme::Bronze, 0.82f));
            y += 8.0f;
            continue;
        }

        Color lineColor = UiTheme::ParchmentDim;
        const std::string& sourceLine = lines[paragraphIndex];
        if (sourceLine.rfind("{penalty}", 0) == 0)
            lineColor = UiTheme::Rust;
        else if (sourceLine.rfind("{bonus}", 0) == 0)
            lineColor = UiTheme::Sage;

        for (const auto& line : paragraph)
        {
            DrawTextFit(line, Rectangle{bounds.x + padding, y, bounds.width - padding * 2.0f, lineH}, lineFont, lineColor);
            y += lineH;
        }
        y += paragraphGap;
    }
}

// Advances this object's state for one frame.
// Draws background, title bar, drag handling and the close button; reports
// the content area below the title bar. Returns false (after already having
// called Close()) when the close button was clicked this frame — the caller
// must stop drawing content, exactly as the panel did when this was all one
// function.
bool GuiPanel::DrawChrome(double dt, Rectangle& outContentArea)
{
    Rectangle bounds = WidgetBounds(*this);

    int margin = std::max(10, size.x / 24);
    int titleBar = std::max(34, size.y / 12);

    DrawRectangleRounded(bounds, 0.02f, 8, Fade(UiTheme::Bark, 0.93f));
    DrawRectangleRoundedLines(bounds, 0.02f, 8, 2.0f, UiTheme::Bronze);

    Rectangle titleBounds{
        bounds.x,
        bounds.y,
        bounds.width,
        static_cast<float>(titleBar)};
    Vector2 mouse = GetMousePosition();
    DrawRectangleRounded(titleBounds, 0.02f, 8, UiTheme::Oak);
    // Draws this widget or render element.
    DrawLine(static_cast<int>(bounds.x), static_cast<int>(bounds.y + titleBar),
             static_cast<int>(bounds.x + bounds.width), static_cast<int>(bounds.y + titleBar),
             UiTheme::Bronze);

    int closeSize = std::max(20, titleBar - 14);
    Rectangle closeBounds{
        bounds.x + bounds.width - closeSize - margin,
        bounds.y + (titleBar - closeSize) * 0.5f,
        static_cast<float>(closeSize),
        static_cast<float>(closeSize)};
    bool closeHovered = CheckCollisionPointRec(GetMousePosition(), closeBounds);
    DrawRectangleRounded(closeBounds, 0.16f, 6, closeHovered ? UiTheme::DangerFill : UiTheme::Timber);
    DrawRectangleRoundedLines(closeBounds, 0.16f, 6, 1.0f, closeHovered ? UiTheme::DangerBorder : UiTheme::Bronze);
    int xFont = std::max(13, closeSize / 2);
    int xWidth = UiText::Measure("X", xFont);
    UiText::Draw("X",
                 closeBounds.x + (closeBounds.width - xWidth) * 0.5f,
                 closeBounds.y + (closeBounds.height - xFont) * 0.5f,
                 xFont,
                 UiTheme::Parchment);

    if (closeHovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        Close();
        return false;
    }

    // Drag: click+hold on title bar (outside close button) to reposition
    if (!closeHovered && CheckCollisionPointRec(mouse, titleBounds) && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        dragging = true;
        dragOffset = Vec2i{static_cast<int>(mouse.x) - pos.x, static_cast<int>(mouse.y) - pos.y};
    }
    if (dragging && InputManager::IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        pos.x = std::clamp(static_cast<int>(mouse.x) - dragOffset.x, 0, std::max(0, GetScreenWidth() - size.x));
        pos.y = std::clamp(static_cast<int>(mouse.y) - dragOffset.y, 0, std::max(0, GetScreenHeight() - size.y));
        bounds = {static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
        titleBounds = {bounds.x, bounds.y, bounds.width, static_cast<float>(titleBar)};
    }
    if (dragging && InputManager::IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        dragging = false;

    UiText::DrawTitleBar(titleBounds, text, static_cast<float>((closeSize + margin) * 2 + margin));

    outContentArea = Rectangle{
        static_cast<float>(pos.x + margin),
        static_cast<float>(pos.y + titleBar + margin),
        static_cast<float>(size.x - margin * 2),
        static_cast<float>(size.y - titleBar - margin * 2)};
    return true;
}

void GuiPanel::Update(double dt)
{
    pendingTooltip.visible = false;

    if (building == nullptr)
        return;

    Rectangle contentArea;
    if (!DrawChrome(dt, contentArea))
        return;

    int margin = std::max(10, size.x / 24);
    int y = static_cast<int>(contentArea.y);
    int contentX = static_cast<int>(contentArea.x);
    int contentW = static_cast<int>(contentArea.width);
    int bottom = static_cast<int>(contentArea.y + contentArea.height);
    auto drawDestroyButton = [&]()
    {
        if (!building->CanBeManuallyDestroyed())
            return;

        destroyButton.pos = Vec2i{contentX, bottom - destroyButton.size.y};
        destroyButton.size = Vec2i{contentW, destroyButton.size.y};
        destroyButton.ChangeText("Destroy building");
        destroyButton.Update(dt);
    };

    if (building->IsUnderConstruction())
    {
        int queuePosition = 0;
        int builderCount = 0;
        bool builderAssigned = false;
        if (building->owner != nullptr)
        {
            queuePosition = building->owner->construction.QueuePosition(building->id);
            builderCount = building->owner->construction.EffectiveBuilders(*building->owner);
            builderAssigned = building->owner->construction.IsActive(building->id);
        }

        UiText::Draw(builderAssigned ? "Under construction" : "Waiting in build queue",
                     contentX, y, 22, Color{224, 204, 168, 255});
        y += 34;
        progressBar.pos = Vec2i{contentX, y};
        progressBar.size = Vec2i{contentW, 30};
        progressBar.ChangeText("Construction");
        progressBar.SetValue(building->GetConstructionProgress());
        progressBar.Update(dt);
        y += 46;

        std::string remaining = "Remaining: " + std::to_string(static_cast<int>(std::ceil(building->constructionRemaining))) + "s";
        DrawTextFit(remaining, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 20.0f}, 15, Color{224, 204, 168, 255});
        y += 24;

        if (queuePosition > 0)
        {
            std::string queueLine = "Queue position: " + std::to_string(queuePosition) +
                                    " / " + std::to_string(building->owner->construction.QueueLength()) +
                                    "   Builders: " + std::to_string(builderCount);
            DrawTextFit(queueLine, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 20.0f}, 15, UiTheme::ParchmentDim);
        }

        // The Destroy path already unwinds a placed-but-unfinished building; here it
        // reads as cancelling the order. Refresh() re-numbers the rest of the queue.
        if (building->CanBeManuallyDestroyed())
        {
            destroyButton.pos = Vec2i{contentX, bottom - destroyButton.size.y};
            destroyButton.size = Vec2i{contentW, destroyButton.size.y};
            destroyButton.ChangeText("Remove from queue");
            destroyButton.Update(dt);
        }
        return;
    }

    // TD(etap-8.3): Headquarters — checked before IsStorageLike() (HqComponent
    // buildings also have StorageComponent) so it gets its own HP/defense
    // content instead of falling into the generic storage-grid branch below.
    // Shown for any player's HQ (own or enemy) per the plan's "HP HQ własnego
    // (i wroga przy kliknięciu)".
    if (auto* hq = building->GetComponent<HqComponent>())
    {
        UiText::Draw("Headquarters", contentX, y, 22, Color{224, 204, 168, 255});
        y += 30;

        double maxHp = hq->GetModifiedMaxHp(*building);
        double hpRatio = maxHp > 0.0 ? std::clamp(hq->currentHp / maxHp, 0.0, 1.0) : 0.0;
        progressBar.pos = Vec2i{contentX, y};
        progressBar.size = Vec2i{contentW, 30};
        progressBar.ChangeText("HP " + std::to_string(static_cast<int>(std::round(std::max(0.0, hq->currentHp)))) +
                                " / " + std::to_string(static_cast<int>(std::round(maxHp))));
        progressBar.SetValue(static_cast<float>(hpRatio));
        progressBar.Update(dt);
        y += 46;

        std::vector<std::string> stats{
            "Hard defense: " + std::to_string(static_cast<int>(std::round(hq->GetModifiedHardDefense(*building)))),
            "Thorns damage: " + std::to_string(static_cast<int>(std::round(hq->GetModifiedThornsDamage(*building)))),
            "Thorns interval: " + FormatDecimal(hq->thornsInterval) + "s"};
        for (const auto& stat : stats)
        {
            DrawTextFit(stat, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 20.0f}, 15, UiTheme::Parchment);
            y += 24;
        }
        y += margin / 2;

        UiText::Draw("Storage", contentX, y, 20, Color{224, 204, 168, 255});
        y += 26;
        Rectangle grid{
            static_cast<float>(contentX),
            static_cast<float>(y),
            static_cast<float>(contentW),
            static_cast<float>(bottom - y - destroyButton.size.y - margin)};
        DrawResourceIconGrid(building->GetOutputBufferViews(), grid, 4, contentScrollOffset, &maxContentScrollOffset);
        contentScrollOffset = std::clamp(contentScrollOffset, 0.0f, maxContentScrollOffset);
        if (maxContentScrollOffset > 0.0f)
        {
            Rectangle track{grid.x + grid.width - 5.0f, grid.y, 4.0f, grid.height};
            DrawRectangleRounded(track, 0.5f, 4, Color{24, 17, 12, 190});
            float thumbH = std::max(28.0f, track.height * (track.height / (track.height + maxContentScrollOffset)));
            float thumbY = track.y + (track.height - thumbH) * (contentScrollOffset / maxContentScrollOffset);
            DrawRectangleRounded(Rectangle{track.x, thumbY, track.width, thumbH}, 0.5f, 4, Color{150, 108, 58, 230});
        }
        drawDestroyButton(); // no-op: Headquarters::CanBeManuallyDestroyed() == false
        DrawPendingTooltip();
        return;
    }

    // TD(etap-8.2): Defense tower — same reordering reason as HQ above
    // (TowerCombatComponent buildings also have StorageComponent, used here
    // purely as the ammo buffer).
    if (auto* tower = building->GetComponent<TowerCombatComponent>())
    {
        UiText::Draw("Defense Tower", contentX, y, 22, Color{224, 204, 168, 255});
        y += 30;

        std::vector<std::string> stats{
            "Damage: " + std::to_string(static_cast<int>(std::round(tower->GetModifiedDamage(*building)))),
            "Range: " + std::to_string(static_cast<int>(std::round(tower->GetModifiedRange(*building)))) + " tiles",
            "Attack speed: " + FormatDecimal(tower->GetModifiedAttackSpeed(*building)) + "/s",
            "Crew: " + std::to_string(building->GetAssignedWorkers()) + "/" + std::to_string(building->GetWorkerCapacity())};
        for (const auto& stat : stats)
        {
            DrawTextFit(stat, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 20.0f}, 15, UiTheme::Parchment);
            y += 24;
        }
        y += margin / 2;

        towerTargetButton.pos = Vec2i{contentX, y};
        towerTargetButton.size = Vec2i{contentW, std::max(30, lockButton.size.y)};
        towerTargetButton.ChangeText(tower->targetMode == TowerTargetMode::NearestToHq
            ? "Target: nearest to HQ"
            : "Target: strongest unit");
        towerTargetButton.Update(dt);
        y += towerTargetButton.size.y + margin;

        UiText::Draw("Ammunition", contentX, y, 20, Color{224, 204, 168, 255});
        y += 26;
        Rectangle grid{
            static_cast<float>(contentX),
            static_cast<float>(y),
            static_cast<float>(contentW),
            static_cast<float>(bottom - y - destroyButton.size.y - margin)};
        DrawResourceIconGrid(building->GetOutputBufferViews(), grid, 4);
        drawDestroyButton();
        DrawPendingTooltip();
        return;
    }

    // TD(etap-8.4): recruitment building (Barracks) — same reordering reason.
    // Deliberately does not also show the generic storage grid (delivered
    // unit-cost resources): the recruit buttons below already surface each
    // unit's cost, and panel space is tight with both the queue and the
    // catalog list present.
    if (auto* recruitment = building->GetComponent<RecruitmentComponent>())
    {
        UiText::Draw("Recruitment", contentX, y, 22, Color{224, 204, 168, 255});
        y += 30;

        // A2 (docs/work_plan_2026-07-13.md): recruit buttons render at a fixed
        // position right after the title so they don't shift as the queue
        // below grows/shrinks — the queue (variable length) is drawn AFTER
        // the buttons instead of before them.
        UiText::Draw("Available units", contentX, y, 18, UiTheme::AmberBright);
        y += 24;

        int rowH = 44;
        Building* self = building;
        GameScene* panelScene = scene;
        for (const auto& [id, def] : GetUnitCatalog())
        {
            if (def.recruitBuilding != building->buildingType)
                continue;
            if (y + rowH > bottom - destroyButton.size.y - margin)
                break;

            // TD(etap-9): show the tech-modified time/manpower cost, not the
            // raw catalog values — UnitRecruitTime/UnitRecruitManpowerCost are
            // real, tunable stats (see RecruitmentComponent::QueueRecruitment).
            double effectiveManpowerCost = building->owner != nullptr
                ? std::max(0.0, building->owner->ModifyBalanceForUnit(BalanceStat::UnitRecruitManpowerCost, def.manpowerCost, id))
                : def.manpowerCost;
            double effectiveRecruitTime = building->owner != nullptr
                ? std::max(1.0, building->owner->ModifyBalanceForUnit(BalanceStat::UnitRecruitTime, def.recruitTime, id))
                : def.recruitTime;

            std::string costText = "Manpower " + std::to_string(static_cast<int>(effectiveManpowerCost));
            for (const auto& cost : def.cost)
                costText += ", " + rt2s(cost.type) + " " + std::to_string(cost.amount);

            // T4 (docs/post_pivot_audit_2026-07-12.md): mirror QueueRecruitment's
            // own checks (non-mutating) so the button visibly disables with a
            // reason instead of silently doing nothing on click.
            std::string blockReason = recruitment->DiagnoseRecruitmentBlock(*building, id);
            Rectangle rowRect{static_cast<float>(contentX), static_cast<float>(y),
                              static_cast<float>(contentW), static_cast<float>(rowH - 6)};

            if (blockReason.empty())
            {
                UiButton recruitButton;
                recruitButton.pos = Vec2i{contentX, y};
                recruitButton.size = Vec2i{contentW, rowH - 6};
                recruitButton.ChangeText(def.displayName + " (" + FormatSeconds(effectiveRecruitTime) + ") - " + costText);
                std::string unitDefId = id;
                recruitButton.func = [self, panelScene, unitDefId]()
                {
                    if (self == nullptr || panelScene == nullptr || panelScene->game == nullptr)
                        return;
                    panelScene->SubmitLocalCommand(GameCommand::RecruitUnit(
                        panelScene->game->GetLocalPlayerId(), self->positionId, unitDefId));
                };
                recruitButton.Update(dt);
            }
            else
            {
                DrawRectangleRounded(rowRect, 0.08f, 8, Color{46, 34, 24, 210});
                DrawRectangleRoundedLines(rowRect, 0.08f, 8, 1.0f, Color{100, 84, 64, 210});
                DrawTextFit(def.displayName + " (" + FormatSeconds(effectiveRecruitTime) + ") - " + costText,
                    {rowRect.x + 12.0f, rowRect.y, rowRect.width - 24.0f, rowRect.height},
                    15, UiTheme::ParchmentDim);
                if (CheckCollisionPointRec(GetMousePosition(), rowRect))
                    QueueTooltip(def.displayName, {blockReason});
            }
            y += rowH;
        }

        if (!recruitment->queue.empty())
        {
            y += margin / 2;
            UiText::Draw("Queue", contentX, y, 18, UiTheme::AmberBright);
            y += 24;
            for (const auto& entry : recruitment->queue)
            {
                if (y + 34 > bottom - destroyButton.size.y - margin)
                    break;

                const UnitDefinition* def = FindUnitDefinition(entry.unitDefId);
                std::string name = def != nullptr ? def->displayName : entry.unitDefId;
                std::string label = entry.resourcesReady
                    ? name + " - " + FormatSeconds(entry.remaining) + " / " + FormatSeconds(entry.total)
                    : name + " - Waiting for resources";
                DrawTextFit(label, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 18.0f},
                    14, entry.resourcesReady ? UiTheme::Parchment : Color{230, 190, 110, 255});
                y += 20;

                float progress = entry.resourcesReady && entry.total > 0.0
                    ? std::clamp(static_cast<float>(1.0 - entry.remaining / entry.total), 0.0f, 1.0f)
                    : 0.0f;
                Rectangle bar{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 8.0f};
                DrawRectangleRounded(bar, 0.2f, 4, Color{22, 16, 12, 255});
                Rectangle fill = bar;
                fill.width *= progress;
                DrawRectangleRounded(fill, 0.2f, 4, entry.resourcesReady ? Color{140, 176, 96, 255} : Color{150, 120, 60, 255});
                y += 14;
            }
        }

        drawDestroyButton();
        DrawPendingTooltip();
        return;
    }

    if (building->IsStorageLike())
    {
        UiText::Draw("Storage", contentX, y, 22, Color{224, 204, 168, 255});
        y += 28;

        Rectangle grid{
            static_cast<float>(contentX),
            static_cast<float>(y),
            static_cast<float>(contentW),
            static_cast<float>(bottom - y - destroyButton.size.y - margin)};
        DrawResourceIconGrid(building->GetOutputBufferViews(), grid, 4, contentScrollOffset, &maxContentScrollOffset);
        contentScrollOffset = std::clamp(contentScrollOffset, 0.0f, maxContentScrollOffset);
        if (maxContentScrollOffset > 0.0f)
        {
            Rectangle track{grid.x + grid.width - 5.0f, grid.y, 4.0f, grid.height};
            DrawRectangleRounded(track, 0.5f, 4, Color{24, 17, 12, 190});
            float thumbH = std::max(28.0f, track.height * (track.height / (track.height + maxContentScrollOffset)));
            float thumbY = track.y + (track.height - thumbH) * (contentScrollOffset / maxContentScrollOffset);
            DrawRectangleRounded(Rectangle{track.x, thumbY, track.width, thumbH}, 0.5f, 4, Color{150, 108, 58, 230});
        }
        drawDestroyButton();
        DrawPendingTooltip();
        return;
    }

    if (building->HasComponent<RoadComponent>())
    {
        auto* road = static_cast<Road*>(building);
        UiText::Draw("Transport", contentX, y, 22, Color{224, 204, 168, 255});
        y += 30;

        if (road != nullptr)
        {
            int used = static_cast<int>(road->transportables.size());
            std::vector<std::string> stats{
                "Capacity: " + std::to_string(used) + "/" + std::to_string(road->GetModifiedMaxCapacity()),
                "Speed: x" + std::to_string(static_cast<int>(road->GetModifiedSpeedModifier() * 100.0)) + "%",
                "Upgrade level: " + std::to_string(road->upgrade.level) + "/" + std::to_string(road->upgrade.maxLevel)};
            if (road->upgrade.isUpgrading)
                stats.push_back("Upgrading: " + std::to_string(static_cast<int>(std::ceil(road->upgrade.upgradeRemaining))) + "s left");

            for (const auto& stat : stats)
            {
                DrawTextFit(stat, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 18.0f}, 15, UiTheme::Parchment);
                y += 22;
            }

            // Upgrade button — road keeps transporting while upgrading
            // (UpgradeComponent never touches constructionRemaining, see
            // GameWorld.Commands.cpp), so this button only disappears once
            // maxed out, never while mid-upgrade elsewhere on the road.
            if (!road->upgrade.isUpgrading && road->upgrade.level < road->upgrade.maxLevel)
            {
                const auto& definition = GetBuildingDefinition(road->buildingType);
                int targetLevel = road->upgrade.level + 1;
                auto levelIt = std::find_if(definition.upgradeLevels.begin(), definition.upgradeLevels.end(),
                    [&](const BuildingUpgradeLevelDefinition& d) { return d.level == targetLevel; });

                if (levelIt != definition.upgradeLevels.end())
                {
                    Building* self = road;
                    GameScene* panelScene = scene;
                    Rectangle upgradeRect{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 32.0f};
                    UiButton upgradeButton;
                    upgradeButton.pos = Vec2i{contentX, y};
                    upgradeButton.size = Vec2i{contentW, 32};
                    upgradeButton.ChangeText("Upgrade to level " + std::to_string(targetLevel));
                    upgradeButton.func = [self, panelScene, targetLevel]()
                    {
                        if (self == nullptr || panelScene == nullptr || panelScene->game == nullptr)
                            return;
                        panelScene->SubmitLocalCommand(GameCommand::UpgradeBuilding(
                            panelScene->game->GetLocalPlayerId(), self->positionId));
                    };
                    upgradeButton.Update(dt);

                    // Custom hover box (not the generic text-only Tooltip::Draw)
                    // so the cost can show an actual small resource icon per
                    // the user's request, not just a text amount.
                    if (CheckCollisionPointRec(GetMousePosition(), upgradeRect))
                    {
                        float rowH = 22.0f;
                        int rowCount = static_cast<int>(levelIt->cost.size()) + (levelIt->buildTime > 0.0 ? 1 : 0);
                        float boxH = std::max(1, rowCount) * rowH + 12.0f;
                        Rectangle box{upgradeRect.x, upgradeRect.y - boxH - 6.0f, upgradeRect.width, boxH};
                        DrawRectangleRounded(box, 0.08f, 8, Color{30, 22, 16, 250});
                        DrawRectangleRoundedLines(box, 0.08f, 8, 1.0f, Color{150, 108, 58, 255});
                        float rowY = box.y + 6.0f;
                        for (const auto& cost : levelIt->cost)
                        {
                            Rectangle icon{box.x + 8.0f, rowY, 18.0f, 18.0f};
                            GuiPanel::DrawResourceIcon(cost.type, icon);
                            UiText::Draw(rt2s(cost.type) + " x" + std::to_string(cost.amount),
                                box.x + 34.0f, rowY + 1.0f, 14, UiTheme::Parchment);
                            rowY += rowH;
                        }
                        if (levelIt->buildTime > 0.0)
                            UiText::Draw("Build time: " + FormatSeconds(levelIt->buildTime),
                                box.x + 8.0f, rowY + 1.0f, 14, UiTheme::ParchmentDim);
                    }
                    y += 32 + 8;
                }
            }

            y += 8;
            UiText::Draw("Resources on road", contentX, y, 20, Color{224, 204, 168, 255});
            y += 26;

            if (road->transportables.empty())
            {
                DrawTextFit("No active transports", Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 20.0f}, 15, UiTheme::ParchmentDim);
                drawDestroyButton();
                return;
            }

            int rowH = 42;
            for (auto* transportable : road->transportables)
            {
                if (y + rowH > bottom - destroyButton.size.y - margin)
                    break;

                auto* resource = dynamic_cast<Resource*>(transportable);
                ResourceType type = resource != nullptr ? resource->type : ResourceType::Null;
                Rectangle row{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), static_cast<float>(rowH - 6)};
                DrawRectangleRounded(row, 0.08f, 6, Color{46, 34, 24, 235});
                DrawRectangleRoundedLines(row, 0.08f, 6, 1.0f, Color{112, 88, 58, 255});

                Rectangle icon{row.x + 6.0f, row.y + 5.0f, 26.0f, 26.0f};
                if (resourceIconAtlas.IsLoaded())
                {
                    Rectangle src = resourceIconAtlas.GetRect(type);
                    DrawTexturePro(resourceIconAtlas.texture, src, icon, {0.0f, 0.0f}, 0.0f, WHITE);
                }
                else
                {
                    DrawRectangleRounded(icon, 0.16f, 6, ResourceColor(type));
                    DrawTextFit(ResourceShortName(type), {icon.x + 3.0f, icon.y + 7.0f, icon.width - 6.0f, 14.0f}, 12, WHITE);
                }

                DrawTextFit(rt2s(type), Rectangle{row.x + 40.0f, row.y + 4.0f, row.width - 46.0f, 16.0f}, 13, UiTheme::Parchment);
                float progress = transportable->transportTime > 0.0
                    ? std::clamp(static_cast<float>(transportable->elapsedTime / transportable->transportTime), 0.0f, 1.0f)
                    : 0.0f;
                Rectangle bar{row.x + 40.0f, row.y + 23.0f, row.width - 48.0f, 8.0f};
                DrawRectangleRounded(bar, 0.2f, 4, Color{22, 16, 12, 255});
                Rectangle fill = bar;
                fill.width *= progress;
                DrawRectangleRounded(fill, 0.2f, 4, Color{140, 176, 96, 255});
                y += rowH;
            }
        }
        drawDestroyButton();
        return;
    }

    if (auto* population = building->GetComponent<PopulationComponent>())
    {
        UiText::Draw("Residential", contentX, y, 22, Color{224, 204, 168, 255});
        y += 32;

        double manpowerRate = building->owner != nullptr
            ? building->owner->ResolveStat(population->manpowerRate, building)
            : population->manpowerRate.GetBase();
        int populationCap = building->owner != nullptr
            ? building->owner->ResolveStat(population->populationCap, building)
            : population->populationCap.GetBase();
        std::vector<std::string> stats{
            "Generates: Manpower",
            "Rate: " + std::to_string(static_cast<int>(manpowerRate * 60.0)) + " / min",
            "Population cap: " + std::to_string(populationCap),
            "Upkeep: " + std::to_string(static_cast<int>(population->foodPackageUpkeep)) + " food / min",
            "Food supply: " + std::to_string(static_cast<int>(std::round(population->GetFoodSupplyRatio() * 100.0))) + "%",
            "Worker output: " + std::to_string(static_cast<int>(std::round(population->GetWorkerProductivity() * 100.0))) + "%",
            "Lifetime: " + std::to_string(static_cast<int>(building->GetLifetime())) + "s"};

        int line = 18;
        for (const auto& stat : stats)
        {
            Color color = population->GetFoodSupplyRatio() < 1.0 && stat.find("Food supply") != std::string::npos ? Color{238, 184, 84, 255} : UiTheme::Parchment;
            DrawTextFit(stat, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), static_cast<float>(line)}, line - 3, color);
            y += line + 4;
        }
        drawDestroyButton();
        return;
    }

    auto* panelProduction = building->GetComponent<ProductionComponent>();
    auto* panelRecipes = building->GetComponent<RecipeComponent>();
    int buttonSpace = destroyButton.size.y + margin +
                      (building->CanBlockProduction() ? lockButton.size.y + margin : 0) +
                      (panelRecipes != nullptr && panelRecipes->HasSelectableRecipes() ? recipeButton.size.y + margin : 0);
    int connectionsH = std::max(48, size.y / 9);
    int statsH = std::max(92, size.y / 5);
    int progressH = std::max(16, std::min(20, progressBar.size.y));
    int resourcesBottom = bottom - buttonSpace - statsH - connectionsH - progressH - margin * 4;
    int resourcesH = std::max(74, resourcesBottom - y);

    int headerH = 22;
    int columnGap = std::max(8, margin / 2);
    int columnW = (contentW - columnGap) / 2;

    UiText::Draw("Input", contentX, y, 20, Color{224, 204, 168, 255});
    UiText::Draw("Output", contentX + columnW + columnGap, y, 20, Color{224, 204, 168, 255});
    y += headerH;

    Rectangle inputGrid{
        static_cast<float>(contentX),
        static_cast<float>(y),
        static_cast<float>(columnW),
        static_cast<float>(resourcesH - headerH)};
    Rectangle outputGrid{
        static_cast<float>(contentX + columnW + columnGap),
        static_cast<float>(y),
        static_cast<float>(columnW),
        static_cast<float>(resourcesH - headerH)};
    DrawResourceIconGrid(building->GetInputBufferViews(), inputGrid, 3);
    DrawResourceIconGrid(building->GetOutputBufferViews(), outputGrid, 3);

    y = static_cast<int>(inputGrid.y + inputGrid.height + margin);
    progressBar.pos = Vec2i{contentX, y};
    progressBar.size = Vec2i{contentW, progressH};
    progressBar.ChangeText("Cycle");
    progressBar.SetValue(building->GetProductionProgress());
    progressBar.Update(dt);

    y += progressH + margin;
    auto receivers = building->GetReceiverViews();
    auto suppliers = building->GetSupplierViews();
    int connectionLine = std::max(18, std::min(22, connectionsH / 3));
    int connectionGap = std::max(8, margin / 2);
    int connectionColumnW = (contentW - connectionGap) / 2;
    int connectionStartY = y;
    int leftY = y + 22;
    int rightY = y + 22;

    UiText::Draw("Supply", contentX, y, 18, UiTheme::AmberBright);
    UiText::Draw("Output", contentX + connectionColumnW + connectionGap, y, 18, UiTheme::AmberBright);

    if (suppliers.empty())
    {
        DrawTextFit("No supplier", Rectangle{static_cast<float>(contentX), static_cast<float>(leftY), static_cast<float>(connectionColumnW), static_cast<float>(connectionLine)}, connectionLine - 3, Color{238, 184, 84, 255});
        leftY += connectionLine;
    }
    for (const auto& supplier : suppliers)
    {
        std::string label = (supplier.building != nullptr ? supplier.building->name : "No supplier") + " -> " + rt2s(supplier.type);
        Color color = supplier.building != nullptr ? UiTheme::AmberBright : Color{238, 184, 84, 255};
        DrawTextFit(label, Rectangle{static_cast<float>(contentX), static_cast<float>(leftY), static_cast<float>(connectionColumnW), static_cast<float>(connectionLine)}, connectionLine - 3, color);
        leftY += connectionLine;
    }

    if (receivers.empty() && !building->GetOutputBufferViews().empty())
    {
        DrawTextFit("No receiver", Rectangle{static_cast<float>(contentX + connectionColumnW + connectionGap), static_cast<float>(rightY), static_cast<float>(connectionColumnW), static_cast<float>(connectionLine)}, connectionLine - 3, Color{238, 184, 84, 255});
        rightY += connectionLine;
    }
    for (const auto& receiver : receivers)
    {
        std::string label = rt2s(receiver.type) + " -> " + (receiver.building != nullptr ? receiver.building->name : "No receiver");
        if (receiver.alternative)
            label += " (alt)";
        Color color = receiver.building != nullptr ? UiTheme::Parchment : Color{238, 184, 84, 255};
        DrawTextFit(label, Rectangle{static_cast<float>(contentX + connectionColumnW + connectionGap), static_cast<float>(rightY), static_cast<float>(connectionColumnW), static_cast<float>(connectionLine)}, connectionLine - 3, color);
        rightY += connectionLine;
    }

    y = std::max(leftY, rightY);
    y = std::max(y, connectionStartY + connectionsH);

    y += margin / 2;
    float efficiency = building->GetEfficiency() * 100.0f;
    double foodProductivityRatio = building->owner != nullptr ? building->owner->GetFoodProductivity() : 1.0;
    int foodProductivity = static_cast<int>(std::round(foodProductivityRatio * 100.0));
    double cycleTime = 0.0;
    int workerOutput = static_cast<int>(building->GetWorkerRatio() * 100.0f);
    if (panelProduction != nullptr)
    {
        double workerEfficiency = static_cast<double>(building->GetWorkerRatio()) * foodProductivityRatio;
        cycleTime = workerEfficiency > 0.0
            ? panelProduction->GetModifiedCycleTime(*building) / workerEfficiency
            : std::numeric_limits<double>::infinity();
    }

    std::vector<std::string> stats{
        "Cycle time: " + FormatSeconds(cycleTime),
        "Workers: " + std::to_string(building->GetAssignedWorkers()) + "/" + std::to_string(building->GetWorkerCapacity()),
        "Worker output: " + std::to_string(workerOutput) + "%",
        "Food productivity: " + std::to_string(foodProductivity) + "%",
        "Produced: " + std::to_string(building->GetTotalProduced()),
        "Efficiency: " + std::to_string(static_cast<int>(efficiency)) + "%",
        "Active: " + std::to_string(static_cast<int>(building->GetActiveTime())) + "s",
        "Lifetime: " + std::to_string(static_cast<int>(building->GetLifetime())) + "s"};
    if (panelRecipes != nullptr && !panelRecipes->recipes.empty())
        stats.insert(stats.begin(), "Recipe: " + panelRecipes->GetActiveRecipeName());
    int localRichness = GetLocalTerrainRichness(building);
    if (localRichness >= 0)
        stats.insert(stats.begin(), "Local richness: " + std::to_string(localRichness));

    int statColumns = 2;
    int statRows = static_cast<int>((stats.size() + statColumns - 1) / statColumns);
    int statGap = std::max(8, margin / 2);
    int statColumnW = (contentW - statGap) / statColumns;
    int statLine = std::max(16, std::min(21, statsH / std::max(1, statRows)));
    for (int i = 0; i < static_cast<int>(stats.size()); i++)
    {
        int col = i % statColumns;
        int row = i / statColumns;
        Rectangle statBounds{
            static_cast<float>(contentX + col * (statColumnW + statGap)),
            static_cast<float>(y + row * statLine),
            static_cast<float>(statColumnW),
            static_cast<float>(statLine)};
        DrawTextFit(stats[i], statBounds, statLine - 3, UiTheme::Parchment);
    }

    if (panelRecipes != nullptr && panelRecipes->HasSelectableRecipes())
    {
        recipeButton.pos = Vec2i{contentX, bottom - destroyButton.size.y - margin - lockButton.size.y * 2 - margin};
        recipeButton.size = Vec2i{contentW, lockButton.size.y};
        recipeButton.ChangeText("Recipe: " + panelRecipes->GetActiveRecipeName());
        recipeButton.Update(dt);
    }

    if (building->CanBlockProduction())
    {
        lockButton.pos = Vec2i{contentX, bottom - destroyButton.size.y - margin - lockButton.size.y};
        lockButton.size = Vec2i{contentW, lockButton.size.y};
        lockButton.ChangeText(building->IsProductionBlocked() ? "Unlock production" : "Block production");
        lockButton.Update(dt);
    }
    drawDestroyButton();
    DrawPendingTooltip();
}

// Initializes GuiPanel::GuiPanel.
GuiPanel::GuiPanel()
{
    lockButton.func = [this]()
    {
        if (building != nullptr && building->CanBlockProduction())
            building->SetProductionBlocked(!building->IsProductionBlocked());
    };
    recipeButton.func = [this]()
    {
        auto* production = building != nullptr ? building->GetComponent<ProductionComponent>() : nullptr;
        auto* logistics = building != nullptr ? building->GetComponent<LogisticsComponent>() : nullptr;
        auto* workers = building != nullptr ? building->GetComponent<WorkerComponent>() : nullptr;
        auto* recipes = building != nullptr ? building->GetComponent<RecipeComponent>() : nullptr;
        if (building != nullptr && production != nullptr && logistics != nullptr && workers != nullptr && recipes != nullptr)
            recipes->CycleRecipe(*building, *production, *logistics, *workers);
    };
    towerTargetButton.func = [this]()
    {
        auto* tower = building != nullptr ? building->GetComponent<TowerCombatComponent>() : nullptr;
        if (building == nullptr || tower == nullptr || scene == nullptr || scene->game == nullptr)
            return;
        TowerTargetMode next = tower->targetMode == TowerTargetMode::NearestToHq
            ? TowerTargetMode::StrongestUnit
            : TowerTargetMode::NearestToHq;
        scene->SubmitLocalCommand(GameCommand::SetTowerTargetMode(
            scene->game->GetLocalPlayerId(), building->positionId, static_cast<int>(next)));
    };
    destroyButton.func = [this]()
    {
        if (building != nullptr)
            destroyRequested = true;
    };
}

// Advances UpdateSize for one frame or simulation tick.
void GuiPanel::UpdateSize(Vec2i windowSize)
{
    UiWidget::UpdateSize(windowSize);

    int margin = std::max(10, size.x / 24);
    int progressH = std::max(28, size.y / 14);
    int buttonH = std::max(32, size.y / 12);

    progressBar.pos = Vec2i{pos.x + margin, pos.y + static_cast<int>(size.y * 0.58f)};
    progressBar.size = Vec2i{size.x - margin * 2, progressH};
    progressBar.ChangeText("Cycle");

    lockButton.pos = Vec2i{pos.x + margin, pos.y + size.y - buttonH - margin};
    lockButton.size = Vec2i{size.x - margin * 2, buttonH};
    recipeButton.pos = Vec2i{pos.x + margin, pos.y + size.y - buttonH * 2 - margin * 2};
    recipeButton.size = Vec2i{size.x - margin * 2, buttonH};
    towerTargetButton.pos = Vec2i{pos.x + margin, pos.y + size.y - buttonH * 2 - margin * 2};
    towerTargetButton.size = Vec2i{size.x - margin * 2, buttonH};
    destroyButton.pos = Vec2i{pos.x + margin, pos.y + size.y - buttonH - margin};
    destroyButton.size = Vec2i{size.x - margin * 2, buttonH};
}

// Updates the requested state value.
void GuiPanel::SetBuilding(Building* ptr)
{
    building = ptr;
    destroyRequested = false;
    contentScrollOffset = 0.0f;
    maxContentScrollOffset = 0.0f;
    ChangeText(building != nullptr ? building->name : "Gui Panel");
    UpdateSize({GetScreenWidth(), GetScreenHeight()});
}

// Scrolls overflowing generic panel content.
void GuiPanel::ScrollContent(float wheel)
{
    if (wheel == 0.0f || maxContentScrollOffset <= 0.0f)
        return;

    contentScrollOffset = std::clamp(contentScrollOffset - wheel * 42.0f, 0.0f, maxContentScrollOffset);
}

// Restricts drawing to a content rectangle so overflowing text/widgets don't
// bleed past the panel edge while scrolled. Pair with EndContentClip().
void GuiPanel::BeginContentClip(Rectangle contentArea)
{
    BeginScissorMode(static_cast<int>(contentArea.x), static_cast<int>(contentArea.y),
                      static_cast<int>(contentArea.width), static_cast<int>(contentArea.height));
}

void GuiPanel::EndContentClip()
{
    EndScissorMode();
}

// Loads the requested data into runtime state.
void GuiPanel::LoadResourceAtlas(const std::string& path, Vec2i iconSize)
{
    resourceIconAtlas.Load(path, iconSize);
}

// Loads the shared UI font and applies it to raygui widgets.
void GuiPanel::LoadUiFont(const std::string& path)
{
    if (!FileExists(path.c_str()))
        return;

    if (uiFontLoaded)
        UnloadFont(uiFont);

    uiFont = LoadFont(path.c_str());
    uiFontLoaded = uiFont.texture.id != 0;
    if (uiFontLoaded)
    {
        SetTextureFilter(uiFont.texture, TEXTURE_FILTER_BILINEAR);
        GuiSetFont(uiFont);
        GuiSetStyle(DEFAULT, TEXT_SIZE, 20);
    }
}

// Draws one resource icon, using the atlas when available.
void GuiPanel::DrawResourceIcon(ResourceType type, Rectangle dest)
{
    if (resourceIconAtlas.IsLoaded())
    {
        Rectangle src = resourceIconAtlas.GetRect(type);
        DrawTexturePro(resourceIconAtlas.texture, src, dest, {0.0f, 0.0f}, 0.0f, WHITE);
        return;
    }

    DrawRectangleRounded(dest, 0.16f, 6, ResourceColor(type));
    DrawTextFit(ResourceShortName(type), {dest.x + 3.0f, dest.y + dest.height * 0.32f, dest.width - 6.0f, 14.0f}, 12, WHITE);
}

// Draws categorized research trees for the selected university owner.
void ResearchPanel::Update(double dt)
{
    if (building == nullptr)
        return;

    Rectangle bounds = WidgetBounds(*this);
    int margin = std::max(14, size.x / 54);
    int titleBar = std::max(42, size.y / 14);
    Vector2 mouse = GetMousePosition();

    DrawRectangleRounded(bounds, 0.018f, 8, Color{30, 22, 16, 242});
    DrawRectangleRoundedLines(bounds, 0.018f, 8, 1.0f, Color{150, 108, 58, 255});

    Rectangle titleBounds{bounds.x, bounds.y, bounds.width, static_cast<float>(titleBar)};
    DrawRectangleRounded(titleBounds, 0.018f, 8, Color{56, 41, 29, 255});
    DrawLine(static_cast<int>(bounds.x), static_cast<int>(bounds.y + titleBar),
             static_cast<int>(bounds.x + bounds.width), static_cast<int>(bounds.y + titleBar),
             Color{150, 108, 58, 255});

    int closeSize = std::max(24, titleBar - 16);
    Rectangle closeBounds{
        bounds.x + bounds.width - closeSize - margin,
        bounds.y + (titleBar - closeSize) * 0.5f,
        static_cast<float>(closeSize),
        static_cast<float>(closeSize)};
    bool closeHovered = CheckCollisionPointRec(GetMousePosition(), closeBounds);
    DrawRectangleRounded(closeBounds, 0.16f, 6, closeHovered ? Color{132, 58, 42, 255} : Color{78, 58, 41, 255});
    DrawRectangleRoundedLines(closeBounds, 0.16f, 6, 1.0f, closeHovered ? Color{214, 128, 92, 255} : Color{150, 108, 58, 255});

    int xFont = std::max(14, closeSize / 2);
    int xWidth = UiText::Measure("X", xFont);
    UiText::Draw("X",
                 closeBounds.x + (closeBounds.width - xWidth) * 0.5f,
                 closeBounds.y + (closeBounds.height - xFont) * 0.5f,
                 xFont,
                 UiTheme::Parchment);

    if (closeHovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        dragging = false;
        SetBuilding(nullptr);
        return;
    }

    bool titleHovered = CheckCollisionPointRec(mouse, titleBounds) && !closeHovered;
    if (titleHovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        dragging = true;
        dragOffset = Vec2i{
            static_cast<int>(mouse.x) - pos.x,
            static_cast<int>(mouse.y) - pos.y};
    }
    if (dragging && InputManager::IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        pos.x = std::clamp(static_cast<int>(mouse.x) - dragOffset.x, 0, std::max(0, GetScreenWidth() - size.x));
        pos.y = std::clamp(static_cast<int>(mouse.y) - dragOffset.y, 0, std::max(0, GetScreenHeight() - size.y));
        bounds = Rectangle{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
        titleBounds = Rectangle{bounds.x, bounds.y, bounds.width, static_cast<float>(titleBar)};
        closeBounds = Rectangle{
            bounds.x + bounds.width - closeSize - margin,
            bounds.y + (titleBar - closeSize) * 0.5f,
            static_cast<float>(closeSize),
            static_cast<float>(closeSize)};
    }
    if (dragging && InputManager::IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        dragging = false;

    const std::string title = "Research";
    int titleFont = std::max(26, std::min(36, titleBar / 2 + 4));
    int titleWidth = UiText::Measure(title, titleFont);
    UiText::Draw(title,
                 bounds.x + (bounds.width - titleWidth) * 0.5f,
                 bounds.y + (titleBar - titleFont) * 0.5f,
                 titleFont,
                 UiTheme::Parchment);

    Rectangle treeBounds{
        bounds.x + margin,
        bounds.y + titleBar + margin,
        bounds.width - margin * 2.0f,
        bounds.height - titleBar - margin * 2.0f};
    if (CheckCollisionPointRec(mouse, treeBounds) && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
    {
        treePanning = true;
        lastTreePanMouse = {mouse.x, mouse.y};
    }
    if (treePanning && InputManager::IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    {
        treePanOffset.x += mouse.x - lastTreePanMouse.x;
        treePanOffset.y += mouse.y - lastTreePanMouse.y;
        lastTreePanMouse = {mouse.x, mouse.y};
    }
    if (treePanning && InputManager::IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
        treePanning = false;

    DrawResearchTree(building->owner,
                     building,
                     treeBounds,
                     treePanOffset,
                     treeZoom,
                     selectedTagFilter,
                     researchRequested);
}

// Uses a larger modal-like layout than standard side panels.
void ResearchPanel::UpdateSize(Vec2i windowSize)
{
    pos = Vec2i{static_cast<int>(windowSize.x * 0.03f), static_cast<int>(windowSize.y * 0.04f)};
    size = Vec2i{static_cast<int>(windowSize.x * 0.94f), static_cast<int>(windowSize.y * 0.90f)};
}

void ResearchPanel::AdjustTreeZoom(Vec2i point, float wheel)
{
    if (wheel == 0.0f || building == nullptr)
        return;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    int margin = std::max(12, size.x / 36);
    int titleBar = std::max(38, size.y / 12);
    Rectangle treeBounds{
        bounds.x + margin,
        bounds.y + titleBar + margin,
        bounds.width - margin * 2.0f,
        bounds.height - titleBar - margin * 2.0f};
    Rectangle treeArea{treeBounds.x, treeBounds.y + 60.0f, treeBounds.width, treeBounds.height - 60.0f};
    Vector2 mouse{static_cast<float>(point.x), static_cast<float>(point.y)};
    if (!CheckCollisionPointRec(mouse, treeArea))
        return;

    float oldZoom = treeZoom;
    float newZoom = std::clamp(treeZoom + wheel * 0.08f, 0.42f, 1.15f);
    if (std::abs(newZoom - oldZoom) < 0.001f)
        return;

    float localX = mouse.x - treeArea.x;
    float localY = mouse.y - treeArea.y;
    treePanOffset.x = localX - (localX - treePanOffset.x) * (newZoom / oldZoom);
    treePanOffset.y = localY - (localY - treePanOffset.y) * (newZoom / oldZoom);
    treeZoom = newZoom;
}
