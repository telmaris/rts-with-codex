#include "Tabs.h"

#include "EditorTheme.h"

#include "ui/UiText.h"

#include <algorithm>
#include <cstdio>

namespace
{
    std::string Percent(int weight, int total)
    {
        if (total <= 0)
            return "-";
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%.0f%%", 100.0 * weight / total);
        return buffer;
    }

    int TotalWeight(const TerrainTextureDefinition& terrain)
    {
        int total = 0;
        for (const auto& variant : terrain.variants)
            total += std::max(0, variant.weight);
        return total;
    }
}

TerrainTab::TerrainTab(ToolContext& context)
: context(context)
{
    slotList.selected = 0;
    variantList.selected = 0;
    grid.dimUnused = true;
    // Other variants of the same tile type: same meaning as the selection, one
    // step quieter, so a four-way grass reads as one group.
    grid.secondaryColor = Color{88, 158, 255, 150};
}

std::string TerrainTab::Status() const
{
    const auto& terrain = context.document.config.terrain;
    int variants = 0;
    for (const auto& entry : terrain)
        variants += static_cast<int>(entry.variants.size());

    return std::to_string(terrain.size()) + " tile types, " + std::to_string(variants) + " variants";
}

bool TerrainTab::StatusIsWarning() const
{
    for (const auto& entry : context.document.config.terrain)
        if (entry.variants.empty())
            return true;
    return false;
}

