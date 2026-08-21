#include "GameBindings.h"

#include "data/RtsDataFile.h"

#include <algorithm>

const char* const kTerrainAtlasGamePath = "assets/textures/terrain/terrain_tileset.png";
const char* const kResourceAtlasGamePath = "assets/textures/resources/basic_resources.png";

int TerrainSlot::TotalWeight() const
{
    int total = 0;
    for (const auto& variant : variants)
        total += std::max(0, variant.weight);
    return total;
}

// ── Terrain ────────────────────────────────────────────────────────────────
// MIRROR of TileMap::terrainVariants (inc/simulation/MapGenerator.h). It is a
// default member initializer inside TileMap, so reading it for real would mean
// constructing a TileMap — which drags in Building, Player and the whole world.
//
// This is the one place in the tool that can silently drift from the game, and
// it is exactly what the integration step deletes: once the table lives in
// assets/data/textures.rtsdata, both sides read the same file and the mirror
// goes away. Until then: change one, change the other.
std::vector<TerrainSlot> LoadTerrainSlots()
{
    return {
        {TileType::GRASS,       "GRASS",       {{9, 1}, {10, 1}, {11, 1}, {12, 1}}, false},
        {TileType::WOOD,        "WOOD",        {{6, 1}, {7, 1}, {8, 1}},            false},
        {TileType::COAL,        "COAL",        {{0, 1}, {1, 1}, {2, 1}},            false},
        {TileType::IRON_ORE,    "IRON_ORE",    {{13, 1}, {14, 1}, {15, 1}},         false},
        {TileType::STONE,       "STONE",       {{16, 1}, {17, 1}, {18, 1}},         false},
        // The game marks these "placeholder visuals (reuse existing ids until
        // dedicated assets exist)" — they borrow another type's cells.
        {TileType::COPPER_ORE,  "COPPER_ORE",  {{0, 1}, {1, 1}},                    true},
        {TileType::TIN_ORE,     "TIN_ORE",     {{16, 1}, {17, 1}},                  true},
        {TileType::SILVER_ORE,  "SILVER_ORE",  {{13, 1}, {14, 1}},                  true},
        {TileType::GOLD_ORE,    "GOLD_ORE",    {{14, 1}, {15, 1}},                  true},
        {TileType::SAND,        "SAND",        {{9, 1}, {10, 1}},                   true},
        {TileType::SULFUR,      "SULFUR",      {{2, 1}},                            true},
        {TileType::SALTPETER,   "SALTPETER",   {{18, 1}},                           true},
    };
}

// ── Buildings ──────────────────────────────────────────────────────────────
// Read with the game's tokenizer off the real file, so the tool sees whatever
// the game sees — including a texture path that points at nothing.
std::vector<BuildingSlot> LoadBuildingSlots(const std::string& dataPath)
{
    std::vector<BuildingSlot> slots;
    RtsDataLines lines = ReadRtsDataLines(dataPath);

    bool inBlock = false;
    BuildingSlot current;

    for (const auto& tokens : lines)
    {
        const std::string& command = tokens[0];

        if (command == "building" && tokens.size() >= 2)
        {
            inBlock = true;
            current = BuildingSlot{};
            current.code = tokens[1];
            current.displayName = tokens[1];
            continue;
        }

        if (!inBlock)
            continue;

        if (command == "end")
        {
            slots.push_back(current);
            inBlock = false;
        }
        else if (command == "name" && tokens.size() >= 2)
        {
            current.displayName = tokens[1];
        }
        else if (command == "texture" && tokens.size() >= 2)
        {
            current.texturePath = tokens[1];
        }
        else if (command == "footprint" && tokens.size() >= 3)
        {
            current.footprintX = RtsDataIntOr(tokens[1], current.footprintX);
            current.footprintY = RtsDataIntOr(tokens[2], current.footprintY);
        }
        else if (command == "texture_id" && tokens.size() >= 2)
        {
            current.legacyTextureId = RtsDataIntOr(tokens[1], current.legacyTextureId);
            current.hasLegacyTextureId = true;
        }
    }

    return slots;
}

// ── Resources ──────────────────────────────────────────────────────────────
// Not a mirror: this is the actual rule, reproduced. ResourceIconAtlas::GetRect
// takes the ResourceType enum value as the atlas cell index, so the icon layout
// is bound to enum ORDER with nothing written down anywhere. Sorted by index so
// the list reads in the same order as the atlas cells next to it.
std::vector<ResourceSlot> LoadResourceSlots()
{
    std::vector<ResourceSlot> slots;
    slots.reserve(std::size(resourceTypes));

    for (ResourceType type : resourceTypes)
    {
        ResourceSlot slot;
        slot.type = type;
        slot.code = rt2s(type);
        slot.iconIndex = std::max(0, static_cast<int>(type));
        slots.push_back(slot);
    }

    std::sort(slots.begin(), slots.end(),
              [](const ResourceSlot& a, const ResourceSlot& b) { return a.iconIndex < b.iconIndex; });
    return slots;
}
