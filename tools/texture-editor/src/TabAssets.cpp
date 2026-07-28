#include "Tabs.h"

#include "EditorTheme.h"

#include "ui/UiText.h"

#include <algorithm>

// Fourth tab, added by writing this file and one emplace_back in main.cpp — the
// shape any future tab (units, UI chrome, projectiles) copies.
//
// It answers what the slot-driven tabs cannot: what art ships in the repo, and
// what nothing draws. It is also the "I just dropped a new PNG in" entry point:
// pick the file, then pick the slot it belongs to.

AssetTab::AssetTab(ToolContext& context)
: context(context)
{
    fileList.selected = context.catalog.Assets().empty() ? -1 : 0;
}

std::string AssetTab::Status() const
{
    return std::to_string(context.catalog.Assets().size()) + " image files, " +
           std::to_string(context.catalog.LoadedCount()) + " loaded, " +
           std::to_string(context.catalog.UnboundCount()) + " bound to no atlas";
}

void AssetTab::Draw(Rectangle area)
{
    Document& document = context.document;
    const auto& assets = context.catalog.Assets();
    const auto& groups = context.catalog.Groups();

    // A slot was chosen for the file waiting in pendingAssignPath.
    std::string chosenSlot;
    if (context.TakePickerResult("assign-slot", chosenSlot) && !pendingAssignPath.empty())
    {
        // Reuse the atlas if the file already is one, so cell size survives.
        const TextureAtlasDefinition* existing = document.config.FindAtlasByPath(pendingAssignPath);
        int atlasId = existing != nullptr
            ? existing->id
            : document.EnsureAtlas(context.catalog, pendingAssignPath);
        int cells = document.CellCount(context.catalog, atlasId);

        std::string kind = chosenSlot.substr(0, 2);
        std::string code = chosenSlot.substr(2);

        if (kind == "b:")
        {
            if (BuildingTextureDefinition* building = document.FindBuilding(code))
            {
                building->sprite = TextureRef{atlasId, 0};
                document.MarkDirty();
            }
        }
        else if (kind == "r:")
        {
            if (ResourceTextureDefinition* resource = document.FindResource(code))
            {
                resource->icon = TextureRef{atlasId, 0};
                document.MarkDirty();
            }
        }
        else if (kind == "t:")
        {
            if (TerrainTextureDefinition* terrain = document.FindTerrain(code))
            {
                // Keep the variant structure and only repoint it: the weights are
                // the part someone tuned, the atlas is the part being swapped.
                if (terrain->variants.empty())
                    terrain->variants.push_back({TextureRef{atlasId, 0}, 1});
                for (auto& variant : terrain->variants)
                {
                    variant.texture.atlasId = atlasId;
                    if (cells > 0)
                        variant.texture.textureId = std::clamp(variant.texture.textureId, 0, cells - 1);
                }
                document.MarkDirty();
            }
        }

        pendingAssignPath.clear();
    }

    // ── Group filter ───────────────────────────────────────────────────────
    float x = area.x;
    if (Ed::Button({x, area.y, 62.0f, 28.0f}, "all", groupFilter < 0))
    {
        groupFilter = -1;
        fileList.selected = -1;
    }
    x += 68.0f;
    for (size_t i = 0; i < groups.size(); i++)
    {
        float width = static_cast<float>(UiText::Measure(groups[i], Ed::FontBody)) + 26.0f;
        if (x + width > area.x + area.width)
            break;
        if (Ed::Button({x, area.y, width, 28.0f}, groups[i], groupFilter == static_cast<int>(i)))
        {
            groupFilter = static_cast<int>(i);
            fileList.selected = -1;
        }
        x += width + 6.0f;
    }

    Rectangle body{area.x, area.y + 38.0f, area.width, area.height - 38.0f};

    std::vector<int> visible;
    for (size_t i = 0; i < assets.size(); i++)
    {
        if (groupFilter < 0 || assets[i].group == groups[groupFilter])
            visible.push_back(static_cast<int>(i));
    }

    constexpr float previewWidth = 420.0f;
    Rectangle listBounds{body.x, body.y, body.width - previewWidth - 10.0f, body.height};
    Rectangle previewBounds{body.x + body.width - previewWidth, body.y, previewWidth, body.height};

    Rectangle listContent = Ed::Panel(listBounds, "Files under assets/",
                                      "\"unbound\" = no atlas in textures.rtsdata points at it");

    fileList.Draw(Rectangle{listContent.x, listContent.y + 4.0f, listContent.width, listContent.height - 8.0f},
                  static_cast<int>(visible.size()), 46.0f,
                  [&](int index, Rectangle row, bool, bool) {
                      const TextureAsset& asset = assets[visible[index]];
                      const TextureAtlasDefinition* atlas = document.config.FindAtlasByPath(asset.gamePath);

                      Rectangle thumb{row.x, row.y + 6.0f, 34.0f, 34.0f};
                      Ed::TextureFitted(thumb, asset.texture);

                      Ed::TextEllipsized(row.x + 44.0f, row.y + 5.0f, row.width - 160.0f, asset.relativePath,
                                         Ed::FontBody, Ed::TextPrimary);
                      UiText::Draw(std::to_string(asset.width) + "x" + std::to_string(asset.height) + "   " +
                                       Ed::FormatFileSize(asset.fileSize),
                                   row.x + 44.0f, row.y + 25.0f, Ed::FontSmall, Ed::TextFaint);

                      if (atlas != nullptr)
                      {
                          int users = document.AtlasUserCount(atlas->id);
                          Ed::Badge({row.x + row.width - 108.0f, row.y + 13.0f, 106.0f, 19.0f},
                                    "atlas " + std::to_string(atlas->id) + "  -  " + std::to_string(users) +
                                        " slots",
                                    users > 0 ? Ed::Ok : Ed::Warn);
                      }
                      else
                      {
                          Ed::Badge({row.x + row.width - 76.0f, row.y + 13.0f, 74.0f, 19.0f}, "unbound",
                                    Ed::TextFaint);
                      }
                  });

    // ── Preview + actions ──────────────────────────────────────────────────
    if (fileList.selected < 0 || fileList.selected >= static_cast<int>(visible.size()))
    {
        Ed::Panel(previewBounds, "Preview", "select a file");
        return;
    }

    const TextureAsset& asset = assets[visible[fileList.selected]];
    Rectangle content = Ed::Panel(previewBounds, "Preview", asset.relativePath);
    Rectangle column{content.x + 14.0f, content.y, content.width - 28.0f, 0.0f};

    Rectangle image{column.x, content.y + 12.0f, column.width, content.height * 0.42f};
    Ed::TextureFitted(image, asset.texture);

    float y = image.y + image.height + 14.0f;
    auto line = [&](const std::string& key, const std::string& value, Color color) {
        Ed::KeyValue({column.x, y, column.width, 18.0f}, key, value, color);
        y += 22.0f;
    };

    line("size", std::to_string(asset.width) + " x " + std::to_string(asset.height) + " px", Ed::TextPrimary);
    line("file", Ed::FormatFileSize(asset.fileSize), Ed::TextMuted);
    line("group", asset.group, Ed::TextMuted);

    const TextureAtlasDefinition* atlas = document.config.FindAtlasByPath(asset.gamePath);
    if (atlas != nullptr)
    {
        line("atlas id", std::to_string(atlas->id) + "  " + atlas->name, Ed::TextPrimary);
        line("cell size", std::to_string(atlas->cellWidth) + " x " + std::to_string(atlas->cellHeight) + " px",
             Ed::TextPrimary);
        line("cells", std::to_string(document.CellCount(context.catalog, atlas->id)), Ed::TextMuted);
        line("used by", std::to_string(document.AtlasUserCount(atlas->id)) + " slots", Ed::TextMuted);
    }
    else
    {
        line("atlas", "not declared", Ed::TextFaint);
    }

    y = Ed::SectionHeader(column, y + 6.0f, "ASSIGN");
    if (Ed::Button({column.x, y, column.width, 30.0f}, "Assign this file to a slot..."))
    {
        std::vector<Ed::PickerRow> rows;
        for (const auto& building : document.config.buildings)
        {
            const BuildingSlot* legacy = context.FindLegacyBuilding(building.buildingType);
            rows.push_back({"b:" + building.buildingType, building.buildingType,
                            std::string("building") +
                                (legacy != nullptr ? "   " + std::to_string(legacy->footprintX) + "x" +
                                                         std::to_string(legacy->footprintY)
                                                   : ""),
                            asset.texture, 0, 0, 0});
        }
        for (const auto& terrain : document.config.terrain)
        {
            rows.push_back({"t:" + terrain.tileType, terrain.tileType,
                            "tile type   " + std::to_string(terrain.variants.size()) + " variants",
                            asset.texture, 0, 0, 0});
        }
        for (const auto& resource : document.config.resources)
            rows.push_back({"r:" + resource.resourceType, resource.resourceType, "resource icon",
                            asset.texture, 0, 0, 0});

        pendingAssignPath = asset.gamePath;
        context.OpenPicker("assign-slot", "Assign " + asset.relativePath + " to...", std::move(rows));
    }
    y += 38.0f;

    if (atlas != nullptr && document.AtlasUserCount(atlas->id) == 0)
    {
        if (Ed::DangerButton({column.x, y, column.width, 28.0f}, "Drop this unused atlas entry"))
        {
            document.PruneUnusedAtlases();
            document.MarkDirty();
        }
    }
}
