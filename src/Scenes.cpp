#include "../inc/Scenes.h"
#include "../inc/BuildingConfig.h"
#include "../inc/GuiController.h"
#include "../inc/TcpGameTransport.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{
    struct MultiplayerConfig
    {
        std::string nickname{"Player"};
        std::string sessionName{"lan_test"};
        std::string hostIp{"127.0.0.1"};
        unsigned short port{27015};
        int aiOpponents{0};
        MapSizePreset sizePreset{MapSizePreset::S};
        int difficulty{0};
        float resourceDensity{0.65f};
        float resourceFieldSize{0.45f};
        float resourceRichness{0.5f};
        bool debugMode{false};
    };

    // Converts user-provided save names into filesystem-safe filenames.
    std::string SanitizeSaveName(std::string name)
    {
        if (name.empty() || name == "Default textbox text")
            return "default";

        for (auto& c : name)
        {
            bool valid = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
            if (!valid)
                c = '_';
        }
        return name;
    }

    // Reads world display name from a save file header.
    std::string ReadSaveDisplayName(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
            return path.stem().string();

        std::string tag;
        int version = 0;
        file >> tag >> version;
        if (tag == "RTS_SAVE")
        {
            std::string worldTag;
            std::string worldName;
            file >> worldTag >> std::quoted(worldName);
            if (worldTag == "WORLD" && !worldName.empty())
                return worldName;
        }

        return path.stem().string();
    }

    // Returns true when a save file with this sanitized name exists.
    bool SaveExists(const std::string& saveName)
    {
        return std::filesystem::exists(std::filesystem::path("saves") / (SanitizeSaveName(saveName) + ".save"));
    }

    // Initializes MapSizeName.
    std::string MapSizeName(MapSizePreset preset)
    {
        switch (preset)
        {
            case MapSizePreset::S: return "S 301x301";
            case MapSizePreset::M: return "M 501x501";
            case MapSizePreset::L: return "L 701x701";
            case MapSizePreset::XL: return "XL 1001x1001";
            default: return "S 301x301";
        }
    }

    // Initializes DifficultyName.
    std::string DifficultyName(int difficulty)
    {
        switch (difficulty)
        {
            case 1: return "Easy";
            case 2: return "Normal";
            case 3: return "Hard";
            default: return "Primitive";
        }
    }

    // Initializes SliderToInt.
    int SliderToInt(float value, int minValue, int maxValue)
    {
        return minValue + static_cast<int>(std::round(std::clamp(value, 0.0f, 1.0f) * (maxValue - minValue)));
    }

    unsigned short ParsePort(const std::string& text)
    {
        try
        {
            int parsed = std::stoi(text);
            return static_cast<unsigned short>(std::clamp(parsed, 1, 65535));
        }
        catch (...)
        {
            return 27015;
        }
    }

    int ParseIntOrDefault(const std::string& text, int fallback)
    {
        try
        {
            return std::stoi(text);
        }
        catch (...)
        {
            return fallback;
        }
    }

    float ParseFloatOrDefault(const std::string& text, float fallback)
    {
        try
        {
            return std::stof(text);
        }
        catch (...)
        {
            return fallback;
        }
    }

    bool ParseBoolOrDefault(const std::string& text, bool fallback)
    {
        if (text == "1" || text == "true")
            return true;
        if (text == "0" || text == "false")
            return false;
        return fallback;
    }

    MultiplayerConfig LoadMultiplayerConfig()
    {
        MultiplayerConfig config;
        std::ifstream file("config/multiplayer.cfg");
        if (!file.is_open())
            return config;

        std::string line;
        while (std::getline(file, line))
        {
            auto split = line.find('=');
            if (split == std::string::npos)
                continue;

            std::string key = line.substr(0, split);
            std::string value = line.substr(split + 1);
            if (key == "nickname" && !value.empty())
                config.nickname = value;
            else if (key == "session" && !value.empty())
                config.sessionName = value;
            else if (key == "host_ip" && !value.empty())
                config.hostIp = value;
            else if (key == "port")
                config.port = ParsePort(value);
            else if (key == "ai_opponents")
                config.aiOpponents = std::clamp(ParseIntOrDefault(value, 0), 0, 5);
            else if (key == "size")
                config.sizePreset = static_cast<MapSizePreset>(std::clamp(ParseIntOrDefault(value, 0), 0, 3));
            else if (key == "difficulty")
                config.difficulty = std::clamp(ParseIntOrDefault(value, 0), 0, 3);
            else if (key == "resource_density")
                config.resourceDensity = std::clamp(ParseFloatOrDefault(value, config.resourceDensity), 0.0f, 1.0f);
            else if (key == "resource_field_size")
                config.resourceFieldSize = std::clamp(ParseFloatOrDefault(value, config.resourceFieldSize), 0.0f, 1.0f);
            else if (key == "resource_richness")
                config.resourceRichness = std::clamp(ParseFloatOrDefault(value, config.resourceRichness), 0.0f, 1.0f);
            else if (key == "debug_mode")
                config.debugMode = ParseBoolOrDefault(value, false);
        }
        return config;
    }

    void SaveMultiplayerConfig(const MultiplayerConfig& config)
    {
        std::error_code error;
        std::filesystem::create_directories("config", error);
        std::ofstream file("config/multiplayer.cfg", std::ios::trunc);
        if (!file.is_open())
            return;

        file << "nickname=" << config.nickname << '\n';
        file << "session=" << config.sessionName << '\n';
        file << "host_ip=" << config.hostIp << '\n';
        file << "port=" << config.port << '\n';
        file << "ai_opponents=" << config.aiOpponents << '\n';
        file << "size=" << static_cast<int>(config.sizePreset) << '\n';
        file << "difficulty=" << config.difficulty << '\n';
        file << "resource_density=" << config.resourceDensity << '\n';
        file << "resource_field_size=" << config.resourceFieldSize << '\n';
        file << "resource_richness=" << config.resourceRichness << '\n';
        file << "debug_mode=" << (config.debugMode ? 1 : 0) << '\n';
    }

    MapParameters MakeDefaultMultiplayerParams(int aiOpponentCount = 0, MapSizePreset sizePreset = MapSizePreset::S, int difficulty = 0,
        float resourceDensity = 0.65f, float resourceFieldSize = 0.45f, float resourceRichnessSlider = 0.5f, bool debugMode = false)
    {
        MapParameters params;
        params.sizePreset = sizePreset;
        params.sizeX = MapGenerator::SizeFromPreset(params.sizePreset);
        params.sizeY = params.sizeX;
        params.seed = 27015;
        params.resourceDensity = resourceDensity;
        params.resourceFieldSize = resourceFieldSize;
        params.resourceRichness = SliderToInt(resourceRichnessSlider, 40, 160);
        params.aiOpponentCount = std::clamp(aiOpponentCount, 0, 5);
        params.aiDifficulty = std::clamp(difficulty, 0, 3);
        params.debugMode = debugMode;
        return params;
    }

    std::string SerializeMultiplayerStart(const std::string& sessionName, const MapParameters& params)
    {
        std::ostringstream out;
        out << "START " << std::quoted(sessionName) << ' '
            << params.aiOpponentCount << ' '
            << static_cast<int>(params.sizePreset) << ' '
            << params.aiDifficulty << ' '
            << params.resourceDensity << ' '
            << params.resourceFieldSize << ' '
            << params.resourceRichness << ' '
            << (params.debugMode ? 1 : 0);
        return out.str();
    }

    bool TryDeserializeMultiplayerStart(const std::string& payload, std::string& sessionName, MapParameters& params)
    {
        std::istringstream in(payload);
        int aiCount = 0;
        int size = 0;
        int difficulty = 0;
        int richness = 120;
        int debug = 0;
        float density = 0.65f;
        float fieldSize = 0.45f;
        if (!(in >> std::quoted(sessionName) >> aiCount >> size >> difficulty >> density >> fieldSize >> richness >> debug))
            return false;

        params = MakeDefaultMultiplayerParams(std::clamp(aiCount, 0, 5),
            static_cast<MapSizePreset>(std::clamp(size, 0, 3)),
            std::clamp(difficulty, 0, 3),
            std::clamp(density, 0.0f, 1.0f),
            std::clamp(fieldSize, 0.0f, 1.0f),
            std::clamp((richness - 40) / 120.0f, 0.0f, 1.0f),
            debug != 0);
        return true;
    }

    std::string SerializeMultiplayerLobbyState(const std::string& sessionName, const std::string& hostName, const std::string& remoteName, const MapParameters& params)
    {
        std::ostringstream out;
        out << "STATE " << std::quoted(sessionName) << ' '
            << std::quoted(hostName) << ' '
            << std::quoted(remoteName) << ' '
            << params.aiOpponentCount << ' '
            << static_cast<int>(params.sizePreset) << ' '
            << params.aiDifficulty << ' '
            << params.resourceDensity << ' '
            << params.resourceFieldSize << ' '
            << params.resourceRichness << ' '
            << (params.debugMode ? 1 : 0);
        return out.str();
    }

    bool TryDeserializeMultiplayerLobbyState(const std::string& payload, std::string& sessionName, std::string& hostName, std::string& remoteName, MapParameters& params)
    {
        std::istringstream in(payload);
        int aiCount = 0;
        int size = 0;
        int difficulty = 0;
        int richness = 120;
        int debug = 0;
        float density = 0.65f;
        float fieldSize = 0.45f;
        if (!(in >> std::quoted(sessionName) >> std::quoted(hostName) >> std::quoted(remoteName) >> aiCount >> size >> difficulty >> density >> fieldSize >> richness >> debug))
            return false;

        params = MakeDefaultMultiplayerParams(std::clamp(aiCount, 0, 5),
            static_cast<MapSizePreset>(std::clamp(size, 0, 3)),
            std::clamp(difficulty, 0, 3),
            std::clamp(density, 0.0f, 1.0f),
            std::clamp(fieldSize, 0.0f, 1.0f),
            std::clamp((richness - 40) / 120.0f, 0.0f, 1.0f),
            debug != 0);
        return true;
    }

    Color LocalPlayerChatColor()
    {
        return Color{66, 154, 255, 255};
    }

    Color RemotePlayerChatColor()
    {
        return Color{220, 72, 72, 255};
    }

    Color AiPlayerColor(int index)
    {
        static const std::array<Color, 5> colors{
            Color{220, 72, 72, 255},
            Color{230, 151, 62, 255},
            Color{176, 86, 216, 255},
            Color{73, 181, 126, 255},
            Color{217, 210, 82, 255}
        };
        return colors[static_cast<size_t>(std::clamp(index, 0, 4))];
    }

    // Rebuilds save-list buttons from files in the saves directory.
    void PopulateSaveButtons(VBox& saveButtons, const std::function<void(std::string)>& onSavePressed)
    {
        namespace fs = std::filesystem;
        fs::path root = "./saves";
        saveButtons.ClearChildren();

        if (!fs::exists(root))
        {
            fs::create_directories(root);
            return;
        }

        for (const auto &entry : fs::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".save")
                continue;

            std::string saveName = ReadSaveDisplayName(entry.path());

            auto button = std::make_shared<UiButton>();
            button->ChangeText(saveName);
            button->func = [onSavePressed, saveName]()
            {
                // Handles the UI action represented by onSavePressed.
                onSavePressed(saveName);
            };

            saveButtons.AddChild(button);
        }
    }
}

