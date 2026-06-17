#include "../inc/Gui.h"
#include "../inc/Building.h"

#include "raylib.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

namespace
{
    ResourceIconAtlas resourceIconAtlas;

    Rectangle WidgetBounds(const UiWidget& widget)
    {
        return Rectangle{
            static_cast<float>(widget.pos.x),
            static_cast<float>(widget.pos.y),
            static_cast<float>(widget.size.x),
            static_cast<float>(widget.size.y)};
    }

    Color ResourceColor(ResourceType type)
    {
        switch (type)
        {
            case ResourceType::WOOD: return Color{126, 87, 54, 255};
            case ResourceType::IRON_ORE: return Color{122, 126, 133, 255};
            case ResourceType::COAL: return Color{42, 43, 45, 255};
            case ResourceType::IRON: return Color{175, 176, 180, 255};
            case ResourceType::PLANKS: return Color{190, 139, 78, 255};
            default: return Color{88, 92, 98, 255};
        }
    }

    const char* ResourceShortName(ResourceType type)
    {
        switch (type)
        {
            case ResourceType::WOOD: return "W";
            case ResourceType::IRON_ORE: return "Ore";
            case ResourceType::COAL: return "C";
            case ResourceType::IRON: return "Fe";
            case ResourceType::PLANKS: return "Pl";
            default: return "?";
        }
    }

    void DrawTextFit(const std::string& text, Rectangle bounds, int fontSize, Color color)
    {
        int measured = MeasureText(text.c_str(), fontSize);
        while (fontSize > 8 && measured > bounds.width)
        {
            fontSize--;
            measured = MeasureText(text.c_str(), fontSize);
        }

        DrawText(text.c_str(), static_cast<int>(bounds.x), static_cast<int>(bounds.y), fontSize, color);
    }

    void DrawResourceCard(const ResourceBufferView& view, Rectangle bounds)
    {
        DrawRectangleRounded(bounds, 0.08f, 6, Color{38, 42, 48, 230});
        DrawRectangleRoundedLines(bounds, 0.08f, 6, Color{88, 94, 104, 255});

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
        DrawTextFit(amount, {bounds.x + iconSize + 18.0f, bounds.y + 8.0f, bounds.width - iconSize - 26.0f, 20.0f}, 16, RAYWHITE);

        if (view.recipeAmount > 0)
        {
            std::string recipe = "x" + std::to_string(view.recipeAmount);
            int fontSize = 14;
            int textWidth = MeasureText(recipe.c_str(), fontSize);
            DrawRectangleRounded({icon.x + icon.width - textWidth - 8.0f, icon.y + icon.height - 18.0f, static_cast<float>(textWidth + 8), 18.0f}, 0.25f, 4, Color{18, 20, 24, 230});
            DrawText(recipe.c_str(), static_cast<int>(icon.x + icon.width - textWidth - 4.0f), static_cast<int>(icon.y + icon.height - 16.0f), fontSize, RAYWHITE);
        }
    }

    void DrawResourceIcon(const ResourceBufferView& view, Rectangle bounds)
    {
        DrawRectangleRounded(bounds, 0.10f, 8, Color{32, 36, 42, 230});
        DrawRectangleRoundedLines(bounds, 0.10f, 8, Color{86, 96, 110, 255});

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

        std::string amount = std::to_string(view.amount);
        int fontSize = std::max(12, static_cast<int>(bounds.height * 0.18f));
        int textWidth = MeasureText(amount.c_str(), fontSize);
        Rectangle badge{
            bounds.x + bounds.width - textWidth - 12.0f,
            bounds.y + bounds.height - fontSize - 8.0f,
            static_cast<float>(textWidth + 8),
            static_cast<float>(fontSize + 4)};

        DrawRectangleRounded(badge, 0.25f, 6, Color{12, 14, 18, 230});
        DrawText(amount.c_str(), static_cast<int>(badge.x + 4.0f), static_cast<int>(badge.y + 2.0f), fontSize, RAYWHITE);
    }

