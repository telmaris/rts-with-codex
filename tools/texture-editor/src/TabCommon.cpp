#include "Tabs.h"

#include "EditorTheme.h"

#include "ui/UiText.h"

#include <algorithm>

void ToolContext::Load(const std::string& assetsDirectory)
{
    assetsDir = assetsDirectory;

    catalog.ScanAndLoad(assetsDir);
    legacyBuildings = LoadBuildingSlots(assetsDir + "/data/buildings.rtsdata");
    legacyResources = LoadResourceSlots();

    document.Load(assetsDir, catalog, legacyBuildings);

    // Join files to slots so the asset browser can answer "is this drawn by
    // anything?". Derived from the document, so it follows every reassignment.
    for (const auto& atlas : document.config.atlases)
    {
        int users = document.AtlasUserCount(atlas.id);
        catalog.MarkBound(atlas.path, "atlas " + std::to_string(atlas.id) + " (" +
                                          std::to_string(users) + " slots)");
    }
}

void ToolContext::Reload()
{
    catalog = TextureCatalog{};
    document = Document{};
    Load(assetsDir);
}

const BuildingSlot* ToolContext::FindLegacyBuilding(const std::string& code) const
{
    auto it = std::find_if(legacyBuildings.begin(), legacyBuildings.end(),
                           [&](const BuildingSlot& slot) { return slot.code == code; });
    return it != legacyBuildings.end() ? &*it : nullptr;
}

void ToolContext::OpenPicker(const std::string& purpose, const std::string& title,
                             std::vector<Ed::PickerRow> rows)
{
    pickerPurpose = purpose;
    pickerResult.clear();
    picker.Open(title, std::move(rows));
}

bool ToolContext::TakePickerResult(const std::string& purpose, std::string& outId)
{
    if (pickerResult.empty() || pickerPurpose != purpose)
        return false;

    outId = pickerResult;
    pickerResult.clear();
    pickerPurpose.clear();
    return true;
}

std::vector<Ed::PickerRow> ToolContext::BuildFilePickerRows() const
{
    std::vector<Ed::PickerRow> rows;
    for (const auto& asset : catalog.Assets())
    {
        Ed::PickerRow row;
        row.id = asset.gamePath;
        row.label = asset.relativePath;
        row.sublabel = std::to_string(asset.width) + " x " + std::to_string(asset.height) + " px   " +
                       Ed::FormatFileSize(asset.fileSize);
        row.thumbnail = asset.texture;
        rows.push_back(row);
    }
    return rows;
}

// The one place any TextureRef is edited. Every tab routes through it so the
// interaction ("Change texture..." -> pick a file -> set cell size -> click a
// cell in the grid) is identical for terrain, buildings and resources.
float DrawAtlasAssignment(Rectangle column, float y, ToolContext& context, const std::string& purpose,
                          TextureRef& ref, int defaultCellWidth, int defaultCellHeight)
{
    Document& document = context.document;

    std::string chosen;
    if (context.TakePickerResult(purpose, chosen))
    {
        int atlasId = document.EnsureAtlas(context.catalog, chosen, defaultCellWidth, defaultCellHeight);
        ref.atlasId = atlasId;
        ref.textureId = 0;
        document.MarkDirty();
    }

    y = Ed::SectionHeader(column, y, "TEXTURE");

    const TextureAtlasDefinition* atlas = document.config.FindAtlas(ref.atlasId);
    std::string path = atlas != nullptr ? atlas->path : std::string("(none)");
    const TextureAsset* asset = atlas != nullptr ? context.catalog.Find(atlas->path) : nullptr;

    // File name on its own line: paths are long and truncating them from the
    // left is the one thing that makes them unreadable.
    std::string fileName = path;
    size_t slash = fileName.find_last_of('/');
    if (slash != std::string::npos)
        fileName = fileName.substr(slash + 1);

    Rectangle thumb{column.x, y, 46.0f, 46.0f};
    if (atlas != nullptr && asset != nullptr)
        Ed::AtlasCell(thumb, asset->texture, atlas->cellWidth, atlas->cellHeight, ref.textureId, Ed::BorderSoft);
    else
        Ed::TextureFitted(thumb, Texture2D{});

    Ed::TextEllipsized(column.x + 56.0f, y + 2.0f, column.width - 56.0f, fileName,
                       Ed::FontBody, atlas != nullptr ? Ed::TextPrimary : Ed::Danger);
    if (asset != nullptr)
    {
        UiText::Draw(std::to_string(asset->width) + " x " + std::to_string(asset->height) + " px",
                     column.x + 56.0f, y + 22.0f, Ed::FontSmall, Ed::TextFaint);
    }
    else if (atlas != nullptr)
    {
        UiText::Draw("FILE NOT FOUND", column.x + 56.0f, y + 22.0f, Ed::FontSmall, Ed::Danger);
    }

    if (Ed::Button({column.x + 56.0f, y + 42.0f, 150.0f, 28.0f}, "Change texture..."))
        context.OpenPicker(purpose, "Choose an image", context.BuildFilePickerRows());

    y += 80.0f;

    if (atlas == nullptr)
        return y;

    // Cell size belongs to the atlas, so editing it here moves every slot that
    // shares the file. Say so rather than surprising someone later.
    int users = document.AtlasUserCount(atlas->id);
    y = Ed::SectionHeader(column, y, "CELL SIZE");

    int cellWidth = atlas->cellWidth;
    int cellHeight = atlas->cellHeight;
    bool changed = false;
    changed |= Ed::IntStepper({column.x, y, column.width, 28.0f}, "width", cellWidth, 1, 1024, 8);
    y += 32.0f;
    changed |= Ed::IntStepper({column.x, y, column.width, 28.0f}, "height", cellHeight, 1, 1024, 8);
    y += 34.0f;

    float presetWidth = (column.width - 18.0f) / 4.0f;
    struct Preset { const char* label; int size; };
    const Preset presets[]{{"full", 0}, {"16", 16}, {"32", 32}, {"64", 64}};
    for (int i = 0; i < 4; i++)
    {
        Rectangle rect{column.x + i * (presetWidth + 6.0f), y, presetWidth, 26.0f};
        bool active = presets[i].size == 0
            ? (asset != nullptr && cellWidth == asset->width && cellHeight == asset->height)
            : (cellWidth == presets[i].size && cellHeight == presets[i].size);
        if (Ed::Button(rect, presets[i].label, active))
        {
            if (presets[i].size == 0 && asset != nullptr)
            {
                cellWidth = asset->width;
                cellHeight = asset->height;
            }
            else if (presets[i].size > 0)
            {
                cellWidth = presets[i].size;
                cellHeight = presets[i].size;
            }
            changed = true;
        }
    }
    y += 32.0f;

    if (changed)
    {
        document.SetAtlasCellSize(atlas->id, cellWidth, cellHeight);
        int cells = document.CellCount(context.catalog, atlas->id);
        // A smaller grid can strand the current cell outside the atlas, where
        // GetRectFromId would silently clamp and draw the wrong art.
        if (cells > 0)
            ref.textureId = std::clamp(ref.textureId, 0, cells - 1);
    }

    if (users > 1)
    {
        Ed::Badge({column.x, y, 190.0f, 19.0f},
                  "shared by " + std::to_string(users) + " slots", Ed::Warn);
        y += 26.0f;
    }

    return y;
}
