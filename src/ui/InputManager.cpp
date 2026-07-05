#include "ui/InputManager.h"

void InputManager::Poll()
{
    // TODO: Implement raylib input polling
    // For now: placeholder
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
    // TODO: Poll keyboard state using raylib IsKeyPressed/IsKeyDown
}

void InputManager::PollMouseInput()
{
    // TODO: Poll mouse state using raylib IsMouseButtonPressed/Down
    // Update mouseX, mouseY
}

void InputManager::DispatchEvent(const InputEvent& event)
{
    // TODO: Iterate subscribers, call Trigger on matching ones
}