    void DrawResourceGrid(const std::vector<ResourceBufferView>& views, Rectangle bounds, int columns)
    {
        if (views.empty())
        {
            DrawTextFit("Empty", {bounds.x, bounds.y + 6.0f, bounds.width, 20.0f}, 16, Color{170, 178, 188, 255});
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

    void DrawResourceIconGrid(const std::vector<ResourceBufferView>& views, Rectangle bounds, int columns)
    {
        if (views.empty())
        {
            DrawTextFit("Empty storage", {bounds.x, bounds.y + 6.0f, bounds.width, 22.0f}, 17, Color{170, 178, 188, 255});
            return;
        }

        columns = std::max(1, columns);
        float gap = 8.0f;
        float cellW = (bounds.width - gap * (columns - 1)) / columns;
        float cellH = cellW;

        for (int i = 0; i < views.size(); i++)
        {
            int col = i % columns;
            int row = i / columns;
            float x = bounds.x + col * (cellW + gap);
            float y = bounds.y + row * (cellH + gap);

            if (y + cellH > bounds.y + bounds.height)
                break;

            DrawResourceIcon(views[i], Rectangle{x, y, cellW, cellH});
        }
    }
}

void UiButton::Update(double dt)
{
    Rectangle bounds = WidgetBounds(*this);
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, bounds);
    bool pressed = hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    if ((hovered && hasHoverTexture) || (!hovered && hasNormalTexture))
    {
        Texture2D tex = (hovered && hasHoverTexture) ? hoverTexture : normalTexture;
        Rectangle src{0.0f, 0.0f, static_cast<float>(tex.width), static_cast<float>(tex.height)};
        DrawTexturePro(tex, src, bounds, {0.0f, 0.0f}, 0.0f, WHITE);
    }
    else
    {
        Color fill = hovered ? Color{78, 89, 106, 255} : Color{45, 52, 63, 255};
        Color line = hovered ? Color{151, 171, 199, 255} : Color{95, 106, 122, 255};
        DrawRectangleRounded(bounds, 0.08f, 8, fill);
        DrawRectangleRoundedLines(bounds, 0.08f, 8, line);
    }

    if (drawText)
    {
        int fontSize = std::max(14, size.y / 3);
        int textWidth = MeasureText(text.c_str(), fontSize);
        DrawText(text.c_str(),
                 static_cast<int>(bounds.x + (bounds.width - textWidth) * 0.5f),
                 static_cast<int>(bounds.y + (bounds.height - fontSize) * 0.5f),
                 fontSize,
                 RAYWHITE);
    }

    if (pressed)
        OnClick();
}

UiButton::UiButton()
{
    LoadTextures("assets/ui/button_plain.png", "assets/ui/button_hover.png");
}

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

void CheckBox::Update(double dt)
{
    GuiCheckBox(WidgetBounds(*this), text.c_str(), &currentState);
}

void SliderBar::Update(double dt)
{
    GuiSliderBar(WidgetBounds(*this), text.c_str(), nullptr, &currentValue, 0.0f, 1.0f);
}

void ProgressBar::Update(double dt)
{
    Rectangle bounds = WidgetBounds(*this);
    DrawRectangleRounded(bounds, 0.12f, 6, Color{33, 36, 42, 255});

    Rectangle fill = bounds;
    fill.width *= value;
    DrawRectangleRounded(fill, 0.12f, 6, Color{79, 181, 128, 255});
    DrawRectangleRoundedLines(bounds, 0.12f, 6, Color{99, 108, 120, 255});

    std::string label = text + " " + std::to_string(static_cast<int>(value * 100.0f)) + "%";
    int fontSize = 16;
    int textWidth = MeasureText(label.c_str(), fontSize);
    DrawText(label.c_str(), static_cast<int>(bounds.x + (bounds.width - textWidth) * 0.5f), static_cast<int>(bounds.y + 5.0f), fontSize, RAYWHITE);
}

void VBox::Update(double dt)
{
    for(auto& child : children)
    {
        child->Update(dt);
    }
}

void HBox::Update(double dt)
{
    for(auto& child : children)
    {
        child->Update(dt);
    }
}

void TextBox::Update(double dt)
{
    GuiTextBox(WidgetBounds(*this), textOutput, 18, true);
    text = textOutput;
}

bool UiImage::LoadTextureFromFile(const std::string& path)
{
    if (!FileExists(path.c_str()))
        return false;

    texture = LoadTexture(path.c_str());
    hasTexture = texture.id != 0;
    return hasTexture;
}

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
        DrawRectangleRounded(bounds, 0.04f, 8, Color{25, 29, 34, 255});
        DrawRectangleRoundedLines(bounds, 0.04f, 8, Color{70, 78, 90, 255});
    }
}

