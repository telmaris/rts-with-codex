#ifndef GUI_HANDLER_H
#define GUI_HANDLER_H

// Per-scene GUI/input contract (2026-07-14, user-directed rework after the
// recurring ESC scene ping-pong bug).
//
// Every scene that reacts to input inherits this NEXT TO Scene:
//
//     class MainMenuScene : public Scene, public IGuiHandler { ... };
//
// and calls ProcessGuiInput(dt) at the top of its Update(), BEFORE drawing.
// The interface gives every scene, in one place:
//
//  - a shared INPUT GATE that closes on every scene activation
//    (GameWindow::ChangeScene calls ResetGuiInputGate() centrally) and opens
//    only after (a) this scene has presented at least one frame and (b) ESC
//    is physically released. This kills the whole class of "the key edge
//    that switched scenes fires again in the next scene" bugs at the root:
//    raylib's IsKeyPressed edge state only advances on PollInputEvents
//    (inside EndDrawing), so a scene that switches away mid-frame — with or
//    without presenting — can otherwise leak its un-consumed edge into
//    whichever scene runs next. That leak was the ESC menu ping-pong: the
//    ingame menu returned to GameScene without presenting, GameScene's first
//    frame re-read the SAME IsKeyPressed(ESC) edge and bounced straight back.
//
//  - a uniform back-navigation hook: scenes override OnNavigateBack() to
//    dispatch their ChangeSceneEvent (NewGame -> MainMenu, Options ->
//    previous scene, ingame menu -> game, ...), and their HandleGuiInput()
//    calls HandleBackNavigation() to wire it to ESC. Adding ESC-back to a
//    new scene is exactly those two one-liners.
//
// GuiController remains the (scene-agnostic) interaction-system switchboard;
// a scene that owns one (GameScene today) simply forwards HandleGuiInput()
// to its own InputProcessor, which feeds the controller. Menu scenes don't
// need a controller — the interface is the part they all share.
class IGuiHandler
{
public:
    virtual ~IGuiHandler() = default;

    // Runs one frame of this scene's GUI input, or nothing while the input
    // gate is still closed. Returns true when input was actually handled.
    // Call at the top of Scene::Update(), before drawing — and ALWAYS keep
    // drawing afterwards, even if this dispatched a scene change: presenting
    // the frame is what advances raylib's input poll for the next scene.
    bool ProcessGuiInput(double dt);

    // Closes the input gate. Called centrally by GameWindow::ChangeScene for
    // every activated scene — scenes never need to remember this themselves.
    void ResetGuiInputGate();

protected:
    // Scene-specific per-frame input handling. Only called with the gate open.
    virtual void HandleGuiInput(double dt) = 0;

    // Back-navigation request (ESC). Default: no destination, do nothing.
    virtual void OnNavigateBack() {}

    // Polls ESC and routes it to OnNavigateBack(). Returns true when it
    // fired. Call from HandleGuiInput() in scenes that want ESC-back.
    bool HandleBackNavigation();

private:
    bool guiInputArmed{false};
    int framesSinceActivation{0};
};

#endif