// Returns whether this condition is currently true.
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

// Returns whether this condition is currently true.
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

// Returns whether this condition is currently true.
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

    auto quitButton = std::make_shared<UiButton>();
    quitButton->ChangeText("Quit");
    quitButton->func = std::bind(&MainMenuScene::OnQuitPressed, this);
    buttonsColumn.AddChild(quitButton);
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

// Handles the UI action represented by OnOptionsPressed.
void MainMenuScene::OnOptionsPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "OptionsScene";
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

// Initializes OptionsScene::OptionsScene.
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

// Advances this object's state for one frame.
void OptionsScene::Update(double dt)
{
    render.Draw({&backButton, &fullScreenCheckBox, &masterVolume}, dt);

    if (fullScreenCheckBox.HasChanged())
    {
        auto msg = std::make_shared<ToggleFullscreenEvent>();
        msg->sender = this;
        broker->Broadcast(msg);
    }

    if (masterVolume.HasChanged())
    {
        Log::Msg("[OptionsScene]", "Master volume: ", masterVolume.GetValue());
    }
}

// Handles the UI action represented by OnBackPressed.
void OptionsScene::OnBackPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = previousSceneName;
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the requested event or transfer.
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

// Initializes NewGameScene::NewGameScene.
NewGameScene::NewGameScene()
{
    backButton.ChangeText("Back");
    backButton.ChangePositionAnchor(Vec2f{0.62f, 0.84f});
    backButton.func = std::bind(&NewGameScene::OnBackPressed, this);

    gameName.ChangePositionAnchor(Vec2f{0.30f, 0.12f});
    gameName.ChangeSizeAnchor(Vec2f{0.40f, 0.07f});

    sizeButton.ChangePositionAnchor(Vec2f{0.30f, 0.22f});
    sizeButton.ChangeSizeAnchor(Vec2f{0.40f, 0.055f});
    sizeButton.func = std::bind(&NewGameScene::OnSizePressed, this);

    difficultyButton.ChangePositionAnchor(Vec2f{0.30f, 0.29f});
    difficultyButton.ChangeSizeAnchor(Vec2f{0.40f, 0.055f});
    difficultyButton.func = std::bind(&NewGameScene::OnDifficultyPressed, this);

    resourceDensity.ChangePositionAnchor(Vec2f{0.30f, 0.38f});
    resourceDensity.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    resourceDensity.currentValue = 0.5f;
    resourceFieldSize.ChangePositionAnchor(Vec2f{0.30f, 0.47f});
    resourceFieldSize.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    resourceFieldSize.currentValue = 0.5f;
    resourceRichness.ChangePositionAnchor(Vec2f{0.30f, 0.56f});
    resourceRichness.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    resourceRichness.currentValue = 0.5f;
    aiOpponents.ChangePositionAnchor(Vec2f{0.30f, 0.65f});
    aiOpponents.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    aiOpponents.currentValue = 0.2f;

    debugMode.ChangeText("Debug mode");
    debugMode.ChangePositionAnchor(Vec2f{0.30f, 0.73f});
    debugMode.ChangeSizeAnchor(Vec2f{0.20f, 0.045f});

    startGame.ChangeText("Start game");
    startGame.ChangePositionAnchor(Vec2f{0.30f, 0.84f});
    startGame.func = std::bind(&NewGameScene::OnStartPressed, this);
    RefreshOptionLabels();

    Vec2i windowSize{GetScreenWidth(), GetScreenHeight()};
    backButton.UpdateSize(windowSize);
    gameName.UpdateSize(windowSize);
    startGame.UpdateSize(windowSize);
    sizeButton.UpdateSize(windowSize);
    difficultyButton.UpdateSize(windowSize);
    resourceDensity.UpdateSize(windowSize);
    resourceFieldSize.UpdateSize(windowSize);
    resourceRichness.UpdateSize(windowSize);
    aiOpponents.UpdateSize(windowSize);
    debugMode.UpdateSize(windowSize);
}

// Advances this object's state for one frame.
void NewGameScene::Update(double dt)
{
    RefreshOptionLabels();

    render.Draw({&gameName,
                 &sizeButton,
                 &difficultyButton,
                 &resourceDensity,
                 &resourceFieldSize,
                 &resourceRichness,
                 &aiOpponents,
                 &debugMode,
                 &startGame,
                 &backButton}, dt);
}

// Handles the UI action represented by OnBackPressed.
void NewGameScene::OnBackPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = previousSceneName;
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnStartPressed.
void NewGameScene::OnStartPressed()
{
    auto msg = std::make_shared<NewGameEvent>();
    msg->sender = this;
    msg->name = gameName.GetText();

    MapParameters params;
    params.sizePreset = selectedSize;
    params.sizeX = MapGenerator::SizeFromPreset(params.sizePreset);
    params.sizeY = params.sizeX;
    params.seed = static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count());
    params.resourceDensity = resourceDensity.GetValue();
    params.resourceFieldSize = resourceFieldSize.GetValue();
    params.resourceRichness = SliderToInt(resourceRichness.GetValue(), 30, 250);
    params.aiOpponentCount = SliderToInt(aiOpponents.GetValue(), 0, 5);
    params.aiDifficulty = selectedDifficulty;
    params.debugMode = debugMode.IsActive();
    if (params.debugMode)
    {
        params.sizePreset = MapSizePreset::S;
        params.sizeX = 101;
        params.sizeY = 101;
        params.aiOpponentCount = 1;
        params.resourceDensity = 0.65f;
        params.resourceFieldSize = 0.45f;
        params.resourceRichness = 120;
    }

    msg->params = params;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnSizePressed.
void NewGameScene::OnSizePressed()
{
    int next = (static_cast<int>(selectedSize) + 1) % 4;
    selectedSize = static_cast<MapSizePreset>(next);
    RefreshOptionLabels();
}

// Handles the UI action represented by OnDifficultyPressed.
void NewGameScene::OnDifficultyPressed()
{
    selectedDifficulty = (selectedDifficulty + 1) % 4;
    RefreshOptionLabels();
}

// Initializes NewGameScene::RefreshOptionLabels.
void NewGameScene::RefreshOptionLabels()
{
    sizeButton.ChangeText("Map size: " + MapSizeName(selectedSize));
    difficultyButton.ChangeText("AI difficulty: " + DifficultyName(selectedDifficulty));
    resourceDensity.ChangeText("Resource density " + std::to_string(SliderToInt(resourceDensity.GetValue(), 50, 225)) + "%");
    resourceFieldSize.ChangeText("Field size " + std::to_string(SliderToInt(resourceFieldSize.GetValue(), 65, 200)) + "%");
    resourceRichness.ChangeText("Richness " + std::to_string(SliderToInt(resourceRichness.GetValue(), 30, 250)));
    aiOpponents.ChangeText("AI opponents " + std::to_string(SliderToInt(aiOpponents.GetValue(), 0, 5)));
    if (debugMode.IsActive())
        sizeButton.ChangeText("Map size: DEBUG 101x101");
}

