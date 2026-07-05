#include "ui/KeyBindings.h"
#include "raylib.h"

KeyBindingMap::KeyBindingMap()
{
    ResetToDefaults();
}

void KeyBindingMap::ResetToDefaults()
{
    keyToAction.clear();
    actionToKey.clear();

    // Build/destroy modes
    SetDefault(GameAction::EnterBuildMode, KEY_B);
    SetDefault(GameAction::EnterDestroyMode, KEY_D);
    SetDefault(GameAction::EnterRoadMode, KEY_R);
    SetDefault(GameAction::TogglePauseGame, KEY_SPACE);

    // Camera controls
    SetDefault(GameAction::CameraUp, KEY_W);
    SetDefault(GameAction::CameraDown, KEY_S);
    SetDefault(GameAction::CameraLeft, KEY_A);
    SetDefault(GameAction::CameraRight, KEY_D);
    SetDefault(GameAction::CameraZoomIn, KEY_Q);
    SetDefault(GameAction::CameraZoomOut, KEY_E);

    // UI panels
    SetDefault(GameAction::OpenResearchPanel, KEY_T);
    SetDefault(GameAction::OpenStatsPanel, KEY_I);
    SetDefault(GameAction::OpenFocusTree, KEY_F);

    // Game state
    SetDefault(GameAction::SaveGame, KEY_F5);
    SetDefault(GameAction::LoadGame, KEY_F9);

    // Map views
    SetDefault(GameAction::ToggleTerritoryView, KEY_ONE);
    SetDefault(GameAction::ToggleRoadNetworkView, KEY_TWO);
    SetDefault(GameAction::ToggleResourceView, KEY_THREE);
}

GameAction KeyBindingMap::GetAction(int raylib_key) const
{
    auto it = keyToAction.find(raylib_key);
    if (it != keyToAction.end())
        return it->second;
    return GameAction::Count;
}

void KeyBindingMap::SetKeyBinding(GameAction action, int raylib_key)
{
    // Remove old binding for this action if it exists
    auto oldKey = GetKeyForAction(action);
    if (oldKey != 0)
        keyToAction.erase(oldKey);

    // Remove old binding for this key if it exists
    auto oldAction = GetAction(raylib_key);
    if (oldAction != GameAction::Count)
        actionToKey.erase(oldAction);

    // Add new binding
    if (raylib_key != 0)
    {
        keyToAction[raylib_key] = action;
        actionToKey[action] = raylib_key;
    }
    else
    {
        // Unbind
        actionToKey.erase(action);
    }
}

int KeyBindingMap::GetKeyForAction(GameAction action) const
{
    auto it = actionToKey.find(action);
    if (it != actionToKey.end())
        return it->second;
    return 0;
}

void KeyBindingMap::SetDefault(GameAction action, int raylib_key)
{
    SetKeyBinding(action, raylib_key);
}
