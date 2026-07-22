#include "scenes/Scenes.h"
#include "scenes/SceneUtils.h"
#include "multiplayer/TcpGameTransport.h"
#include "ui/GuiController.h"

// InputProcessor's polling methods live here since GameScene is its only
// owner (InputProcessor inputs; is a GameScene member).
bool InputProcessor::IsActionPressed(int action)
{
    bool result = false;

    if (action > ACTION_NULL && action < MAX_ACTION)
    {
        auto input = actionInputs[action];
        result = ((input.key >= 0) && InputManager::IsKeyPressed(input.key)) ||
                 ((input.button >= 0) && InputManager::IsMouseButtonPressed(input.button));
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
        result = ((input.key >= 0) && InputManager::IsKeyReleased(input.key)) ||
                 ((input.button >= 0) && InputManager::IsMouseButtonReleased(input.button));
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
        result = ((input.key >= 0) && InputManager::IsKeyDown(input.key)) ||
                 ((input.button >= 0) && InputManager::IsMouseButtonDown(input.button));
    }

    return result;
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
    controller->AddSystem<TechGuiSystem>("tech");
    controller->AddSystem<RosterGuiSystem>("roster");
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
        ClearBackground(Color{20, 14, 10, 255});
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

            // Gated through IGuiHandler (GameScene::HandleGuiInput forwards to
            // inputs.HandleInputs()) so the first frame after a scene switch
            // never re-consumes the key edge that caused the switch.
            scene.ProcessGuiInput(dt);
            scene.controller->Update(dt);

            std::vector<UiWidget*> widgets = scene.controller->GetUiWidgets();
            AppendDiagnostics(scene, widgets);

            // Keep the world lock held through widget rendering: every widget's
            // Update() (called from render.DrawContent) reads live simulation state —
            // garrison divisions, battle markers, order arrows — so releasing the
            // lock before drawing races the sim thread mutating those vectors (with
            // unique_ptr storage a torn read dereferences a freed pointer → garbage
            // colours, vanishing markers, flickering arrows).
            //
            // But we must NOT hold it across the present: EndDrawing() blocks on
            // vsync / the frame cap (FLAG_VSYNC_HINT + SetTargetFPS), and holding
            // worldMutex across that ~frame-long wait starves the 100 Hz background
            // sim thread — the whole simulation crawls (build/production/transport
            // stall). So issue all draw calls under the lock via DrawContent(),
            // then release the lock, then PresentFrame() unlocked.
            scene.render.DrawContent(widgets, dt);

            // Win/lose banner — driven by the deterministic sim state (a player is
            // defeated when its HQ is captured). Read from the live world (host/SP);
            // MP clients rendering from a snapshot show nothing here yet. Drawn under
            // the lock and before the present so it lands in this frame.
            GameWorld* stateWorld = renderWorld != nullptr ? renderWorld : scene.game.get();
            if (stateWorld != nullptr)
            {
                const int localId = stateWorld->GetLocalPlayerId();
                const int victor = stateWorld->GetVictorPlayerId();
                const char* banner = nullptr;
                Color col{};
                if (stateWorld->IsPlayerDefeated(localId)) { banner = "DEFEAT"; col = Color{224, 92, 92, 255}; }
                else if (victor == localId) { banner = "VICTORY"; col = Color{130, 224, 156, 255}; }
                if (banner != nullptr)
                {
                    int sw = GetScreenWidth(), sh = GetScreenHeight();
                    DrawRectangle(0, sh / 2 - 70, sw, 140, Color{0, 0, 0, 190});
                    int fs = 72;
                    int tw = MeasureText(banner, fs);
                    DrawText(banner, sw / 2 - tw / 2, sh / 2 - fs / 2, fs, col);
                    const char* hint = "Press Esc for the menu";
                    int hw = MeasureText(hint, 22);
                    DrawText(hint, sw / 2 - hw / 2, sh / 2 + 44, 22, Color{210, 214, 220, 235});
                }
            }

            // Release the world lock BEFORE presenting so the sim thread runs
            // freely during the vsync / frame-cap wait inside EndDrawing().
            if (worldLock.owns_lock())
                worldLock.unlock();

            scene.render.PresentFrame();
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

    // Unified host session loop (SP + MP host both use HostSession with background thread)
    class HostRuntimeLoop : public GameRuntimeLoopBase
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

    if (audioSystem != nullptr && game != nullptr)
    {
        int localId = game->GetLocalPlayerId();
        for (const auto& result : commandResults)
        {
            if (result.playerId != localId)
                continue;
            if (!result.accepted)
                audioSystem->PlaySound("error");
        }

        auto pit = game->GetPlayerHandler().players.find(localId);
        if (pit != game->GetPlayerHandler().players.end())
        {
            const Player* p = pit->second.get();
            std::size_t techCount  = p->technologies.GetUnlocked().size();
            std::size_t focusCount = p->focuses.GetUnlocked().size();
            if (techCount > prevUnlockedTechCount || focusCount > prevUnlockedFocusCount)
                audioSystem->PlaySound("notification");
            prevUnlockedTechCount  = techCount;
            prevUnlockedFocusCount = focusCount;
        }

        std::set<int> incomingUnitIds;
        for (const auto& [instanceId, unit] : game->GetDeployedUnits())
        {
            if (unit.ownerPlayerId != localId && unit.routeToPlayerId == localId &&
                unit.state != BattleUnitState::Dying)
                incomingUnitIds.insert(instanceId);
        }
        for (int instanceId : incomingUnitIds)
        {
            if (!knownIncomingUnitIds.contains(instanceId))
            {
                audioSystem->PlaySound("notification");
                break;
            }
        }
        knownIncomingUnitIds = std::move(incomingUnitIds);
    }

    commandResults.clear();
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
    game->InitWorld(worldName, &render, audioSystem, params);
    runtimeLoop = std::make_unique<HostRuntimeLoop>(std::make_unique<HostSession>(*game));
    prevUnlockedTechCount  = 0;
    prevUnlockedFocusCount = 0;
    knownIncomingUnitIds.clear();
    if (audioSystem != nullptr)
        audioSystem->PlayMusic("gameplay");
}