// Handles the requested event or transfer.
void NewGameScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        backButton.UpdateSize(ptr->windowSize);
        gameName.UpdateSize(ptr->windowSize);
        startGame.UpdateSize(ptr->windowSize);
        sizeButton.UpdateSize(ptr->windowSize);
        difficultyButton.UpdateSize(ptr->windowSize);
        resourceDensity.UpdateSize(ptr->windowSize);
        resourceFieldSize.UpdateSize(ptr->windowSize);
        resourceRichness.UpdateSize(ptr->windowSize);
        aiOpponents.UpdateSize(ptr->windowSize);
        debugMode.UpdateSize(ptr->windowSize);
    }
}

// Initializes MultiplayerScene::MultiplayerScene.
MultiplayerScene::MultiplayerScene()
{
    MultiplayerConfig config = LoadMultiplayerConfig();

    backButton.ChangeText("Back");
    backButton.ChangeSizeAnchor(Vec2f{0.18f, 0.07f});
    backButton.ChangePositionAnchor(Vec2f{0.04f, 0.90f});
    backButton.func = std::bind(&MultiplayerScene::OnBackPressed, this);

    nicknameLabel.ChangeText("Nickname");
    nicknameLabel.ChangeSizeAnchor(Vec2f{0.18f, 0.045f});
    nicknameLabel.ChangePositionAnchor(Vec2f{0.16f, 0.172f});

    nickname.SetValue(config.nickname);
    nickname.ChangeSizeAnchor(Vec2f{0.32f, 0.07f});
    nickname.ChangePositionAnchor(Vec2f{0.34f, 0.16f});

    sessionNameLabel.ChangeText("Session");
    sessionNameLabel.ChangeSizeAnchor(Vec2f{0.18f, 0.045f});
    sessionNameLabel.ChangePositionAnchor(Vec2f{0.16f, 0.252f});

    sessionName.SetValue(config.sessionName);
    sessionName.ChangeSizeAnchor(Vec2f{0.32f, 0.07f});
    sessionName.ChangePositionAnchor(Vec2f{0.34f, 0.24f});

    addressLabel.ChangeText("Host IP");
    addressLabel.ChangeSizeAnchor(Vec2f{0.18f, 0.045f});
    addressLabel.ChangePositionAnchor(Vec2f{0.16f, 0.362f});

    address.SetValue(config.hostIp);
    address.ChangeSizeAnchor(Vec2f{0.32f, 0.07f});
    address.ChangePositionAnchor(Vec2f{0.34f, 0.35f});

    portLabel.ChangeText("Port");
    portLabel.ChangeSizeAnchor(Vec2f{0.18f, 0.045f});
    portLabel.ChangePositionAnchor(Vec2f{0.16f, 0.472f});

    port.SetValue(std::to_string(config.port));
    port.ChangeSizeAnchor(Vec2f{0.18f, 0.07f});
    port.ChangePositionAnchor(Vec2f{0.41f, 0.46f});

    aiOpponents.ChangePositionAnchor(Vec2f{0.34f, 0.56f});
    aiOpponents.ChangeSizeAnchor(Vec2f{0.32f, 0.045f});
    aiOpponents.currentValue = std::clamp(config.aiOpponents / 5.0f, 0.0f, 1.0f);
    lobbySizePreset = config.sizePreset;
    lobbyDifficulty = config.difficulty;

    hostButton.ChangeText("Host");
    hostButton.ChangeSizeAnchor(Vec2f{0.24f, 0.08f});
    hostButton.ChangePositionAnchor(Vec2f{0.25f, 0.68f});
    hostButton.func = std::bind(&MultiplayerScene::OnHostPressed, this);

    joinButton.ChangeText("Join");
    joinButton.ChangeSizeAnchor(Vec2f{0.24f, 0.08f});
    joinButton.ChangePositionAnchor(Vec2f{0.51f, 0.68f});
    joinButton.func = std::bind(&MultiplayerScene::OnJoinPressed, this);

    startButton.ChangeText("Start");
    startButton.ChangeSizeAnchor(Vec2f{0.18f, 0.07f});
    startButton.ChangePositionAnchor(Vec2f{0.68f, 0.88f});
    startButton.func = std::bind(&MultiplayerScene::OnStartPressed, this);

    gameSettingsButton.ChangeText("Game Settings");
    gameSettingsButton.ChangeSizeAnchor(Vec2f{0.22f, 0.07f});
    gameSettingsButton.ChangePositionAnchor(Vec2f{0.44f, 0.88f});
    gameSettingsButton.func = std::bind(&MultiplayerScene::OnGameSettingsPressed, this);

    closeSettingsButton.ChangeText("Close");
    closeSettingsButton.ChangeSizeAnchor(Vec2f{0.20f, 0.065f});
    closeSettingsButton.ChangePositionAnchor(Vec2f{0.30f, 0.84f});
    closeSettingsButton.func = std::bind(&MultiplayerScene::OnCloseGameSettingsPressed, this);

    multiplayerSizeButton.ChangeSizeAnchor(Vec2f{0.40f, 0.055f});
    multiplayerSizeButton.ChangePositionAnchor(Vec2f{0.30f, 0.22f});
    multiplayerSizeButton.func = std::bind(&MultiplayerScene::OnMultiplayerSizePressed, this);

    multiplayerDifficultyButton.ChangeSizeAnchor(Vec2f{0.40f, 0.055f});
    multiplayerDifficultyButton.ChangePositionAnchor(Vec2f{0.30f, 0.29f});
    multiplayerDifficultyButton.func = std::bind(&MultiplayerScene::OnMultiplayerDifficultyPressed, this);

    multiplayerResourceDensity.ChangePositionAnchor(Vec2f{0.30f, 0.38f});
    multiplayerResourceDensity.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    multiplayerResourceDensity.currentValue = config.resourceDensity;
    multiplayerResourceFieldSize.ChangePositionAnchor(Vec2f{0.30f, 0.47f});
    multiplayerResourceFieldSize.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    multiplayerResourceFieldSize.currentValue = config.resourceFieldSize;
    multiplayerResourceRichness.ChangePositionAnchor(Vec2f{0.30f, 0.56f});
    multiplayerResourceRichness.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    multiplayerResourceRichness.currentValue = config.resourceRichness;
    multiplayerDebugMode.ChangeText("Debug mode");
    multiplayerDebugMode.ChangePositionAnchor(Vec2f{0.30f, 0.73f});
    multiplayerDebugMode.ChangeSizeAnchor(Vec2f{0.20f, 0.045f});
    multiplayerDebugMode.currentState = config.debugMode;

    chatInput.SetValue("");
    chatInput.ChangeSizeAnchor(Vec2f{0.48f, 0.055f});
    chatInput.ChangePositionAnchor(Vec2f{0.34f, 0.80f});

    sendChatButton.ChangeText("Send");
    sendChatButton.ChangeSizeAnchor(Vec2f{0.08f, 0.055f});
    sendChatButton.ChangePositionAnchor(Vec2f{0.83f, 0.80f});
    sendChatButton.func = std::bind(&MultiplayerScene::OnSendChatPressed, this);

    Vec2i size{GetScreenWidth(), GetScreenHeight()};
    backButton.UpdateSize(size);
    nicknameLabel.UpdateSize(size);
    nickname.UpdateSize(size);
    sessionNameLabel.UpdateSize(size);
    sessionName.UpdateSize(size);
    addressLabel.UpdateSize(size);
    address.UpdateSize(size);
    portLabel.UpdateSize(size);
    port.UpdateSize(size);
    aiOpponents.UpdateSize(size);
    hostButton.UpdateSize(size);
    joinButton.UpdateSize(size);
    startButton.UpdateSize(size);
    gameSettingsButton.UpdateSize(size);
    closeSettingsButton.UpdateSize(size);
    multiplayerSizeButton.UpdateSize(size);
    multiplayerDifficultyButton.UpdateSize(size);
    multiplayerResourceDensity.UpdateSize(size);
    multiplayerResourceFieldSize.UpdateSize(size);
    multiplayerResourceRichness.UpdateSize(size);
    multiplayerDebugMode.UpdateSize(size);
    chatInput.UpdateSize(size);
    sendChatButton.UpdateSize(size);
    RefreshMultiplayerLabels();
}

