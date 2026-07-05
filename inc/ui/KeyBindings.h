#ifndef KEYBINDINGS_H
#define KEYBINDINGS_H

#include <map>
#include <string>

// Game actions that can be triggered by keyboard input
enum class GameAction
{
    // Build/destroy/UI modes
    EnterBuildMode,
    EnterDestroyMode,
    EnterRoadMode,
    TogglePauseGame,

    // Camera controls
    CameraUp,
    CameraDown,
    CameraLeft,
    CameraRight,
    CameraZoomIn,
    CameraZoomOut,

    // UI panels
    OpenResearchPanel,
    OpenStatsPanel,
    OpenFocusTree,

    // Game state
    SaveGame,
    LoadGame,

    // Map views
    ToggleTerritoryView,
    ToggleRoadNetworkView,
    ToggleResourceView,

    Count  // For iteration bounds
};

// Maps keyboard keys (raylib KEY_* constants) to game actions
class KeyBindingMap
{
public:
    KeyBindingMap();

    // Get action for a key; returns GameAction::Count if not bound
    GameAction GetAction(int raylib_key) const;

    // Set custom key binding (0 = unbind)
    void SetKeyBinding(GameAction action, int raylib_key);

    // Reset to defaults
    void ResetToDefaults();

    // Get key for action (useful for UI display); returns 0 if unbound
    int GetKeyForAction(GameAction action) const;

private:
    std::map<int, GameAction> keyToAction;      // raylib_key -> GameAction
    std::map<GameAction, int> actionToKey;      // GameAction -> raylib_key

    void SetDefault(GameAction action, int raylib_key);
};

#endif
