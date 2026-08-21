#include "scenes/Game.h"
#include "scenes/GameWindow.h"
#include "core/Log.h"

// Initializes main.
int main(void)
{
    Log::Initialize();
    Log::Msg("[Main]", "Starting Tvorin");
    GameWindow window;
    window.LaunchGame();
    Log::Msg("[Main]", "Tvorin shutdown");
    Log::Shutdown();
   
    return 0;
}
