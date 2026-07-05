#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <functional>
#include <vector>

// Input event types recognized by the observer-pattern input system.
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

// Snapshot of one input occurrence delivered to matching subscribers.
struct InputEvent
{
    InputType type;
    int key = 0;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float scrollDelta = 0.0f;
    bool consumed = false;  // Set true by a handler to stop further dispatch this frame.
};

// Non-owning registration interface implemented by InputEventSubscriber<T,K>.
class IInputSubscriber
{
public:
    virtual ~IInputSubscriber() = default;
    virtual InputType GetType() const = 0;
    virtual int GetKey() const = 0;
    virtual void Trigger(const InputEvent& event) = 0;
};

// Process-wide dispatcher: the only place allowed to call raylib's
// IsKeyPressed/IsKeyDown/IsMouseButton*/GetMouseWheelMove (see Poll()).
// Input is a hardware-level concept (one keyboard/mouse), so a single
// instance is appropriate here — unlike PathingService, which is owned
// per-GameWorld because simulations must stay independent.
class InputManager
{
public:
    static InputManager& Instance();

    // Polls raylib input state once per frame and dispatches to subscribers.
    void Poll();

    // Registers/removes a subscriber. Called by InputEventSubscriber's RAII lifecycle.
    void AddSubscriber(IInputSubscriber* subscriber);
    void RemoveSubscriber(IInputSubscriber* subscriber);

    float GetMouseX() const { return mouseX; }
    float GetMouseY() const { return mouseY; }

private:
    InputManager() = default;

    void PollKeyboardInput();
    void PollMouseInput();
    void DispatchEvent(const InputEvent& event);

    std::vector<IInputSubscriber*> subscribers;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
};

// RAII observer for one (InputType, key) combination. Registers itself with
// the global InputManager at construction and unregisters at destruction, so
// aggregating one as a plain member is enough to manage the subscription's
// lifetime automatically:
//
//   class GuiPanel {
//       InputEventSubscriber<InputType::KeyPressed, KEY_ESCAPE> escClose{
//           [this](const InputEvent&) { Close(); }};
//   };
//
// Not copyable: copying would register the same callback under two
// addresses, and the destructor of one copy would unregister a pointer the
// other copy still expects to be live.
template<InputType T, int K>
class InputEventSubscriber : public IInputSubscriber
{
public:
    using Callback = std::function<void(const InputEvent&)>;

    InputEventSubscriber() { InputManager::Instance().AddSubscriber(this); }
    explicit InputEventSubscriber(Callback cb) : callback(std::move(cb))
    {
        InputManager::Instance().AddSubscriber(this);
    }
    ~InputEventSubscriber() override { InputManager::Instance().RemoveSubscriber(this); }

    InputEventSubscriber(const InputEventSubscriber&) = delete;
    InputEventSubscriber& operator=(const InputEventSubscriber&) = delete;

    void SetCallback(Callback cb) { callback = std::move(cb); }

    InputType GetType() const override { return T; }
    int GetKey() const override { return K; }

    void Trigger(const InputEvent& event) override
    {
        if (callback)
            callback(event);
    }

private:
    Callback callback;
};

#endif
