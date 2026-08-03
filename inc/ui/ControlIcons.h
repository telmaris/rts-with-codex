#ifndef CONTROL_ICONS_H
#define CONTROL_ICONS_H

#include "raylib.h"

#include <string>

// Runtime loader for the 64x64 keyboard/mouse shortcut icons used by the
// controls reference and tutorial popups.
namespace UiControlIcons
{
    void Load(const std::string& directory = "assets/ui/controls");
    void Unload();
    bool IsLoaded();

    // Draws one icon into a destination rectangle. Returns false when the
    // requested asset is unavailable, allowing callers to fall back to text.
    bool Draw(const std::string& name, Rectangle destination, Color tint = WHITE);
}

#endif
