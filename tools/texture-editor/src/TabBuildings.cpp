#include "Tabs.h"

#include "EditorTheme.h"

#include "ui/UiText.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    // COPY of ResolveAnimationFrame (src/ui/Renderer.cpp). Copied, not linked,
    // because it shares a translation unit with Renderer, which drags in
    // Building/GameSnapshot. Ten lines of pure math — but still a second copy of
    // a rule, so phase 3 should extract it into its own TU the way UiText was.
    int ResolveFrame(int frameCount, double frameTime, bool looping, double elapsed)
    {
        if (frameCount <= 1)
            return 0;

        double total = frameCount * frameTime;
        double normalized = looping ? std::fmod(elapsed, total) : std::min(elapsed, total);
        return std::clamp(static_cast<int>(normalized / frameTime), 0, frameCount - 1);
    }
}

BuildingTab::BuildingTab(ToolContext& context)
: context(context)
{
    slotList.selected = 0;
    playbackStart = GetTime();
    grid.secondaryColor = Ed::Warn;
}

std::string BuildingTab::Status() const
{
    const auto& buildings = context.document.config.buildings;
    if (buildings.empty())
        return "no buildings in the document";

    int unassigned = 0;
    int animated = 0;
    for (const auto& building : buildings)
    {
        if (!building.sprite.IsValid())
            unassigned++;
        if (building.animation.enabled)
            animated++;
    }

    std::string status = std::to_string(buildings.size()) + " buildings, " +
                         std::to_string(animated) + " animated";
    if (unassigned > 0)
        status += "   |   " + std::to_string(unassigned) + " without a texture (drawn as a brown box in game)";
    return status;
}

bool BuildingTab::StatusIsWarning() const
{
    const auto& buildings = context.document.config.buildings;
    return buildings.empty() ||
           std::any_of(buildings.begin(), buildings.end(),
                       [](const BuildingTextureDefinition& b) { return !b.sprite.IsValid(); });
}

