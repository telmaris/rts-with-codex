#include "scenes/Scenes.h"

#include "raylib.h"

// Initializes GameWindow::LaunchGame.
void GameWindow::LaunchGame()
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "RTS");
    int monitor = GetCurrentMonitor();
    SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
    SetWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    SetWindowMinSize(960, 540);
    GuiPanel::LoadUiFont("assets/fonts/MarcellusSC-Regular.ttf");

    InitAudioDevice();
    audio.Init();

    AudioConfig audioConfig = LoadAudioConfig();
    audio.SetMasterVolume(audioConfig.masterVolume);
    audio.SetMusicVolume(audioConfig.musicVolume);
    audio.SetSfxVolume(audioConfig.sfxVolume);

    // Music themes — add .ogg/.mp3 files to assets/audio/music/ to activate them.
    audio.RegisterMusic("menu",      "assets/music/menu_theme.ogg");
    audio.RegisterMusic("gameplay",  "assets/music/gameplay_theme.ogg");
    audio.RegisterMusic("battle",    "assets/music/battle_theme.ogg");

    // Sound effects — add .wav/.ogg files to assets/audio/sfx/ to activate them.
    audio.RegisterSound("click",        "assets/sfx/mouse_click.wav");
    audio.RegisterSound("build",        "assets/sfx/button_clicked.mp3");
    audio.RegisterSound("notification", "assets/sfx/notification.mp3");
    audio.RegisterSound("error",        "assets/sfx/error.wav");
    audio.RegisterSound("research",     "assets/sfx/research_ready.mp3");
    audio.RegisterSound("destroy",      "assets/sfx/destroy.wav");
    audio.RegisterSound("recruit",      "assets/sfx/recruit.wav");
    audio.RegisterSound("march",        "assets/sfx/march.wav");
    audio.RegisterSound("attack",       "assets/sfx/attack.wav");

    AddScene<MainMenuScene>("MainScene");
    ChangeScene("MainScene", "");
    AddScene<OptionsScene>("OptionsScene");
    AddScene<GameScene>("GameScene");
    AddScene<NewGameScene>("NewGameScene");
    AddScene<MultiplayerScene>("MultiplayerScene");
    AddScene<LoadGameScene>("LoadGameScene");
    AddScene<SaveGameScene>("SaveGameScene");
    AddScene<GameMenuScene>("GameMenuScene");
    AddScene<ControlsScene>("ControlsScene");

    MainLoop();

    audio.Cleanup();
    CloseAudioDevice();
    CloseWindow();
}

// Handles the requested event or transfer.
void GameWindow::HandleEvent(std::shared_ptr<Event> e)
{
    Log::Msg(tag, e->msgName, " received!");
    auto ptr = std::dynamic_pointer_cast<QuitGameEvent>(e);
    if (ptr != nullptr)
    {
        isRunning = false;
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

// Initializes GameWindow::MainLoop.
void GameWindow::MainLoop()
{
    SetTargetFPS(150);
    while (isRunning)
    {
        UpdateWindowSize();
        audio.Update(GetFrameTime());
        Update(GetFrameTime());
    }
}

// Advances UpdateWindowSize for one frame or simulation tick.
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
