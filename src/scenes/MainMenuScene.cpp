#include "scenes/Scenes.h"

// Initializes MainMenuScene::MainMenuScene.
MainMenuScene::MainMenuScene()
{
    buttonsColumn.ChangeSizeAnchor(Vec2f{0.4f, 0.3f});
    buttonsColumn.ChangePositionAnchor(Vec2f{0.3f, 0.4f});

    menuGraphic.ChangeSizeAnchor(Vec2f{1.0f, 1.0f});
    menuGraphic.ChangePositionAnchor(Vec2f{0.0f, 0.0f});
    menuGraphic.cover = true;
    menuGraphic.LoadTextureFromFile("assets/ui/menu/main_menu.png");

    statusLabel.ChangePositionAnchor(Vec2f{0.26f, 0.82f});
    statusLabel.ChangeSizeAnchor(Vec2f{0.48f, 0.05f});
    statusLabel.fontSize = 22;
    statusLabel.color = Color{238, 184, 84, 255};

    auto newGameButton = std::make_shared<UiButton>();
    newGameButton->ChangeText("New Game");
    newGameButton->func = std::bind(&MainMenuScene::OnNewGamePressed, this);
    buttonsColumn.AddChild(newGameButton);

    auto loadGameButton = std::make_shared<UiButton>();
    loadGameButton->ChangeText("Load Game");
    loadGameButton->func = std::bind(&MainMenuScene::OnLoadGamePressed, this);
    buttonsColumn.AddChild(loadGameButton);

    auto multiplayerButton = std::make_shared<UiButton>();
    multiplayerButton->ChangeText("Multiplayer");
    multiplayerButton->func = std::bind(&MainMenuScene::OnMultiplayerPressed, this);
    buttonsColumn.AddChild(multiplayerButton);

    auto optionsButton = std::make_shared<UiButton>();
    optionsButton->ChangeText("Options");
    optionsButton->func = std::bind(&MainMenuScene::OnOptionsPressed, this);
    buttonsColumn.AddChild(optionsButton);

    auto controlsButton = std::make_shared<UiButton>();
    controlsButton->ChangeText("Controls");
    controlsButton->func = std::bind(&MainMenuScene::OnControlsPressed, this);
    buttonsColumn.AddChild(controlsButton);

    auto quitButton = std::make_shared<UiButton>();
    quitButton->ChangeText("Quit");
    quitButton->func = std::bind(&MainMenuScene::OnQuitPressed, this);
    buttonsColumn.AddChild(quitButton);
}

// Starts the menu music theme.
void MainMenuScene::OnActivated()
{
    if (audioSystem != nullptr)
        audioSystem->PlayMusic("menu");
}

// Advances this object's state for one frame.
void MainMenuScene::Update(double dt)
{
    if (statusTimer > 0.0)
        statusTimer = std::max(0.0, statusTimer - dt);

    std::vector<UiWidget*> widgets{&menuGraphic, &buttonsColumn};
    if (statusTimer > 0.0)
        widgets.push_back(&statusLabel);
    render.Draw(widgets, dt);
}

// Handles the UI action represented by OnNewGamePressed.
void MainMenuScene::OnNewGamePressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "NewGameScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnLoadGamePressed.
void MainMenuScene::OnLoadGamePressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "LoadGameScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnMultiplayerPressed.
void MainMenuScene::OnMultiplayerPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "MultiplayerScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnOptionsPressed.
void MainMenuScene::OnOptionsPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "OptionsScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnControlsPressed.
void MainMenuScene::OnControlsPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "ControlsScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnQuitPressed.
void MainMenuScene::OnQuitPressed()
{
    auto msg = std::make_shared<QuitGameEvent>();
    msg->sender = this;
    broker->Broadcast(msg);
}

// Handles the requested event or transfer.
void MainMenuScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        buttonsColumn.UpdateSize(ptr->windowSize);
        menuGraphic.UpdateSize(ptr->windowSize);
        statusLabel.UpdateSize(ptr->windowSize);
    }

    auto networkStatus = std::dynamic_pointer_cast<NetworkStatusEvent>(e);
    if (networkStatus != nullptr)
    {
        statusLabel.ChangeText(networkStatus->message);
        statusTimer = 8.0;
    }
}