void BuildingTab::Draw(Rectangle area)
{
    Document& document = context.document;
    auto& buildings = document.config.buildings;

    constexpr float listWidth = 280.0f;
    constexpr float inspectorWidth = 380.0f;
    Rectangle listBounds{area.x, area.y, listWidth, area.height};
    Rectangle stageBounds{area.x + listWidth + 10.0f, area.y,
                          area.width - listWidth - inspectorWidth - 20.0f, area.height};
    Rectangle inspectorBounds{area.x + area.width - inspectorWidth, area.y, inspectorWidth, area.height};

    // ── Building list ──────────────────────────────────────────────────────
    Rectangle listContent = Ed::Panel(listBounds, "Buildings", "sprite + animation clip per type");

    slotList.Draw(Rectangle{listContent.x, listContent.y + 4.0f, listContent.width, listContent.height - 8.0f},
                  static_cast<int>(buildings.size()), 52.0f,
                  [&](int index, Rectangle row, bool, bool) {
                      const BuildingTextureDefinition& definition = buildings[index];
                      const BuildingSlot* legacy = context.FindLegacyBuilding(definition.buildingType);
                      const TextureAtlasDefinition* atlas = document.config.FindAtlas(definition.sprite.atlasId);

                      Rectangle thumb{row.x, row.y + 7.0f, 38.0f, 38.0f};
                      if (atlas != nullptr)
                      {
                          Ed::AtlasCell(thumb, context.catalog.TextureFor(atlas->path), atlas->cellWidth,
                                        atlas->cellHeight, definition.sprite.textureId, Ed::BorderSoft);
                      }
                      else
                      {
                          Ed::Checkerboard(thumb);
                          DrawRectangleLinesEx(thumb, 1.0f, Ed::Danger);
                      }

                      std::string name = legacy != nullptr ? legacy->displayName : definition.buildingType;
                      Ed::TextEllipsized(row.x + 48.0f, row.y + 7.0f, row.width - 100.0f, name,
                                         Ed::FontBody, Ed::TextPrimary);

                      std::string meta = definition.buildingType;
                      if (legacy != nullptr)
                          meta += "   " + std::to_string(legacy->footprintX) + "x" +
                                  std::to_string(legacy->footprintY);
                      UiText::Draw(meta, row.x + 48.0f, row.y + 28.0f, Ed::FontSmall, Ed::TextFaint);

                      if (definition.animation.enabled)
                      {
                          Ed::Badge({row.x + row.width - 44.0f, row.y + 16.0f, 42.0f, 19.0f},
                                    std::to_string(definition.animation.frames) + "f", Ed::Warn);
                      }
                      else if (atlas == nullptr)
                      {
                          Ed::Badge({row.x + row.width - 44.0f, row.y + 16.0f, 42.0f, 19.0f}, "none", Ed::Danger);
                      }
                  });

    if (buildings.empty())
    {
        Ed::Panel(stageBounds, "Preview", "nothing loaded");
        Ed::Panel(inspectorBounds, "Inspector", "");
        return;
    }

    int selected = std::clamp(slotList.selected, 0, static_cast<int>(buildings.size()) - 1);
    slotList.selected = selected;
    BuildingTextureDefinition& definition = buildings[selected];
    const BuildingSlot* legacy = context.FindLegacyBuilding(definition.buildingType);

    if (selected != lastSelected)
    {
        lastSelected = selected;
        playbackStart = GetTime();
    }

    // ── Inspector ──────────────────────────────────────────────────────────
    Rectangle inspectorContent = Ed::Panel(inspectorBounds, definition.buildingType,
                                           legacy != nullptr ? legacy->displayName : "not in buildings.rtsdata");
    Rectangle column{inspectorContent.x + 14.0f, inspectorContent.y, inspectorContent.width - 28.0f, 0.0f};
    DrawInspector(column, inspectorContent.y + 12.0f, definition, legacy);

    // ── Stage: on-map preview + live playback, then the atlas ──────────────
    float stageHeight = area.height * 0.46f;
    DrawStage(Rectangle{stageBounds.x, stageBounds.y, stageBounds.width, stageHeight}, definition, legacy);

    const TextureAtlasDefinition* atlas = document.config.FindAtlas(definition.sprite.atlasId);

    grid.highlighted.clear();
    grid.secondary.clear();
    grid.highlighted.push_back(definition.sprite.textureId);
    if (definition.animation.enabled)
    {
        for (int i = 1; i < definition.animation.frames; i++)
            grid.secondary.push_back(definition.sprite.textureId + i);
    }

    Rectangle gridBounds{stageBounds.x, stageBounds.y + stageHeight + 10.0f, stageBounds.width,
                         stageBounds.height - stageHeight - 10.0f};
    std::string subtitle = atlas != nullptr ? atlas->path : "no texture assigned";
    Rectangle gridContent = Ed::Panel(gridBounds, "Atlas cells", subtitle);
    grid.emptyMessage = "assign a texture in the inspector";

    int clicked = grid.Draw(
        Rectangle{gridContent.x + 8.0f, gridContent.y + 8.0f, gridContent.width - 16.0f, gridContent.height - 16.0f},
        atlas != nullptr ? context.catalog.TextureFor(atlas->path) : Texture2D{},
        atlas != nullptr ? atlas->cellWidth : 0,
        atlas != nullptr ? atlas->cellHeight : 0);

    if (clicked >= 0)
    {
        definition.sprite.textureId = clicked;
        playbackStart = GetTime();
        document.MarkDirty();
    }
}

