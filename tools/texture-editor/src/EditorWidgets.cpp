#include "EditorWidgets.h"

#include "EditorTheme.h"

#include "ui/UiText.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    // Only one control can be held at a time, so key repeat needs one slot, not
    // a map. The id is the button rectangle's origin, which is stable per frame.
    struct RepeatState
    {
        float x{0.0f};
        float y{0.0f};
        double heldSince{0.0};
        double lastFire{0.0};
    };
    RepeatState repeat;

    bool inputBlocked = false;

    bool StepButton(Rectangle bounds, const std::string& label)
    {
        Vector2 mouse = GetMousePosition();
        bool hover = !inputBlocked && CheckCollisionPointRec(mouse, bounds);
        bool down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);

        DrawRectangleRounded(bounds, 0.28f, 6, down ? Ed::Accent : hover ? Ed::Hover : Ed::Raised);
        DrawRectangleRoundedLines(bounds, 0.28f, 6, 1.0f, hover ? Ed::Accent : Ed::Border);
        UiText::DrawFit(label,
                        Rectangle{bounds.x, bounds.y + 2.0f, bounds.width, bounds.height - 4.0f},
                        Ed::FontBody, down ? Ed::Void : Ed::TextPrimary);

        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            repeat.x = bounds.x;
            repeat.y = bounds.y;
            repeat.heldSince = GetTime();
            repeat.lastFire = GetTime();
            return true;
        }

        bool sameButton = std::abs(repeat.x - bounds.x) < 0.5f && std::abs(repeat.y - bounds.y) < 0.5f;
        if (down && sameButton && GetTime() - repeat.heldSince > 0.4)
        {
            if (GetTime() - repeat.lastFire > 0.045)
            {
                repeat.lastFire = GetTime();
                return true;
            }
        }

        return false;
    }
}

namespace Ed
{
    void SetInputBlocked(bool blocked) { inputBlocked = blocked; }
    bool InputBlocked() { return inputBlocked; }

    Rectangle Panel(Rectangle bounds, const std::string& title, const std::string& subtitle)
    {
        DrawRectangleRounded(bounds, 0.014f, 8, Surface);
        DrawRectangleRoundedLines(bounds, 0.014f, 8, 1.0f, Border);

        if (title.empty())
            return Rectangle{bounds.x + 1.0f, bounds.y + 1.0f, bounds.width - 2.0f, bounds.height - 2.0f};

        float headerHeight = subtitle.empty() ? 36.0f : 52.0f;
        UiText::Draw(title, bounds.x + 14.0f, bounds.y + 9.0f, FontTitle, TextPrimary);
        if (!subtitle.empty())
        {
            UiText::DrawFit(subtitle,
                            Rectangle{bounds.x + 14.0f, bounds.y + 31.0f, bounds.width - 28.0f, 16.0f},
                            FontSmall, TextFaint);
        }
        DrawLineEx({bounds.x + 1.0f, bounds.y + headerHeight},
                   {bounds.x + bounds.width - 1.0f, bounds.y + headerHeight}, 1.0f, BorderSoft);

        return Rectangle{bounds.x + 1.0f, bounds.y + headerHeight + 1.0f,
                         bounds.width - 2.0f, bounds.height - headerHeight - 2.0f};
    }

    float SectionHeader(Rectangle bounds, float y, const std::string& label)
    {
        UiText::Draw(label, bounds.x, y, FontSmall, TextFaint);
        float textWidth = static_cast<float>(UiText::Measure(label, FontSmall));
        DrawLineEx({bounds.x + textWidth + 10.0f, y + 8.0f},
                   {bounds.x + bounds.width, y + 8.0f}, 1.0f, BorderSoft);
        return y + 24.0f;
    }

    bool Button(Rectangle bounds, const std::string& label, bool active, bool enabled)
    {
        Vector2 mouse = GetMousePosition();
        bool hover = enabled && !inputBlocked && CheckCollisionPointRec(mouse, bounds);

        Color fill = !enabled ? Sunken : active ? AccentDim : hover ? Hover : Raised;
        Color border = !enabled ? BorderSoft : (active || hover) ? Accent : Border;
        Color text = !enabled ? TextFaint : TextPrimary;

        DrawRectangleRounded(bounds, 0.2f, 6, fill);
        DrawRectangleRoundedLines(bounds, 0.2f, 6, 1.0f, border);
        UiText::DrawFit(label, Rectangle{bounds.x + 8.0f, bounds.y + 4.0f, bounds.width - 16.0f, bounds.height - 8.0f},
                        FontBody, text);

        return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    }

