#include "scenes/Scenes.h"
#include "core/Log.h"
#include "ui/ControlIcons.h"
#include "ui/InputManager.h"

#include "raylib.h"

namespace
{
    float SmoothStep(float value)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - 2.0f * value);
    }
}

// Initializes GameWindow::LaunchGame.
void GameWindow::LaunchGame()
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1920, 1080, "Tvorin");
    int monitor = GetCurrentMonitor();
    SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
    SetWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    SetWindowMinSize(1280, 720);
    // Pilot: use Departure Mono consistently for both shared UI roles. Keeping
    // both roles loaded preserves the existing title/body role switching while
    // making panels, title bars, buttons, tooltips and dense labels identical.
    GuiPanel::LoadUiFont("assets/fonts/DepartureMono-Regular.otf");
    GuiPanel::LoadUiPlainFont("assets/fonts/DepartureMono-Regular.otf");
    UiControlIcons::Load();

    InitAudioDevice();
    audio.Init();

    AudioConfig audioConfig = LoadAudioConfig();
    audio.SetMasterVolume(audioConfig.masterVolume);
    audio.SetMusicVolume(audioConfig.musicVolume);
    audio.SetSfxVolume(audioConfig.sfxVolume);

    // Music themes — add supported audio files to assets/music/ to activate them.
    audio.RegisterMusic("menu",      "assets/music/menu_theme.wav");
    audio.RegisterMusic("gameplay",  "assets/music/game_theme_ambient.wav");
    audio.RegisterMusic("gameplay_ambient_1", "assets/music/game_ambient_1.wav");
    audio.RegisterMusic("gameplay_ambient_2", "assets/music/game_ambient_2.ogg");
    audio.RegisterMusic("battle",    "assets/music/battle_theme.ogg");
    audio.RegisterMusicRotation("gameplay_rotation",
                                {"gameplay", "gameplay_ambient_1", "gameplay_ambient_2"});

    // Both tracks participate in the first menu/game transition. Preloading
    // them while the window is still on its startup black frame avoids a
    // direction-dependent hitch when gameplay is selected from the menu.
    audio.PreloadMusic("menu");
    audio.PreloadMusic("gameplay");
    audio.PreloadMusic("gameplay_ambient_1");
    audio.PreloadMusic("gameplay_ambient_2");

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

    AddScene<StudioSplashScene>("StudioSplashScene");
    AddScene<MainMenuScene>("MainScene");
    AddScene<OptionsScene>("OptionsScene");
    AddScene<GameScene>("GameScene");
    AddScene<TutorialScene>("TutorialScene");
    AddScene<NewGameScene>("NewGameScene");
    AddScene<MultiplayerScene>("MultiplayerScene");
    AddScene<LoadGameScene>("LoadGameScene");
    AddScene<SaveGameScene>("SaveGameScene");
    AddScene<GameMenuScene>("GameMenuScene");
    AddScene<ControlsScene>("ControlsScene");

    // Start underneath an opaque overlay so the first visible frame is a
    // deliberate studio splash rather than a partially initialized menu.
    transitionAlpha = 1.0f;
    transitionPhase = SceneTransitionPhase::FadeIn;
    ActivateScene("StudioSplashScene", "");

    MainLoop();

    ShutdownRenderers();
    UiControlIcons::Unload();
    audio.Cleanup();
    CloseAudioDevice();
    CloseWindow();
}

void GameWindow::ShutdownRenderers()
{
    for (auto& [name, scene] : scenes)
    {
        if (scene != nullptr)
            scene->render.Shutdown();
    }
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

    auto newGame = std::dynamic_pointer_cast<NewGameEvent>(e);
    if (newGame != nullptr)
    {
        RequestSceneChange("GameScene", activeScene != nullptr ? activeScene->name : "", true);
    }

    auto tutorialGame = std::dynamic_pointer_cast<TutorialGameEvent>(e);
    if (tutorialGame != nullptr)
    {
        RequestSceneChange("TutorialScene", activeScene != nullptr ? activeScene->name : "", true);
    }

    auto ptr2 = std::dynamic_pointer_cast<ChangeSceneEvent>(e);
    if (ptr2 != nullptr)
    {
        RequestSceneChange(ptr2->sceneName, ptr2->previousSceneName, false);
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
        const float dt = GetFrameTime();
        UpdateSceneTransition(dt);
        audio.Update(dt);
        Update(dt);
    }
}

