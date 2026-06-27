#ifndef GAMEWINDOW_H
#define GAMEWINDOW_H

#include "Events.h"

// Base scene that owns a renderer and receives events from the window broker.
class Scene : public EventClient
{
    public:
    // Updates and draws the scene for one frame.
    virtual void Update(double dt) = 0;

    std::string name;
    std::string previousSceneName;
    Renderer render;
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
        scene->broker = this;
        scene->name = name;
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
    }

    std::map<std::string, std::shared_ptr<Scene>> scenes;
    std::shared_ptr<Scene> activeScene;

    bool isRunning{true};
    const std::string tag{"GameWindow"};
    Vec2i lastWindowSize{};
};
#endif
