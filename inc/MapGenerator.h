#ifndef MAP_GENERATOR_H
#define MAP_GENERATOR_H

#include "Utils.h"
#include "Building.h"

class Player;

enum class MapSizePreset
{
    S,
    M,
    L,
    XL
};

// Parameters controlling one generated resource patch family.
struct ResourcePatchParameters
{
    TileType type{TileType::WOOD};
    int patchCount{10};
    int minRadius{3};
    int maxRadius{8};
    float fillChance{0.46f};
    int smoothingPasses{4};
};

// Parameters controlling world generation and terrain distribution.
struct MapParameters
{
    MapSizePreset sizePreset{MapSizePreset::S};
    int sizeX{301}, sizeY{301};
    unsigned int seed{12345};
    float resourceDensity{0.5f};
    float resourceFieldSize{0.5f};
    int resourceRichness{80};
    int aiOpponentCount{1};
    int aiDifficulty{0};
    bool debugMode{false};
    std::vector<ResourcePatchParameters> resourcePatches{
        {TileType::WOOD, 18, 4, 10, 0.50f, 4},
        {TileType::COAL, 8, 3, 7, 0.46f, 4},
        {TileType::IRON_ORE, 8, 3, 7, 0.46f, 4},
        {TileType::STONE, 8, 3, 8, 0.47f, 4}
    };
};

// Generates terrain, resources and starting area constraints for a tile map.
class MapGenerator
{
    public:
        MapGenerator() = default;

        // Fills a tile map according to the supplied generation parameters.
        void GenerateTileMap(TileMap&,MapParameters&);
        // Converts a size preset to square map side length.
        static int SizeFromPreset(MapSizePreset preset);
        // Chooses the headquarters anchor tile for a generated map.
        static Vec2i PickHeadquartersAnchor(const MapParameters& params);
        // Returns the fixed headquarters footprint.
        static Vec2i HeadquartersFootprint() { return {3, 3}; }
        // Returns the starting territory side length around headquarters.
        static int HeadquartersTerritorySize() { return 27; }
        // Ensures a starting territory contains required early resources.
        static void PrepareStartingArea(TileMap&, Vec2i hqAnchor, std::mt19937&);

    private:
        // Generates every configured resource patch group.
        void GenerateResourcePatches(TileMap&, const MapParameters&, std::mt19937&);
        // Generates one organic resource patch.
        void GeneratePatch(TileMap&, const ResourcePatchParameters&, std::mt19937&);
};

// One square of terrain with optional owning player and building occupancy.
class Tile
{
    public:
        Tile() = default;
        Tile(int i) : id(i){}

        // Places an owning building anchor on this tile.
        void CreateBuilding(std::unique_ptr<Building>&& building);
        // Removes the anchored building from this tile.
        void DestroyBuilding();
        // Assigns territory ownership.
        void SetOwner(Player* player);
        // Returns true when the player may build on this tile.
        bool CanBuild(Player* player);
        // Returns true when this tile is occupied by a building anchor or footprint reference.
        bool HasBuilding() const { return building != nullptr || buildingRef != nullptr; }
        // Returns the building occupying this tile.
        Building* GetBuilding();
        // Returns the building occupying this tile.
        const Building* GetBuilding() const;
        // Returns true when this tile owns the building instance.
        bool IsBuildingAnchor() const { return building != nullptr; }

        int id;
        Player* owner{nullptr};
        std::unique_ptr<Building> building{nullptr};
        Building* buildingRef{nullptr};
        TileType tileType{static_cast<TileType>(0)};
        int terrainTextureId{0};
        int resourceRichness{0};
};

// Weighted renderer texture candidate for a terrain type.
struct WeightedTileVariant
{
    int textureId{0};
    int weight{1};
};

// Owns all map tiles and placement/pathing helpers that depend on tile layout.
class TileMap
{
    public:
        TileMap() = default;

