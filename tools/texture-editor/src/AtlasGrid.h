#ifndef ATLAS_GRID_H
#define ATLAS_GRID_H

// Pannable/zoomable view of one texture sliced into cells, with the cell index
// drawn on every cell. Clicking a cell is how every assignment in the editor is
// made, so this is the tool's primary input surface, not a preview.

#include "raylib.h"

#include <string>
#include <vector>

class AtlasGrid
{
public:
    // Draws `texture` inside `area`. Returns the cell clicked this frame, or -1.
    int Draw(Rectangle area, const Texture2D& texture, int cellWidth, int cellHeight);

    // Fits the whole atlas into the view on the next Draw.
    void ResetView() { fitPending = true; }

    // Cells drawn with the primary accent outline (the current assignment).
    std::vector<int> highlighted;
    // Cells drawn with a secondary outline (e.g. the frames of a clip).
    std::vector<int> secondary;
    // Cells drawn with a faint marker (referenced by some other slot).
    std::vector<int> marked;
    Color secondaryColor{224, 166, 60, 255};

    int hovered{-1};

    // Geometry from the last Draw, for status lines.
    int columns{0};
    int rows{0};
    int cellCount{0};

    bool showIds{true};
    // Fades cells that carry no marker at all.
    bool dimUnused{false};
    // Shown centered when there is no texture to draw.
    std::string emptyMessage{"no texture"};

private:
    float zoom{4.0f};
    Vector2 pan{0.0f, 0.0f};
    bool dragging{false};
    bool dragMoved{false};
    Vector2 dragOrigin{0.0f, 0.0f};
    bool fitPending{true};
};

#endif
