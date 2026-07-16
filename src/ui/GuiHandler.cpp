#include "ui/GuiHandler.h"
#include "ui/InputManager.h"

#include "raylib.h"

bool IGuiHandler::ProcessGuiInput(double dt)
{
    if (!guiInputArmed)
    {
        // Frame 1 after activation is always skipped: the PREVIOUS scene may
        // have switched here mid-frame without presenting, leaving raylib's
        // per-frame edge state (IsKeyPressed) un-polled — this scene's first
        // frame draws + presents, which finally advances the poll, so from
        // frame 2 on every edge observed here is genuinely new. On top of
        // that, wait for ESC to be physically released, so HOLDING the key
        // can't chain transitions through several scenes in a row.
        framesSinceActivation++;
        bool presentedOnce = framesSinceActivation >= 2;
        if (!presentedOnce || InputManager::IsKeyDown(KEY_ESCAPE))
            return false;

        // Arm and fall through: this frame's edges are already fresh.
        guiInputArmed = true;
    }

    HandleGuiInput(dt);
    return true;
}

void IGuiHandler::ResetGuiInputGate()
{
    guiInputArmed = false;
    framesSinceActivation = 0;
}

bool IGuiHandler::HandleBackNavigation()
{
    if (InputManager::IsKeyPressed(KEY_ESCAPE))
    {
        OnNavigateBack();
        return true;
    }
    return false;
}
