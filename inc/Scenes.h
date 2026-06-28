#ifndef SCENES_H
#define SCENES_H

#include "GameWindow.h"
#include "GameSession.h"
#include "Gui.h"
#include "Input.h"
#include "GuiController.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class TcpGameTransport;
class GameScene;

// Strategy object for mode-specific gameplay update/render details.
class IGameRuntimeLoop
{
    public:
        virtual ~IGameRuntimeLoop() = default;
        virtual void Update(GameScene& scene, double dt) = 0;
        virtual std::uint64_t SubmitCommand(const GameCommand& command) = 0;
        virtual std::vector<GameCommandResult> ConsumeCommandResults() = 0;
        virtual bool IsConnectionClosed() const = 0;
        virtual std::string GetConnectionStatus() const = 0;
        virtual std::recursive_mutex* GetWorldMutex() = 0;
};

// Main menu scene with textured background and primary navigation buttons.
class MainMenuScene : public Scene
{
    public: 

    MainMenuScene();

    // Updates main menu widgets.
    void Update(double dt) override;

    // Opens the new game scene.
    void OnNewGamePressed();
    // Opens the multiplayer scene.
    void OnMultiplayerPressed();
    // Opens the load game scene.
    void OnLoadGamePressed();
    // Opens the options scene.
    void OnOptionsPressed();
    // Requests application shutdown.
    void OnQuitPressed();

    // Handles resize and scene navigation events.
    void HandleEvent(std::shared_ptr<Event>) override;

    VBox buttonsColumn;
    UiImage menuGraphic;
    UiLabel statusLabel;
    double statusTimer{0.0};
};

// Options scene for display and audio preferences.
class OptionsScene : public Scene
{
    public:

        OptionsScene();
        // Updates options widgets.
        void Update(double dt) override;
        // Handles resize and navigation events.
        void HandleEvent(std::shared_ptr<Event>) override;

        // Returns to the previous scene.
        void OnBackPressed();

        UiButton backButton;
        CheckBox fullScreenCheckBox;
        SliderBar masterVolume;
};

// New game form scene.
class NewGameScene : public Scene
{
    public:

        NewGameScene();
        // Updates new game form widgets.
        void Update(double dt) override;
        // Handles resize and navigation events.
        void HandleEvent(std::shared_ptr<Event>) override;

        // Returns to the previous scene.
        void OnBackPressed();
        // Starts a new generated world.
        void OnStartPressed();
        // Cycles map size preset.
        void OnSizePressed();
        // Cycles placeholder AI difficulty.
        void OnDifficultyPressed();
        // Refreshes option labels from current values.
        void RefreshOptionLabels();

        UiButton backButton;
        TextBox gameName;
        UiButton sizeButton;
        UiButton difficultyButton;
        SliderBar resourceDensity;
        SliderBar resourceFieldSize;
        SliderBar resourceRichness;
        SliderBar aiOpponents;
        CheckBox debugMode;
        UiButton startGame;
        MapSizePreset selectedSize{MapSizePreset::S};
        int selectedDifficulty{0};
};

// LAN multiplayer entry scene.
class MultiplayerScene : public Scene
{
    public:

        MultiplayerScene();
        // Updates multiplayer form widgets.
        void Update(double dt) override;
        // Handles resize and navigation events.
        void HandleEvent(std::shared_ptr<Event>) override;

        // Returns to the main menu.
        void OnBackPressed();
        // Starts a local host session.
        void OnHostPressed();
        // Joins a host by IPv4 address.
        void OnJoinPressed();
        // Starts the hosted lobby game.
        void OnStartPressed();
        // Opens host-only multiplayer world settings.
        void OnGameSettingsPressed();
        // Closes host-only multiplayer world settings.
        void OnCloseGameSettingsPressed();
        // Cycles hosted game size preset.
        void OnMultiplayerSizePressed();
        // Cycles hosted game AI difficulty.
        void OnMultiplayerDifficultyPressed();
        // Sends one lobby chat message.
        void OnSendChatPressed();
        void AddLobbyLine(const std::string& line, Color color = Color{190, 205, 224, 255});
        void ResetLobby();
        void UpdateLobbyMessages(double dt);
        void DrawConnectionDialog() const;
        void DrawLobbyLog() const;
        void DrawLobbyPlayerPanels() const;
        void DrawGameSettingsPanel() const;
        void RefreshMultiplayerLabels();
        void SaveMultiplayerSettings() const;
        MapParameters BuildLobbyMapParameters() const;
        void BroadcastLobbyState(const std::string& infoMessage = "");
        bool ApplyLobbyState(const std::string& payload);
        void MaybeBroadcastSettingsChange(const std::string& infoMessage = "");

