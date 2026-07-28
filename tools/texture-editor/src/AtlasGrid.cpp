#include "AtlasGrid.h"

#include "EditorTheme.h"
#include "EditorWidgets.h"

#include "ui/UiText.h"

#include <algorithm>

namespace
{
    bool Contains(const std::vector<int>& values, int value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }
}

int AtlasGrid::Draw(Rectangle area, const Texture2D& texture, int cellWidth, int cellHeight)
{
    DrawRectangleRounded(area, 0.014f, 8, Ed::Sunken);
    DrawRectangleRoundedLines(area, 0.014f, 8, 1.0f, Ed::BorderSoft);

    hovered = -1;

    if (texture.id == 0 || cellWidth <= 0 || cellHeight <= 0)
    {
        columns = rows = cellCount = 0;
        UiText::DrawFit(emptyMessage,
                        Rectangle{area.x + 16.0f, area.y + area.height * 0.5f - 10.0f, area.width - 32.0f, 20.0f},
                        Ed::FontBody, Ed::TextFaint);
        return -1;
    }

    columns = std::max(1, texture.width / cellWidth);
    rows = std::max(1, texture.height / cellHeight);
    cellCount = columns * rows;

    float contentW = static_cast<float>(columns * cellWidth);
    float contentH = static_cast<float>(rows * cellHeight);
    Vector2 center{area.x + area.width * 0.5f, area.y + area.height * 0.5f};

    if (fitPending)
    {
        zoom = std::clamp(std::min((area.width - 60.0f) / contentW, (area.height - 70.0f) / contentH), 0.2f, 24.0f);
        pan = {0.0f, 0.0f};
        fitPending = false;
    }

    Vector2 mouse = GetMousePosition();
    bool inside = !Ed::InputBlocked() && CheckCollisionPointRec(mouse, area);

    auto originAt = [&](float z, Vector2 p) {
        return Vector2{center.x + p.x - contentW * z * 0.5f, center.y + p.y - contentH * z * 0.5f};
    };

    float wheel = GetMouseWheelMove();
    if (inside && wheel != 0.0f)
    {
        Vector2 origin = originAt(zoom, pan);
        // Pin whatever pixel is under the cursor across the zoom change.
        Vector2 atlasPoint{(mouse.x - origin.x) / zoom, (mouse.y - origin.y) / zoom};
        zoom = std::clamp(zoom * (wheel > 0.0f ? 1.18f : 1.0f / 1.18f), 0.2f, 24.0f);
        pan.x = mouse.x - atlasPoint.x * zoom - center.x + contentW * zoom * 0.5f;
        pan.y = mouse.y - atlasPoint.y * zoom - center.y + contentH * zoom * 0.5f;
    }

    bool panButtonDown = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
    if (inside && !dragging && panButtonDown)
    {
        dragging = true;
        dragMoved = false;
        dragOrigin = mouse;
    }
    if (dragging)
    {
        if (!panButtonDown)
        {
            dragging = false;
        }
        else
        {
            if (std::abs(mouse.x - dragOrigin.x) + std::abs(mouse.y - dragOrigin.y) > 2.0f)
                dragMoved = true;
            pan.x += mouse.x - dragOrigin.x;
            pan.y += mouse.y - dragOrigin.y;
            dragOrigin = mouse;
        }
    }

    Vector2 origin = originAt(zoom, pan);
    float cw = cellWidth * zoom;
    float ch = cellHeight * zoom;

    BeginScissorMode(static_cast<int>(area.x) + 1, static_cast<int>(area.y) + 1,
                     static_cast<int>(area.width) - 2, static_cast<int>(area.height) - 2);

    for (int row = 0; row < rows; row++)
    {
        for (int column = 0; column < columns; column++)
        {
            int id = row * columns + column;
            Rectangle dest{origin.x + column * cw, origin.y + row * ch, cw, ch};
            if (dest.x > area.x + area.width || dest.y > area.y + area.height ||
                dest.x + dest.width < area.x || dest.y + dest.height < area.y)
                continue;

            bool isHighlighted = Contains(highlighted, id);
            bool isSecondary = !isHighlighted && Contains(secondary, id);
            bool isMarked = Contains(marked, id);
            bool faded = dimUnused && !isHighlighted && !isSecondary && !isMarked;

            Ed::Checkerboard(dest, faded ? 0.3f : 1.0f);

            Rectangle src{static_cast<float>(column * cellWidth), static_cast<float>(row * cellHeight),
                          static_cast<float>(cellWidth), static_cast<float>(cellHeight)};
            DrawTexturePro(texture, src, dest, {0.0f, 0.0f}, 0.0f,
                           faded ? Color{255, 255, 255, 70} : WHITE);

            if (CheckCollisionPointRec(mouse, dest) && inside)
                hovered = id;

            DrawRectangleLinesEx(dest, 1.0f, Color{0, 0, 0, 80});
            if (isMarked && !isHighlighted && !isSecondary)
                DrawRectangleLinesEx(dest, 1.0f, Ed::Border);
            if (isSecondary)
            {
                Color fill = secondaryColor;
                fill.a = 30;
                DrawRectangleRec(dest, fill);
                DrawRectangleLinesEx(dest, 2.0f, secondaryColor);
            }
            if (isHighlighted)
            {
                DrawRectangleRec(dest, Ed::AccentSoft);
                DrawRectangleLinesEx(dest, 2.0f, Ed::Accent);
            }

            if (showIds && cw >= 24.0f)
            {
                std::string label = std::to_string(id);
                int fontSize = cw >= 44.0f ? Ed::FontSmall : Ed::FontTiny;
                float textWidth = static_cast<float>(UiText::Measure(label, fontSize));
                Rectangle badge{dest.x + 2.0f, dest.y + 2.0f, textWidth + 7.0f, fontSize + 5.0f};
                DrawRectangleRounded(badge, 0.3f, 4, Color{10, 12, 15, 200});
                UiText::Draw(label, badge.x + 3.5f, badge.y + 2.0f, fontSize,
                             isHighlighted ? Ed::Accent : isSecondary ? secondaryColor : Ed::TextMuted);
            }
        }
    }

    if (hovered >= 0)
    {
        int row = hovered / columns;
        int column = hovered % columns;
        DrawRectangleLinesEx(Rectangle{origin.x + column * cw, origin.y + row * ch, cw, ch}, 2.0f, Ed::TextPrimary);
    }

    EndScissorMode();

    std::string info = std::to_string(columns) + " x " + std::to_string(rows) + " cells (" +
                       std::to_string(cellCount) + ")   " + std::to_string(cellWidth) + "x" +
                       std::to_string(cellHeight) + " px   zoom " +
                       std::to_string(static_cast<int>(zoom * 100.0f)) + "%";
    if (hovered >= 0)
        info += "   cell " + std::to_string(hovered);
    UiText::Draw(info, area.x + 12.0f, area.y + area.height - 22.0f, Ed::FontSmall, Ed::TextFaint);

    std::string help = "click: assign   wheel: zoom   RMB drag: pan";
    UiText::Draw(help, area.x + area.width - UiText::Measure(help, Ed::FontSmall) - 12.0f,
                 area.y + area.height - 22.0f, Ed::FontSmall, Ed::TextFaint);

    // A pan that moved must not also count as a click on whatever it ended over.
    if (inside && !dragMoved && hovered >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return hovered;

    return -1;
}
