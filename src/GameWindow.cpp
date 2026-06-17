#include "../inc/Scenes.h"

#include "raylib.h"

void GameWindow::LaunchGame()
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "RTS");
    SetWindowMinSize(960, 540);

    AddScene<MainMenuScene>("MainScene");
    ChangeScene("MainScene", "");
    AddScene<OptionsScene>("OptionsScene");
    AddScene<GameScene>("GameScene");
    AddScene<NewGameScene>("NewGameScene");
    AddScene<LoadGameScene>("LoadGameScene");
    AddScene<GameMenuScene>("GameMenuScene");
    // itd

    MainLoop();

    CloseWindow();
}

void GameWindow::HandleEvent(std::shared_ptr<Event> e)
{
    Log::Msg(tag, e->msgName, " received!");
    auto ptr = std::dynamic_pointer_cast<QuitGameEvent>(e);
    if (ptr != nullptr)
    {
        
        isRunning = false;
        // handle quit game eventl
    }

    auto ptr2 = std::dynamic_pointer_cast<ChangeSceneEvent>(e);
    if (ptr2 != nullptr)
    {
        ChangeScene(ptr2->sceneName, ptr2->previousSceneName);
    }

    auto ptr3 = std::dynamic_pointer_cast<ToggleFullscreenEvent>(e);
    if (ptr3 != nullptr)
    {
        if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE))
            ClearWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
        else
            SetWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    }
}

void GameWindow::MainLoop()
{
    // todo: pomiar czasu itd

    SetTargetFPS(150);
    while (isRunning)
    {
        UpdateWindowSize();
        Update(GetFrameTime());
    }
}

void GameWindow::UpdateWindowSize()
{
    Vec2i currentSize{GetRenderWidth(), GetRenderHeight()};
    if(currentSize != lastWindowSize)
    {
        auto e = std::make_shared<WindowSizeChangedEvent>();
        e->sender = nullptr;
        e->windowSize = currentSize;
        Broadcast(e);
    }
    lastWindowSize = currentSize;
}
