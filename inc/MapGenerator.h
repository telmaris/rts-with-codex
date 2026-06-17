#ifndef MAP_GENERATOR_H
#define MAP_GENERATOR_H

#include "Utils.h"
#include "Building.h"

class Player;

constexpr int TILE_SIZE = 32;

struct MapParameters
{
    int sizeX, sizeY;
    unsigned int seed{12345};
};

class MapGenerator
{
    public:
        MapGenerator() = default;

        void GenerateTileMap(TileMap&,MapParameters&);
};



class Tile
{
    public:
        Tile() = default;
        Tile(int i) : id(i){}

        void CreateBuilding(std::unique_ptr<Building>&& building);
        void DestroyBuilding();
        void SetOwner(Player* player);
        bool CanBuild(Player* player);
        bool HasBuilding() const { return building != nullptr || buildingRef != nullptr; }
        Building* GetBuilding();
        const Building* GetBuilding() const;
        bool IsBuildingAnchor() const { return building != nullptr; }

        int id;
        Player* owner = nullptr;  //ten tam ten pointer oznacza wlasciciela tego terytorium
        std::unique_ptr<Building> building{nullptr};
        Building* buildingRef{nullptr};
        TileType tileType{static_cast<TileType>(0)};
        int terrainTextureId{0};
};

struct WeightedTileVariant
{
    int textureId{0};
    int weight{1};
};

class TileMap
{
    public:
        TileMap() = default;

        Tile& GetTile(int id);              //Tile pointer bo std::unique_ptr
        void SetTile(int id, Tile&& tile);
        void BuildOnTile(int id, Player* player, std::unique_ptr<Building>&& building);
        void UpdateBuildings(double dt);
        Building* GetBuilding(int id);
        Building* GetBuilding(Vec2i pos);
        bool IsInside(Vec2i coords) const;
        bool IsInsideFootprint(Vec2i anchor, Vec2i footprint) const;
        bool CanBuildFootprint(Vec2i anchor, Vec2i footprint, Player* player) const;
        std::vector<int> GetBuildingTileIds(const Building* building) const;
        std::vector<int> GetAdjacentTileIds(const Building* building) const;
        int GetTerrainTextureId(TileType type) const;
        int PickTerrainTexture(TileType type, std::mt19937& rng) const;
        int GetRoadAutotileMask(Vec2i pos) const;
        int GetRoadTextureId(Vec2i pos) const;
        void RefreshRoadTilesAround(Vec2i pos);
        Building* FindNearestStorage(Building* source, Player* player);
        void AutoConnectBuilding(Building* building);
        void ConnectReceiver(Building* source, Building* receiver);
        void SetTerritory(Vec2i source, int size, Player* player);

        int GetIdFromCoords(Vec2i coords) const;
        Vec2i GetCoordsFromId(int id) const;
        
        Tile& operator [] (size_t idx) { return tilemap[idx];}
        Tile& operator [] (Vec2i pos) { return tilemap[GetIdFromCoords(pos)];}
        
        MapParameters params;
        MapGenerator generator;
        std::map<TileType, std::vector<WeightedTileVariant>> terrainVariants{
            {TileType::GRASS, {{0, 1}}},
            {TileType::COAL, {{1, 1}}},
            {TileType::IRON_ORE, {{2, 1}}},
            {TileType::WOOD, {{0, 1}}}
        };

        std::vector<Tile> tilemap;
};






#endif
