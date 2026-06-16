#ifndef GAMEWINDOW_H
#define GAMEWINDOW_H

#include "Events.h"

class Scene : public EventClient
{
    public:
    virtual void Update(double dt) = 0;

    std::string name;
    std::string previousSceneName;
    Renderer render;
};


class GameWindow : public EventBroker
{
    public:

    void HandleEvent(std::shared_ptr<Event>) override;

    template <typename T> void AddScene(std::string name)
    {
        static_assert(std::is_base_of<Scene, T>::value);

        auto scene = std::make_shared<T>();
        scene->broker = this;
        scene->name = name;
        scenes.insert({name, scene});
        
        AddClient(name, scene.get());
    }

    inline void Update(double dt)
    {
        activeScene->Update(dt);
    }

    void UpdateWindowSize();

    void LaunchGame();

    void MainLoop();

    // 1) funkcje związane z zarządzaniem oknem i FPS
    // 2) agregacja i zarządzanie logiką gry (class Game)
    // 3) agregacja i zarządzanie GUI
    // 4) zarządzanie renderem assetów Game (tekstury i dźwięk)
    // 5) przechwytywanie i przekazywanie Inputu z myszy/klawiatury

    inline void ChangeScene(std::string name, std::string previousSceneName) 
    {
        activeScene = scenes[name];
        activeScene->previousSceneName = previousSceneName;
    }

    std::map<std::string, std::shared_ptr<Scene>> scenes;
    std::shared_ptr<Scene> activeScene;

    bool isRunning = true;
    const std::string tag{"GameWindow"};
    Vec2i lastWindowSize;
};
#endif