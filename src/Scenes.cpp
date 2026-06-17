#include "../inc/Scenes.h"
#include "../inc/GuiController.h"

// ======= INPUT ============

bool InputProcessor::IsActionPressed(int action)
{
    bool result = false;

    if (action > ACTION_NULL && action < MAX_ACTION)
    {
        auto input = actionInputs[action];
        result = ((input.key >= 0) && IsKeyPressed(input.key)) ||
                 ((input.button >= 0) && IsMouseButtonPressed(input.button));
    }

    return result;
}

bool InputProcessor::IsActionReleased(int action)
{
    bool result = false;

    if (action > ACTION_NULL && action < MAX_ACTION)
    {
        auto input = actionInputs[action];
        result = ((input.key >= 0) && IsKeyReleased(input.key)) ||
                 ((input.button >= 0) && IsMouseButtonReleased(input.button));
    }

    return result;
}

bool InputProcessor::IsActionDown(int action)
{
    bool result = false;

    if (action > ACTION_NULL && action < MAX_ACTION)
    {
        auto input = actionInputs[action];
        result = ((input.key >= 0) && IsKeyDown(input.key)) ||
                 ((input.button >= 0) && IsMouseButtonDown(input.button));
    }

    return result;
}

// ===============================================

MainMenuScene::MainMenuScene()
{
    buttonsColumn.ChangeSizeAnchor(Vec2f{0.4f, 0.3f});
    buttonsColumn.ChangePositionAnchor(Vec2f{0.3f, 0.4f});

    menuGraphic.ChangeSizeAnchor(Vec2f{1.0f, 1.0f});
    menuGraphic.ChangePositionAnchor(Vec2f{0.0f, 0.0f});
    menuGraphic.cover = true;
    menuGraphic.LoadTextureFromFile("assets/ui/menu/main_menu.png");

    auto newGameButton = std::make_shared<UiButton>();
    newGameButton->ChangeText("New Game");
    newGameButton->SetDrawText(false);
    newGameButton->LoadTextures("assets/ui/menu/menu_newgame_normal.png", "assets/ui/menu/menu_newgame_hover.png");
    newGameButton->func = std::bind(&MainMenuScene::OnNewGamePressed, this);
    buttonsColumn.AddChild(newGameButton);

    auto loadGameButton = std::make_shared<UiButton>();
    loadGameButton->ChangeText("Load Game");
    loadGameButton->SetDrawText(false);
    loadGameButton->LoadTextures("assets/ui/menu/menu_loadgame_normal.png", "assets/ui/menu/menu_loadgame_hover.png");
    loadGameButton->func = std::bind(&MainMenuScene::OnLoadGamePressed, this);
    buttonsColumn.AddChild(loadGameButton);

    auto optionsButton = std::make_shared<UiButton>();
    optionsButton->ChangeText("Options");
    optionsButton->SetDrawText(false);
    optionsButton->LoadTextures("assets/ui/menu/menu_options_normal.png", "assets/ui/menu/menu_options_hover.png");
    optionsButton->func = std::bind(&MainMenuScene::OnOptionsPressed, this);
    buttonsColumn.AddChild(optionsButton);

    auto quitButton = std::make_shared<UiButton>();
    quitButton->ChangeText("Quit");
    quitButton->SetDrawText(false);
    quitButton->LoadTextures("assets/ui/menu/menu_quit_normal.png", "assets/ui/menu/menu_quit_hover.png");
    quitButton->func = std::bind(&MainMenuScene::OnQuitPressed, this);
    buttonsColumn.AddChild(quitButton);
}

void MainMenuScene::Update(double dt)
{
    render.Draw({&menuGraphic, &buttonsColumn}, dt);
}

void MainMenuScene::OnNewGamePressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "NewGameScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void MainMenuScene::OnLoadGamePressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "LoadGameScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void MainMenuScene::OnOptionsPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "OptionsScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void MainMenuScene::OnQuitPressed()
{
    auto msg = std::make_shared<QuitGameEvent>();
    msg->sender = this;
    broker->Broadcast(msg);
}

void MainMenuScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        buttonsColumn.UpdateSize(ptr->windowSize);
        menuGraphic.UpdateSize(ptr->windowSize);
    }
}

// ============ OPTIONS ================