void GameWindow::ActivateScene(const std::string& name, const std::string& previousSceneName)
{
    auto sceneIt = scenes.find(name);
    if (sceneIt == scenes.end() || sceneIt->second == nullptr)
        return;

    if (activeScene != nullptr)
        activeScene->OnDeactivated();

    activeScene = sceneIt->second;
    activeScene->previousSceneName = previousSceneName;

    // Central input-gate reset (see IGuiHandler): every activated scene
    // starts with its GUI input gated until it has presented a frame and
    // ESC is released. This remains centralized despite delayed transitions.
    if (auto* guiHandler = dynamic_cast<IGuiHandler*>(activeScene.get()))
        guiHandler->ResetGuiInputGate();
    activeScene->OnActivated();
}

void GameWindow::RequestSceneChange(std::string name, std::string previousSceneName,
                                    bool stopMusicBeforeGameplayStart)
{
    if (scenes.find(name) == scenes.end())
        return;

    // Ignore duplicate requests while the current transition is already in
    // flight. This prevents a held key or a second event from replacing the
    // destination half-way through a fade.
    if (transitionPhase != SceneTransitionPhase::Idle)
        return;

    pendingSceneName = std::move(name);
    pendingPreviousSceneName = std::move(previousSceneName);
    transitionPhase = SceneTransitionPhase::FadeOut;
    transitionElapsed = 0.0f;
    transitionHoldRemaining = HoldSeconds;
    InputManager::SetInputEnabled(false);

    // For a newly generated world, silence the menu before entering the
    // opaque/loading part of the transition. The gameplay track starts only
    // after the session has actually been created.
    if (stopMusicBeforeGameplayStart)
        audio.StopMusic(0.22f);
}

void GameWindow::UpdateSceneTransition(float dt)
{
    const float safeDt = std::clamp(dt, 0.0f, 0.1f);

    switch (transitionPhase)
    {
        case SceneTransitionPhase::Idle:
            transitionAlpha = 0.0f;
            break;

        case SceneTransitionPhase::FadeOut:
            transitionElapsed += safeDt;
            transitionAlpha = SmoothStep(transitionElapsed / FadeOutSeconds);
            if (transitionAlpha >= 1.0f)
            {
                transitionPhase = SceneTransitionPhase::Hold;
                transitionElapsed = 0.0f;
                ActivateScene(pendingSceneName, pendingPreviousSceneName);
                // OnActivated() may restore input for gameplay scenes; keep
                // it blocked until the new scene is fully visible.
                InputManager::SetInputEnabled(false);
            }
            break;

        case SceneTransitionPhase::Hold:
            transitionHoldRemaining = std::max(0.0f, transitionHoldRemaining - safeDt);
            if (transitionHoldRemaining <= 0.0f)
            {
                transitionPhase = SceneTransitionPhase::FadeIn;
                transitionElapsed = 0.0f;
            }
            break;

        case SceneTransitionPhase::FadeIn:
            transitionElapsed += safeDt;
            transitionAlpha = 1.0f - SmoothStep(transitionElapsed / FadeInSeconds);
            if (transitionAlpha <= 0.0f)
            {
                transitionPhase = SceneTransitionPhase::Idle;
                pendingSceneName.clear();
                pendingPreviousSceneName.clear();
                InputManager::SetInputEnabled(true);
            }
            break;
    }

    SetSceneTransitionOverlayAlpha(transitionAlpha);
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
