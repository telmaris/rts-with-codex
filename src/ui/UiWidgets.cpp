#include "ui/UiWidgets.h"

#include "ui/ControlIcons.h"
#include "ui/UiText.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float rowHeight = 26.0f;
    constexpr int maxVisibleRows = 10;

    constexpr UiWidgetPalette defaultPalette{
        UiTheme::Surface, UiTheme::SurfaceHover, UiTheme::InsetHover,
        UiTheme::Iron, UiTheme::SteelHover, UiTheme::Bronze,
        UiTheme::Parchment, UiTheme::ParchmentDim, UiTheme::ParchmentFaint,
        UiTheme::Inset, UiTheme::SurfaceHover, UiTheme::Surface};
    UiWidgetPalette activePalette = defaultPalette;

    // At most one dropdown may be expanded at a time; DrawOpenList() renders
    // this one after every panel has drawn.
    DropdownWidget* openDropdown = nullptr;
    Rectangle openBounds{};

    TextFieldWidget* focusedField = nullptr;

    const std::string emptyText;

    // Truncates from the FRONT so the tail stays visible — for a text field the
    // characters you just typed matter more than the ones you typed first.
    std::string FitTail(const std::string& text, float maxWidth, int fontSize)
    {
        if (UiText::Measure(text, fontSize) <= maxWidth)
            return text;

        std::string trimmed = text;
        while (!trimmed.empty() && UiText::Measure("..." + trimmed, fontSize) > maxWidth)
        {
            // Drop a whole leading codepoint, never a stray continuation byte.
            size_t index = 1;
            while (index < trimmed.size() && (static_cast<unsigned char>(trimmed[index]) & 0xC0) == 0x80)
                index++;
            trimmed.erase(0, index);
        }
        return "..." + trimmed;
    }

    bool IsUnsignedNumericChar(int codepoint)
    {
        return (codepoint >= '0' && codepoint <= '9') || codepoint == '.';
    }
}

void SetUiWidgetPalette(const UiWidgetPalette& palette)
{
    activePalette = palette;
}

void ResetUiWidgetPalette()
{
    activePalette = defaultPalette;
}

// --- DropdownWidget ---------------------------------------------------------

void DropdownWidget::SetOptions(std::vector<std::string> values)
{
    options = std::move(values);
    if (selectedIndex >= static_cast<int>(options.size()))
        selectedIndex = -1;
}

const std::string& DropdownWidget::SelectedText() const
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(options.size()))
        return emptyText;
    return options[selectedIndex];
}

void DropdownWidget::SelectByText(const std::string& value)
{
    auto it = std::find(options.begin(), options.end(), value);
    selectedIndex = it == options.end() ? -1 : static_cast<int>(std::distance(options.begin(), it));
}

void DropdownWidget::ClearSelection()
{
    selectedIndex = -1;
}

bool DropdownWidget::ConsumeChanged()
{
    bool result = changed;
    changed = false;
    return result;
}

bool DropdownWidget::IsAnyOpen()
{
    return openDropdown != nullptr;
}

void DropdownWidget::CloseAll()
{
    openDropdown = nullptr;
}