// Advances this object's state for one frame.
void MultiplayerScene::Update(double dt)
{
    UpdateLobbyMessages(dt);
    if (connectionMessageTimer > 0.0)
        connectionMessageTimer = std::max(0.0, connectionMessageTimer - dt);
    RefreshMultiplayerLabels();

    std::vector<UiWidget*> widgets;
    if (lobbyActive)
    {
        if (showGameSettings && isLobbyHost)
        {
            aiOpponents.ChangePositionAnchor(Vec2f{0.30f, 0.65f});
            aiOpponents.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
            aiOpponents.UpdateSize({GetScreenWidth(), GetScreenHeight()});
            widgets = {
                &multiplayerSizeButton,
                &multiplayerDifficultyButton,
                &multiplayerResourceDensity,
                &multiplayerResourceFieldSize,
                &multiplayerResourceRichness,
                &aiOpponents,
                &multiplayerDebugMode,
                &closeSettingsButton,
                &backButton};
        }
        else
        {
            if (isLobbyHost)
            {
                widgets.push_back(&gameSettingsButton);
                widgets.push_back(&startButton);
            }
            widgets.push_back(&chatInput);
            widgets.push_back(&sendChatButton);
            widgets.push_back(&backButton);
        }
    }
    else
    {
        widgets = {
            &nicknameLabel,
            &nickname,
            &sessionNameLabel,
            &sessionName,
            &addressLabel,
            &address,
            &portLabel,
            &port,
            &hostButton,
            &joinButton,
            &backButton};
    }

    BeginDrawing();
    ClearBackground(BLACK);
    if (lobbyActive && showGameSettings && isLobbyHost)
        DrawGameSettingsPanel();
    else if (lobbyActive)
    {
        Rectangle chatBounds{
            GetScreenWidth() * 0.34f,
            GetScreenHeight() * 0.20f,
            GetScreenWidth() * 0.58f,
            GetScreenHeight() * 0.58f};
        if (CheckCollisionPointRec(GetMousePosition(), chatBounds))
        {
            int maxScroll = std::max(0, static_cast<int>(lobbyLines.size()) - 10);
            lobbyChatScroll = std::clamp(lobbyChatScroll + static_cast<int>(GetMouseWheelMove()), 0, maxScroll);
        }
        DrawLobbyLog();
    }
    for (auto* widget : widgets)
        if (widget != nullptr)
            widget->Update(dt);
    if (connectingToLobby || connectionMessageTimer > 0.0)
        DrawConnectionDialog();
    EndDrawing();

    if (lobbyActive && !showGameSettings && IsKeyPressed(KEY_ENTER) && !chatInput.GetText().empty())
        OnSendChatPressed();
    if (lobbyActive && showGameSettings && isLobbyHost && IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
        MaybeBroadcastSettingsChange("Game settings updated.");
}

// Handles the requested event or transfer.
void MultiplayerScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        backButton.UpdateSize(ptr->windowSize);
        nicknameLabel.UpdateSize(ptr->windowSize);
        nickname.UpdateSize(ptr->windowSize);
        sessionNameLabel.UpdateSize(ptr->windowSize);
        sessionName.UpdateSize(ptr->windowSize);
        addressLabel.UpdateSize(ptr->windowSize);
        address.UpdateSize(ptr->windowSize);
        portLabel.UpdateSize(ptr->windowSize);
        port.UpdateSize(ptr->windowSize);
        aiOpponents.UpdateSize(ptr->windowSize);
        hostButton.UpdateSize(ptr->windowSize);
        joinButton.UpdateSize(ptr->windowSize);
        startButton.UpdateSize(ptr->windowSize);
        gameSettingsButton.UpdateSize(ptr->windowSize);
        closeSettingsButton.UpdateSize(ptr->windowSize);
        multiplayerSizeButton.UpdateSize(ptr->windowSize);
        multiplayerDifficultyButton.UpdateSize(ptr->windowSize);
        multiplayerResourceDensity.UpdateSize(ptr->windowSize);
        multiplayerResourceFieldSize.UpdateSize(ptr->windowSize);
        multiplayerResourceRichness.UpdateSize(ptr->windowSize);
        multiplayerDebugMode.UpdateSize(ptr->windowSize);
        chatInput.UpdateSize(ptr->windowSize);
        sendChatButton.UpdateSize(ptr->windowSize);
    }
}

