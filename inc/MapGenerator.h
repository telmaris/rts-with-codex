#ifndef MAP_GENERATOR_H
#define MAP_GENERATOR_H

#include "Utils.h"
#include "Player.h"

struct MapParameters
{
    int sizeX, sizeY;
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

        int id;
        Player* owner = nullptr;  //ten tam ten pointer oznacza wlasciciela tego terytorium
        std::unique_ptr<Building> building{nullptr};
        TileType tileType{static_cast<TileType>(0)};
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
        void SetTerritory(Vec2i source, int size, Player* player);

        int GetIdFromCoords(Vec2i coords);
        Vec2i GetCoordsFromId(int id);
        
        Tile& operator [] (size_t idx) { return tilemap[idx];}
        Tile& operator [] (Vec2i pos) { return tilemap[GetIdFromCoords(pos)];}
        
        MapParameters params;
        MapGenerator generator;

        std::vector<Tile> tilemap;
};






#endif