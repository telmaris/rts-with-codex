#include "Tabs.h"

#include "EditorTheme.h"

#include "ui/UiText.h"

#include <algorithm>

ResourceTab::ResourceTab(ToolContext& context)
: context(context)
{
    slotList.selected = 0;
    grid.dimUnused = true;
}

std::string ResourceTab::Status() const
{
    const auto& resources = context.document.config.resources;
    int broken = 0;
    for (const auto& resource : resources)
    {
        int cells = context.document.CellCount(context.catalog, resource.icon.atlasId);
        if (!resource.icon.IsValid() || (cells > 0 && resource.icon.textureId >= cells))
            broken++;
    }

    std::string status = std::to_string(resources.size()) + " resource types";
    if (broken > 0)
        status += "   |   " + std::to_string(broken) + " point at a cell that does not exist in their atlas";
    return status;
}

bool ResourceTab::StatusIsWarning() const
{
    for (const auto& resource : context.document.config.resources)
    {
        int cells = context.document.CellCount(context.catalog, resource.icon.atlasId);
        if (!resource.icon.IsValid() || (cells > 0 && resource.icon.textureId >= cells))
            return true;
    }
    return false;
}

void ResourceTab::Draw(Rectangle area)
{
    Document& document = context.document;
    auto& resources = document.config.resources;

    constexpr float listWidth = 330.0f;
    constexpr float inspectorWidth = 360.0f;
    Rectangle listBounds{area.x, area.y, listWidth, area.height};
    Rectangle inspectorBounds{area.x + listWidth + 10.0f, area.y, inspectorWidth, area.height};
    Rectangle gridBounds{inspectorBounds.x + inspectorWidth + 10.0f, area.y,
                         area.width - listWidth - inspectorWidth - 20.0f, area.height};

    // ── Resource list ──────────────────────────────────────────────────────
    Rectangle listContent = Ed::Panel(listBounds, "Resource icons", "one atlas cell per ResourceType");

    slotList.Draw(Rectangle{listContent.x, listContent.y + 4.0f, listContent.width, listContent.height - 8.0f},
                  static_cast<int>(resources.size()), 46.0f,
                  [&](int index, Rectangle row, bool, bool) {
                      const ResourceTextureDefinition& resource = resources[index];
                      const TextureAtlasDefinition* atlas = document.config.FindAtlas(resource.icon.atlasId);
                      int cells = document.CellCount(context.catalog, resource.icon.atlasId);
                      bool valid = atlas != nullptr && (cells == 0 || resource.icon.textureId < cells);

                      Rectangle thumb{row.x, row.y + 6.0f, 34.0f, 34.0f};
                      if (valid)
                      {
                          Ed::AtlasCell(thumb, context.catalog.TextureFor(atlas->path), atlas->cellWidth,
                                        atlas->cellHeight, resource.icon.textureId, Ed::BorderSoft);
                      }
                      else
                      {
                          Ed::Checkerboard(thumb);
                          DrawRectangleLinesEx(thumb, 1.0f, Ed::Danger);
                      }

                      UiText::Draw(resource.resourceType, row.x + 44.0f, row.y + 6.0f, Ed::FontBody,
                                   Ed::TextPrimary);

                      // The enum value is shown because it used to BE the icon
                      // index; keeping it visible makes the decoupling obvious.
                      auto legacy = std::find_if(context.legacyResources.begin(), context.legacyResources.end(),
                                                 [&](const ResourceSlot& slot) {
                                                     return slot.code == resource.resourceType;
                                                 });
                      if (legacy != context.legacyResources.end())
                      {
                          UiText::Draw("enum " + std::to_string(static_cast<int>(legacy->type)),
                                       row.x + 44.0f, row.y + 25.0f, Ed::FontSmall, Ed::TextFaint);
                      }

                      std::string cell = valid ? "cell " + std::to_string(resource.icon.textureId)
                                               : "OUT OF RANGE";
                      float width = static_cast<float>(UiText::Measure(cell, Ed::FontSmall));
                      UiText::Draw(cell, row.x + row.width - width, row.y + 14.0f, Ed::FontSmall,
                                   valid ? Ed::TextMuted : Ed::Danger);
                  });

    if (resources.empty())
    {
        Ed::Panel(inspectorBounds, "Inspector", "nothing loaded");
        return;
    }

    int selected = std::clamp(slotList.selected, 0, static_cast<int>(resources.size()) - 1);
    slotList.selected = selected;
    ResourceTextureDefinition& resource = resources[selected];

    // ── Inspector ──────────────────────────────────────────────────────────
    Rectangle inspectorContent = Ed::Panel(inspectorBounds, resource.resourceType, "icon binding");
    Rectangle column{inspectorContent.x + 14.0f, inspectorContent.y, inspectorContent.width - 28.0f, 0.0f};

    int atlasBefore = resource.icon.atlasId;
    float y = DrawAtlasAssignment(column, inspectorContent.y + 12.0f, context, "resource-icon", resource.icon,
                                  kResourceIconSize, kResourceIconSize);
    if (resource.icon.atlasId != atlasBefore)
        document.MarkDirty();

    int cellCount = document.CellCount(context.catalog, resource.icon.atlasId);
    if (resource.icon.IsValid())
    {
        y = Ed::SectionHeader(column, y + 4.0f, "CELL");
        int cell = resource.icon.textureId;
        if (Ed::IntStepper({column.x, y, column.width, 28.0f}, "cell", cell, 0, std::max(0, cellCount - 1)))
        {
            resource.icon.textureId = cell;
            document.MarkDirty();
        }
        y += 36.0f;

        // Switching the icon sheet is a 49-slot operation; doing it one row at a
        // time is the difference between usable and not.
        y = Ed::SectionHeader(column, y + 4.0f, "BULK");
        if (Ed::Button({column.x, y, column.width, 28.0f}, "Use this atlas for every resource"))
        {
            for (auto& other : resources)
            {
                other.icon.atlasId = resource.icon.atlasId;
                if (cellCount > 0)
                    other.icon.textureId = std::clamp(other.icon.textureId, 0, cellCount - 1);
            }
            document.MarkDirty();
        }
        y += 34.0f;

        if (Ed::Button({column.x, y, column.width, 28.0f}, "Reset cells to enum order"))
        {
            for (auto& other : resources)
            {
                auto legacy = std::find_if(context.legacyResources.begin(), context.legacyResources.end(),
                                           [&](const ResourceSlot& slot) { return slot.code == other.resourceType; });
                if (legacy != context.legacyResources.end())
                    other.icon.textureId = legacy->iconIndex;
            }
            document.MarkDirty();
        }
        y += 34.0f;

        if (cellCount > 0 && resource.icon.textureId >= cellCount)
        {
            UiText::DrawFit("cell " + std::to_string(resource.icon.textureId) + " is outside this atlas (" +
                                std::to_string(cellCount) + " cells)",
                            Rectangle{column.x, y, column.width, 18.0f}, Ed::FontSmall, Ed::Danger);
        }
    }

    // ── Atlas ──────────────────────────────────────────────────────────────
    const TextureAtlasDefinition* atlas = document.config.FindAtlas(resource.icon.atlasId);

    grid.highlighted.clear();
    grid.marked.clear();
    grid.highlighted.push_back(resource.icon.textureId);
    for (const auto& other : resources)
    {
        if (&other != &resource && atlas != nullptr && other.icon.atlasId == atlas->id)
            grid.marked.push_back(other.icon.textureId);
    }

    Rectangle gridContent = Ed::Panel(gridBounds, "Icon atlas",
                                      atlas != nullptr ? atlas->path : "no atlas assigned");
    grid.emptyMessage = "assign a texture in the inspector";

    int clicked = grid.Draw(
        Rectangle{gridContent.x + 8.0f, gridContent.y + 8.0f, gridContent.width - 16.0f, gridContent.height - 16.0f},
        atlas != nullptr ? context.catalog.TextureFor(atlas->path) : Texture2D{},
        atlas != nullptr ? atlas->cellWidth : 0,
        atlas != nullptr ? atlas->cellHeight : 0);

    if (clicked >= 0)
    {
        resource.icon.textureId = clicked;
        document.MarkDirty();
    }
}
