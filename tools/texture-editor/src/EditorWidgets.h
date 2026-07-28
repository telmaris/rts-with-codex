#ifndef EDITOR_WIDGETS_H
#define EDITOR_WIDGETS_H

// Immediate-mode controls for the tool, on the Ed:: palette.
//
// Text entry and dropdowns come from the game (ui/UiWidgets.h) so the tool does
// not grow a second implementation of them; everything here is chrome the game
// has no equivalent of.

#include "raylib.h"

#include <functional>
#include <string>
#include <vector>

namespace Ed
{
    // While set, every control here reports "not clicked" and every scrollable
    // ignores the wheel. Set it around the tab pass whenever a modal is up, so
    // the overlay cannot be clicked through — one flag instead of a guard at
    // each of the ~40 call sites.
    void SetInputBlocked(bool blocked);
    bool InputBlocked();

    // Frames a titled panel; returns the content rectangle inside it.
    Rectangle Panel(Rectangle bounds, const std::string& title, const std::string& subtitle = "");
    // Small caps-ish divider inside a panel. Returns the row below it.
    float SectionHeader(Rectangle bounds, float y, const std::string& label);

    bool Button(Rectangle bounds, const std::string& label, bool active = false, bool enabled = true);
    // Button that reads as destructive.
    bool DangerButton(Rectangle bounds, const std::string& label);
    // Toggles `value` in place; returns true when it changed this frame.
    bool Checkbox(Rectangle bounds, const std::string& label, bool& value);
    // "label  [-] value [+]" stepper. Returns true when `value` changed.
    // Holding a step button repeats after a short delay.
    bool IntStepper(Rectangle bounds, const std::string& label, int& value, int minimum, int maximum,
                    int step = 1, const std::string& suffix = "");
    bool DoubleStepper(Rectangle bounds, const std::string& label, double& value, double minimum,
                       double maximum, double step, const char* format = "%.2f");

    void Label(float x, float y, const std::string& text, int fontSize, Color color);
    // Left-aligned text that gets an ellipsis instead of overflowing. UiText's
    // DrawFit centers and shrinks, which is right for buttons and wrong for a
    // column of names — a list has to line up on its left edge to be scannable.
    void TextEllipsized(float x, float y, float maxWidth, const std::string& text, int fontSize, Color color);
    // Left-aligned key, right-aligned value on one row.
    void KeyValue(Rectangle row, const std::string& key, const std::string& value, Color valueColor);
    void Badge(Rectangle bounds, const std::string& text, Color accent);
    // Transparency checkerboard. Clamps its own edges instead of scissoring —
    // raylib's scissor is a single rect, not a stack, so an EndScissorMode in
    // here would unclip whatever list or atlas view is drawing it.
    void Checkerboard(Rectangle area, float alpha = 1.0f);
    // Whole texture centered in `area`, aspect preserved, never upscaled past
    // `maxScale` so pixel art stays crisp.
    void TextureFitted(Rectangle area, const Texture2D& texture, float maxScale = 8.0f);
    // One cell of an atlas texture, drawn over a checkerboard.
    void AtlasCell(Rectangle dest, const Texture2D& texture, int cellWidth, int cellHeight, int cellId,
                   Color border);
    std::string FormatFileSize(long long bytes);

    // Scrollable list of fixed-height rows. Handles wheel, clipping and hover;
    // the caller draws the row body. Returns the row clicked this frame, or -1.
    class RowList
    {
    public:
        int Draw(Rectangle area, int rowCount, float rowHeight,
                 const std::function<void(int index, Rectangle row, bool hovered, bool selected)>& drawRow);
        // Scrolls so `index` is visible on the next Draw.
        void Reveal(int index) { revealRequest = index; }

        int selected{-1};
        float scroll{0.0f};

    private:
        int revealRequest{-1};
    };

    // Modal chooser. One overlay serves both "pick an image file" and "pick a
    // slot": callers hand it rows, it hands back the chosen row's id.
    struct PickerRow
    {
        std::string id;
        std::string label;
        std::string sublabel;
        // Optional thumbnail; a zero-id texture just leaves the cell empty.
        Texture2D thumbnail{};
        // When set, only this cell of the thumbnail is drawn.
        int cellWidth{0};
        int cellHeight{0};
        int cellId{0};
    };

    class Picker
    {
    public:
        void Open(std::string title, std::vector<PickerRow> rows);
        void Close();
        bool IsOpen() const { return open; }

        // Draws the overlay over `screen`. Returns the chosen id, or empty when
        // nothing was chosen this frame. Closes itself on choice or on ESC.
        std::string Draw(Rectangle screen);

    private:
        bool open{false};
        std::string title;
        std::vector<PickerRow> rows;
        RowList list;
        std::string filter;
    };
}

#endif
