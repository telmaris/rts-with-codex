#ifndef DOCUMENT_H
#define DOCUMENT_H

// The edited document: assets/data/textures.rtsdata plus the bookkeeping an
// editor needs around it (dirty flag, seeding, save with round-trip check).

#include "GameBindings.h"
#include "TextureCatalog.h"

#include "data/TextureConfig.h"

#include <string>
#include <vector>

class Document
{
public:
    // Loads textures.rtsdata. When the file does not exist, SEEDS the model from
    // what the game does today (hardcoded terrain table, buildings.rtsdata,
    // enum-order resource icons) and says so — `WasSeeded()`.
    //
    // The distinction matters more here than anywhere else in the tool: the
    // other .rtsdata loaders silently fall back to built-in defaults, and an
    // editor that cannot tell the two apart lets you spend an hour editing
    // definitions the game will never read.
    void Load(const std::string& assetsDirectory, const TextureCatalog& catalog,
              const std::vector<BuildingSlot>& legacyBuildings);

    // Writes the file, then reads it back with the game's parser and compares.
    // A mismatch is reported instead of leaving a file that the game would
    // interpret differently from what the editor is showing.
    bool Save();

    bool IsDirty() const { return dirty; }
    void MarkDirty() { dirty = true; }
    bool WasSeeded() const { return seeded; }
    const std::string& Status() const { return status; }
    bool StatusIsError() const { return statusError; }
    const std::string& Path() const { return filePath; }

    // ── Mutations ──────────────────────────────────────────────────────────
    // Returns the id of the atlas for `gamePath`, creating one if needed.
    // `cellWidth`/`cellHeight` <= 0 means "one cell covering the whole image".
    int EnsureAtlas(const TextureCatalog& catalog, const std::string& gamePath,
                    int cellWidth = 0, int cellHeight = 0);
    void SetAtlasCellSize(int atlasId, int cellWidth, int cellHeight);
    // Drops atlases nothing refers to any more, so the file does not accumulate
    // dead entries as textures get reassigned.
    void PruneUnusedAtlases();

    // ── Queries ────────────────────────────────────────────────────────────
    int CellCount(const TextureCatalog& catalog, int atlasId) const;
    Texture2D AtlasTexture(const TextureCatalog& catalog, int atlasId) const;
    std::string AtlasLabel(int atlasId) const;
    // How many slots (any kind) point at this atlas.
    int AtlasUserCount(int atlasId) const;

    TerrainTextureDefinition* FindTerrain(const std::string& tileType);
    BuildingTextureDefinition* FindBuilding(const std::string& buildingType);
    ResourceTextureDefinition* FindResource(const std::string& resourceType);

    TextureConfig config;

private:
    void Seed(const TextureCatalog& catalog, const std::vector<BuildingSlot>& legacyBuildings);
    int NextAtlasId() const;

    std::string filePath;
    std::string status;
    bool statusError{false};
    bool dirty{false};
    bool seeded{false};
    bool backupWritten{false};
};

#endif
