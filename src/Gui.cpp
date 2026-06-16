#include "../inc/Gui.h"

#include "raylib.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

void UiButton::Update(double dt)
{
    if (GuiButton((Rectangle){pos.x, pos.y, size.x, size.y}, text.c_str()))
        OnClick();
}

void CheckBox::Update(double dt)
{
    GuiCheckBox((Rectangle){pos.x, pos.y, size.x, size.y}, text.c_str(), &currentState);
}

void SliderBar::Update(double dt)
{
    GuiSliderBar((Rectangle){pos.x, pos.y, size.x, size.y}, text.c_str(), nullptr, &currentValue, 0.0f, 1.0f);
}

void VBox::Update(double dt)
{
    for(auto& child : children)
    {
        child->Update(dt);
    }
}

void HBox::Update(double dt)
{
    for(auto& child : children)
    {
        child->Update(dt);
    }
}

void TextBox::Update(double dt)
{
    GuiTextBox((Rectangle){pos.x, pos.y, size.x, size.y}, textOutput, 18, true);
    text = textOutput;
}

void GuiPanel::Update(double dt)
{
    GuiWindowBox((Rectangle){pos.x, pos.y, size.x, size.y}, text.c_str());
}