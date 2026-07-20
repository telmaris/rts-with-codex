#ifndef UI_THEME_H
#define UI_THEME_H

#include "raylib.h"

// Shared medieval color palette for GUI chrome (panels, tooltips, buttons,
// HUD, cards). Centralized so the whole interface stays visually consistent
// and can be retuned from one place instead of scattered Color literals.
namespace UiTheme
{
    // Wood / leather neutrals, darkest to lightest.
    constexpr Color Ink    {20, 14, 10, 255};  // deepest background (game viewport clear)
    constexpr Color Bark   {40, 29, 21, 255};  // panel body
    constexpr Color Oak    {56, 41, 29, 255};  // title bars, header strips, cards
    constexpr Color Timber {78, 58, 41, 255};  // hover / raised surfaces

    // Metal accents.
    constexpr Color Bronze {150, 108, 58, 255}; // default borders, dividers
    constexpr Color Iron   {96, 82, 66, 255};   // muted / disabled borders
    constexpr Color Gold   {214, 170, 86, 255}; // selection, hover accents, highlights

    // Text.
    constexpr Color Parchment      {236, 220, 190, 255}; // primary text (replaces RAYWHITE)
    constexpr Color ParchmentDim   {184, 162, 128, 255}; // muted / secondary text
    constexpr Color ParchmentFaint {138, 120, 96, 255};  // disabled / locked text

    // Status.
    constexpr Color Sage {150, 180, 98, 255};  // positive / affordable / ok
    constexpr Color Rust {198, 94, 68, 255};   // negative / missing / locked / danger

    // Brighter status variants for small text/labels on dark backgrounds.
    constexpr Color SageBright  {168, 214, 128, 255}; // affordable cost, positive stat delta
    constexpr Color RustBright  {214, 110, 90, 255};  // unaffordable cost, negative stat delta
    constexpr Color AmberBright {224, 188, 120, 255}; // informational highlight (replaces cool blue accents)

    // Pre-alpha'd fills/borders for specific chrome states.
    constexpr Color SelectedFill   {66, 82, 52, 245};
    constexpr Color SelectedBorder {182, 214, 120, 220};
    constexpr Color DangerFill     {132, 58, 42, 255};
    constexpr Color DangerBorder   {214, 128, 92, 255};
}

#endif