void DropdownWidget::Draw(Rectangle bounds, const std::string& placeholder)
{
    Vector2 mouse = GetMousePosition();
    bool isOpen = openDropdown == this;
    // While one list is expanded it owns pointer input for the whole frame.
    // Other dropdowns may be geometrically underneath the floating list and
    // must not open from the same click that selects an item above them.
    bool pointerAvailable = openDropdown == nullptr || isOpen;
    bool hover = pointerAvailable && CheckCollisionPointRec(mouse, bounds);

    if (!UiControlIcons::DrawPixelHudWidgetFrame(bounds, hover || isOpen))
    {
        DrawRectangleRounded(bounds, 0.18f, 6, isOpen ? activePalette.focusedFill : hover ? activePalette.hoverFill : activePalette.fill);
        DrawRectangleRoundedLines(bounds, 0.18f, 6, 1.0f, isOpen ? activePalette.focusedBorder : hover ? activePalette.hoverBorder : activePalette.border);
    }

    const std::string& label = SelectedText();
    float arrowWidth = 18.0f;
    Rectangle textArea{bounds.x + 8.0f, bounds.y, bounds.width - arrowWidth - 14.0f, bounds.height};
    UiText::Draw(FitTail(label.empty() ? placeholder : label, textArea.width, fontSize),
                 textArea.x,
                 textArea.y + (textArea.height - fontSize) * 0.5f,
                 fontSize,
                 label.empty() ? activePalette.faintText : activePalette.text);

    // Caret triangle, flipped while the list is expanded.
    float cx = bounds.x + bounds.width - arrowWidth * 0.5f - 4.0f;
    float cy = bounds.y + bounds.height * 0.5f;
    if (isOpen)
        DrawTriangle({cx - 5.0f, cy + 3.0f}, {cx + 5.0f, cy + 3.0f}, {cx, cy - 4.0f}, activePalette.focusedBorder);
    else
        DrawTriangle({cx - 5.0f, cy - 3.0f}, {cx, cy + 4.0f}, {cx + 5.0f, cy - 3.0f}, activePalette.mutedText);

    if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        if (isOpen)
        {
            openDropdown = nullptr;
        }
        else
        {
            openDropdown = this;
            openBounds = bounds;
            focusedField = nullptr;
            highlightedIndex = std::max(0, selectedIndex);
            // Scroll so the current selection is visible when the list opens.
            listScroll = std::max(0.0f, static_cast<float>(highlightedIndex - maxVisibleRows + 1) * rowHeight);
        }
    }
}

