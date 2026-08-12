#ifndef TECH_TREE_EDITOR_THEME_H
#define TECH_TREE_EDITOR_THEME_H

#include "ui/UiWidgets.h"

// Workbench styling for the editor only. The preview tree intentionally keeps
// UiTheme and the display font used by the game; everything around it should
// read as a practical authoring tool instead of another in-game screen.
namespace EditorTheme
{
    constexpr Color Canvas       {24, 28, 34, 255};
    constexpr Color Panel        {38, 44, 52, 250};
    constexpr Color PanelHeader  {47, 54, 64, 255};
    constexpr Color Surface      {50, 57, 67, 255};
    constexpr Color SurfaceHover {64, 72, 84, 255};
    constexpr Color SurfaceFocus {51, 65, 81, 255};
    constexpr Color Border       {93, 105, 120, 255};
    constexpr Color Divider      {77, 89, 104, 255};
    constexpr Color Accent       {91, 169, 230, 255};
    constexpr Color Text         {232, 236, 241, 255};
    constexpr Color TextMuted    {180, 191, 204, 255};
    constexpr Color TextFaint    {132, 145, 160, 255};
    constexpr Color Positive     {112, 207, 151, 255};
    constexpr Color Negative     {235, 121, 121, 255};

    constexpr UiWidgetPalette Widgets{
        Surface, SurfaceHover, SurfaceFocus,
        Border, Accent, Accent,
        Text, TextMuted, TextFaint,
        Color{32, 37, 45, 255}, Color{69, 87, 105, 255}, Color{57, 69, 83, 255}};
}

#endif