OptionsScene::OptionsScene()
{
    backButton.ChangeText("Back");
    backButton.ChangePositionAnchor(Vec2f{0.5f, 0.7f});
    backButton.func = std::bind(&OptionsScene::OnBackPressed, this);

    fullScreenCheckBox.ChangeText("Fullscreen");
    fullScreenCheckBox.ChangePositionAnchor(Vec2f{0.4f, 0.4f});

    masterVolume.ChangeText("Music Volume");
    masterVolume.ChangePositionAnchor(Vec2f{0.4f, 0.5f});
}

void OptionsScene::Update(double dt)
{
    backButton.Update(dt);
    fullScreenCheckBox.Update(dt);
    masterVolume.Update(dt);

    if (fullScreenCheckBox.HasChanged())
    {
        // send toggle fullscreen event
        auto msg = std::make_shared<ToggleFullscreenEvent>();
        msg->sender = this;
        broker->Broadcast(msg);

        if (fullScreenCheckBox.IsActive())
        {
            std::cout << "Fullscreen activated!\n";
        }
        else
            std::cout << "Fullscreen disabled!\n";
    }

    if (masterVolume.HasChanged())
    {
        std::cout << "Master volume: " << masterVolume.GetValue() << std::endl;
    }

    render.Draw();
}

void OptionsScene::OnBackPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = previousSceneName;
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void OptionsScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        backButton.UpdateSize(ptr->windowSize);
        fullScreenCheckBox.UpdateSize(ptr->windowSize);
        masterVolume.UpdateSize(ptr->windowSize);
    }
}

// =========== NEW GAME ================

NewGameScene::NewGameScene()
{
    backButton.ChangeText("Back");
    backButton.ChangePositionAnchor(Vec2f{0.7f, 0.7f});
    backButton.func = std::bind(&NewGameScene::OnBackPressed, this);

    gameName.ChangePositionAnchor(Vec2f{0.5f, 0.4f});
    gameName.ChangeSizeAnchor(Vec2f{0.4f, 0.1f});
    // gameName.textOutput = "";

    startGame.ChangeText("Start game");
    startGame.ChangePositionAnchor(Vec2f{0.3f, 0.7f});
    startGame.func = std::bind(&NewGameScene::OnStartPressed, this);
}

void NewGameScene::Update(double dt)
{
    backButton.Update(dt);
    startGame.Update(dt);
    gameName.Update(dt);

    render.Draw();
}

void NewGameScene::OnBackPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = previousSceneName;
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void NewGameScene::OnStartPressed()
{
    auto msg = std::make_shared<NewGameEvent>();
    msg->sender = this;
    msg->name = gameName.GetText();

    MapParameters params;
    params.sizeX = 100;
    params.sizeY = 100;

    msg->params = params;
    broker->Broadcast(msg);
}

void NewGameScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        backButton.UpdateSize(ptr->windowSize);
        gameName.UpdateSize(ptr->windowSize);
        startGame.UpdateSize(ptr->windowSize);
    }
}

// ============== LOAD GAME ==============

LoadGameScene::LoadGameScene()
{
    backButton.ChangeText("Back");
    backButton.ChangePositionAnchor(Vec2f{0.7f, 0.7f});
    backButton.func = std::bind(&LoadGameScene::OnBackPressed, this);

    saveButtons.ChangeSizeAnchor(Vec2f{0.3f, 0.3f});
    saveButtons.ChangePositionAnchor(Vec2f{0.1f, 0.1f});

    LoadSaves();
}

void LoadGameScene::Update(double dt)
{
    backButton.Update(dt);
    saveButtons.Update(dt);

    render.Draw();
}

void LoadGameScene::OnBackPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = previousSceneName;
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void LoadGameScene::OnSavePressed(std::string save)
{
    auto msg = std::make_shared<LoadGameEvent>();
    msg->sender = this;
    msg->name = save;
    broker->Broadcast(msg);
}