bool DropdownWidget::DrawOpenList()
{
    DropdownWidget* dropdown = openDropdown;
    if (dropdown == nullptr)
        return false;

    const auto& options = dropdown->options;
    int optionCount = static_cast<int>(options.size());
    if (optionCount == 0)
    {
        openDropdown = nullptr;
        return false;
    }

    float listHeight = std::min(static_cast<float>(optionCount), static_cast<float>(maxVisibleRows)) * rowHeight;
    float contentHeight = optionCount * rowHeight;
    float maxScroll = std::max(0.0f, contentHeight - listHeight);

    // Prefer dropping down; flip above when the screen bottom is in the way.
    float below = openBounds.y + openBounds.height + 2.0f;
    bool flipUp = below + listHeight > static_cast<float>(GetScreenHeight()) - 4.0f &&
                  openBounds.y - listHeight - 2.0f > 4.0f;
    Rectangle list{openBounds.x, flipUp ? openBounds.y - listHeight - 2.0f : below, openBounds.width, listHeight};

    Vector2 mouse = GetMousePosition();
    bool mouseOverList = CheckCollisionPointRec(mouse, list);

    if (mouseOverList)
    {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
            dropdown->listScroll = std::clamp(dropdown->listScroll - wheel * rowHeight * 2.0f, 0.0f, maxScroll);
    }
    dropdown->listScroll = std::clamp(dropdown->listScroll, 0.0f, maxScroll);

    if (IsKeyPressed(KEY_DOWN))
        dropdown->highlightedIndex = std::min(optionCount - 1, dropdown->highlightedIndex + 1);
    if (IsKeyPressed(KEY_UP))
        dropdown->highlightedIndex = std::max(0, dropdown->highlightedIndex - 1);
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_DOWN))
    {
        // Keep the keyboard highlight inside the visible window.
        float top = dropdown->highlightedIndex * rowHeight;
        dropdown->listScroll = std::clamp(dropdown->listScroll, top - listHeight + rowHeight, top);
        dropdown->listScroll = std::clamp(dropdown->listScroll, 0.0f, maxScroll);
    }

    if (!UiControlIcons::DrawPixelHudWidgetFrame(list))
    {
        DrawRectangleRounded(list, 0.06f, 6, activePalette.listFill);
        DrawRectangleRoundedLines(list, 0.06f, 6, 1.0f, activePalette.focusedBorder);
    }

    BeginScissorMode(static_cast<int>(list.x), static_cast<int>(list.y), static_cast<int>(list.width), static_cast<int>(list.height));
    int clickedIndex = -1;
    for (int i = 0; i < optionCount; i++)
    {
        Rectangle row{list.x, list.y + i * rowHeight - dropdown->listScroll, list.width, rowHeight};
        if (row.y + rowHeight < list.y || row.y > list.y + list.height)
            continue;

        bool rowHover = mouseOverList && CheckCollisionPointRec(mouse, row);
        if (rowHover)
            dropdown->highlightedIndex = i;

        bool isSelected = i == dropdown->selectedIndex;
        bool isHighlighted = i == dropdown->highlightedIndex;
        if (isHighlighted)
            DrawRectangleRec(row, activePalette.listHighlight);
        else if (isSelected)
            DrawRectangleRec(row, activePalette.listSelected);

        UiText::Draw(FitTail(options[i], row.width - 16.0f, dropdown->fontSize),
                     row.x + 8.0f,
                     row.y + (rowHeight - dropdown->fontSize) * 0.5f,
                     dropdown->fontSize,
                     isSelected ? activePalette.focusedBorder : activePalette.text);

        if (rowHover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            clickedIndex = i;
    }
    EndScissorMode();

    if (maxScroll > 0.0f)
    {
        Rectangle track{list.x + list.width - 4.0f, list.y + 2.0f, 3.0f, list.height - 4.0f};
        float thumbH = std::max(20.0f, track.height * (listHeight / contentHeight));
        float thumbY = track.y + (track.height - thumbH) * (dropdown->listScroll / maxScroll);
        DrawRectangleRounded(Rectangle{track.x, thumbY, track.width, thumbH}, 0.5f, 4, activePalette.border);
    }

    bool committed = false;
    int commitIndex = -1;
    if (clickedIndex >= 0)
    {
        commitIndex = clickedIndex;
    }
    else if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
    {
        commitIndex = dropdown->highlightedIndex;
    }

    if (commitIndex >= 0)
    {
        if (commitIndex != dropdown->selectedIndex)
        {
            dropdown->selectedIndex = commitIndex;
            dropdown->changed = true;
            committed = true;
        }
        openDropdown = nullptr;
    }
    else if (IsKeyPressed(KEY_ESCAPE) ||
             (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !mouseOverList && !CheckCollisionPointRec(mouse, openBounds)))
    {
        openDropdown = nullptr;
    }

    return committed;
}

// --- TextFieldWidget --------------------------------------------------------

bool TextFieldWidget::IsFocused() const
{
    return focusedField == this;
}

void TextFieldWidget::Focus()
{
    focusedField = this;
}

void TextFieldWidget::ClearFocus()
{
    focusedField = nullptr;
}

bool TextFieldWidget::IsAnyFocused()
{
    return focusedField != nullptr;
}

