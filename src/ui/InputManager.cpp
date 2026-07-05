#include "ui/InputManager.h"

void InputManager::Poll()
{
    // Update mouse position
    mouseX = GetMouseX();
    mouseY = GetMouseY();

    // Poll keyboard inputs
    PollKeyboardInput();

    // Poll mouse inputs
    PollMouseInput();
}

void InputManager::AddSubscriber(std::shared_ptr<IInputSubscriber> subscriber)
{
    subscribers.push_back(subscriber);
}

void InputManager::RemoveSubscriber(IInputSubscriber* subscriber)
{
    // TODO: Remove subscriber from list
}

void InputManager::PollKeyboardInput()
{
    // TODO: Implement raylib IsKeyPressed/IsKeyDown polling
    // For now: placeholder - would poll all input keys and dispatch to matching subscribers
}

void InputManager::PollMouseInput()
{
    // TODO: Implement raylib IsMouseButtonPressed/Down polling
    // For now: placeholder - would poll mouse buttons and dispatch to matching subscribers
}

void InputManager::DispatchEvent(const InputEvent& event)
{
    // Iterate registered subscribers and call trigger on matching ones
    for (auto& subscriber : subscribers)
    {
        if (subscriber && subscriber->Matches(event.type, event.key))
        {
            subscriber->Trigger(event);
            if (event.consumed)
                break;  // Event consumed, stop dispatch
        }
    }
}