        // Returns a tile by linear id.
        Tile& GetTile(int id);
        // Replaces a tile at linear id.
        void SetTile(int id, Tile&& tile);
        // Places a new building and marks every tile in its footprint.
        void BuildOnTile(int id, Player* player, std::unique_ptr<Building>&& building);
        // Removes an anchored building and clears footprint references.
        void DestroyBuildingAt(int id);
        // Places a building restored from a save file.
        Building* PlaceLoadedBuilding(int id, Player* player, std::unique_ptr<Building>&& building);
        // Advances all anchored buildings.
        void UpdateBuildings(double dt);
        // Returns a building occupying a linear tile id.
        Building* GetBuilding(int id);
        // Returns a building occupying map coordinates.
        Building* GetBuilding(Vec2i pos);
        // Returns true when coordinates are within map bounds.
        bool IsInside(Vec2i coords) const;
        // Returns true when every footprint tile is inside map bounds.
        bool IsInsideFootprint(Vec2i anchor, Vec2i footprint) const;
        // Returns true when a footprint can be placed for the player.
        bool CanBuildFootprint(Vec2i anchor, Vec2i footprint, Player* player) const;
        // Returns true when terrain requirements for a building type are satisfied.
        bool HasRequiredTerrainForBuilding(BuildingType type, Vec2i anchor, Vec2i footprint, int minimumTiles = 2) const;
        // Returns true when all gameplay placement rules are satisfied.
        bool CanPlaceBuilding(BuildingType type, Vec2i anchor, Vec2i footprint, Player* player) const;
        // Returns all tile ids occupied by a building footprint.
        std::vector<int> GetBuildingTileIds(const Building* building) const;
        // Returns all tile ids adjacent to a building footprint.
        std::vector<int> GetAdjacentTileIds(const Building* building) const;
        // Returns the selected terrain texture for a terrain type.
        int GetTerrainTextureId(TileType type) const;
        // Picks a weighted terrain texture variant for generation.
        int PickTerrainTexture(TileType type, std::mt19937& rng) const;
        // Computes road autotile bitmask from neighboring roads.
        int GetRoadAutotileMask(Vec2i pos) const;
        // Returns road texture id matching current road neighborhood.
        int GetRoadTextureId(Vec2i pos) const;
        // Refreshes road textures at and around a changed position.
        void RefreshRoadTilesAround(Vec2i pos);
        // Finds the closest storage building reachable from a source building.
        Building* FindNearestStorage(Building* source, Player* player);
        // Connects a placed building to default supplier and receiver candidates.
        void AutoConnectBuilding(Building* building);
        // Makes one building send compatible outputs to another building.
        void ConnectReceiver(Building* source, Building* receiver);
        // Marks rounded territory around a source coordinate for one player.
        void SetTerritory(Vec2i source, int size, Player* player);
        // Rebuilds all territory owned by one player from active military buildings.
        void RecalculateTerritory(Player* player);

        // Converts map coordinates to linear tile id.
        int GetIdFromCoords(Vec2i coords) const;
        // Converts linear tile id to map coordinates.
        Vec2i GetCoordsFromId(int id) const;
        
        Tile& operator [] (size_t idx) { return tilemap[idx];}
        Tile& operator [] (Vec2i pos) { return tilemap[GetIdFromCoords(pos)];}
        
        MapParameters params;
        MapGenerator generator;
        std::map<TileType, std::vector<WeightedTileVariant>> terrainVariants{
            {TileType::GRASS, {{9, 1},{10, 1},{11, 1},{12, 1}}},
            {TileType::COAL, {{0, 1},{1, 1},{2, 1}}},
            {TileType::IRON_ORE, {{13, 1},{14, 1},{15, 1}}},
            {TileType::WOOD, {{6, 1},{7, 1},{8, 1}}},
            {TileType::STONE, {{16, 1},{17, 1},{18, 1}}}
        };

        std::vector<Tile> tilemap;
        bool terrainDirty{true};
        bool buildingsDirty{true};
        bool territoryDirty{true};
};






#endif
