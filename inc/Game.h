#ifndef GAME_H
#define GAME_H

#include "Utils.h"
#include "Window.h"
#include "GameWorld.h"

// fasada gry
class Game
{
public:
    Game() = default;
    ~Game() = default;

    void InitGame();
    void GameLoop();
    void Cleanup();

private:
    bool gameRunning = true;
    std::unique_ptr<Window> window;
    std::unique_ptr<GameWorld> gameWorld;
};


#endif