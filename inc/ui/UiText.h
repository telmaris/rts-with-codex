#ifndef UI_TEXT_H
#define UI_TEXT_H

// Shared text + tooltip rendering, extracted from Gui.cpp so it can be linked
// without dragging in Player/Building/GameScene. Both the game and the
// standalone tools under tools/ compile this translation unit, which is what
// keeps their text metrics identical instead of hand-mirrored.

#include "raylib.h"

#include <functional>
#include <string>
#include <vector>

// Which typeface subsequent UiText calls use.
enum class UiFontRole
{
    // The game's decorative face (MarcellusSC). Small-caps serif: right for
    // titles and map/tree chrome, hard to read at small sizes.
    Display,
    // A plain UI face for dense forms and long value strings. Falls back to
    // raylib's built-in font when none is loaded, so this is always safe.
    Plain
};

// One token in a short UI sentence. Icon tokens contain an asset name such
// as `key_q` or `mouse_mmb`; text tokens contain ordinary display text.
struct UiInlineRun
{
    std::string value;
    bool icon{false};
    // Inline resource/building names can use the same gold accent as tooltips.
    bool highlighted{false};
};

// Owns the shared UI fonts. Loading is separate from UiText so raygui's
// GuiSetFont (which lives with RAYGUI_IMPLEMENTATION in Gui.cpp) stays out of
// here — tools link this file without raygui.
class UiTextFont
{
public:
    // Loads the Display font from disk. Missing files leave the previous font
    // in place; measurement then falls back to raylib's default font.
    static void Load(const std::string& path);
    // Loads the Plain font. Optional — without it the Plain role uses raylib's
    // built-in font. `baseSize` is the rasterization size, not the draw size.
    static void LoadPlain(const std::string& path, int baseSize = 32);
    static void Unload();
    static bool IsLoaded();
    static const Font& Get();
};

// Shared UI text rendering helpers backed by the configured UI font.
class UiText
{
public:
    // Switches the face used by every subsequent call, returning the previous
    // role so callers can restore it. The game never calls this, so it stays on
    // Display and renders exactly as before.
    static UiFontRole SetRole(UiFontRole role);
    static UiFontRole GetRole();

    // Measures text width using the shared UI font.
    static int Measure(const std::string& text, int fontSize);
    // Draws text using the shared UI font.
    static void Draw(const std::string& text, float x, float y, int fontSize, Color color);
    // Draws text that shrinks until it fits within bounds.
    static void DrawFit(const std::string& text, Rectangle bounds, int fontSize, Color color);
    // Centers `text` on `titleBar` (both axes), shrinking the font (down to
    // 14px) only when it would run into a close button reserving
    // `closeButtonReserve` px from the right edge — the reserve affects the
    // shrink threshold, not the centering origin, so text stays centered on
    // the full bar even when shrunk. Shared by every panel title bar so they
    // render identically instead of each hand-rolling its own centering.
    static void DrawTitleBar(Rectangle titleBar, const std::string& text, float closeButtonReserve);
    // Greedy word wrap at `maxWidth`, measured with the shared UI font.
    static std::vector<std::string> Wrap(const std::string& text, int fontSize, float maxWidth);
    // Greedy word wrap for text containing `{icon:name}` tokens.
    static std::vector<std::vector<UiInlineRun>> WrapWithControlIcons(
        const std::string& text, int fontSize, float maxWidth, float iconSize = 30.0f);
    // Draws one line returned by WrapWithControlIcons.
    static void DrawWithControlIcons(const std::vector<UiInlineRun>& line,
                                     float x, float y, int fontSize, Color color,
                                     float iconSize = 30.0f);
};

// UTF-8 helpers for text entry, so every editable field agrees on what one
// "character" is (a backspace must delete a whole codepoint, not one byte).
namespace Utf8
{
    // Encodes one codepoint (e.g. from raylib's GetCharPressed).
    std::string Encode(int codepoint);
    // Erases the last complete codepoint in place.
    void RemoveLast(std::string& value);
}

// Shared tooltip renderer used by panels and build/research views.
class Tooltip
{
public:
    // Renders a tooltip near the mouse using the shared UI style. Lines
    // prefixed "{bonus}"/"{penalty}" are colored; the literal "{separator}"
    // draws a divider rule.
    static void Draw(const std::string& title, const std::vector<std::string>& lines,
                     float preferredWidth = 0.0f,
                     const std::function<void(Rectangle)>& titleIcon = {});
};

#endif
