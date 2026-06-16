#include "Game.h"
// #include "Scenes.h"
#include "GameWindow.h"

int main(void)
{
    // auto g = std::make_unique<Game>();
    const auto tag = "[MAIN]";

//    g->InitGame();
//    g->GameLoop();

    GameWindow window;
    
   window.LaunchGame();
   
    return 0;
}