void TerrainTab::Draw(Rectangle area)
{
    Document& document = context.document;
    auto& terrainEntries = document.config.terrain;

    constexpr float listWidth = 260.0f;
    constexpr float inspectorWidth = 380.0f;
    Rectangle listBounds{area.x, area.y, listWidth, area.height};
    Rectangle inspectorBounds{area.x + listWidth + 10.0f, area.y, inspectorWidth, area.height};
    Rectangle gridBounds{inspectorBounds.x + inspectorWidth + 10.0f, area.y,
                         area.width - listWidth - inspectorWidth - 20.0f, area.height};

    // ── Tile type list ─────────────────────────────────────────────────────
    Rectangle listContent = Ed::Panel(listBounds, "Tile types", "TileType -> weighted cells");

    int clickedSlot = slotList.Draw(
        Rectangle{listContent.x, listContent.y + 4.0f, listContent.width, listContent.height - 8.0f},
        static_cast<int>(terrainEntries.size()), 48.0f,
        [&](int index, Rectangle row, bool, bool) {
            const TerrainTextureDefinition& entry = terrainEntries[index];

            UiText::Draw(entry.tileType, row.x, row.y + 7.0f, Ed::FontBody, Ed::TextPrimary);
            std::string meta = entry.variants.empty()
                ? "no variants"
                : std::to_string(entry.variants.size()) + " variants";
            UiText::Draw(meta, row.x, row.y + 27.0f, Ed::FontSmall,
                         entry.variants.empty() ? Ed::Danger : Ed::TextFaint);

            float thumb = 30.0f;
            float x = row.x + row.width - thumb;
            for (auto it = entry.variants.rbegin(); it != entry.variants.rend(); ++it)
            {
                if (x < row.x + 110.0f)
                    break;
                const TextureAtlasDefinition* atlas = document.config.FindAtlas(it->texture.atlasId);
                if (atlas != nullptr)
                {
                    Ed::AtlasCell(Rectangle{x, row.y + 7.0f, thumb, thumb},
                                  context.catalog.TextureFor(atlas->path), atlas->cellWidth, atlas->cellHeight,
                                  it->texture.textureId, Ed::BorderSoft);
                }
                x -= thumb + 4.0f;
            }
        });

    if (clickedSlot >= 0)
        variantList.selected = 0;

    if (terrainEntries.empty())
    {
        Ed::Panel(inspectorBounds, "Nothing loaded", "");
        return;
    }

    int selected = std::clamp(slotList.selected, 0, static_cast<int>(terrainEntries.size()) - 1);
    slotList.selected = selected;
    TerrainTextureDefinition& entry = terrainEntries[selected];

    // ── Inspector ──────────────────────────────────────────────────────────
    Rectangle inspectorContent = Ed::Panel(inspectorBounds, entry.tileType,
                                           "one roll per generated tile, weighted");
    Rectangle column{inspectorContent.x + 14.0f, inspectorContent.y, inspectorContent.width - 28.0f, 0.0f};

    int variantIndex = entry.variants.empty()
        ? -1
        : std::clamp(variantList.selected, 0, static_cast<int>(entry.variants.size()) - 1);
    variantList.selected = variantIndex;

    // All variants of a tile type share one atlas: that is how the game loads
    // terrain (one tileset), and per-variant atlases would only invite a tile
    // type whose art is scattered across files.
    TextureRef sharedRef;
    sharedRef.atlasId = entry.variants.empty() ? -1 : entry.variants.front().texture.atlasId;
    sharedRef.textureId = variantIndex >= 0 ? entry.variants[variantIndex].texture.textureId : 0;
    int atlasBefore = sharedRef.atlasId;

    float y = inspectorContent.y + 12.0f;
    y = DrawAtlasAssignment(column, y, context, "terrain-atlas", sharedRef,
                            kTerrainTileSize, kTerrainTileSize);

    if (sharedRef.atlasId != atlasBefore)
    {
        int cells = document.CellCount(context.catalog, sharedRef.atlasId);
        for (auto& variant : entry.variants)
        {
            variant.texture.atlasId = sharedRef.atlasId;
            if (cells > 0)
                variant.texture.textureId = std::clamp(variant.texture.textureId, 0, cells - 1);
        }
        document.MarkDirty();
    }

    y = Ed::SectionHeader(column, y + 6.0f, "VARIANTS");

    int total = TotalWeight(entry);
    int removeIndex = -1;
    float rowHeight = 40.0f;
    float variantsBottom = inspectorContent.y + inspectorContent.height - 46.0f;

    for (size_t i = 0; i < entry.variants.size(); i++)
    {
        if (y + rowHeight > variantsBottom)
        {
            UiText::Draw("... " + std::to_string(entry.variants.size() - i) + " more",
                         column.x, y, Ed::FontSmall, Ed::TextFaint);
            y += 20.0f;
            break;
        }

        TerrainVariantDefinition& variant = entry.variants[i];
        bool isSelected = static_cast<int>(i) == variantIndex;

        Rectangle row{column.x, y, column.width, rowHeight - 4.0f};
        DrawRectangleRounded(row, 0.14f, 6, isSelected ? Ed::AccentSoft : Ed::Raised);
        DrawRectangleRoundedLines(row, 0.14f, 6, 1.0f, isSelected ? Ed::Accent : Ed::BorderSoft);

        const TextureAtlasDefinition* atlas = document.config.FindAtlas(variant.texture.atlasId);
        Rectangle thumb{row.x + 6.0f, row.y + 4.0f, 28.0f, 28.0f};
        if (atlas != nullptr)
        {
            Ed::AtlasCell(thumb, context.catalog.TextureFor(atlas->path), atlas->cellWidth, atlas->cellHeight,
                          variant.texture.textureId, Ed::BorderSoft);
        }

        UiText::Draw("cell " + std::to_string(variant.texture.textureId), row.x + 42.0f, row.y + 4.0f,
                     Ed::FontSmall, Ed::TextPrimary);
        UiText::Draw(Percent(variant.weight, total), row.x + 42.0f, row.y + 20.0f, Ed::FontSmall, Ed::Warn);

        if (Ed::IntStepper(Rectangle{row.x + 100.0f, row.y + 3.0f, row.width - 138.0f, 28.0f},
                           "weight", variant.weight, 0, 999))
            document.MarkDirty();

        if (Ed::DangerButton(Rectangle{row.x + row.width - 32.0f, row.y + 5.0f, 26.0f, 24.0f}, "x"))
            removeIndex = static_cast<int>(i);

        // Selecting a row is what the atlas grid writes into.
        if (!Ed::InputBlocked() && CheckCollisionPointRec(GetMousePosition(), row) &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            variantList.selected = static_cast<int>(i);

        y += rowHeight;
    }

    if (removeIndex >= 0)
    {
        entry.variants.erase(entry.variants.begin() + removeIndex);
        variantList.selected = std::min(variantList.selected, static_cast<int>(entry.variants.size()) - 1);
        document.MarkDirty();
    }

    Rectangle addRect{column.x, inspectorContent.y + inspectorContent.height - 38.0f, column.width, 28.0f};
    bool canAdd = sharedRef.atlasId >= 0;
    if (Ed::Button(addRect, canAdd ? "+ add variant" : "+ add variant (assign a texture first)", false, canAdd))
    {
        TerrainVariantDefinition variant;
        variant.texture.atlasId = sharedRef.atlasId;
        // Start on the cell the user is looking at, not on 0.
        variant.texture.textureId = grid.hovered >= 0 ? grid.hovered : 0;
        variant.weight = 1;
        entry.variants.push_back(variant);
        variantList.selected = static_cast<int>(entry.variants.size()) - 1;
        document.MarkDirty();
    }

    // ── Atlas ──────────────────────────────────────────────────────────────
    const TextureAtlasDefinition* atlas = document.config.FindAtlas(sharedRef.atlasId);
    grid.highlighted.clear();
    grid.secondary.clear();
    grid.marked.clear();

    for (size_t i = 0; i < entry.variants.size(); i++)
    {
        if (static_cast<int>(i) == variantIndex)
            grid.highlighted.push_back(entry.variants[i].texture.textureId);
        else
            grid.secondary.push_back(entry.variants[i].texture.textureId);
    }
    // Cells claimed by any other tile type, so unclaimed art is obvious.
    for (const auto& other : terrainEntries)
    {
        if (&other == &entry)
            continue;
        for (const auto& variant : other.variants)
            if (atlas != nullptr && variant.texture.atlasId == atlas->id)
                grid.marked.push_back(variant.texture.textureId);
    }

    std::string subtitle = atlas != nullptr ? atlas->path : "no atlas assigned";
    Rectangle gridContent = Ed::Panel(gridBounds, "Atlas", subtitle);
    grid.emptyMessage = variantIndex < 0 ? "add a variant to start assigning cells" : "no texture assigned";

    int clickedCell = grid.Draw(
        Rectangle{gridContent.x + 8.0f, gridContent.y + 8.0f, gridContent.width - 16.0f, gridContent.height - 16.0f},
        atlas != nullptr ? context.catalog.TextureFor(atlas->path) : Texture2D{},
        atlas != nullptr ? atlas->cellWidth : 0,
        atlas != nullptr ? atlas->cellHeight : 0);

    if (clickedCell >= 0 && variantIndex >= 0)
    {
        entry.variants[variantIndex].texture.textureId = clickedCell;
        document.MarkDirty();
    }
}