// Handles the UI action represented by OnBackPressed.
void MultiplayerScene::OnBackPressed()
{
    if (connectingToLobby)
    {
        ResetLobby();
        return;
    }

    if (showGameSettings)
    {
        showGameSettings = false;
        SaveMultiplayerSettings();
        return;
    }

    if (lobbyActive)
    {
        ResetLobby();
        return;
    }

    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "MainScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnHostPressed.
void MultiplayerScene::OnHostPressed()
{
    ResetLobby();
    SaveMultiplayerSettings();
    lobbyNickname = SanitizeSaveName(nickname.GetText());
    lobbySessionName = SanitizeSaveName(sessionName.GetText());
    lobbyPort = ParsePort(port.GetText());
    lobbyAiOpponentCount = SliderToInt(aiOpponents.GetValue(), 0, 5);
    lobbyTransport = TcpGameTransport::CreateHost(lobbyPort);
    isLobbyHost = true;
    lobbyActive = true;
    AddLobbyLine(lobbyNickname + " is hosting '" + lobbySessionName + "' on port " + std::to_string(lobbyPort));
    AddLobbyLine("AI players: " + std::to_string(lobbyAiOpponentCount));
    AddLobbyLine("Waiting for players...");
    lastBroadcastLobbyState.clear();
    Log::Msg("[Lobby]", "Host lobby opened: ", lobbySessionName, " port=", lobbyPort);
}

// Handles the UI action represented by OnJoinPressed.
void MultiplayerScene::OnJoinPressed()
{
    ResetLobby();
    SaveMultiplayerSettings();
    lobbyNickname = SanitizeSaveName(nickname.GetText());
    lobbySessionName = SanitizeSaveName(sessionName.GetText());
    lobbyAddress = address.GetText();
    lobbyPort = ParsePort(port.GetText());
    lobbyTransport = TcpGameTransport::CreateClient(lobbyAddress, lobbyPort);
    isLobbyHost = false;
    lobbyActive = false;
    connectingToLobby = true;
    connectionWaitTimer = 0.0;
    connectionMessageTimer = 0.0;
    connectionMessage = "Connecting to " + lobbyAddress + ":" + std::to_string(lobbyPort);
    lastBroadcastLobbyState.clear();
    Log::Msg("[Lobby]", "Client lobby join requested: ", lobbyAddress, ":", lobbyPort, " session=", lobbySessionName);
}

// Handles the UI action represented by OnStartPressed.
void MultiplayerScene::OnStartPressed()
{
    if (!lobbyActive || !isLobbyHost || lobbyTransport == nullptr)
    {
        AddLobbyLine("Only the host can start an active lobby.");
        return;
    }

    SaveMultiplayerSettings();
    MaybeBroadcastSettingsChange();
    MapParameters params = BuildLobbyMapParameters();
    lobbyTransport->SendLobbyMessage(SerializeMultiplayerStart(lobbySessionName, params));
    AddLobbyLine("Starting game...");
    Log::Msg("[Lobby]", "Host starting multiplayer game: ", lobbySessionName);

    auto msg = std::make_shared<HostMultiplayerGameEvent>();
    msg->sender = this;
    msg->name = lobbySessionName;
    msg->params = params;
    msg->port = lobbyPort;
    msg->transport = lobbyTransport;
    lobbyTransport = nullptr;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnGameSettingsPressed.
void MultiplayerScene::OnGameSettingsPressed()
{
    if (isLobbyHost)
        showGameSettings = true;
}

// Handles the UI action represented by OnCloseGameSettingsPressed.
void MultiplayerScene::OnCloseGameSettingsPressed()
{
    showGameSettings = false;
    SaveMultiplayerSettings();
    MaybeBroadcastSettingsChange("Game settings updated.");
}

// Handles the UI action represented by OnMultiplayerSizePressed.
void MultiplayerScene::OnMultiplayerSizePressed()
{
    int next = (static_cast<int>(lobbySizePreset) + 1) % 4;
    lobbySizePreset = static_cast<MapSizePreset>(next);
    RefreshMultiplayerLabels();
    MaybeBroadcastSettingsChange("Map size set to " + MapSizeName(lobbySizePreset) + ".");
}

// Handles the UI action represented by OnMultiplayerDifficultyPressed.
void MultiplayerScene::OnMultiplayerDifficultyPressed()
{
    lobbyDifficulty = (lobbyDifficulty + 1) % 4;
    RefreshMultiplayerLabels();
    MaybeBroadcastSettingsChange("Difficulty set to " + DifficultyName(lobbyDifficulty) + ".");
}

// Handles the UI action represented by OnSendChatPressed.
void MultiplayerScene::OnSendChatPressed()
{
    if (!lobbyActive || lobbyTransport == nullptr)
    {
        AddLobbyLine("Open or join a lobby first.");
        return;
    }

    std::string text = chatInput.GetText();
    if (text.empty())
        return;

    AddLobbyLine(lobbyNickname + ": " + text, LocalPlayerChatColor());
    lobbyTransport->SendLobbyMessage("CHAT " + lobbyNickname + ": " + text);
    chatInput.SetValue("");
}

// Adds one visible lobby/chat line.
void MultiplayerScene::AddLobbyLine(const std::string& line, Color color)
{
    lobbyLines.push_back({line, color});
    if (lobbyLines.size() > 100)
        lobbyLines.erase(lobbyLines.begin());
    lobbyChatScroll = std::clamp(lobbyChatScroll, 0, std::max(0, static_cast<int>(lobbyLines.size()) - 10));
}

// Clears current lobby connection state.
void MultiplayerScene::ResetLobby()
{
    lobbyTransport.reset();
    lobbyLines.clear();
    remoteLobbyNickname.clear();
    lobbyActive = false;
    connectingToLobby = false;
    isLobbyHost = false;
    announcedConnection = false;
    showGameSettings = false;
    hasRemoteLobbyPlayer = false;
    lobbyChatScroll = 0;
    connectionWaitTimer = 0.0;
    connectionMessageTimer = 0.0;
    connectionMessage.clear();
    lastBroadcastLobbyState.clear();
}

// Polls socket state and lobby messages.
void MultiplayerScene::UpdateLobbyMessages(double dt)
{
    if ((!lobbyActive && !connectingToLobby) || lobbyTransport == nullptr)
        return;

    std::string status = lobbyTransport->GetStatus();
    if (connectingToLobby)
    {
        connectionWaitTimer += dt;
        connectionMessage = "Connecting to " + lobbyAddress + ":" + std::to_string(lobbyPort) + "...";
        if (lobbyTransport->IsConnected())
        {
            connectingToLobby = false;
            lobbyActive = true;
            announcedConnection = true;
            connectionMessage.clear();
            AddLobbyLine("Connected to host.");
            Log::Msg("[Lobby]", status);
            lobbyTransport->SendLobbyMessage("JOIN " + lobbyNickname);
        }
        else if (lobbyTransport->HasFailed() || connectionWaitTimer >= 10.0)
        {
            connectionMessage = "Connection failed: " + status;
            connectionMessageTimer = 3.0;
            Log::Msg("[Lobby]", "Connection failed or timed out: ", status);
            lobbyTransport.reset();
            connectingToLobby = false;
            lobbyActive = false;
            announcedConnection = false;
            return;
        }
        else
        {
            return;
        }
    }
    if (!announcedConnection && lobbyTransport->IsConnected())
    {
        announcedConnection = true;
        AddLobbyLine(isLobbyHost ? "Client connected." : "Connected to host.");
        Log::Msg("[Lobby]", status);
        if (!isLobbyHost)
            lobbyTransport->SendLobbyMessage("JOIN " + lobbyNickname);
        else
            BroadcastLobbyState();
    }
    if (lobbyTransport->HasFailed())
        AddLobbyLine("Connection failed: " + status);

    for (const auto& payload : lobbyTransport->ReceiveLobbyMessages())
    {
        if (payload.rfind("JOIN ", 0) == 0)
        {
            std::string playerName = payload.substr(5);
            if (isLobbyHost)
            {
                for (const auto& [line, color] : lobbyLines)
                    lobbyTransport->SendLobbyMessage("HISTORY " + line);
            }
            remoteLobbyNickname = playerName;
            hasRemoteLobbyPlayer = true;
            AddLobbyLine(playerName + " joined.");
            Log::Msg("[Lobby]", "Player joined: ", playerName);
            if (isLobbyHost)
            {
                lobbyTransport->SendLobbyMessage("INFO " + playerName + " joined.");
                BroadcastLobbyState();
            }
        }
        else if (payload.rfind("INFO ", 0) == 0)
        {
            AddLobbyLine(payload.substr(5));
        }
        else if (payload.rfind("HISTORY ", 0) == 0)
        {
            AddLobbyLine(payload.substr(8));
        }
        else if (payload.rfind("STATE ", 0) == 0)
        {
            if (!ApplyLobbyState(payload.substr(6)))
                AddLobbyLine("Failed to parse lobby state from host.", Color{240, 120, 120, 255});
        }
        else if (payload.rfind("START ", 0) == 0)
        {
            MapParameters params;
            if (!TryDeserializeMultiplayerStart(payload.substr(6), lobbySessionName, params))
            {
                AddLobbyLine("Failed to parse game settings from host.", Color{240, 120, 120, 255});
                continue;
            }
            lobbyAiOpponentCount = params.aiOpponentCount;
            AddLobbyLine("Host started the game.");
            Log::Msg("[Lobby]", "Client received start for session ", lobbySessionName);

            auto msg = std::make_shared<JoinMultiplayerGameEvent>();
            msg->sender = this;
            msg->name = lobbySessionName;
            msg->params = params;
            msg->address = lobbyAddress;
            msg->port = lobbyPort;
            msg->transport = lobbyTransport;
            lobbyTransport = nullptr;
            broker->Broadcast(msg);
        }
        else if (payload.rfind("CHAT ", 0) == 0)
        {
            AddLobbyLine(payload.substr(5), RemotePlayerChatColor());
        }
        else
        {
            AddLobbyLine(payload);
        }
    }
}

// Refreshes dynamic labels in the multiplayer setup view.
void MultiplayerScene::RefreshMultiplayerLabels()
{
    aiOpponents.ChangeText("AI players " + std::to_string(SliderToInt(aiOpponents.GetValue(), 0, 5)));
    multiplayerSizeButton.ChangeText("Map size " + MapSizeName(lobbySizePreset));
    multiplayerDifficultyButton.ChangeText("Difficulty " + DifficultyName(lobbyDifficulty));
    multiplayerResourceDensity.ChangeText("Resource density " + std::to_string(SliderToInt(multiplayerResourceDensity.GetValue(), 50, 225)) + "%");
    multiplayerResourceFieldSize.ChangeText("Resource field size " + std::to_string(SliderToInt(multiplayerResourceFieldSize.GetValue(), 50, 225)) + "%");
    multiplayerResourceRichness.ChangeText("Resource richness " + std::to_string(SliderToInt(multiplayerResourceRichness.GetValue(), 40, 160)));
}

// Stores the multiplayer setup for future sessions.
void MultiplayerScene::SaveMultiplayerSettings() const
{
    MultiplayerConfig config;
    config.nickname = SanitizeSaveName(nickname.GetText());
    config.sessionName = SanitizeSaveName(sessionName.GetText());
    config.hostIp = address.GetText();
    config.port = ParsePort(port.GetText());
    config.aiOpponents = SliderToInt(aiOpponents.GetValue(), 0, 5);
    config.sizePreset = lobbySizePreset;
    config.difficulty = lobbyDifficulty;
    config.resourceDensity = multiplayerResourceDensity.GetValue();
    config.resourceFieldSize = multiplayerResourceFieldSize.GetValue();
    config.resourceRichness = multiplayerResourceRichness.GetValue();
    config.debugMode = multiplayerDebugMode.currentState;
    SaveMultiplayerConfig(config);
}

// Builds the authoritative hosted game generation parameters.
MapParameters MultiplayerScene::BuildLobbyMapParameters() const
{
    return MakeDefaultMultiplayerParams(
        SliderToInt(aiOpponents.GetValue(), 0, 5),
        lobbySizePreset,
        lobbyDifficulty,
        multiplayerResourceDensity.GetValue(),
        multiplayerResourceFieldSize.GetValue(),
        multiplayerResourceRichness.GetValue(),
        multiplayerDebugMode.currentState);
}

void MultiplayerScene::BroadcastLobbyState(const std::string& infoMessage)
{
    if (!lobbyActive || !isLobbyHost || lobbyTransport == nullptr)
        return;

    lobbyAiOpponentCount = SliderToInt(aiOpponents.GetValue(), 0, 5);
    MapParameters params = BuildLobbyMapParameters();
    std::string state = SerializeMultiplayerLobbyState(lobbySessionName, lobbyNickname, remoteLobbyNickname, params);
    lobbyTransport->SendLobbyMessage(state);
    lastBroadcastLobbyState = state;
    if (!infoMessage.empty())
    {
        AddLobbyLine(infoMessage);
        lobbyTransport->SendLobbyMessage("INFO " + infoMessage);
    }
}

bool MultiplayerScene::ApplyLobbyState(const std::string& payload)
{
    if (isLobbyHost)
        return true;

    std::string session;
    std::string hostName;
    std::string remoteName;
    MapParameters params;
    if (!TryDeserializeMultiplayerLobbyState(payload, session, hostName, remoteName, params))
        return false;

    lobbySessionName = session;
    remoteLobbyNickname = hostName.empty() ? "Host" : hostName;
    hasRemoteLobbyPlayer = true;
    lobbyAiOpponentCount = params.aiOpponentCount;
    lobbySizePreset = params.sizePreset;
    lobbyDifficulty = params.aiDifficulty;
    multiplayerResourceDensity.currentValue = params.resourceDensity;
    multiplayerResourceFieldSize.currentValue = params.resourceFieldSize;
    multiplayerResourceRichness.currentValue = std::clamp((params.resourceRichness - 40) / 120.0f, 0.0f, 1.0f);
    multiplayerDebugMode.currentState = params.debugMode;
    RefreshMultiplayerLabels();
    return true;
}

void MultiplayerScene::MaybeBroadcastSettingsChange(const std::string& infoMessage)
{
    if (!lobbyActive || !isLobbyHost || lobbyTransport == nullptr)
        return;

    lobbyAiOpponentCount = SliderToInt(aiOpponents.GetValue(), 0, 5);
    MapParameters params = BuildLobbyMapParameters();
    std::string state = SerializeMultiplayerLobbyState(lobbySessionName, lobbyNickname, remoteLobbyNickname, params);
    if (state == lastBroadcastLobbyState)
        return;

    BroadcastLobbyState(infoMessage);
}

void MultiplayerScene::DrawConnectionDialog() const
{
    int width = static_cast<int>(GetScreenWidth() * 0.46f);
    int height = static_cast<int>(GetScreenHeight() * 0.22f);
    int x = (GetScreenWidth() - width) / 2;
    int y = (GetScreenHeight() - height) / 2;
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 120});
    DrawRectangle(x, y, width, height, Color{20, 24, 31, 245});
    DrawRectangleLines(x, y, width, height, Color{96, 116, 142, 255});

    std::string title = connectingToLobby ? "Connecting" : "Connection failed";
    UiText::DrawFit(title,
        Rectangle{static_cast<float>(x + 22), static_cast<float>(y + 18), static_cast<float>(width - 44), 32.0f},
        28,
        RAYWHITE);

    std::string body = connectionMessage;
    if (connectingToLobby)
    {
        int remaining = std::max(0, 10 - static_cast<int>(std::floor(connectionWaitTimer)));
        body += "  Timeout in " + std::to_string(remaining) + "s";
    }

    UiText::DrawFit(body,
        Rectangle{static_cast<float>(x + 24), static_cast<float>(y + 68), static_cast<float>(width - 48), static_cast<float>(height - 88)},
        21,
        Color{202, 216, 236, 255});
}