bool TextFieldWidget::Draw(Rectangle bounds, const std::string& placeholder)
{
    Vector2 mouse = GetMousePosition();
    // DrawOpenList() runs at the end of the frame so the list paints on top.
    // Suppress pointer handling here while it is open; otherwise a click on a
    // list row also focuses the text field rendered underneath that row.
    bool pointerAvailable = !DropdownWidget::IsAnyOpen();
    bool hover = pointerAvailable && CheckCollisionPointRec(mouse, bounds);

    if (pointerAvailable && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        if (hover)
            focusedField = this;
        else if (focusedField == this)
            focusedField = nullptr;
    }

    bool focused = focusedField == this;
    bool changed = false;

    if (focused)
    {
        const bool controlHeld = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const bool copyRequested = controlHeld && IsKeyPressed(KEY_C);
        const bool pasteRequested = controlHeld && IsKeyPressed(KEY_V);

        // The widget currently has an end-only caret and no highlighted range,
        // so copying the whole field and pasting at the end is the useful,
        // unambiguous clipboard behaviour. Handle these before character input
        // so Ctrl+C/Ctrl+V can never also insert a literal c/v into the value.
        if (copyRequested)
        {
            SetClipboardText(text.c_str());
        }
        else if (pasteRequested)
        {
            const char* clipboard = GetClipboardText();
            if (clipboard != nullptr)
            {
                std::string pasted = clipboard;
                if (numericOnly)
                {
                    const bool pastedNegative = allowNegative && pasted.starts_with('-');
                    pasted.erase(std::remove_if(pasted.begin(), pasted.end(), [](unsigned char character)
                    {
                        return !IsUnsignedNumericChar(character);
                    }), pasted.end());

                    // Keep a pasted sign at the beginning of the resulting
                    // number. Numeric fields append by design, so a leading
                    // minus needs the same sign-toggle treatment as typing it.
                    if (pastedNegative)
                    {
                        if (!text.starts_with('-'))
                            text.insert(text.begin(), '-');
                    }
                }
                if (!pasted.empty())
                {
                    text += pasted;
                    changed = true;
                }
            }
        }
        else
        {
            int codepoint = GetCharPressed();
            while (codepoint > 0)
            {
                if (numericOnly && allowNegative && codepoint == '-')
                {
                    if (text.starts_with('-'))
                        text.erase(0, 1);
                    else
                        text.insert(text.begin(), '-');
                    changed = true;
                }
                else if (numericOnly && allowNegative && codepoint == '+')
                {
                    if (text.starts_with('-'))
                    {
                        text.erase(0, 1);
                        changed = true;
                    }
                }
                else if (!numericOnly || IsUnsignedNumericChar(codepoint))
                {
                    text += Utf8::Encode(codepoint);
                    changed = true;
                }
                codepoint = GetCharPressed();
            }
        }

        // Manual key repeat: hold backspace to keep deleting.
        double now = GetTime();
        if (IsKeyPressed(KEY_BACKSPACE))
        {
            Utf8::RemoveLast(text);
            changed = true;
            backspaceHeldSince = now;
            lastRepeat = now;
        }
        else if (IsKeyDown(KEY_BACKSPACE) && now - backspaceHeldSince > 0.4 && now - lastRepeat > 0.035)
        {
            Utf8::RemoveLast(text);
            changed = true;
            lastRepeat = now;
        }

        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_ESCAPE))
            focusedField = nullptr;
    }

    if (!UiControlIcons::DrawPixelHudWidgetFrame(bounds, focused || hover))
    {
        DrawRectangleRounded(bounds, 0.18f, 6, focused ? activePalette.focusedFill : hover ? activePalette.hoverFill : activePalette.fill);
        DrawRectangleRoundedLines(bounds, 0.18f, 6, 1.0f, focused ? activePalette.focusedBorder : hover ? activePalette.hoverBorder : activePalette.border);
    }

    float padding = 8.0f;
    float textWidth = bounds.width - padding * 2.0f;
    bool showPlaceholder = text.empty() && !focused;
    std::string shown = FitTail(showPlaceholder ? placeholder : text, textWidth, fontSize);
    float textY = bounds.y + (bounds.height - fontSize) * 0.5f;
    UiText::Draw(shown, bounds.x + padding, textY, fontSize, showPlaceholder ? activePalette.faintText : activePalette.text);

    if (focused && std::fmod(GetTime(), 1.0) < 0.5)
    {
        float caretX = bounds.x + padding + UiText::Measure(shown, fontSize) + 1.0f;
        caretX = std::min(caretX, bounds.x + bounds.width - 4.0f);
        DrawLineEx({caretX, textY}, {caretX, textY + fontSize}, 1.0f, activePalette.focusedBorder);
    }

    return changed;
}