void ResourceIconAtlas::Load(const std::string& path, Vec2i iconSize)
{
    if (!FileExists(path.c_str()))
        return;

    texture = LoadTexture(path.c_str());
    loaded = texture.id != 0;
    size = iconSize;
}

Rectangle ResourceIconAtlas::GetRect(ResourceType type) const
{
    int index = std::max(0, static_cast<int>(type) - 1);
    int columns = std::max(1, texture.width / size.x);
    return Rectangle{
        static_cast<float>((index % columns) * size.x),
        static_cast<float>((index / columns) * size.y),
        static_cast<float>(size.x),
        static_cast<float>(size.y)};
}

void GuiPanel::Update(double dt)
{
    Rectangle bounds = WidgetBounds(*this);

    if (building == nullptr)
        return;

    int margin = std::max(10, size.x / 24);
    int titleBar = std::max(34, size.y / 12);

    DrawRectangleRounded(bounds, 0.025f, 8, Color{28, 32, 38, 238});
    DrawRectangleRoundedLines(bounds, 0.025f, 8, Color{92, 102, 118, 255});

    Rectangle titleBounds{
        bounds.x,
        bounds.y,
        bounds.width,
        static_cast<float>(titleBar)};
    DrawRectangleRounded(titleBounds, 0.025f, 8, Color{44, 52, 65, 255});
    DrawLine(static_cast<int>(bounds.x), static_cast<int>(bounds.y + titleBar),
             static_cast<int>(bounds.x + bounds.width), static_cast<int>(bounds.y + titleBar),
             Color{105, 118, 136, 255});

    int closeSize = std::max(20, titleBar - 14);
    Rectangle closeBounds{
        bounds.x + bounds.width - closeSize - margin,
        bounds.y + (titleBar - closeSize) * 0.5f,
        static_cast<float>(closeSize),
        static_cast<float>(closeSize)};
    bool closeHovered = CheckCollisionPointRec(GetMousePosition(), closeBounds);
    DrawRectangleRounded(closeBounds, 0.16f, 6, closeHovered ? Color{120, 62, 62, 255} : Color{65, 72, 84, 255});
    DrawRectangleRoundedLines(closeBounds, 0.16f, 6, closeHovered ? Color{210, 125, 125, 255} : Color{120, 132, 150, 255});
    int xFont = std::max(13, closeSize / 2);
    int xWidth = MeasureText("X", xFont);
    DrawText("X",
             static_cast<int>(closeBounds.x + (closeBounds.width - xWidth) * 0.5f),
             static_cast<int>(closeBounds.y + (closeBounds.height - xFont) * 0.5f),
             xFont,
             RAYWHITE);

    if (closeHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        SetBuilding(nullptr);
        return;
    }

    int titleFont = std::max(17, std::min(24, titleBar / 2));
    int titleWidth = MeasureText(text.c_str(), titleFont);
    while (titleFont > 14 && titleWidth > bounds.width - (closeSize + margin) * 2 - margin)
    {
        titleFont--;
        titleWidth = MeasureText(text.c_str(), titleFont);
    }
    DrawText(text.c_str(),
             static_cast<int>(bounds.x + (bounds.width - titleWidth) * 0.5f),
             static_cast<int>(bounds.y + (titleBar - titleFont) * 0.5f),
             titleFont,
             RAYWHITE);

    int y = pos.y + titleBar + margin;
    int contentX = pos.x + margin;
    int contentW = size.x - margin * 2;
    int bottom = pos.y + size.y - margin;

    if (building->buildingType == BuildingType::StorageBuilding)
    {
        DrawText("Storage", contentX, y, 18, Color{190, 198, 208, 255});
        y += 28;

        Rectangle grid{
            static_cast<float>(contentX),
            static_cast<float>(y),
            static_cast<float>(contentW),
            static_cast<float>(bottom - y)};
        DrawResourceIconGrid(building->GetOutputBufferViews(), grid, 4);
        return;
    }

    if (building->buildingType == BuildingType::MilitaryBuilding)
    {
        DrawTextFit("Military interface placeholder", {static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 24.0f}, 18, RAYWHITE);
        DrawTextFit("Orders, garrison and territory stats will live here.", {static_cast<float>(contentX), static_cast<float>(y + 30), static_cast<float>(contentW), 24.0f}, 15, Color{180, 188, 198, 255});
        return;
    }

    int buttonSpace = building->CanBlockProduction() ? lockButton.size.y + margin : 0;
    int connectionsH = std::max(48, size.y / 8);
    int statsH = std::max(76, size.y / 5);
    int progressH = progressBar.size.y;
    int resourcesBottom = bottom - buttonSpace - statsH - connectionsH - progressH - margin * 4;
    int resourcesH = std::max(74, resourcesBottom - y);

    int headerH = 22;
    int columnGap = std::max(8, margin / 2);
    int columnW = (contentW - columnGap) / 2;

    DrawText("Input", contentX, y, 16, Color{190, 198, 208, 255});
    DrawText("Output", contentX + columnW + columnGap, y, 16, Color{190, 198, 208, 255});
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
    DrawResourceGrid(building->GetInputBufferViews(), inputGrid, 1);
    DrawResourceGrid(building->GetOutputBufferViews(), outputGrid, 1);

    y = static_cast<int>(inputGrid.y + inputGrid.height + margin);
    progressBar.pos = Vec2i{contentX, y};
    progressBar.size = Vec2i{contentW, progressH};
    progressBar.SetValue(building->GetProductionProgress());
    progressBar.Update(dt);

    y += progressH + margin;
    auto receivers = building->GetReceiverViews();
    auto suppliers = building->GetSupplierViews();
    int connectionLine = std::max(15, std::min(18, connectionsH / 3));
    if (!receivers.empty())
    {
        for (const auto& receiver : receivers)
        {
            std::string label = rt2s(receiver.type) + " -> " + (receiver.building != nullptr ? receiver.building->name : "No receiver");
            Color color = receiver.building != nullptr ? RAYWHITE : Color{238, 184, 84, 255};
            DrawTextFit(label, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), static_cast<float>(connectionLine)}, connectionLine - 3, color);
            y += connectionLine;
        }
    }
    else if (!building->GetOutputBufferViews().empty())
    {
        DrawTextFit("No receiver", Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), static_cast<float>(connectionLine)}, connectionLine - 3, Color{238, 184, 84, 255});
        y += connectionLine;
    }

    for (const auto& supplier : suppliers)
    {
        std::string label = rt2s(supplier.type) + " <- " + (supplier.building != nullptr ? supplier.building->name : "No supplier");
        Color color = supplier.building != nullptr ? Color{190, 215, 255, 255} : Color{238, 184, 84, 255};
        DrawTextFit(label, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), static_cast<float>(connectionLine)}, connectionLine - 3, color);
        y += connectionLine;
    }

    y += margin / 2;
    float efficiency = building->GetEfficiency() * 100.0f;
    std::vector<std::string> stats{
        "Produced: " + std::to_string(building->GetTotalProduced()),
        "Efficiency: " + std::to_string(static_cast<int>(efficiency)) + "%",
        "Active: " + std::to_string(static_cast<int>(building->GetActiveTime())) + "s",
        "Lifetime: " + std::to_string(static_cast<int>(building->GetLifetime())) + "s"};

    int statLine = std::max(16, std::min(21, statsH / static_cast<int>(stats.size())));
    for (const auto& stat : stats)
    {
        DrawTextFit(stat, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), static_cast<float>(statLine)}, statLine - 3, RAYWHITE);
        y += statLine;
    }

    if (building->CanBlockProduction())
    {
        lockButton.pos = Vec2i{contentX, bottom - lockButton.size.y};
        lockButton.size = Vec2i{contentW, lockButton.size.y};
        lockButton.ChangeText(building->IsProductionBlocked() ? "Unlock production" : "Block production");
        lockButton.Update(dt);
    }
}

GuiPanel::GuiPanel()
{
    lockButton.func = [this]()
    {
        if (building != nullptr && building->CanBlockProduction())
            building->SetProductionBlocked(!building->IsProductionBlocked());
    };
}

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
}

void GuiPanel::SetBuilding(Building* ptr)
{
    building = ptr;
    ChangeText(building != nullptr ? building->name : "Gui Panel");
}

void GuiPanel::LoadResourceAtlas(const std::string& path, Vec2i iconSize)
{
    resourceIconAtlas.Load(path, iconSize);
}