// Draws the lobby/chat log under the form.
void MultiplayerScene::DrawLobbyLog() const
{
    int x = static_cast<int>(GetScreenWidth() * 0.04f);
    int y = static_cast<int>(GetScreenHeight() * 0.045f);
    int width = static_cast<int>(GetScreenWidth() * 0.92f);
    int height = static_cast<int>(GetScreenHeight() * 0.80f);
    DrawRectangle(x, y, width, height, Color{20, 24, 31, 225});
    DrawRectangleLines(x, y, width, height, Color{86, 100, 120, 255});

    UiText::DrawFit("Multiplayer Lobby", Rectangle{static_cast<float>(x + 18), static_cast<float>(y + 14), static_cast<float>(width - 36), 34.0f}, 30, RAYWHITE);
    std::string status = isLobbyHost
        ? "Host: " + lobbySessionName + "  Port: " + std::to_string(lobbyPort)
        : "Client: " + lobbySessionName + "  Host: " + lobbyAddress + ":" + std::to_string(lobbyPort);
    UiText::DrawFit(status, Rectangle{static_cast<float>(x + 18), static_cast<float>(y + 54), static_cast<float>(width - 36), 28.0f}, 22, Color{190, 205, 224, 255});

    DrawLobbyPlayerPanels();

    Rectangle chatPanel{
        static_cast<float>(x + static_cast<int>(GetScreenWidth() * 0.30f)),
        static_cast<float>(y + 96),
        static_cast<float>(width - static_cast<int>(GetScreenWidth() * 0.33f)),
        static_cast<float>(height - 124)};
    DrawRectangleRec(chatPanel, Color{15, 18, 24, 210});
    DrawRectangleLinesEx(chatPanel, 1.0f, Color{70, 84, 104, 255});
    UiText::Draw("Chat", chatPanel.x + 12.0f, chatPanel.y + 10.0f, 22, Color{220, 230, 244, 255});

    int visibleLines = std::max(1, static_cast<int>((chatPanel.height - 50.0f) / 24.0f));
    int maxScroll = std::max(0, static_cast<int>(lobbyLines.size()) - visibleLines);
    int scroll = std::clamp(lobbyChatScroll, 0, maxScroll);
    int firstLine = std::max(0, static_cast<int>(lobbyLines.size()) - visibleLines - scroll);
    int lastLine = std::min(static_cast<int>(lobbyLines.size()), firstLine + visibleLines);
    int lineY = static_cast<int>(chatPanel.y + 42.0f);
    BeginScissorMode(static_cast<int>(chatPanel.x), static_cast<int>(chatPanel.y + 36.0f), static_cast<int>(chatPanel.width - 16.0f), static_cast<int>(chatPanel.height - 42.0f));
    for (int i = firstLine; i < lastLine; i++)
    {
        const auto& line = lobbyLines[static_cast<size_t>(i)];
        UiText::Draw(line.first, chatPanel.x + 12.0f, static_cast<float>(lineY), 20, line.second);
        lineY += 24;
    }
    EndScissorMode();

    if (maxScroll > 0)
    {
        Rectangle track{chatPanel.x + chatPanel.width - 10.0f, chatPanel.y + 38.0f, 4.0f, chatPanel.height - 48.0f};
        float thumbHeight = std::max(24.0f, track.height * (visibleLines / static_cast<float>(lobbyLines.size())));
        float thumbRange = std::max(1.0f, track.height - thumbHeight);
        float thumbY = track.y + thumbRange * (scroll / static_cast<float>(maxScroll));
        DrawRectangleRec(track, Color{52, 62, 78, 220});
        DrawRectangleRounded({track.x - 2.0f, thumbY, 8.0f, thumbHeight}, 0.5f, 4, Color{118, 148, 188, 255});
    }
}

// Draws lobby player cards above chat.
void MultiplayerScene::DrawLobbyPlayerPanels() const
{
    int x = static_cast<int>(GetScreenWidth() * 0.06f);
    int y = static_cast<int>(GetScreenHeight() * 0.18f);
    int width = static_cast<int>(GetScreenWidth() * 0.26f);
    int cardHeight = static_cast<int>(GetScreenHeight() * 0.085f);
    int gap = 10;

    Rectangle panel{
        static_cast<float>(x),
        static_cast<float>(y - 40),
        static_cast<float>(width),
        static_cast<float>(GetScreenHeight() * 0.66f)};
    DrawRectangleRec(panel, Color{15, 18, 24, 210});
    DrawRectangleLinesEx(panel, 1.0f, Color{70, 84, 104, 255});
    UiText::Draw("Players", panel.x + 12.0f, panel.y + 10.0f, 22, Color{220, 230, 244, 255});

    auto drawCard = [&](int index, const std::string& label, const std::string& role, Color color)
    {
        Rectangle card{
            static_cast<float>(x + 12),
            static_cast<float>(y + index * (cardHeight + gap)),
            static_cast<float>(width - 24),
            static_cast<float>(cardHeight)};
        DrawRectangleRec(card, Color{25, 31, 40, 230});
        DrawRectangleLinesEx(card, 1.0f, Color{78, 94, 116, 255});
        Rectangle swatch{card.x + 10.0f, card.y + 9.0f, 22.0f, card.height - 18.0f};
        DrawRectangleRec(swatch, color);
        DrawRectangleLinesEx(swatch, 1.0f, Color{220, 230, 245, 180});
        UiText::DrawFit(label, Rectangle{card.x + 44.0f, card.y + 9.0f, card.width - 58.0f, 24.0f}, 20, RAYWHITE);
        UiText::DrawFit(role, Rectangle{card.x + 44.0f, card.y + 38.0f, card.width - 58.0f, 22.0f}, 18, Color{184, 198, 218, 255});
    };

    drawCard(0, lobbyNickname, isLobbyHost ? "Host" : "You", LocalPlayerChatColor());
    if (hasRemoteLobbyPlayer || !isLobbyHost)
        drawCard(1, hasRemoteLobbyPlayer ? remoteLobbyNickname : "Host", isLobbyHost ? "Client" : "Host", RemotePlayerChatColor());

    int aiCount = isLobbyHost ? SliderToInt(aiOpponents.GetValue(), 0, 5) : lobbyAiOpponentCount;
    int startIndex = (hasRemoteLobbyPlayer || !isLobbyHost) ? 2 : 1;
    for (int i = 0; i < aiCount && startIndex + i < 5; i++)
        drawCard(startIndex + i, "AI Opponent " + std::to_string(i + 1), "AI", AiPlayerColor(i));
}

// Draws the hosted game settings panel behind controls.
void MultiplayerScene::DrawGameSettingsPanel() const
{
    int x = static_cast<int>(GetScreenWidth() * 0.18f);
    int y = static_cast<int>(GetScreenHeight() * 0.08f);
    int width = static_cast<int>(GetScreenWidth() * 0.64f);
    int height = static_cast<int>(GetScreenHeight() * 0.84f);
    DrawRectangle(x, y, width, height, Color{20, 24, 31, 225});
    DrawRectangleLines(x, y, width, height, Color{86, 100, 120, 255});
    UiText::DrawFit("Game Settings", Rectangle{static_cast<float>(x + 18), static_cast<float>(y + 14), static_cast<float>(width - 36), 34.0f}, 30, RAYWHITE);
    UiText::DrawFit("Host configuration. These settings are sent to every client on Start.",
        Rectangle{static_cast<float>(x + 22), static_cast<float>(y + 56), static_cast<float>(width - 44), 26.0f},
        19,
        Color{190, 205, 224, 255});
}

