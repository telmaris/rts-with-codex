#include "scenes/Scenes.h"
#include "scenes/SceneUtils.h"

// Loads the requested data into runtime state.
LoadGameScene::LoadGameScene()
{
    menuBackground.ChangePositionAnchor({0.0f, 0.0f});
    menuBackground.ChangeSizeAnchor({1.0f, 1.0f});
    menuBackground.SetScrollLayer(0);
    menuBackground.LoadFromDirectory("assets/ui/menu/loadgame");

    backButton.ChangeText("Back");
    backButton.ChangePositionAnchor(Vec2f{0.7f, 0.7f});
    backButton.func = std::bind(&LoadGameScene::OnBackPressed, this);

    saveButtons.ChangeSizeAnchor(Vec2f{0.3f, 0.3f});
    saveButtons.ChangePositionAnchor(Vec2f{0.1f, 0.1f});

    LoadSaves();
    backButton.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    saveButtons.UpdateSize({GetScreenWidth(), GetScreenHeight()});
}

// Advances this object's state for one frame.
void LoadGameScene::Update(double dt)
{
    ProcessGuiInput(dt);
    render.Draw({&menuBackground, &backButton, &saveButtons}, dt);
}

// Handles the UI action represented by OnBackPressed.
void LoadGameScene::OnBackPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = previousSceneName;
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnSavePressed.
void LoadGameScene::OnSavePressed(std::string save)
{
    auto msg = std::make_shared<LoadGameEvent>();
    msg->sender = this;
    msg->name = save;
    broker->Broadcast(msg);
}

// Loads the requested data into runtime state.
void LoadGameScene::LoadSaves()
{
    // Initializes PopulateSaveButtons.
    PopulateSaveButtons(saveButtons, [this](std::string saveName)
    {
        // Handles the UI action represented by OnSavePressed.
        OnSavePressed(saveName);
    });
}

// Handles the requested event or transfer.
void LoadGameScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        menuBackground.UpdateSize(ptr->windowSize);
        backButton.UpdateSize(ptr->windowSize);
        saveButtons.UpdateSize(ptr->windowSize);
    }

    auto sceneChange = std::dynamic_pointer_cast<ChangeSceneEvent>(e);
    if (sceneChange != nullptr && sceneChange->sceneName == name)
        LoadSaves();

    auto saveListChanged = std::dynamic_pointer_cast<SaveListChangedEvent>(e);
    if (saveListChanged != nullptr)
        LoadSaves();
}