    bool DangerButton(Rectangle bounds, const std::string& label)
    {
        Vector2 mouse = GetMousePosition();
        bool hover = !inputBlocked && CheckCollisionPointRec(mouse, bounds);

        DrawRectangleRounded(bounds, 0.2f, 6, hover ? DangerSoft : Raised);
        DrawRectangleRoundedLines(bounds, 0.2f, 6, 1.0f, hover ? Danger : Border);
        UiText::DrawFit(label, Rectangle{bounds.x + 8.0f, bounds.y + 4.0f, bounds.width - 16.0f, bounds.height - 8.0f},
                        FontBody, hover ? Danger : TextMuted);

        return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    }

    bool Checkbox(Rectangle bounds, const std::string& label, bool& value)
    {
        Vector2 mouse = GetMousePosition();

        float box = 18.0f;
        // Hit area is the box plus its label, not the whole row: callers pass a
        // full-width rect for alignment, and a stray click far to the right of a
        // short label must not silently toggle a setting.
        Rectangle hit{bounds.x, bounds.y,
                      std::min(bounds.width, box + 10.0f + UiText::Measure(label, FontBody) + 6.0f),
                      bounds.height};
        bool hover = !inputBlocked && CheckCollisionPointRec(mouse, hit);

        Rectangle boxRect{bounds.x, bounds.y + (bounds.height - box) * 0.5f, box, box};
        DrawRectangleRounded(boxRect, 0.24f, 6, value ? Accent : hover ? Hover : Raised);
        DrawRectangleRoundedLines(boxRect, 0.24f, 6, 1.0f, value ? Accent : hover ? Accent : Border);
        if (value)
        {
            DrawLineEx({boxRect.x + 4.5f, boxRect.y + 9.0f}, {boxRect.x + 7.5f, boxRect.y + 12.5f}, 2.0f, Void);
            DrawLineEx({boxRect.x + 7.5f, boxRect.y + 12.5f}, {boxRect.x + 13.5f, boxRect.y + 5.5f}, 2.0f, Void);
        }

        UiText::Draw(label, bounds.x + box + 10.0f, bounds.y + (bounds.height - FontBody) * 0.5f - 1.0f,
                     FontBody, hover ? TextPrimary : TextMuted);

        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            value = !value;
            return true;
        }
        return false;
    }

    bool IntStepper(Rectangle bounds, const std::string& label, int& value, int minimum, int maximum,
                    int step, const std::string& suffix)
    {
        float buttonSize = std::min(26.0f, bounds.height);
        float valueWidth = 62.0f;
        float controlsWidth = buttonSize * 2.0f + valueWidth + 8.0f;

        UiText::Draw(label, bounds.x, bounds.y + (bounds.height - FontBody) * 0.5f - 1.0f, FontBody, TextMuted);

        float x = bounds.x + bounds.width - controlsWidth;
        float y = bounds.y + (bounds.height - buttonSize) * 0.5f;

        bool changed = false;
        if (StepButton({x, y, buttonSize, buttonSize}, "-") && value > minimum)
        {
            value = std::max(minimum, value - step);
            changed = true;
        }

        Rectangle valueRect{x + buttonSize + 4.0f, y, valueWidth, buttonSize};
        DrawRectangleRounded(valueRect, 0.28f, 6, Sunken);
        DrawRectangleRoundedLines(valueRect, 0.28f, 6, 1.0f, BorderSoft);
        UiText::DrawFit(std::to_string(value) + suffix,
                        Rectangle{valueRect.x + 4.0f, valueRect.y + 3.0f, valueRect.width - 8.0f, valueRect.height - 6.0f},
                        FontBody, TextPrimary);

        if (StepButton({valueRect.x + valueWidth + 4.0f, y, buttonSize, buttonSize}, "+") && value < maximum)
        {
            value = std::min(maximum, value + step);
            changed = true;
        }

        return changed;
    }

    bool DoubleStepper(Rectangle bounds, const std::string& label, double& value, double minimum,
                       double maximum, double step, const char* format)
    {
        float buttonSize = std::min(26.0f, bounds.height);
        float valueWidth = 62.0f;
        float controlsWidth = buttonSize * 2.0f + valueWidth + 8.0f;

        UiText::Draw(label, bounds.x, bounds.y + (bounds.height - FontBody) * 0.5f - 1.0f, FontBody, TextMuted);

        float x = bounds.x + bounds.width - controlsWidth;
        float y = bounds.y + (bounds.height - buttonSize) * 0.5f;

        bool changed = false;
        if (StepButton({x, y, buttonSize, buttonSize}, "-") && value > minimum)
        {
            value = std::max(minimum, value - step);
            changed = true;
        }

        Rectangle valueRect{x + buttonSize + 4.0f, y, valueWidth, buttonSize};
        DrawRectangleRounded(valueRect, 0.28f, 6, Sunken);
        DrawRectangleRoundedLines(valueRect, 0.28f, 6, 1.0f, BorderSoft);
        char text[32];
        std::snprintf(text, sizeof(text), format, value);
        UiText::DrawFit(text,
                        Rectangle{valueRect.x + 4.0f, valueRect.y + 3.0f, valueRect.width - 8.0f, valueRect.height - 6.0f},
                        FontBody, TextPrimary);

        if (StepButton({valueRect.x + valueWidth + 4.0f, y, buttonSize, buttonSize}, "+") && value < maximum)
        {
            value = std::min(maximum, value + step);
            changed = true;
        }

        return changed;
    }

    void Label(float x, float y, const std::string& text, int fontSize, Color color)
    {
        UiText::Draw(text, x, y, fontSize, color);
    }

    void TextEllipsized(float x, float y, float maxWidth, const std::string& text, int fontSize, Color color)
    {
        if (static_cast<float>(UiText::Measure(text, fontSize)) <= maxWidth)
        {
            UiText::Draw(text, x, y, fontSize, color);
            return;
        }

        // Drop whole codepoints, not bytes: these strings carry file paths that
        // may well be UTF-8, and half a codepoint renders as garbage.
        std::string clipped = text;
        while (!clipped.empty() &&
               static_cast<float>(UiText::Measure(clipped + "...", fontSize)) > maxWidth)
        {
            Utf8::RemoveLast(clipped);
        }
        UiText::Draw(clipped + "...", x, y, fontSize, color);
    }

    void KeyValue(Rectangle row, const std::string& key, const std::string& value, Color valueColor)
    {
        UiText::Draw(key, row.x, row.y, FontSmall, TextFaint);
        float width = static_cast<float>(UiText::Measure(value, FontSmall));
        float keyWidth = static_cast<float>(UiText::Measure(key, FontSmall));
        float available = row.width - keyWidth - 12.0f;
        if (width <= available)
            UiText::Draw(value, row.x + row.width - width, row.y, FontSmall, valueColor);
        else
            UiText::DrawFit(value, Rectangle{row.x + keyWidth + 12.0f, row.y, available, 16.0f}, FontSmall, valueColor);
    }

    void Badge(Rectangle bounds, const std::string& text, Color accent)
    {
        Color fill = accent;
        fill.a = 34;
        DrawRectangleRounded(bounds, 0.42f, 6, fill);
        DrawRectangleRoundedLines(bounds, 0.42f, 6, 1.0f, accent);
        UiText::DrawFit(text, Rectangle{bounds.x + 5.0f, bounds.y + 2.0f, bounds.width - 10.0f, bounds.height - 4.0f},
                        FontTiny, accent);
    }

    void Checkerboard(Rectangle area, float alpha)
    {
        constexpr float square = 8.0f;
        auto scale = [alpha](Color c) {
            return Color{c.r, c.g, c.b, static_cast<unsigned char>(std::clamp(c.a * alpha, 0.0f, 255.0f))};
        };
        Color light = scale(CheckerA);
        Color dark = scale(CheckerB);

        int rows = static_cast<int>(std::ceil(area.height / square));
        int columns = static_cast<int>(std::ceil(area.width / square));
        for (int y = 0; y < rows; y++)
        {
            for (int x = 0; x < columns; x++)
            {
                float left = area.x + x * square;
                float top = area.y + y * square;
                DrawRectangleRec(Rectangle{left, top,
                                           std::min(square, area.x + area.width - left),
                                           std::min(square, area.y + area.height - top)},
                                 ((x + y) % 2 == 0) ? light : dark);
            }
        }
    }

    void TextureFitted(Rectangle area, const Texture2D& texture, float maxScale)
    {
        if (texture.id == 0 || texture.width <= 0 || texture.height <= 0)
        {
            UiText::DrawFit("no texture", Rectangle{area.x, area.y + area.height * 0.5f - 9.0f, area.width, 18.0f},
                            FontBody, Danger);
            return;
        }

        float scale = std::min(std::min(area.width / texture.width, area.height / texture.height), maxScale);
        float width = texture.width * scale;
        float height = texture.height * scale;
        Rectangle dest{area.x + (area.width - width) * 0.5f, area.y + (area.height - height) * 0.5f, width, height};

        Checkerboard(dest);
        DrawTexturePro(texture,
                       Rectangle{0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)},
                       dest, {0.0f, 0.0f}, 0.0f, WHITE);
        DrawRectangleLinesEx(dest, 1.0f, BorderSoft);
    }

    void AtlasCell(Rectangle dest, const Texture2D& texture, int cellWidth, int cellHeight, int cellId,
                   Color border)
    {
        Checkerboard(dest);
        if (texture.id != 0 && cellWidth > 0 && cellHeight > 0)
        {
            int columns = std::max(1, texture.width / cellWidth);
            Rectangle src{static_cast<float>((cellId % columns) * cellWidth),
                          static_cast<float>((cellId / columns) * cellHeight),
                          static_cast<float>(cellWidth), static_cast<float>(cellHeight)};
            DrawTexturePro(texture, src, dest, {0.0f, 0.0f}, 0.0f, WHITE);
        }
        DrawRectangleLinesEx(dest, 1.0f, border);
    }

    std::string FormatFileSize(long long bytes)
    {
        char buffer[32];
        if (bytes >= 1024 * 1024)
            std::snprintf(buffer, sizeof(buffer), "%.1f MB", bytes / (1024.0 * 1024.0));
        else if (bytes >= 1024)
            std::snprintf(buffer, sizeof(buffer), "%.0f kB", bytes / 1024.0);
        else
            std::snprintf(buffer, sizeof(buffer), "%lld B", bytes);
        return buffer;
    }

    int RowList::Draw(Rectangle area, int rowCount, float rowHeight,
                      const std::function<void(int, Rectangle, bool, bool)>& drawRow)
    {
        float contentHeight = rowCount * rowHeight;
        float maxScroll = std::max(0.0f, contentHeight - area.height);

        if (revealRequest >= 0 && revealRequest < rowCount)
        {
            float top = revealRequest * rowHeight;
            if (top < scroll)
                scroll = top;
            else if (top + rowHeight > scroll + area.height)
                scroll = top + rowHeight - area.height;
            revealRequest = -1;
        }

        Vector2 mouse = GetMousePosition();
        bool inside = !inputBlocked && CheckCollisionPointRec(mouse, area);
        float wheel = GetMouseWheelMove();
        if (inside && wheel != 0.0f)
            scroll -= wheel * rowHeight * 2.0f;
        scroll = std::clamp(scroll, 0.0f, maxScroll);

        BeginScissorMode(static_cast<int>(area.x), static_cast<int>(area.y),
                         static_cast<int>(area.width), static_cast<int>(area.height));

        int clicked = -1;
        int first = std::max(0, static_cast<int>(scroll / rowHeight) - 1);
        int last = std::min(rowCount - 1, static_cast<int>((scroll + area.height) / rowHeight) + 1);

        for (int i = first; i <= last; i++)
        {
            Rectangle row{area.x, area.y + i * rowHeight - scroll, area.width, rowHeight - 2.0f};
            bool hovered = inside && CheckCollisionPointRec(mouse, row);
            bool isSelected = i == selected;

            Rectangle body{row.x + 4.0f, row.y, row.width - 12.0f, row.height};
            DrawRectangleRounded(body, 0.1f, 6, isSelected ? AccentSoft : hovered ? Hover : Raised);
            if (isSelected)
            {
                DrawRectangleRoundedLines(body, 0.1f, 6, 1.0f, Accent);
                DrawRectangleRec(Rectangle{body.x, body.y + 3.0f, 3.0f, body.height - 6.0f}, Accent);
            }

            drawRow(i, Rectangle{body.x + 12.0f, body.y, body.width - 24.0f, body.height}, hovered, isSelected);

            if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                selected = i;
                clicked = i;
            }
        }

        EndScissorMode();

        if (maxScroll > 0.0f)
        {
            float thumbHeight = std::max(28.0f, area.height * (area.height / contentHeight));
            float thumbY = area.y + (area.height - thumbHeight) * (scroll / maxScroll);
            DrawRectangleRounded(Rectangle{area.x + area.width - 6.0f, thumbY, 4.0f, thumbHeight}, 0.5f, 4, Border);
        }

        return clicked;
    }

    void Picker::Open(std::string pickerTitle, std::vector<PickerRow> pickerRows)
    {
        open = true;
        title = std::move(pickerTitle);
        rows = std::move(pickerRows);
        filter.clear();
        list.selected = -1;
        list.scroll = 0.0f;
    }

    void Picker::Close()
    {
        open = false;
        rows.clear();
        filter.clear();
    }

    std::string Picker::Draw(Rectangle screen)
    {
        if (!open)
            return {};

        DrawRectangleRec(screen, Color{0, 0, 0, 170});

        float width = std::min(720.0f, screen.width - 80.0f);
        float height = std::min(640.0f, screen.height - 80.0f);
        Rectangle dialog{screen.x + (screen.width - width) * 0.5f, screen.y + (screen.height - height) * 0.5f,
                         width, height};

        // Type to filter. Raw char input rather than a focused text field: the
        // overlay owns all input while it is up, so there is nothing to focus.
        int codepoint = GetCharPressed();
        while (codepoint > 0)
        {
            filter += Utf8::Encode(codepoint);
            list.scroll = 0.0f;
            codepoint = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) && !filter.empty())
            Utf8::RemoveLast(filter);

        std::string lowered;
        for (char c : filter)
            lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        std::vector<int> visible;
        for (size_t i = 0; i < rows.size(); i++)
        {
            if (lowered.empty())
            {
                visible.push_back(static_cast<int>(i));
                continue;
            }
            std::string haystack = rows[i].label + " " + rows[i].sublabel;
            std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (haystack.find(lowered) != std::string::npos)
                visible.push_back(static_cast<int>(i));
        }

        Rectangle content = Panel(dialog, title,
                                  filter.empty() ? "type to filter   -   ESC to cancel"
                                                 : "filter: " + filter);

        Rectangle listArea{content.x + 8.0f, content.y + 8.0f, content.width - 16.0f, content.height - 16.0f};
        int clicked = list.Draw(listArea, static_cast<int>(visible.size()), 52.0f,
                                [&](int index, Rectangle row, bool, bool) {
                                    const PickerRow& entry = rows[visible[index]];

                                    Rectangle thumb{row.x, row.y + 7.0f, 38.0f, 38.0f};
                                    if (entry.cellWidth > 0 && entry.cellHeight > 0)
                                        AtlasCell(thumb, entry.thumbnail, entry.cellWidth, entry.cellHeight,
                                                  entry.cellId, BorderSoft);
                                    else
                                        TextureFitted(thumb, entry.thumbnail);

                                    UiText::Draw(entry.label, row.x + 50.0f, row.y + 8.0f, FontBody, TextPrimary);
                                    UiText::Draw(entry.sublabel, row.x + 50.0f, row.y + 28.0f, FontSmall, TextFaint);
                                });

        std::string chosen;
        if (clicked >= 0 && clicked < static_cast<int>(visible.size()))
        {
            chosen = rows[visible[clicked]].id;
            Close();
        }

        if (IsKeyPressed(KEY_ESCAPE))
            Close();

        return chosen;
    }
}
