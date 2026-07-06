#include "scenes/Scenes.h"

// Initializes GameMenuScene::GameMenuScene.
GameMenuScene::GameMenuScene()
{
    vbox.ChangeSizeAnchor(Vec2f{0.3f, 0.3f});
    vbox.ChangePositionAnchor(Vec2f{0.1f, 0.1f});

    auto saveButton = std::make_shared<UiButton>();
    saveButton->ChangeText("Save Game");
    saveButton->func = std::bind(&GameMenuScene::OnSaveGamePressed, this);
    vbox.AddChild(saveButton);

    auto loadGameButton = std::make_shared<UiButton>();
    loadGameButton->ChangeText("Load Game");
    loadGameButton->func = std::bind(&GameMenuScene::OnLoadGamePressed, this);
    vbox.AddChild(loadGameButton);

    auto optionsButton = std::make_shared<UiButton>();
    optionsButton->ChangeText("Options");
    optionsButton->func = std::bind(&GameMenuScene::OnOptionsPressed, this);
    vbox.AddChild(optionsButton);

    auto mainMenuButton = std::make_shared<UiButton>();
    mainMenuButton->ChangeText("Main Menu");
    mainMenuButton->func = std::bind(&GameMenuScene::OnMainMenuPressed, this);
    vbox.AddChild(mainMenuButton);

    auto quitButton = std::make_shared<UiButton>();
    quitButton->ChangeText("Quit Game");
    quitButton->func = std::bind(&GameMenuScene::OnQuitPressed, this);
    vbox.AddChild(quitButton);

    auto returnButton = std::make_shared<UiButton>();
    returnButton->ChangeText("Return");
    returnButton->func = std::bind(&GameMenuScene::OnBackPressed, this);
    vbox.AddChild(returnButton);

    vbox.UpdateSize({GetScreenWidth(), GetScreenHeight()});
}

// Handles the UI action represented by OnBackPressed.
void GameMenuScene::OnBackPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "GameScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnOptionsPressed.
void GameMenuScene::OnOptionsPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "OptionsScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnMainMenuPressed.
void GameMenuScene::OnMainMenuPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "MainScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnSaveGamePressed.
void GameMenuScene::OnSaveGamePressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "SaveGameScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnLoadGamePressed.
void GameMenuScene::OnLoadGamePressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "LoadGameScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnQuitPressed.
void GameMenuScene::OnQuitPressed()
{
    auto msg = std::make_shared<QuitGameEvent>();
    msg->sender = this;
    broker->Broadcast(msg);
}

// Advances this object's state for one frame.
void GameMenuScene::Update(double dt)
{
    // ESC returns to the game — but only once the key that opened the menu has
    // been released, so a single press can't open and immediately close it.
    if (!escArmed)
    {
        if (!InputManager::IsKeyDown(KEY_ESCAPE))
            escArmed = true;
    }
    else if (InputManager::IsKeyPressed(KEY_ESCAPE))
    {
        OnBackPressed();
        return;
    }
    render.Draw({&vbox}, dt);
}

// Handles the requested event or transfer.
void GameMenuScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        vbox.UpdateSize(ptr->windowSize);
    }
}
