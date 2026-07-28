#ifndef GAME_BINDINGS_H
#define GAME_BINDINGS_H

// What the GAME currently believes about textures — the slot side of the tool.
//
// This header is the whole point of the pilot: today the answer to "which image
// does slot X draw?" lives in three unrelated places, in three different forms.
// Collecting them here makes the drift visible, and gives the integration step
// one file to replace with a real assets/data/textures.rtsdata reader.
//
//   terrain   — hardcoded C++ table (TileMap::terrainVariants, MapGenerator.h).
//               MIRRORED below. The only mirror in this file; it cannot be read
//               from the game without constructing a TileMap.
//   buildings — assets/data/buildings.rtsdata, parsed here with the game's own
//               tokenizer, so no vocabulary is duplicated.
//   resources — no data at all: the icon index IS the ResourceType enum value
//               (ResourceIconAtlas::GetRect). Derived below, not mirrored.

#include "data/Resource.h"
#include "economy/Building.h"  // TileType — enum only, nothing is instantiated.

#include <string>
#include <vector>

// ── Terrain (tilemap) ──────────────────────────────────────────────────────
// One weighted candidate cell in the terrain atlas. Same shape as the game's
// WeightedTileVariant; a tile type rolls one of these per generated tile, which
// is what makes grass look like four different tiles.
struct TerrainVariantBinding
{
    int atlasId{0};
    int weight{1};
};

struct TerrainSlot
{
    TileType type{TileType::GRASS};
    // Vocabulary accepted by the game's ParseTileType ("GRASS", "IRON_ORE", ...).
    std::string code;
    std::vector<TerrainVariantBinding> variants;
    // Set when the game reuses another type's art instead of owning any.
    bool placeholder{false};

    int TotalWeight() const;
};

// ── Buildings ──────────────────────────────────────────────────────────────
struct BuildingSlot
{
    // Block key in buildings.rtsdata ("Headquarters") — the code the game keys on.
    std::string code;
    std::string displayName;
    // Empty when the block declares no texture: the game then draws a brown
    // rounded rectangle instead (Renderer::DrawBuildingTexture's fallback).
    std::string texturePath;
    int footprintX{1};
    int footprintY{1};
    // buildings.rtsdata's `texture_id`. Parsed into Building::textureId, saved to
    // disk, and read by NO renderer — see README, "znaleziska".
    int legacyTextureId{0};
    bool hasLegacyTextureId{false};
};

// ── Resources ──────────────────────────────────────────────────────────────
struct ResourceSlot
{
    ResourceType type{ResourceType::Null};
    std::string code;
    // Exactly what ResourceIconAtlas::GetRect computes today.
    int iconIndex{0};
};

// ── Accessors ──────────────────────────────────────────────────────────────
// Paths and cell sizes as GameScene::GameScene passes them today.
extern const char* const kTerrainAtlasGamePath;
extern const char* const kResourceAtlasGamePath;
constexpr int kTerrainTileSize = 32;   // TILE_SIZE
constexpr int kResourceIconSize = 64;  // GuiPanel::LoadResourceAtlas default

std::vector<TerrainSlot> LoadTerrainSlots();
// Parses assets/data/buildings.rtsdata with the game's tokenizer.
// `dataPath` is absolute; a missing file yields an empty list (reported by the tab).
std::vector<BuildingSlot> LoadBuildingSlots(const std::string& dataPath);
std::vector<ResourceSlot> LoadResourceSlots();

#endif
