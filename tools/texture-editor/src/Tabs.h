#ifndef TABS_H
#define TABS_H

// Tab seam + shared state.
//
// A tab is anything that can name itself and draw into a rectangle. main.cpp
// owns a vector<unique_ptr<ITextureTab>> and knows nothing else about them, so a
// fifth kind of texture (units, UI chrome, projectiles) is one new file plus one
// emplace_back — no changes to the shell.

#include "AtlasGrid.h"
#include "Document.h"
#include "EditorWidgets.h"
#include "GameBindings.h"
#include "TextureCatalog.h"

#include "raylib.h"

#include <memory>
#include <string>
#include <vector>

// Everything loaded once at startup and shared by every tab.
struct ToolContext
{
    void Load(const std::string& assetsDirectory);
    // Rescans assets/ and reloads the document, discarding unsaved edits.
    void Reload();

    TextureCatalog catalog;
    Document document;
    std::string assetsDir;

    // Gameplay facts that textures.rtsdata deliberately does NOT own: footprint
    // and display name come from buildings.rtsdata, the enum value from the
    // ResourceType enum. The editor shows them; it never writes them.
    std::vector<BuildingSlot> legacyBuildings;
    std::vector<ResourceSlot> legacyResources;

    const BuildingSlot* FindLegacyBuilding(const std::string& code) const;

    // ── Modal plumbing ─────────────────────────────────────────────────────
    // main draws the picker after the active tab (so it paints on top), which
    // means a choice lands one frame after the click. Tabs poll for their own
    // purpose, so two tabs can never pick up each other's result.
    Ed::Picker picker;
    std::string pickerPurpose;
    std::string pickerResult;

    void OpenPicker(const std::string& purpose, const std::string& title, std::vector<Ed::PickerRow> rows);
    bool TakePickerResult(const std::string& purpose, std::string& outId);

    // Rows listing every image file, for "choose a texture" pickers.
    std::vector<Ed::PickerRow> BuildFilePickerRows() const;
};

class ITextureTab
{
public:
    virtual ~ITextureTab() = default;
    virtual const char* Title() const = 0;
    // One line for the status bar: counts, and anything that looks wrong.
    virtual std::string Status() const = 0;
    virtual bool StatusIsWarning() const { return false; }
    virtual void Draw(Rectangle area) = 0;
};

// Assignment editor shared by every tab: "atlas + cell size" controls for a
// TextureRef, including the file picker hookup. Returns the y below the block.
// `purpose` must be unique per call site.
float DrawAtlasAssignment(Rectangle column, float y, ToolContext& context, const std::string& purpose,
                          TextureRef& ref, int defaultCellWidth, int defaultCellHeight);

class TerrainTab : public ITextureTab
{
public:
    explicit TerrainTab(ToolContext& context);
    const char* Title() const override { return "Tilemap"; }
    std::string Status() const override;
    bool StatusIsWarning() const override;
    void Draw(Rectangle area) override;

private:
    ToolContext& context;
    Ed::RowList slotList;
    Ed::RowList variantList;
    AtlasGrid grid;
};

class BuildingTab : public ITextureTab
{
public:
    explicit BuildingTab(ToolContext& context);
    const char* Title() const override { return "Buildings + animations"; }
    std::string Status() const override;
    bool StatusIsWarning() const override;
    void Draw(Rectangle area) override;

private:
    void DrawStage(Rectangle area, const BuildingTextureDefinition& definition, const BuildingSlot* legacy);
    float DrawInspector(Rectangle column, float y, BuildingTextureDefinition& definition, const BuildingSlot* legacy);

    ToolContext& context;
    Ed::RowList slotList;
    AtlasGrid grid;
    bool playing{true};
    double playbackStart{0.0};
    int lastSelected{-1};
};

class ResourceTab : public ITextureTab
{
public:
    explicit ResourceTab(ToolContext& context);
    const char* Title() const override { return "Resources"; }
    std::string Status() const override;
    bool StatusIsWarning() const override;
    void Draw(Rectangle area) override;

private:
    ToolContext& context;
    Ed::RowList slotList;
    AtlasGrid grid;
};

class AssetTab : public ITextureTab
{
public:
    explicit AssetTab(ToolContext& context);
    const char* Title() const override { return "All assets"; }
    std::string Status() const override;
    void Draw(Rectangle area) override;

private:
    ToolContext& context;
    Ed::RowList fileList;
    int groupFilter{-1};  // -1 = all groups
    std::string pendingAssignPath;
};

#endif