// Draws the sprite the way the map draws it — stretched to footprint * TILE_SIZE
// on a tile grid — next to the animation playing at its authored rate.
void BuildingTab::DrawStage(Rectangle area, const BuildingTextureDefinition& definition,
                            const BuildingSlot* legacy)
{
    Rectangle content = Ed::Panel(area, "On-map preview",
                                  "sprite is stretched to the footprint - Renderer::DrawBuildingTexture");

    int footprintX = legacy != nullptr ? legacy->footprintX : 1;
    int footprintY = legacy != nullptr ? legacy->footprintY : 1;
    float footprintW = static_cast<float>(footprintX * kTerrainTileSize);
    float footprintH = static_cast<float>(footprintY * kTerrainTileSize);

    const TextureAtlasDefinition* atlas = context.document.config.FindAtlas(definition.sprite.atlasId);
    Texture2D texture = atlas != nullptr ? context.catalog.TextureFor(atlas->path) : Texture2D{};

    int frame = 0;
    if (definition.animation.enabled && playing)
    {
        frame = ResolveFrame(definition.animation.frames, definition.animation.frameTime,
                             definition.animation.looping, GetTime() - playbackStart);
    }
    int cellId = definition.sprite.textureId + frame;

    Rectangle stage{content.x + 14.0f, content.y + 14.0f, content.width - 28.0f, content.height - 58.0f};
    float scale = std::min(std::min(stage.width / footprintW, stage.height / footprintH), 7.0f);
    float width = footprintW * scale;
    float height = footprintH * scale;
    Rectangle dest{stage.x + (stage.width - width) * 0.5f, stage.y + (stage.height - height) * 0.5f, width, height};

    // Grass-ish ground so the sprite is judged against something map-like.
    DrawRectangleRec(dest, Color{38, 48, 34, 255});
    for (int x = 0; x <= footprintX; x++)
        DrawLineEx({dest.x + x * kTerrainTileSize * scale, dest.y},
                   {dest.x + x * kTerrainTileSize * scale, dest.y + height}, 1.0f, Color{62, 78, 56, 255});
    for (int y = 0; y <= footprintY; y++)
        DrawLineEx({dest.x, dest.y + y * kTerrainTileSize * scale},
                   {dest.x + width, dest.y + y * kTerrainTileSize * scale}, 1.0f, Color{62, 78, 56, 255});

    if (atlas != nullptr && texture.id != 0)
    {
        int columns = std::max(1, texture.width / std::max(1, atlas->cellWidth));
        Rectangle src{static_cast<float>((cellId % columns) * atlas->cellWidth),
                      static_cast<float>((cellId / columns) * atlas->cellHeight),
                      static_cast<float>(atlas->cellWidth), static_cast<float>(atlas->cellHeight)};
        DrawTexturePro(texture, src, dest, {0.0f, 0.0f}, 0.0f, WHITE);
    }
    else
    {
        // The game's fallback shape, so "no texture" looks like it does in game.
        DrawRectangleRounded(dest, 0.04f, 8, Color{96, 78, 56, 255});
        DrawRectangleRoundedLines(dest, 0.04f, 8, 1.0f, Color{150, 108, 58, 255});
    }
    DrawRectangleLinesEx(dest, 1.0f, Ed::Accent);

    // Footer: the resampling fact, which is invisible until it is stated.
    float footerY = content.y + content.height - 34.0f;
    if (atlas != nullptr)
    {
        bool exact = atlas->cellWidth == static_cast<int>(footprintW) &&
                     atlas->cellHeight == static_cast<int>(footprintH);
        std::string text = "cell " + std::to_string(atlas->cellWidth) + "x" + std::to_string(atlas->cellHeight) +
                           " px  ->  drawn " + std::to_string(static_cast<int>(footprintW)) + "x" +
                           std::to_string(static_cast<int>(footprintH)) + " px";
        if (!exact)
            text += "   (resampled every frame)";
        UiText::Draw(text, content.x + 14.0f, footerY, Ed::FontSmall, exact ? Ed::Ok : Ed::Warn);
    }

    if (definition.animation.enabled)
    {
        std::string label = "frame " + std::to_string(frame) + " / " +
                            std::to_string(definition.animation.frames) + "   cell " + std::to_string(cellId);
        UiText::Draw(label, content.x + content.width - UiText::Measure(label, Ed::FontSmall) - 14.0f, footerY,
                     Ed::FontSmall, Ed::Warn);
    }
}

