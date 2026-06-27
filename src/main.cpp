#include "Game.h"
#include "GameWindow.h"
#include "Utils.h"

// Initializes main.
int main(void)
{
    Log::Msg("[Main]", "Starting RTS");
    GameWindow window;
    window.LaunchGame();
    Log::Msg("[Main]", "Shutdown");
   
    return 0;
}
