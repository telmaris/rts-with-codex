#ifndef TEXTURE_CONFIG_H
#define TEXTURE_CONFIG_H

// Data model + parser for assets/data/textures.rtsdata: which image every
// drawable slot uses, and how it animates.
//
// Addressing is uniform: everything is "cell `textureId` of atlas `atlasId`".
// An atlas is just (image, cell size), so a standalone 96x96 building sprite is
// a legal one-cell atlas — no art needs repacking to fit the scheme, and the
// horizontal-strip animation case becomes an ordinary run of consecutive cells.
//
// Layering, deliberately:
//   atlasId   — property of the SLOT (tile/building/resource type), from this
//               file. Never serialized into saves or the wire; the type implies it.
//   textureId — property of the INSTANCE (which cell it rolled / autotiled to),
//               serialized exactly as today. Save format is unaffected.
//
// GameScene reads building sprites and their animation clips from this file.
// Terrain and resource bindings are still loaded through their legacy paths
// until their runtime migration is completed.

#include <string>
#include <vector>

struct TextureAtlasDefinition
{
    int id{0};
    // Human label, for tooling and error messages. Not an identity — `id` is.
    std::string name;
    std::string path;
    int cellWidth{32};
    int cellHeight{32};
};

// One cell of one atlas. An unset reference has atlasId < 0.
struct TextureRef
{
    int atlasId{-1};
    int textureId{0};

    bool IsValid() const { return atlasId >= 0; }
    bool operator==(const TextureRef& other) const
    {
        return atlasId == other.atlasId && textureId == other.textureId;
    }
};

// Frames are CONSECUTIVE cells starting at the slot's textureId — the constraint
// baked into ResolveAnimationFrame, which returns startFrameId + frameIndex.
struct TextureAnimationDefinition
{
    bool enabled{false};
    int frames{1};
    double frameTime{0.12};
    bool looping{true};

    bool operator==(const TextureAnimationDefinition& other) const;
};

struct TerrainVariantDefinition
{
    TextureRef texture;
    int weight{1};
};

struct TerrainTextureDefinition
{
    // Vocabulary of BuildingConfig's ParseTileType ("GRASS", "IRON_ORE", ...).
    std::string tileType;
    std::vector<TerrainVariantDefinition> variants;
};

// A transparent resource sprite drawn over its terrain tile. `edge` variants
// are selected only at the rim of a generated deposit and should therefore be
// authored more sparsely than its ordinary fillers.
struct ResourceOverlayVariantDefinition
{
    TextureRef texture;
    int weight{1};
    bool edge{false};
};

struct ResourceOverlayTextureDefinition
{
    // Same vocabulary as TerrainTextureDefinition, but this is visual-only.
    std::string tileType;
    std::vector<ResourceOverlayVariantDefinition> variants;
};

struct BuildingTextureDefinition
{
    // Block key of buildings.rtsdata ("Headquarters", "Foundry", ...).
    std::string buildingType;
    TextureRef sprite;
    TextureAnimationDefinition animation;
};

struct ResourceTextureDefinition
{
    // rt2s() spelling ("IRON_ORE", "STEEL", ...).
    std::string resourceType;
    TextureRef icon;
};

struct TextureConfig
{
    std::vector<TextureAtlasDefinition> atlases;
    std::vector<TerrainTextureDefinition> terrain;
    std::vector<ResourceOverlayTextureDefinition> resourceOverlays;
    std::vector<BuildingTextureDefinition> buildings;
    std::vector<ResourceTextureDefinition> resources;

    const TextureAtlasDefinition* FindAtlas(int id) const;
    const TextureAtlasDefinition* FindAtlasByPath(const std::string& path) const;
    bool IsEmpty() const;
};

// Parses the file.
//
// Unlike LoadTechnologyDefinitionsFromFile / GetBuildingDefinitions, this NEVER
// substitutes built-in defaults for a missing or unparseable file: it reports
// the failure through `outError` and returns an empty config. An editor that
// cannot tell "empty file" from "no file" edits data the game will never see.
TextureConfig LoadTextureConfig(const std::string& path, std::string* outError = nullptr);

// Writes the whole config, replacing the file. Returns false on IO failure.
bool SaveTextureConfig(const std::string& path, const TextureConfig& config, std::string* outError = nullptr);

// The exact text SaveTextureConfig writes. Exposed for round-trip checks.
std::string FormatTextureConfig(const TextureConfig& config);

// Field-by-field comparison, for verifying a save survived the round trip.
bool TextureConfigEquals(const TextureConfig& a, const TextureConfig& b, std::string* outDifference = nullptr);

#endif
