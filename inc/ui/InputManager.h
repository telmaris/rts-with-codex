#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <functional>
#include <vector>
#include <memory>

// Input event types
enum class InputType
{
    KeyPressed,
    KeyReleased,
    KeyDown,
    MouseButtonPressed,
    MouseButtonReleased,
    MouseButtonDown,
    MouseScroll,
    MouseMove
};

// Input event data
struct InputEvent
{
    InputType type;
    int key = 0;  // For keyboard events
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float scrollDelta = 0.0f;  // For scroll events
    bool consumed = false;  // Can be set to true to prevent further processing
};

// Base interface for input subscribers
class IInputSubscriber
{
public:
    virtual ~IInputSubscriber() = default;
    virtual bool Matches(InputType type, int key) const = 0;
    virtual void Trigger(const InputEvent& event) = 0;
    virtual void Register(class InputManager& manager) = 0;
    virtual void Unregister(class InputManager& manager) = 0;
};

// Template subscriber for specific input type and key combination
template<InputType T, int K>
class InputEventSubscriber : public IInputSubscriber
{
public:
    using Callback = std::function<void(const InputEvent&)>;

    InputEventSubscriber() = default;
    explicit InputEventSubscriber(Callback cb) : callback(cb) {}

    void SetCallback(Callback cb) { callback = cb; }

    bool Matches(InputType type, int key) const override
    {
        return type == T && key == K;
    }

    void Trigger(const InputEvent& event) override
    {
        if (callback) callback(event);
    }

    void Register(InputManager& manager) override;
    void Unregister(InputManager& manager) override;

private:
    Callback callback;
};

// Global input manager with observer pattern
class InputManager
{
public:
    InputManager() = default;

    // Poll input state from raylib and dispatch to subscribers
    void Poll();

    // Register/unregister a subscriber (called by InputEventSubscriber RAII)
    void AddSubscriber(std::shared_ptr<IInputSubscriber> subscriber);
    void RemoveSubscriber(IInputSubscriber* subscriber);

    // Get mouse position
    float GetMouseX() const { return mouseX; }
    float GetMouseY() const { return mouseY; }

private:
    std::vector<std::shared_ptr<IInputSubscriber>> subscribers;
    float mouseX = 0.0f;
    float mouseY = 0.0f;

    // Helper methods for polling specific input types
    void PollKeyboardInput();
    void PollMouseInput();
    void DispatchEvent(const InputEvent& event);
};

// Template implementation
template<InputType T, int K>
void InputEventSubscriber<T, K>::Register(InputManager& manager)
{
    manager.AddSubscriber(std::make_shared<InputEventSubscriber<T, K>>(*this));
}

template<InputType T, int K>
void InputEventSubscriber<T, K>::Unregister(InputManager& manager)
{
    manager.RemoveSubscriber(this);
}

#endif
