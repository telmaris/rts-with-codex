#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

// Self-contained input widgets (dropdown, text field) shared by the game and the
// tools under tools/. Deliberately depends on nothing but raylib + UiTheme +
// UiText: anything that reaches for Player/Building/GameScene belongs in Gui.h
// instead, or this stops being linkable from a standalone tool.
//
// Both widgets are retained-state objects drawn immediate-mode style: keep one
// alive per control, call Draw() each frame.

#include "raylib.h"

#include <string>
#include <vector>

// Single-choice dropdown.
//
// Drawing is two-phase because an open list has to paint over whatever is drawn
// after it: Draw() renders the collapsed box, then one DrawOpenList() call at
// the very end of the frame renders whichever list is expanded.
class DropdownWidget
{
public:
    void SetOptions(std::vector<std::string> values);
    const std::vector<std::string>& GetOptions() const { return options; }

    // Renders the collapsed control; clicking it toggles the list.
    void Draw(Rectangle bounds, const std::string& placeholder = "Select...");

    // Renders the expanded list of whichever dropdown is open, handling hover,
    // wheel scrolling, arrow keys and Enter. Call once per frame, after every
    // panel has drawn. Returns true when a selection was committed.
    static bool DrawOpenList();
    static bool IsAnyOpen();
    static void CloseAll();

    // True exactly once after the selection changed, so callers can react
    // without diffing the index themselves.
    bool ConsumeChanged();

    // Draw size for the label and list rows.
    int fontSize{17};

    // Empty when nothing is selected.
    const std::string& SelectedText() const;
    // Selects the option equal to `value`; clears the selection if absent.
    void SelectByText(const std::string& value);
    void ClearSelection();

    int selectedIndex{-1};

private:
    std::vector<std::string> options;
    int highlightedIndex{0};
    float listScroll{0.0f};
    bool changed{false};
};

// Single-line text entry. Editing is append/backspace at the end only, matching
// the text box the game already ships — no caret movement or selection.
class TextFieldWidget
{
public:
    // Returns true on any frame where the text changed.
    bool Draw(Rectangle bounds, const std::string& placeholder = "");

    bool IsFocused() const;
    void Focus();
    static void ClearFocus();
    // True while any text field has keyboard focus — check before consuming
    // single-key shortcuts, or typing "d" into a field also triggers Delete.
    static bool IsAnyFocused();

    std::string text;
    // Restricts input to characters that can appear in a number.
    bool numericOnly{false};
    int fontSize{17};

private:
    double backspaceHeldSince{0.0};
    double lastRepeat{0.0};
};

#endif