// Loads the requested data into runtime state.
LoadGameScene::LoadGameScene()
{
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
    render.Draw({&backButton, &saveButtons}, dt);
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

// Serializes current runtime state.
SaveGameScene::SaveGameScene()
{
    backButton.ChangeText("Back");
    backButton.ChangePositionAnchor(Vec2f{0.7f, 0.7f});
    backButton.func = std::bind(&SaveGameScene::OnBackPressed, this);

    saveName.ChangeText("");
    saveName.ChangeSizeAnchor(Vec2f{0.32f, 0.07f});
    saveName.ChangePositionAnchor(Vec2f{0.1f, 0.1f});

    newSaveButton.ChangeText("Create Save");
    newSaveButton.ChangeSizeAnchor(Vec2f{0.18f, 0.07f});
    newSaveButton.ChangePositionAnchor(Vec2f{0.44f, 0.1f});
    newSaveButton.func = std::bind(&SaveGameScene::OnNewSavePressed, this);

    saveButtons.ChangeSizeAnchor(Vec2f{0.36f, 0.36f});
    saveButtons.ChangePositionAnchor(Vec2f{0.1f, 0.22f});

    confirmOverwriteButton.ChangeText("Confirm overwrite");
    confirmOverwriteButton.ChangeSizeAnchor(Vec2f{0.24f, 0.07f});
    confirmOverwriteButton.ChangePositionAnchor(Vec2f{0.5f, 0.32f});
    confirmOverwriteButton.func = std::bind(&SaveGameScene::OnConfirmOverwrite, this);

    cancelOverwriteButton.ChangeText("Cancel");
    cancelOverwriteButton.ChangeSizeAnchor(Vec2f{0.16f, 0.07f});
    cancelOverwriteButton.ChangePositionAnchor(Vec2f{0.5f, 0.41f});
    cancelOverwriteButton.func = std::bind(&SaveGameScene::OnCancelOverwrite, this);

    LoadSaves();
    backButton.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    saveName.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    newSaveButton.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    saveButtons.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    confirmOverwriteButton.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    cancelOverwriteButton.UpdateSize({GetScreenWidth(), GetScreenHeight()});
}

// Advances this object's state for one frame.
void SaveGameScene::Update(double dt)
{
    if (overwriteConfirmationVisible)
        render.Draw({&backButton, &saveName, &newSaveButton, &saveButtons, &confirmOverwriteButton, &cancelOverwriteButton}, dt);
    else
        render.Draw({&backButton, &saveName, &newSaveButton, &saveButtons}, dt);
}

// Handles the UI action represented by OnBackPressed.
void SaveGameScene::OnBackPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = previousSceneName;
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the UI action represented by OnSavePressed.
void SaveGameScene::OnSavePressed(std::string save)
{
    pendingOverwriteSave = save;
    overwriteConfirmationVisible = true;
    confirmOverwriteButton.ChangeText("Overwrite " + save);
}

// Handles the UI action represented by OnNewSavePressed.
void SaveGameScene::OnNewSavePressed()
{
    std::string requestedName = SanitizeSaveName(saveName.GetText());
    if (SaveExists(requestedName))
    {
        // Handles the UI action represented by OnSavePressed.
        OnSavePressed(requestedName);
        return;
    }

    auto msg = std::make_shared<SaveGameEvent>();
    msg->sender = this;
    msg->name = requestedName;
    broker->Broadcast(msg);
    overwriteConfirmationVisible = false;
}

// Handles the UI action represented by OnConfirmOverwrite.
void SaveGameScene::OnConfirmOverwrite()
{
    if (pendingOverwriteSave.empty())
        return;

    auto msg = std::make_shared<SaveGameEvent>();
    msg->sender = this;
    msg->name = pendingOverwriteSave;
    broker->Broadcast(msg);
    pendingOverwriteSave.clear();
    overwriteConfirmationVisible = false;
}

// Handles the UI action represented by OnCancelOverwrite.
void SaveGameScene::OnCancelOverwrite()
{
    pendingOverwriteSave.clear();
    overwriteConfirmationVisible = false;
}

// Loads the requested data into runtime state.
void SaveGameScene::LoadSaves()
{
    // Initializes PopulateSaveButtons.
    PopulateSaveButtons(saveButtons, [this](std::string saveName)
    {
        // Handles the UI action represented by OnSavePressed.
        OnSavePressed(saveName);
    });
}

// Handles the requested event or transfer.
void SaveGameScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        backButton.UpdateSize(ptr->windowSize);
        saveName.UpdateSize(ptr->windowSize);
        newSaveButton.UpdateSize(ptr->windowSize);
        saveButtons.UpdateSize(ptr->windowSize);
        confirmOverwriteButton.UpdateSize(ptr->windowSize);
        cancelOverwriteButton.UpdateSize(ptr->windowSize);
    }

    auto sceneChange = std::dynamic_pointer_cast<ChangeSceneEvent>(e);
    if (sceneChange != nullptr && sceneChange->sceneName == name)
        LoadSaves();

    auto saveListChanged = std::dynamic_pointer_cast<SaveListChangedEvent>(e);
    if (saveListChanged != nullptr)
        LoadSaves();
}

// Initializes GameScene::GameScene.
GameScene::GameScene()
{
    render.atlasMap[0] = TextureAtlas{};
    render.atlasMap[0].LoadTextureAtlas("assets/textures/terrain/terrain_tileset.png");

    for (const auto& definition : GetBuildingDefinitions())
    {
        if (!definition.texturePath.empty())
            render.LoadBuildingTexture(definition.type, definition.texturePath);
    }

    GuiPanel::LoadResourceAtlas("assets/textures/resources/basic_resources.png", {64, 64});

    controller = std::make_unique<GuiController>();
    controller->Init(this);
    controller->AddSystem<BasicMapViewSystem>("default");
    controller->AddSystem<BuildGuiSystem>("build");
    controller->AddSystem<RoadBuildSystem>("road_build");
    controller->AddSystem<DestroyGuiSystem>("destroy");
    controller->AddSystem<StatsGuiSystem>("stats");
    controller->AddSystem<FocusGuiSystem>("focus");
    controller->ChangeSystem("default");
    
    inputs.Init(controller.get());

    networkStatusLabel.ChangeText("");
    networkStatusLabel.ChangeSize(180, 28);
    networkStatusLabel.fontSize = 18;
    networkStatusLabel.color = Color{188, 226, 255, 255};
    UpdateNetworkStatusWidget({GetScreenWidth(), GetScreenHeight()});
}

namespace
{
    void DrawRuntimeLoadingScreen(const std::string& message)
    {
        BeginDrawing();
        ClearBackground(Color{12, 16, 22, 255});
        int fontSize = 28;
        int width = UiText::Measure(message, fontSize);
        UiText::Draw(message,
                     (GetScreenWidth() - width) * 0.5f,
                     GetScreenHeight() * 0.5f,
                     fontSize,
                     Color{210, 224, 242, 255});
        EndDrawing();
    }

    class GameRuntimeLoopBase : public IGameRuntimeLoop
    {
    public:
        explicit GameRuntimeLoopBase(std::unique_ptr<IGameSession> session)
        : session(std::move(session))
        {
        }

        std::uint64_t SubmitCommand(const GameCommand& command) override
        {
            return session != nullptr ? session->SubmitCommand(command) : 0;
        }

        std::vector<GameCommandResult> ConsumeCommandResults() override
        {
            return session != nullptr ? session->ConsumeCommandResults() : std::vector<GameCommandResult>{};
        }

        bool IsConnectionClosed() const override
        {
            return session != nullptr && session->IsConnectionClosed();
        }

        std::string GetConnectionStatus() const override
        {
            return session != nullptr ? session->GetConnectionStatus() : std::string{};
        }

        std::recursive_mutex* GetWorldMutex() override
        {
            return session != nullptr ? session->GetWorldMutex() : nullptr;
        }

    protected:
        void UpdateSessionAndResults(GameScene& scene, double dt)
        {
            if (session == nullptr)
                return;

            session->Update(dt);
            auto results = session->ConsumeCommandResults();
            scene.commandResults.insert(scene.commandResults.end(), results.begin(), results.end());

            GameSnapshot incomingSnapshot;
            if (session->ConsumeLatestSnapshot(incomingSnapshot))
                scene.latestSnapshot = std::move(incomingSnapshot);
        }

        void DrawReadyGameplay(GameScene& scene, double dt, bool lockWorld)
        {
            std::unique_lock<std::recursive_mutex> worldLock;
            if (lockWorld)
                if (auto* mutex = GetWorldMutex())
                    worldLock = std::unique_lock<std::recursive_mutex>(*mutex);

            GameWorld* renderWorld = session != nullptr ? session->GetWorld() : nullptr;
            if (renderWorld != nullptr)
                renderWorld->DrawMap();
            else if (scene.latestSnapshot.IsValid())
                scene.render.DrawSnapshot(scene.latestSnapshot);

            scene.inputs.HandleInputs();
            scene.controller->Update(dt);

            std::vector<UiWidget*> widgets = scene.controller->GetUiWidgets();
            AppendDiagnostics(scene, widgets);

            if (worldLock.owns_lock())
                worldLock.unlock();
            scene.render.Draw(widgets, dt);
        }

        void AppendDiagnostics(GameScene& scene, std::vector<UiWidget*>& widgets)
        {
            if (session == nullptr)
                return;

            int pingMs = session->GetPingMs();
            std::string connectionStatus = session->GetConnectionStatus();
            if (pingMs >= 0)
            {
                scene.networkStatusLabel.ChangeText("Ping " + std::to_string(pingMs) + " ms");
                widgets.push_back(&scene.networkStatusLabel);
            }
            else if (!connectionStatus.empty() && connectionStatus != "Connected")
            {
                scene.networkStatusLabel.ChangeText(connectionStatus);
                widgets.push_back(&scene.networkStatusLabel);
            }
        }

        std::unique_ptr<IGameSession> session;
    };

    class SinglePlayerRuntimeLoop : public GameRuntimeLoopBase
    {
    public:
        using GameRuntimeLoopBase::GameRuntimeLoopBase;

        void Update(GameScene& scene, double dt) override
        {
            UpdateSessionAndResults(scene, dt);
            DrawReadyGameplay(scene, dt, false);
        }
    };

    class MultiplayerHostRuntimeLoop : public GameRuntimeLoopBase
    {
    public:
        using GameRuntimeLoopBase::GameRuntimeLoopBase;

