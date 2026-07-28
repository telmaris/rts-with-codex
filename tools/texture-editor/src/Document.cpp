#include "Document.h"

#include <algorithm>
#include <filesystem>

namespace
{
    // Sanitizes a file stem into an atlas name: no quotes, no spaces, so the
    // token survives a round trip through the .rtsdata tokenizer.
    std::string AtlasNameFor(const std::string& gamePath)
    {
        std::string stem = std::filesystem::path(gamePath).stem().string();
        std::string name;
        for (char c : stem)
            name += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? c : '_';
        return name.empty() ? "atlas" : name;
    }
}

void Document::Load(const std::string& assetsDirectory, const TextureCatalog& catalog,
                    const std::vector<BuildingSlot>& legacyBuildings)
{
    filePath = assetsDirectory + "/data/textures.rtsdata";
    dirty = false;
    backupWritten = false;

    std::string error;
    config = LoadTextureConfig(filePath, &error);

    if (error.empty())
    {
        seeded = false;
        statusError = false;
        status = "loaded " + std::to_string(config.atlases.size()) + " atlases, " +
                 std::to_string(config.terrain.size()) + " terrain, " +
                 std::to_string(config.buildings.size()) + " buildings, " +
                 std::to_string(config.resources.size()) + " resources";
        return;
    }

    Seed(catalog, legacyBuildings);
    seeded = true;
    statusError = false;
    status = "textures.rtsdata does not exist yet - seeded from the game's current behaviour. "
             "Nothing is written until you save.";
    // Seeded content is unsaved by definition; showing it as clean would invite
    // closing the tool and losing the migration.
    dirty = true;
}

// Reconstructs, as data, exactly what the game hardcodes today. This function
// is the migration: once it has been saved once and the game reads the file,
// the hardcoded tables on the game side can go.
void Document::Seed(const TextureCatalog& catalog, const std::vector<BuildingSlot>& legacyBuildings)
{
    config = TextureConfig{};

    // Atlas 0/1 keep the ids the game already uses for terrain and icons.
    TextureAtlasDefinition terrainAtlas;
    terrainAtlas.id = 0;
    terrainAtlas.name = "terrain";
    terrainAtlas.path = kTerrainAtlasGamePath;
    terrainAtlas.cellWidth = kTerrainTileSize;
    terrainAtlas.cellHeight = kTerrainTileSize;
    config.atlases.push_back(terrainAtlas);

    TextureAtlasDefinition resourceAtlas;
    resourceAtlas.id = 1;
    resourceAtlas.name = "resources";
    resourceAtlas.path = kResourceAtlasGamePath;
    resourceAtlas.cellWidth = kResourceIconSize;
    resourceAtlas.cellHeight = kResourceIconSize;
    config.atlases.push_back(resourceAtlas);

    for (const auto& slot : LoadTerrainSlots())
    {
        TerrainTextureDefinition terrain;
        terrain.tileType = slot.code;
        for (const auto& variant : slot.variants)
            terrain.variants.push_back({TextureRef{0, variant.atlasId}, variant.weight});
        config.terrain.push_back(terrain);
    }

    for (const auto& slot : legacyBuildings)
    {
        BuildingTextureDefinition building;
        building.buildingType = slot.code;
        if (!slot.texturePath.empty())
        {
            // Standalone sprite -> one-cell atlas covering the whole image. No
            // repacking; the scheme just describes what is already there.
            building.sprite.atlasId = EnsureAtlas(catalog, slot.texturePath);
            building.sprite.textureId = 0;
        }
        config.buildings.push_back(building);
    }

    for (const auto& slot : LoadResourceSlots())
    {
        ResourceTextureDefinition resource;
        resource.resourceType = slot.code;
        resource.icon = TextureRef{1, slot.iconIndex};
        config.resources.push_back(resource);
    }
}

int Document::NextAtlasId() const
{
    int highest = -1;
    for (const auto& atlas : config.atlases)
        highest = std::max(highest, atlas.id);
    return highest + 1;
}

int Document::EnsureAtlas(const TextureCatalog& catalog, const std::string& gamePath,
                          int cellWidth, int cellHeight)
{
    if (const TextureAtlasDefinition* existing = config.FindAtlasByPath(gamePath))
        return existing->id;

    TextureAtlasDefinition atlas;
    atlas.id = NextAtlasId();
    atlas.name = AtlasNameFor(gamePath);
    atlas.path = gamePath;

    const TextureAsset* asset = catalog.Find(gamePath);
    int imageWidth = asset != nullptr ? asset->width : 0;
    int imageHeight = asset != nullptr ? asset->height : 0;

    atlas.cellWidth = cellWidth > 0 ? cellWidth : std::max(1, imageWidth);
    atlas.cellHeight = cellHeight > 0 ? cellHeight : std::max(1, imageHeight);

    config.atlases.push_back(atlas);
    dirty = true;
    return atlas.id;
}

void Document::SetAtlasCellSize(int atlasId, int cellWidth, int cellHeight)
{
    for (auto& atlas : config.atlases)
    {
        if (atlas.id != atlasId)
            continue;
        atlas.cellWidth = std::max(1, cellWidth);
        atlas.cellHeight = std::max(1, cellHeight);
        dirty = true;
        return;
    }
}