void LoadGameScene::LoadSaves()
{
    namespace fs = std::filesystem;
    fs::path root = "./saves";

    if (!fs::exists(root))
    {
        fs::create_directories(root);
        return;
    }

    for (const auto &entry : fs::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
            continue;

        if (entry.path().extension() == ".save")
        {
            std::cout << "Znaleziono: " << entry.path() << '\n';

            std::ifstream file(entry.path());

            if (!file)
            {
                std::cerr << "Nie mozna otworzyc pliku\n";
                continue;
            }

            std::string data(
                (std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());

            auto button = std::make_shared<UiButton>();
            button->ChangeText(data);
            button->func = [this, data]()
            {
                OnSavePressed(data);
            };

            saveButtons.AddChild(button);
        }
    }
}

void LoadGameScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        backButton.UpdateSize(ptr->windowSize);
        saveButtons.UpdateSize(ptr->windowSize);
    }
}

// ============== GAME SCENE ==============

GameScene::GameScene()
{
    render.atlasMap[0] = TextureAtlas{};
    render.atlasMap[0].LoadTextureAtlas("assets/textures/atlas_terrain.png");

    render.atlasMap[1] = TextureAtlas{};
    render.atlasMap[1].LoadTextureAtlas("assets/textures/atlas_building.png", {64, 64});
    // render.atlasMap[1].LoadTextureAtlas("assets/textures/atlas1.png");

    GuiPanel::LoadResourceAtlas("assets/ui/resource_icons.png", {32, 32});

    controller = std::make_unique<GuiController>();
    controller->Init(this);
    controller->AddSystem<BasicMapViewSystem>("default");
    controller->AddSystem<BuildGuiSystem>("build");
    controller->AddSystem<RoadBuildSystem>("road_build");
    controller->ChangeSystem("default");
    
    inputs.Init(controller.get());
}

void GameScene::Update(double dt)
{
    render.ClearLayers();

    inputs.HandleInputs();
    controller->Update(dt);
    game->Update(dt);

    render.Draw(controller->GetUiWidgets());
}

void GameScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        for(auto& [name, system] : controller->systems)
        {
            system->UpdateUiWidgets(ptr->windowSize);
        }
    }

    auto ptr2 = std::dynamic_pointer_cast<NewGameEvent>(e);
    if (ptr2 != nullptr)
    {
        StartNewGame(ptr2->name, ptr2->params);

        auto msg = std::make_shared<ChangeSceneEvent>();
        msg->sender = this;
        msg->sceneName = "GameScene";
        msg->previousSceneName = name;
        broker->Broadcast(msg);

        SaveGame();
    }

    auto ptr3 = std::dynamic_pointer_cast<LoadGameEvent>(e);
    if (ptr3 != nullptr)
    {
        LoadGame(ptr3->name);

        auto msg = std::make_shared<ChangeSceneEvent>();
        msg->sender = this;
        msg->sceneName = "GameScene";
        msg->previousSceneName = name;
        broker->Broadcast(msg);
    }
}

void GameScene::StartNewGame(std::string name, MapParameters params)
{
    game = std::make_unique<GameWorld>();
    // todo: add params parameter to InitWorld
    game->InitWorld(name, &render);
}

void GameScene::LoadGame(std::string name)
{
    game = std::make_unique<GameWorld>();
    game->InitWorld(name, &render);

    Log::Msg("GameScene", "Save ", name, " loaded!");
    // load game state
}

void GameScene::SaveGame()
{
    // save game state (temporarily, only world name)
    // todo: prepare a serializable struct with game state data

    std::string filename{"saves/" + game->worldName + ".save"};
    std::filesystem::create_directories("saves");
    std::fstream saveFile{filename, saveFile.trunc | saveFile.out};

    if (!saveFile.is_open())
        std::cout << "failed to open " << filename << '\n';
    else
    {
        saveFile << game->worldName;
        saveFile.close();
    }
}

// ================ ESCAPE MENU =================

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

    auto returnButton = std::make_shared<UiButton>();
    returnButton->ChangeText("Return");
    returnButton->func = std::bind(&GameMenuScene::OnBackPressed, this);
    vbox.AddChild(returnButton);
}

void GameMenuScene::OnBackPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "GameScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void GameMenuScene::OnOptionsPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "OptionsScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void GameMenuScene::OnMainMenuPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "MainScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void GameMenuScene::OnSaveGamePressed()
{
    Log::Msg("[Game Menu]", "Saveing");
}

void GameMenuScene::OnLoadGamePressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "LoadGameScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void GameMenuScene::Update(double dt)
{
    vbox.Update(dt);
    render.Draw();
}

void GameMenuScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        vbox.UpdateSize(ptr->windowSize);
    }
}
