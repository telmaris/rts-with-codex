#include "../inc/Game.h"

// Initializes runtime state for this object.
void Game::InitGame()
{
    gameWorld = std::make_unique<GameWorld>();
}

// Initializes Game::GameLoop.
void Game::GameLoop()
{
    using clock_t = std::chrono::steady_clock;
    using time_point_t = clock_t::time_point;

    time_point_t last = clock_t::now();

    while(gameRunning)
    {
        time_point_t now = clock_t::now();
        std::chrono::duration<double> delta = now - last;
        last = now;

        double dt = delta.count();

        gameWorld->Update(dt);

    }
}

// Initializes Game::Cleanup.
void Game::Cleanup()
{
    
}
