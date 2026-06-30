#ifndef INPUT_H
#define INPUT_H

#include "raylib.h"

class GuiController;

// Logical input actions consumed by GUI controller systems.
enum InputMap
{
    ACTION_NULL = 0,

    CLOSE_TOP_GUI,
    OPEN_BUILD_GUI,
    OPEN_ROAD_BUILD_GUI,
    OPEN_DESTROY_GUI,
    OPEN_HEADQUARTERS_GUI,
    OPEN_STATS_GUI,
    OPEN_FOCUS_GUI,
    CENTER_CAMERA_ON_HEADQUARTERS,
    DEBUG_GRANT_RESOURCES,

    LEFT_BUTTON_DOWN,
    LEFT_BUTTON_UP,
    RIGHT_BUTTON_DOWN,
    RIGHT_BUTTON_UP,
    MIDDLE_BUTTON_DOWN,
    MIDDLE_BUTTON_UP,
    MOUSE_SCROLL,


    MAX_ACTION
};

// Physical key or mouse binding for one logical action.
struct ActionInput
{
    int key = -1;
    int button = -1;
};

// Polls Raylib input and exposes action-oriented queries.
struct InputProcessor
{
    // Assigns default bindings and target controller.
    inline void Init(GuiController*);
    // Dispatches currently active input actions to the controller.
    void HandleInputs();
    // Returns true on the frame an action becomes active.
    bool IsActionPressed(int action);
    // Returns true on the frame an action is released.
    bool IsActionReleased(int action);
    // Returns true while an action is held.
    bool IsActionDown(int action);


    ActionInput actionInputs[MAX_ACTION];

    GuiController* controller{nullptr};
};

inline void InputProcessor::Init(GuiController* gui)
{
    controller = gui;

    actionInputs[CLOSE_TOP_GUI].key = KEY_ESCAPE;
    actionInputs[OPEN_BUILD_GUI].key = KEY_Q;
    actionInputs[OPEN_ROAD_BUILD_GUI].key = KEY_R;
    actionInputs[OPEN_DESTROY_GUI].key = KEY_D;
    actionInputs[OPEN_HEADQUARTERS_GUI].key = KEY_E;
    actionInputs[OPEN_STATS_GUI].key = KEY_S;
    actionInputs[OPEN_FOCUS_GUI].key = KEY_F;
    actionInputs[CENTER_CAMERA_ON_HEADQUARTERS].key = KEY_SPACE;
    actionInputs[DEBUG_GRANT_RESOURCES].key = KEY_F10;
    
    actionInputs[LEFT_BUTTON_DOWN].button = MOUSE_BUTTON_LEFT;
    actionInputs[LEFT_BUTTON_UP].button = MOUSE_BUTTON_LEFT;
    actionInputs[RIGHT_BUTTON_DOWN].button = MOUSE_BUTTON_RIGHT;
    actionInputs[RIGHT_BUTTON_UP].button = MOUSE_BUTTON_RIGHT;
    actionInputs[MIDDLE_BUTTON_DOWN].button = MOUSE_BUTTON_MIDDLE;
    actionInputs[MIDDLE_BUTTON_UP].button = MOUSE_BUTTON_MIDDLE;
}





#endif
