#ifndef EVENTS_H
#define EVENTS_H

#include "Utils.h"
#include "GameWorld.h"

class EventBroker;
class Event;

class EventClient
{
    public:

    virtual ~EventClient() = default;

    virtual void HandleEvent(std::shared_ptr<Event>) {}

    EventBroker* broker;
};

class EventBroker
{
    public:
    virtual ~EventBroker() = default;

    inline void AddClient(std::string name, EventClient* client)
    {
        clients[name] = client;
    }

    inline void Broadcast(std::shared_ptr<Event> e)
    {
        HandleEvent(e);

        for(auto [name, client] : clients)
        {
            client->HandleEvent(e);
        }
    }

    // dodać wysyłanie do konkretnego adresata

    virtual void HandleEvent(std::shared_ptr<Event>) {}

    std::map<std::string, EventClient*> clients;
};

struct Event
{
    virtual ~Event() = default;

    EventClient* sender;
    std::string  msgName;
};

// ====== EVENT MESSAGES =========

struct QuitGameEvent : Event
{
    QuitGameEvent() {msgName = "QuitGameEvent";}
};

struct ChangeSceneEvent : Event
{
    ChangeSceneEvent() {msgName = "ChangeSceneEvent";}
    std::string sceneName;
    std::string previousSceneName;
};

struct WindowSizeChangedEvent : Event
{
    WindowSizeChangedEvent() {msgName = "WindowSizeChangedEvent";}

    Vec2i windowSize;
};

struct ToggleFullscreenEvent : Event
{
    ToggleFullscreenEvent() {msgName = "ToggleFullscreenEvent";}
};

struct NewGameEvent : Event
{
    NewGameEvent() {msgName = "NewGameEvent";}

    MapParameters params;
    std::string name;
};

struct LoadGameEvent : Event
{
    LoadGameEvent() {msgName = "LoadGameEvent";}

    std::string name;
};

#endif