float BuildingTab::DrawInspector(Rectangle column, float y, BuildingTextureDefinition& definition,
                                 const BuildingSlot* legacy)
{
    Document& document = context.document;

    int defaultCell = 0;
    if (legacy != nullptr)
        defaultCell = 0;  // whole image; a building sprite is one cell unless it is a strip

    y = DrawAtlasAssignment(column, y, context, "building-texture", definition.sprite, defaultCell, defaultCell);

    const TextureAtlasDefinition* atlas = document.config.FindAtlas(definition.sprite.atlasId);
    int cellCount = document.CellCount(context.catalog, definition.sprite.atlasId);

    if (atlas != nullptr)
    {
        y = Ed::SectionHeader(column, y + 4.0f, "CELL");
        int cell = definition.sprite.textureId;
        if (Ed::IntStepper({column.x, y, column.width, 28.0f}, "first cell", cell, 0, std::max(0, cellCount - 1)))
        {
            definition.sprite.textureId = cell;
            playbackStart = GetTime();
            document.MarkDirty();
        }
        y += 36.0f;
    }

    // ── Animation ──────────────────────────────────────────────────────────
    y = Ed::SectionHeader(column, y + 4.0f, "ANIMATION");

    bool enabled = definition.animation.enabled;
    if (Ed::Checkbox({column.x, y, column.width, 24.0f}, "animated", enabled))
    {
        definition.animation.enabled = enabled;
        if (enabled && definition.animation.frames < 2)
            definition.animation.frames = 2;
        playbackStart = GetTime();
        document.MarkDirty();
    }
    y += 32.0f;

    if (definition.animation.enabled)
    {
        int frames = definition.animation.frames;
        int maxFrames = cellCount > 0 ? std::max(1, cellCount - definition.sprite.textureId) : 64;
        if (Ed::IntStepper({column.x, y, column.width, 28.0f}, "frames", frames, 1, maxFrames))
        {
            definition.animation.frames = frames;
            playbackStart = GetTime();
            document.MarkDirty();
        }
        y += 32.0f;

        double frameTime = definition.animation.frameTime;
        if (Ed::DoubleStepper({column.x, y, column.width, 28.0f}, "frame time", frameTime, 0.02, 2.0, 0.02, "%.2fs"))
        {
            definition.animation.frameTime = frameTime;
            playbackStart = GetTime();
            document.MarkDirty();
        }
        y += 32.0f;

        bool looping = definition.animation.looping;
        if (Ed::Checkbox({column.x, y, column.width * 0.5f, 24.0f}, "loop", looping))
        {
            definition.animation.looping = looping;
            playbackStart = GetTime();
            document.MarkDirty();
        }
        if (Ed::Button({column.x + column.width - 96.0f, y - 2.0f, 96.0f, 26.0f}, playing ? "pause" : "play", playing))
        {
            playing = !playing;
            playbackStart = GetTime();
        }
        y += 34.0f;

        char loopLabel[64];
        std::snprintf(loopLabel, sizeof(loopLabel), "loop length %.0f ms",
                      definition.animation.frames * definition.animation.frameTime * 1000.0);
        UiText::Draw(loopLabel, column.x, y, Ed::FontSmall, Ed::TextFaint);
        y += 22.0f;

        // Frames are consecutive cells; running past the end is the one way to
        // author a clip that silently draws the wrong art (GetRectFromId clamps).
        int lastCell = definition.sprite.textureId + definition.animation.frames - 1;
        if (cellCount > 0 && lastCell >= cellCount)
        {
            UiText::DrawFit("clip runs to cell " + std::to_string(lastCell) + ", atlas ends at " +
                                std::to_string(cellCount - 1),
                            Rectangle{column.x, y, column.width, 18.0f}, Ed::FontSmall, Ed::Danger);
            y += 22.0f;
        }
    }

    // ── Facts ──────────────────────────────────────────────────────────────
    y = Ed::SectionHeader(column, y + 6.0f, "FROM buildings.rtsdata");
    if (legacy != nullptr)
    {
        Ed::KeyValue({column.x, y, column.width, 18.0f}, "footprint",
                     std::to_string(legacy->footprintX) + " x " + std::to_string(legacy->footprintY),
                     Ed::TextPrimary);
        y += 22.0f;
        Ed::KeyValue({column.x, y, column.width, 18.0f}, "legacy texture", legacy->texturePath.empty()
                                                                               ? "(none)"
                                                                               : legacy->texturePath,
                     Ed::TextFaint);
        y += 22.0f;
        if (legacy->hasLegacyTextureId)
        {
            Ed::KeyValue({column.x, y, column.width, 18.0f}, "legacy texture_id",
                         std::to_string(legacy->legacyTextureId), Ed::TextFaint);
            y += 22.0f;
        }
    }
    else
    {
        UiText::Draw("no matching block in buildings.rtsdata", column.x, y, Ed::FontSmall, Ed::Warn);
        y += 22.0f;
    }

    return y;
}
