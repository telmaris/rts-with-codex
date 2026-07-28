#include "scenes/Game.h"
#include "scenes/GameWindow.h"
#include "core/Log.h"

// Initializes main.
int main(void)
{
    Log::Msg("[Main]", "Starting RTS");
    GameWindow window;
    window.LaunchGame();
    Log::Msg("[Main]", "Shutdown");
   
    return 0;
}
