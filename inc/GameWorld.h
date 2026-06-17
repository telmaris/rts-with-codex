#ifndef GAMEWORLD_H
#define GAMEWORLD_H

#include "Utils.h"
#include "MapGenerator.h"
#include "Player.h"
#include "Renderer.h"

class PlayerHandler
{
    public:
        PlayerHandler() = default;

        std::map<int, std::unique_ptr<Player>> players;
};

// main game logic class
// todo: make save by serializing GameWorld class

class GameWorld
{
    public:
        GameWorld() = default;

        void InitWorld(std::string, Renderer*);
        void Update(double);
        void DrawMap();
    
        TileMap tilemap;
        PlayerHandler playerHandler;
        Renderer* render;
        std::string worldName{"default"};


        // test!!
        Texture2D test;
};


#endif
