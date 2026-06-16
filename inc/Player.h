#ifndef PLAYER_H
#define PLAYER_H

#include "Utils.h"
#include "InputHandler.h"
#include "BuildingFactory.h"
#include "RoadNetwork.h"

class TileMap;

class Player
{
public:
    Player() = default;
    Player(int i, TileMap& tmap) : tilemap(tmap), id(i), build(this, tilemap){ roadNetwork = std::make_unique<RoadNetwork>(tilemap);}

    void Update(double dt)
    {
        input->Update(dt);
        input->GetInputs();
    }

    template <typename T>
    Building* Build(int tilePos)
    {
        static_assert(std::is_base_of<Building, T>::value);
        build.Build<T>(tilePos);
        auto bld = tilemap.GetBuilding(tilePos);
        if(bld != nullptr)
        {
            roadNetwork->UpdateNavMap(tilePos, bld);
        }
        return bld;
    }

    template <typename T>
    Building* Build(Vec2i pos)
    {
        return Build<T>(tilemap.GetIdFromCoords(pos));
    }

    inline void BeginTransport(Building* src, Building* dest, Resource* res)
    {
        roadNetwork->BeginTransport(src, dest, res);
    }

    int id;

// private:
    std::unique_ptr<InputHandler> input;
    std::unique_ptr<RoadNetwork> roadNetwork;
    TileMap& tilemap;
    BFactory build; // initialize BuildingFactory with current player
};

class HumanPlayer : public Player
{
public:
    HumanPlayer() = default;
};

#endif