        UiButton backButton;
        UiLabel nicknameLabel;
        TextBox nickname;
        UiLabel sessionNameLabel;
        TextBox sessionName;
        UiLabel addressLabel;
        TextBox address;
        UiLabel portLabel;
        TextBox port;
        SliderBar aiOpponents;
        UiButton hostButton;
        UiButton joinButton;
        UiButton startButton;
        UiButton gameSettingsButton;
        UiButton closeSettingsButton;
        UiButton multiplayerSizeButton;
        UiButton multiplayerDifficultyButton;
        SliderBar multiplayerResourceDensity;
        SliderBar multiplayerResourceFieldSize;
        SliderBar multiplayerResourceRichness;
        CheckBox multiplayerDebugMode;
        TextBox chatInput;
        UiButton sendChatButton;
        std::shared_ptr<TcpGameTransport> lobbyTransport;
        std::vector<std::pair<std::string, Color>> lobbyLines;
        std::string lobbyNickname{"Player"};
        std::string remoteLobbyNickname;
        std::string lobbySessionName{"lan_test"};
        std::string lobbyAddress{"127.0.0.1"};
        unsigned short lobbyPort{27015};
        int lobbyAiOpponentCount{0};
        MapSizePreset lobbySizePreset{MapSizePreset::S};
        int lobbyDifficulty{0};
        bool showGameSettings{false};
        bool isLobbyHost{false};
        bool lobbyActive{false};
        bool connectingToLobby{false};
        bool announcedConnection{false};
        bool hasRemoteLobbyPlayer{false};
        int lobbyChatScroll{0};
        double connectionWaitTimer{0.0};
        double connectionMessageTimer{0.0};
        std::string connectionMessage;
        std::string lastBroadcastLobbyState;
};

// Save selection scene used to load existing games.
class LoadGameScene : public Scene
{
    public:

        LoadGameScene();
        // Updates save-list widgets.
        void Update(double dt) override;
        // Handles resize, save-list and navigation events.
        void HandleEvent(std::shared_ptr<Event>) override;

        // Returns to the previous scene.
        void OnBackPressed();
        // Rebuilds the visible save-file list.
        void LoadSaves();
        // Emits a load request for the selected save.
        void OnSavePressed(std::string);

        UiButton backButton;
        VBox saveButtons;
};

// Save selection scene used to create or overwrite save files.
class SaveGameScene : public Scene
{
    public:

        SaveGameScene();
        // Updates save form, save list and overwrite confirmation.
        void Update(double dt) override;
        // Handles resize, save-list and navigation events.
        void HandleEvent(std::shared_ptr<Event>) override;

        // Returns to the previous scene.
        void OnBackPressed();
        // Rebuilds the visible save-file list.
        void LoadSaves();
        // Selects an existing save for overwrite confirmation.
        void OnSavePressed(std::string);
        // Saves into the name typed in the text box.
        void OnNewSavePressed();
        // Confirms overwriting the selected save.
        void OnConfirmOverwrite();
        // Cancels overwrite confirmation.
        void OnCancelOverwrite();

        UiButton backButton;
        TextBox saveName;
        UiButton newSaveButton;
        VBox saveButtons;
        UiButton confirmOverwriteButton;
        UiButton cancelOverwriteButton;
        std::string pendingOverwriteSave;
        bool overwriteConfirmationVisible{false};
};

// Live gameplay scene owning world simulation, renderer and GUI controller.
class GameScene : public Scene
{
    public:

        GameScene();
        // Advances input, world simulation and rendering.
        void Update(double dt) override;
        // Handles game lifecycle and menu events.
        void HandleEvent(std::shared_ptr<Event>) override;


        // Creates and enters a generated world.
        void StartNewGame(std::string, MapParameters);
        // Creates and hosts a LAN multiplayer world.
        void StartMultiplayerHost(std::string, MapParameters, unsigned short port, std::shared_ptr<IGameTransport> transport = nullptr);
        // Joins a LAN multiplayer world with a local mirror.
        void StartMultiplayerClient(std::string, MapParameters, const std::string& address, unsigned short port, std::shared_ptr<IGameTransport> transport = nullptr);
        // Loads a save file into the gameplay scene.
        bool LoadGame(std::string);
        // Saves the current gameplay state.
        void SaveGame(std::string saveName = "");
        // Sends a local player's intent to the active session authority.
        std::uint64_t SubmitLocalCommand(const GameCommand& command);
        // Returns command results received from the active session.
        std::vector<GameCommandResult> ConsumeCommandResults();
        // Clears the active runtime and closes owned network transports.
        void ShutdownActiveGame();
        // Repositions the small multiplayer diagnostics label.
        void UpdateNetworkStatusWidget(Vec2i windowSize);

        std::unique_ptr<GameWorld> game{nullptr};
        std::unique_ptr<IGameRuntimeLoop> runtimeLoop{nullptr};
        std::unique_ptr<GuiController> controller{nullptr};
        InputProcessor inputs;
        UiLabel networkStatusLabel;
        GameSnapshot latestSnapshot;
        std::vector<GameCommandResult> commandResults;
};

// In-game pause/menu scene.
class GameMenuScene : public Scene
{
    public:

        GameMenuScene();
        // Updates menu widgets.
        void Update(double dt) override;
        // Handles resize and navigation events.
        void HandleEvent(std::shared_ptr<Event>) override;

        // Returns to gameplay.
        void OnBackPressed();
        // Opens options.
        void OnOptionsPressed();
        // Returns to main menu.
        void OnMainMenuPressed();
        // Opens save game scene.
        void OnSaveGamePressed();
        // Opens load game scene.
        void OnLoadGamePressed();
        // Requests application shutdown.
        void OnQuitPressed();

        VBox vbox;
};

#endif
