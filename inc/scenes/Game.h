#ifndef GAME_H
#define GAME_H

#include "core/Types.h"
#include "ui/Window.h"
#include "core/GameWorld.h"

// Legacy game facade kept for experiments outside the scene-driven window shell.
class Game
{
public:
    Game() = default;
    ~Game() = default;

    // Creates owned game systems.
    void InitGame();
    // Runs the legacy simulation loop.
    void GameLoop();
    // Releases owned game systems.
    void Cleanup();

private:
    bool gameRunning = true;
    std::unique_ptr<Window> window;
    std::unique_ptr<GameWorld> gameWorld;
};


#endif