        void Update(GameScene& scene, double dt) override
        {
            UpdateSessionAndResults(scene, dt);
            if (session != nullptr && !session->IsReadyForGameplay())
            {
                std::string status = session->GetConnectionStatus();
                DrawRuntimeLoadingScreen(status.empty() ? "Waiting for client map sync" : status);
                return;
            }

            DrawReadyGameplay(scene, dt, true);
        }
    };

    class MultiplayerClientRuntimeLoop : public GameRuntimeLoopBase
    {
    public:
        using GameRuntimeLoopBase::GameRuntimeLoopBase;

        void Update(GameScene& scene, double dt) override
        {
            UpdateSessionAndResults(scene, dt);
            if (session != nullptr && !session->IsReadyForGameplay())
            {
                std::string status = session->GetConnectionStatus();
                DrawRuntimeLoadingScreen(status.empty() ? "Syncing map" : status);
                return;
            }

            DrawReadyGameplay(scene, dt, true);
        }
    };
}

// Advances this object's state for one frame.
void GameScene::Update(double dt)
{
    if (game == nullptr || runtimeLoop == nullptr)
        return;

    if (runtimeLoop->IsConnectionClosed())
    {
        std::string status = runtimeLoop->GetConnectionStatus();
        if (status.empty())
            status = "Server closed the connection";
        Log::Msg("GameScene", "Network session closed: ", status);
        ShutdownActiveGame();

        auto statusEvent = std::make_shared<NetworkStatusEvent>();
        statusEvent->sender = this;
        statusEvent->message = "Multiplayer disconnected: " + status;
        broker->Broadcast(statusEvent);

        auto sceneEvent = std::make_shared<ChangeSceneEvent>();
        sceneEvent->sender = this;
        sceneEvent->sceneName = "MainScene";
        sceneEvent->previousSceneName = name;
        broker->Broadcast(sceneEvent);
        return;
    }

    runtimeLoop->Update(*this, dt);
    commandResults.clear();
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

// Handles the requested event or transfer.
void GameScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        for(auto& [name, system] : controller->systems)
        {
            system->UpdateUiWidgets(ptr->windowSize);
        }
        UpdateNetworkStatusWidget(ptr->windowSize);
    }

    auto sceneChange = std::dynamic_pointer_cast<ChangeSceneEvent>(e);
    if (sceneChange != nullptr && sceneChange->sceneName == "MainScene" && runtimeLoop != nullptr)
        ShutdownActiveGame();

    auto ptr2 = std::dynamic_pointer_cast<NewGameEvent>(e);
    if (ptr2 != nullptr)
    {
        StartNewGame(ptr2->name, ptr2->params);

        auto msg = std::make_shared<ChangeSceneEvent>();
        msg->sender = this;
        msg->sceneName = "GameScene";
        msg->previousSceneName = name;
        broker->Broadcast(msg);
    }

    auto ptr3 = std::dynamic_pointer_cast<LoadGameEvent>(e);
    if (ptr3 != nullptr)
    {
        if (!LoadGame(ptr3->name))
            return;

        auto msg = std::make_shared<ChangeSceneEvent>();
        msg->sender = this;
        msg->sceneName = "GameScene";
        msg->previousSceneName = name;
        broker->Broadcast(msg);
    }

    auto saveEvent = std::dynamic_pointer_cast<SaveGameEvent>(e);
    if (saveEvent != nullptr)
    {
        SaveGame(saveEvent->name);
        auto msg = std::make_shared<SaveListChangedEvent>();
        msg->sender = this;
        broker->Broadcast(msg);
    }

    auto hostEvent = std::dynamic_pointer_cast<HostMultiplayerGameEvent>(e);
    if (hostEvent != nullptr)
    {
        StartMultiplayerHost(hostEvent->name, hostEvent->params, hostEvent->port, hostEvent->transport);

        auto msg = std::make_shared<ChangeSceneEvent>();
        msg->sender = this;
        msg->sceneName = "GameScene";
        msg->previousSceneName = name;
        broker->Broadcast(msg);
    }

    auto joinEvent = std::dynamic_pointer_cast<JoinMultiplayerGameEvent>(e);
    if (joinEvent != nullptr)
    {
        StartMultiplayerClient(joinEvent->name, joinEvent->params, joinEvent->address, joinEvent->port, joinEvent->transport);

        auto msg = std::make_shared<ChangeSceneEvent>();
        msg->sender = this;
        msg->sceneName = "GameScene";
        msg->previousSceneName = name;
        broker->Broadcast(msg);
    }
}

// Initializes GameScene::StartNewGame.
void GameScene::StartNewGame(std::string name, MapParameters params)
{
    render.ClearLayers();
    game = std::make_unique<GameWorld>();
    std::string worldName = SanitizeSaveName(name);
    game->InitWorld(worldName, &render, params);
    runtimeLoop = std::make_unique<SinglePlayerRuntimeLoop>(std::make_unique<HostGameSession>(*game));
}

// Creates and hosts a LAN multiplayer world.
void GameScene::StartMultiplayerHost(std::string name, MapParameters params, unsigned short port, std::shared_ptr<IGameTransport> transport)
{
    render.ClearLayers();
    game = std::make_unique<GameWorld>();
    std::string worldName = SanitizeSaveName(name);
    game->InitMultiplayerWorld(worldName, &render, params, 0, true);
    if (transport == nullptr)
        transport = TcpGameTransport::CreateHost(port);
    bool requireRemoteSync = transport != nullptr && transport->IsConnected();
    Log::Msg("GameScene", "Starting multiplayer host world '", worldName, "' on port ", port);
    runtimeLoop = std::make_unique<MultiplayerHostRuntimeLoop>(
        std::make_unique<ThreadedGameSession>(std::make_unique<LocalhostHostSession>(*game, transport, 1, requireRemoteSync)));
}

// Joins a LAN multiplayer world with a local mirror.
void GameScene::StartMultiplayerClient(std::string name, MapParameters params, const std::string& address, unsigned short port, std::shared_ptr<IGameTransport> transport)
{
    render.ClearLayers();
    game = std::make_unique<GameWorld>();
    std::string worldName = SanitizeSaveName(name);
    game->InitMultiplayerWorld(worldName, &render, params, 1, false);
    if (transport == nullptr)
        transport = TcpGameTransport::CreateClient(address, port);
    Log::Msg("GameScene", "Starting multiplayer client world '", worldName, "' connecting to ", address, ":", port);
    runtimeLoop = std::make_unique<MultiplayerClientRuntimeLoop>(
        std::make_unique<ThreadedGameSession>(std::make_unique<LocalhostClientSession>(game.get(), transport, 1)));
}

// Loads the requested data into runtime state.
bool GameScene::LoadGame(std::string name)
{
    render.ClearLayers();
    game = std::make_unique<GameWorld>();
    std::string saveName = SanitizeSaveName(name);
    std::string filename{"saves/" + saveName + ".save"};
    if (game->LoadFromFile(filename, &render))
    {
        runtimeLoop = std::make_unique<SinglePlayerRuntimeLoop>(std::make_unique<HostGameSession>(*game));
        Log::Msg("GameScene", "Save ", saveName, " loaded!");
        return true;
    }
    else
    {
        Log::Msg("GameScene", "Failed to load save ", saveName);
        runtimeLoop = nullptr;
        game = nullptr;
        return false;
    }
}

// Serializes current runtime state.
void GameScene::SaveGame(std::string saveName)
{
    if (game == nullptr)
        return;

    std::unique_lock<std::recursive_mutex> worldLock;
    if (runtimeLoop != nullptr)
        if (auto* mutex = runtimeLoop->GetWorldMutex())
            worldLock = std::unique_lock<std::recursive_mutex>(*mutex);

    saveName = saveName.empty() ? game->worldName : saveName;
    saveName = SanitizeSaveName(saveName);
    game->worldName = saveName;

    std::string filename{"saves/" + saveName + ".save"};
    std::filesystem::create_directories("saves");
    if (!game->SaveToFile(filename))
        Log::Msg("GameScene", "Failed to save ", filename);
    else
        Log::Msg("GameScene", "Saved ", filename);
}

// Sends a local player's intent to the active session authority.
std::uint64_t GameScene::SubmitLocalCommand(const GameCommand& command)
{
    if (runtimeLoop != nullptr)
        return runtimeLoop->SubmitCommand(command);
    return 0;
}

// Returns command results received from the active session.
std::vector<GameCommandResult> GameScene::ConsumeCommandResults()
{
    std::vector<GameCommandResult> results = std::move(commandResults);
    commandResults.clear();
    return results;
}

// Tears down the active runtime and closes any owned network transport.
void GameScene::ShutdownActiveGame()
{
    runtimeLoop.reset();
    game.reset();
    latestSnapshot = GameSnapshot{};
    commandResults.clear();
    render.ClearLayers();
    Log::Msg("GameScene", "Active game session shut down");
}

// Keeps the multiplayer diagnostics label pinned to the top-right corner.
void GameScene::UpdateNetworkStatusWidget(Vec2i windowSize)
{
    networkStatusLabel.ChangeSize(190, 28);
    networkStatusLabel.ChangePosition(std::max(8, windowSize.x - 205), 10);
}

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
