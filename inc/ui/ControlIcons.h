#ifndef CONTROL_ICONS_H
#define CONTROL_ICONS_H

#include "raylib.h"

#include <string>

// Runtime loader for the 64x64 keyboard/mouse shortcut icons used by the
// controls reference and tutorial popups.
namespace UiControlIcons
{
    // Semantic slots in the generated royal HUD icon atlas. The atlas retains
    // its high-resolution source cells and is downscaled at draw time, so HUD
    // controls stay sharp across supported window sizes.
    enum class HudIcon
    {
        Build,
        Destroy,
        Road,
        Logistics,
        Resources,
        Roster,
        Decisions,
        Technology,
        Manpower,
        Builders
    };

    void Load(const std::string& directory = "assets/ui/controls");
    void Unload();
    bool IsLoaded();

    // Draws one icon into a destination rectangle. Returns false when the
    // requested asset is unavailable, allowing callers to fall back to text.
    bool Draw(const std::string& name, Rectangle destination, Color tint = WHITE);

    // Draws one framed strategic-HUD icon from the generated atlas. Returns
    // false when the atlas is unavailable so the HUD can use its simple
    // procedural fallback instead of becoming unusable.
    bool DrawHud(HudIcon icon, Rectangle destination, bool hovered = false, Color tint = WHITE);

    // Draws only the subject and its dark inset, omitting the atlas cell frame.
    // Used inside the independently framed statistic chips.
    bool DrawHudGlyph(HudIcon icon, Rectangle destination, Color tint = WHITE);

    // Draws the cold-castle panel from independent background, rail, corner
    // and fixed-keystone modules. Unique ornaments are never stretched.
    bool DrawRoyalPanel(Rectangle destination, Color tint = WHITE);

    // Draws the reusable left-side status chip with fixed ornamental end caps
    // and a stretchable, detail-free middle section.
    bool DrawRoyalChip(Rectangle destination, Color tint = WHITE);

    // Draws the shared 9-slice graphite button frame. The four ornamental
    // corners remain fixed; only quiet metal rails and the central face are
    // stretched, so menu and in-game controls use one coherent chrome.
    bool DrawRoyalButtonFrame(Rectangle destination, bool hovered = false,
                              Color tint = WHITE);

    // Draws the fixed-aspect lion crest used as the left anchor of the HUD.
    bool DrawRoyalCrest(Rectangle destination, Color tint = WHITE);

    // Draws the shared scalable frame for all large game windows.
    bool DrawRoyalWindowPanel(Rectangle destination, Color tint = WHITE);

    // Returns the fixed rail inset used by DrawRoyalWindowPanel.  Title bars
    // and dividers use this exact value so they never paint over the frame.
    float RoyalWindowPanelInset(Rectangle destination);

    // Draws the quiet steel-and-navy background used by compact resource
    // slots.  The resource icon and value badge are drawn by the caller.
    bool DrawRoyalResourceSlot(Rectangle destination, Color tint = WHITE);

    // Draws the scalable steel-and-navy title plaque. Its fixed end caps and
    // detail-free middle keep the header consistent at every panel width.
    bool DrawRoyalTitleBar(Rectangle destination, Color tint = WHITE);

    // Draws the dedicated close control.  `hovered` selects its cool-blue
    // highlight asset instead of falling back to a text glyph.
    bool DrawPanelCloseButton(Rectangle destination, bool hovered, Color tint = WHITE);
}

#endif
