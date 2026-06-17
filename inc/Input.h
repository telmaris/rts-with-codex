#ifndef INPUT_H
#define INPUT_H

#include "raylib.h"

class GuiController;

enum InputMap
{
    ACTION_NULL = 0,

    // keyboard input actions

    CLOSE_TOP_GUI,
    OPEN_BUILD_GUI,
    OPEN_ROAD_BUILD_GUI,


    // mouse actions

    LEFT_BUTTON_DOWN,
    LEFT_BUTTON_UP,
    RIGHT_BUTTON_DOWN,
    RIGHT_BUTTON_UP,


    MAX_ACTION
};

struct ActionInput
{
    int key = -1;
    int button = -1;
};

struct InputProcessor
{
    inline void Init(GuiController*);
    void HandleInputs();
    bool IsActionPressed(int action);
    bool IsActionReleased(int action);
    bool IsActionDown(int action);


    ActionInput actionInputs[MAX_ACTION];

    GuiController* controller;
};

inline void InputProcessor::Init(GuiController* gui)
{
    controller = gui;

    actionInputs[CLOSE_TOP_GUI].key = KEY_ESCAPE;
    actionInputs[OPEN_BUILD_GUI].key = KEY_Q;
    actionInputs[OPEN_ROAD_BUILD_GUI].key = KEY_R;
    
    actionInputs[LEFT_BUTTON_DOWN].button = MOUSE_BUTTON_LEFT;
    actionInputs[LEFT_BUTTON_UP].button = MOUSE_BUTTON_LEFT;
    actionInputs[RIGHT_BUTTON_DOWN].button = MOUSE_BUTTON_RIGHT;
    actionInputs[RIGHT_BUTTON_UP].button = MOUSE_BUTTON_RIGHT;
}





#endif
