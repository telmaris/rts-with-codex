#ifndef EDITOR_THEME_H
#define EDITOR_THEME_H

// Anthracite palette for the tool. Deliberately NOT the game's UiTheme: this is
// a dense editing surface read for hours, and the game's brown/parchment chrome
// is tuned for atmosphere at a glance, not for contrast in a table of numbers.
//
// Contrast against Surface (#22262B): TextPrimary ~11:1, TextMuted ~5.6:1,
// TextFaint ~3.2:1 (labels only, never a value you have to read).

#include "raylib.h"

namespace Ed
{
    // Backgrounds, darkest to lightest.
    constexpr Color Void      {14, 16, 19, 255};    // window background
    constexpr Color Sunken    {26, 29, 33, 255};    // insets: atlas view, previews
    constexpr Color Surface   {34, 38, 43, 255};    // panels
    constexpr Color Raised    {44, 49, 56, 255};    // rows, controls
    constexpr Color Hover     {57, 63, 72, 255};    // hover / pressed

    // Lines.
    constexpr Color Border    {58, 64, 72, 255};
    constexpr Color BorderSoft{46, 51, 58, 255};

    // Text.
    constexpr Color TextPrimary {230, 233, 237, 255};
    constexpr Color TextMuted   {160, 168, 180, 255};
    constexpr Color TextFaint   {110, 118, 129, 255};

    // Accents. Blue carries selection/focus; amber carries "look at this";
    // red is a real problem; green is confirmation.
    constexpr Color Accent      {88, 158, 255, 255};
    constexpr Color AccentSoft  {88, 158, 255, 38};   // fill behind a selected row
    constexpr Color AccentDim   {58, 96, 150, 255};
    constexpr Color Warn        {224, 166, 60, 255};
    constexpr Color WarnSoft    {224, 166, 60, 34};
    constexpr Color Danger      {229, 83, 75, 255};
    constexpr Color DangerSoft  {229, 83, 75, 34};
    constexpr Color Ok          {87, 192, 107, 255};

    // Checkerboard behind sprites with alpha.
    constexpr Color CheckerA    {40, 44, 50, 255};
    constexpr Color CheckerB    {32, 35, 40, 255};

    // One place to retune text sizes; the tool never hardcodes a size elsewhere.
    constexpr int FontTitle   = 18;
    constexpr int FontBody    = 15;
    constexpr int FontSmall   = 13;
    constexpr int FontTiny    = 12;
}

#endif
