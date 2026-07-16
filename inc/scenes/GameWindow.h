#ifndef GAMEWINDOW_H
#define GAMEWINDOW_H

#include "ui/AudioSystem.h"
#include "ui/GuiHandler.h"
#include "core/Events.h"

// Base scene that owns a renderer and receives events from the window broker.
class Scene : public EventClient
{
    public:
    // Updates and draws the scene for one frame.
    virtual void Update(double dt) = 0;
    // Called by GameWindow each time this scene becomes the active scene.
    virtual void OnActivated() {}

    std::string   name;
    std::string   previousSceneName;
    Renderer      render;
    AudioSystem*  audioSystem{nullptr};  // points to GameWindow::audio; set by AddScene<T>()
};

// Raylib application shell that owns scenes, window state and the main loop.
class GameWindow : public EventBroker
{
    public:

    // Handles global events such as quit, fullscreen and scene switching.
    void HandleEvent(std::shared_ptr<Event>) override;

    // Creates a scene, registers it in the scene map and subscribes it to events.
    template <typename T> void AddScene(std::string name)
    {
        static_assert(std::is_base_of<Scene, T>::value);

        auto scene = std::make_shared<T>();
        scene->broker      = this;
        scene->name        = name;
        scene->audioSystem = &audio;
        scenes.insert({name, scene});

        AddClient(name, scene.get());
    }

    // Updates the currently active scene.
    inline void Update(double dt)
    {
        activeScene->Update(dt);
    }

    // Broadcasts a resize event when the render size changes.
    void UpdateWindowSize();

    // Initializes Raylib, creates scenes and starts the game loop.
    void LaunchGame();

    // Runs the frame loop until a quit event is received.
    void MainLoop();

    // Activates a registered scene and records where navigation came from.
    inline void ChangeScene(std::string name, std::string previousSceneName)
    {
        activeScene = scenes[name];
        activeScene->previousSceneName = previousSceneName;
        // Central input-gate reset (see IGuiHandler): every activated scene
        // starts with its GUI input gated until it has presented a frame and
        // ESC is released — no scene can react to the same key edge that
        // caused this very transition. Done here, once, so individual scenes
        // can't forget it.
        if (auto* guiHandler = dynamic_cast<IGuiHandler*>(activeScene.get()))
            guiHandler->ResetGuiInputGate();
        activeScene->OnActivated();
    }

    std::map<std::string, std::shared_ptr<Scene>> scenes;
    std::shared_ptr<Scene> activeScene;

    AudioSystem audio;

    bool isRunning{true};
    const std::string tag{"GameWindow"};
    Vec2i lastWindowSize{};
};
#endif