void Document::PruneUnusedAtlases()
{
    std::vector<int> keep;
    for (const auto& terrain : config.terrain)
        for (const auto& variant : terrain.variants)
            keep.push_back(variant.texture.atlasId);
    for (const auto& building : config.buildings)
        keep.push_back(building.sprite.atlasId);
    for (const auto& resource : config.resources)
        keep.push_back(resource.icon.atlasId);

    size_t before = config.atlases.size();
    config.atlases.erase(
        std::remove_if(config.atlases.begin(), config.atlases.end(),
                       [&](const TextureAtlasDefinition& atlas) {
                           return std::find(keep.begin(), keep.end(), atlas.id) == keep.end();
                       }),
        config.atlases.end());

    if (config.atlases.size() != before)
        dirty = true;
}

int Document::CellCount(const TextureCatalog& catalog, int atlasId) const
{
    const TextureAtlasDefinition* atlas = config.FindAtlas(atlasId);
    if (atlas == nullptr)
        return 0;

    const TextureAsset* asset = catalog.Find(atlas->path);
    if (asset == nullptr || atlas->cellWidth <= 0 || atlas->cellHeight <= 0)
        return 0;

    return std::max(0, (asset->width / atlas->cellWidth) * (asset->height / atlas->cellHeight));
}

Texture2D Document::AtlasTexture(const TextureCatalog& catalog, int atlasId) const
{
    const TextureAtlasDefinition* atlas = config.FindAtlas(atlasId);
    return atlas != nullptr ? catalog.TextureFor(atlas->path) : Texture2D{};
}

std::string Document::AtlasLabel(int atlasId) const
{
    const TextureAtlasDefinition* atlas = config.FindAtlas(atlasId);
    if (atlas == nullptr)
        return "(none)";
    return std::to_string(atlas->id) + " " + atlas->name;
}

// Counts SLOTS, not references: a tile type with four grass variants is one
// user, because it is one thing someone would go and change. Counting variants
// made the terrain tileset read "shared by 28 slots", which is true of the
// references and useless as a warning.
int Document::AtlasUserCount(int atlasId) const
{
    int users = 0;
    for (const auto& terrain : config.terrain)
    {
        bool uses = std::any_of(terrain.variants.begin(), terrain.variants.end(),
                                [atlasId](const TerrainVariantDefinition& variant) {
                                    return variant.texture.atlasId == atlasId;
                                });
        if (uses)
            users++;
    }
    for (const auto& building : config.buildings)
        if (building.sprite.atlasId == atlasId)
            users++;
    for (const auto& resource : config.resources)
        if (resource.icon.atlasId == atlasId)
            users++;
    return users;
}

TerrainTextureDefinition* Document::FindTerrain(const std::string& tileType)
{
    auto it = std::find_if(config.terrain.begin(), config.terrain.end(),
                           [&](const TerrainTextureDefinition& e) { return e.tileType == tileType; });
    return it != config.terrain.end() ? &*it : nullptr;
}

BuildingTextureDefinition* Document::FindBuilding(const std::string& buildingType)
{
    auto it = std::find_if(config.buildings.begin(), config.buildings.end(),
                           [&](const BuildingTextureDefinition& e) { return e.buildingType == buildingType; });
    return it != config.buildings.end() ? &*it : nullptr;
}

ResourceTextureDefinition* Document::FindResource(const std::string& resourceType)
{
    auto it = std::find_if(config.resources.begin(), config.resources.end(),
                           [&](const ResourceTextureDefinition& e) { return e.resourceType == resourceType; });
    return it != config.resources.end() ? &*it : nullptr;
}

bool Document::Save()
{
    if (config.IsEmpty())
    {
        status = "refusing to save an empty config";
        statusError = true;
        return false;
    }

    // One-time backup, so the first save of a hand-edited file is recoverable.
    std::error_code ec;
    if (!backupWritten && std::filesystem::exists(filePath, ec))
    {
        std::filesystem::copy_file(filePath, filePath + ".bak",
                                   std::filesystem::copy_options::overwrite_existing, ec);
        backupWritten = true;
    }

    std::filesystem::create_directories(std::filesystem::path(filePath).parent_path(), ec);

    std::string error;
    if (!SaveTextureConfig(filePath, config, &error))
    {
        status = error;
        statusError = true;
        return false;
    }

    // Read back with the game's own parser. Without this the tool can happily
    // write something it alone understands.
    std::string readError;
    TextureConfig reloaded = LoadTextureConfig(filePath, &readError);
    if (!readError.empty())
    {
        status = "saved, but reading it back failed: " + readError;
        statusError = true;
        return false;
    }

    std::string difference;
    if (!TextureConfigEquals(config, reloaded, &difference))
    {
        status = "Round-trip mismatch: " + difference + " - the file on disk does not mean what the editor shows";
        statusError = true;
        return false;
    }

    dirty = false;
    seeded = false;
    statusError = false;
    status = "saved " + filePath + " (round-trip verified)";
    return true;
}
