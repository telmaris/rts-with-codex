#include "../inc/Game.h"

void Game::InitGame()
{
    gameWorld = std::make_unique<GameWorld>();
    // gameWorld->InitWorld("default");
}

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

void Game::Cleanup()
{
    
}