// Creates and hosts a LAN multiplayer world.
void GameScene::StartMultiplayerHost(std::string name, MapParameters params, unsigned short port, std::shared_ptr<IGameTransport> transport)
{
    render.ClearLayers();
    game = std::make_unique<GameWorld>();
    std::string worldName = SanitizeSaveName(name);
    game->InitMultiplayerWorld(worldName, &render, audioSystem, params, 0, true);
    knownIncomingUnitIds.clear();
    if (transport == nullptr)
        transport = TcpGameTransport::CreateHost(port);
    bool requireRemoteSync = transport != nullptr && transport->IsConnected();
    Log::Msg("GameScene", "Starting multiplayer host world '", worldName, "' on port ", port);
    runtimeLoop = std::make_unique<HostRuntimeLoop>(
        std::make_unique<HostSession>(*game, transport, 1, requireRemoteSync));
    if (audioSystem != nullptr)
        audioSystem->PlayMusic("gameplay");
}

// Joins a LAN multiplayer world with a local mirror.
void GameScene::StartMultiplayerClient(std::string name, MapParameters params, const std::string& address, unsigned short port, std::shared_ptr<IGameTransport> transport)
{
    render.ClearLayers();
    game = std::make_unique<GameWorld>();
    std::string worldName = SanitizeSaveName(name);
    game->InitMultiplayerWorld(worldName, &render, audioSystem, params, 1, false);
    knownIncomingUnitIds.clear();
    if (transport == nullptr)
        transport = TcpGameTransport::CreateClient(address, port);
    Log::Msg("GameScene", "Starting multiplayer client world '", worldName, "' connecting to ", address, ":", port);
    runtimeLoop = std::make_unique<MultiplayerClientRuntimeLoop>(
        std::make_unique<ClientSession>(game.get(), transport, 1));
    if (audioSystem != nullptr)
        audioSystem->PlayMusic("gameplay");
}

// Loads the requested data into runtime state.
bool GameScene::LoadGame(std::string name)
{
    render.ClearLayers();
    game = std::make_unique<GameWorld>();
    std::string saveName = SanitizeSaveName(name);
    std::string filename{"saves/" + saveName + ".save"};
    if (game->LoadFromFile(filename, &render, audioSystem))
    {
        knownIncomingUnitIds.clear();
        runtimeLoop = std::make_unique<HostRuntimeLoop>(std::make_unique<HostSession>(*game));
        {
            auto pit = game->GetPlayerHandler().players.find(game->GetLocalPlayerId());
            if (pit != game->GetPlayerHandler().players.end())
            {
                prevUnlockedTechCount  = pit->second->technologies.GetUnlocked().size();
                prevUnlockedFocusCount = pit->second->focuses.GetUnlocked().size();
            }
        }
        Log::Msg("GameScene", "Save ", saveName, " loaded!");
        if (audioSystem != nullptr)
            audioSystem->PlayMusic("gameplay");
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
    knownIncomingUnitIds.clear();
    render.ClearLayers();
    Log::Msg("GameScene", "Active game session shut down");
}

// Keeps the multiplayer diagnostics label pinned to the top-right corner.
void GameScene::UpdateNetworkStatusWidget(Vec2i windowSize)
{
    networkStatusLabel.ChangeSize(190, 28);
    networkStatusLabel.ChangePosition(std::max(8, windowSize.x - 205), 10);
}
