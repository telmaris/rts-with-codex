// Research-tree editor for assets/data/technologies.rtsdata and focuses.rtsdata.
//
// Layout: toolbar strip on top, the game's research panel on the left, and a
// right column split between the node inspector and the bonus total.
// See README.md for what is shared with the game and what was copied.

#include "BonusCalculator.h"
#include "Inspector.h"
#include "TreeModel.h"
#include "TreeView.h"

#include "ui/UiText.h"
#include "ui/UiTheme.h"
#include "ui/UiWidgets.h"

#include "raylib.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    constexpr const char* assetsDir = RTS_ASSETS_DIR;
    constexpr float sidebarWidth = 520.0f;

    std::string AssetPath(const std::string& relative)
    {
        return std::string(assetsDir) + "/" + relative;
    }

    // Draws one toolbar button and reports whether it was clicked this frame.
    bool DrawToolbarButton(Rectangle rect, const std::string& label, bool active, Color accent)
    {
        Vector2 mouse = GetMousePosition();
        bool hover = CheckCollisionPointRec(mouse, rect);
        Color fill = active ? Color{92, 74, 38, 245} : hover ? UiTheme::Timber : UiTheme::Oak;
        Color border = active ? UiTheme::Gold : hover ? accent : UiTheme::Bronze;
        DrawRectangleRounded(rect, 0.18f, 8, fill);
        DrawRectangleRoundedLines(rect, 0.18f, 8, 1.0f, border);
        UiText::DrawFit(label,
            Rectangle{rect.x + 10.0f, rect.y + 5.0f, rect.width - 20.0f, rect.height - 10.0f},
            15,
            active ? UiTheme::Parchment : UiTheme::ParchmentDim);
        return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    }
}

// `--selftest <dir>` loads both real data files, saves them into <dir> and
// verifies the round trip, without opening a window or touching assets/.
// Exercises exactly the production save path, so a wrong enum name in
// TreeSerializer's tables fails here instead of in the game later.
static int RunSelfTest(const std::string& outputDir)
{
    std::filesystem::create_directories(outputDir);

    struct Case { TreeKind kind; const char* source; const char* output; };
    const Case cases[]{
        {TreeKind::Technology, "data/technologies.rtsdata", "technologies.rtsdata"},
        {TreeKind::Focus, "data/focuses.rtsdata", "focuses.rtsdata"}};

    int failures = 0;
    for (const auto& testCase : cases)
    {
        TreeDocument document(testCase.kind, AssetPath(testCase.source));
        std::string target = outputDir + "/" + testCase.output;
        SaveResult result = SaveTree(target, document.GetDefinitions(), testCase.kind == TreeKind::Focus);
        printf("%-24s %s: %s\n", testCase.output, result.ok ? "OK  " : "FAIL", result.message.c_str());
        if (!result.ok)
            failures++;
    }
    printf("%s\n", failures == 0 ? "self-test passed" : "self-test FAILED");
    return failures == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
    if (argc >= 3 && std::string(argv[1]) == "--selftest")
        return RunSelfTest(argv[2]);

    // No FLAG_VSYNC_HINT: with vsync on, SetTargetFPS's own wait loop fights the
    // driver's swap wait and the result is visible judder rather than a smooth
    // 60. One limiter only — the frame timer — at the requested 150.
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1760, 980, "RTS Tech Tree Editor");
    SetTargetFPS(150);
    SetExitKey(KEY_NULL);

    UiTextFont::Load(AssetPath("fonts/MarcellusSC-Regular.ttf"));
    // The tree keeps the game's display face; the side panels are dense forms
    // and get a plain UI face. A system font is fine here — this is a dev tool,
    // nothing ships with it. Missing file just falls back to the display face.
    UiTextFont::LoadPlain("C:/Windows/Fonts/segoeui.ttf", 32);

    std::vector<TreeDocument> documents;
    documents.emplace_back(TreeKind::Technology, AssetPath("data/technologies.rtsdata"));
    documents.emplace_back(TreeKind::Focus, AssetPath("data/focuses.rtsdata"));
    size_t activeDocument = 0;

    // One view/inspector per document so switching trees keeps each one's
    // pan, zoom and selection.
    std::vector<TreeView> views(documents.size());
    std::vector<Inspector> inspectors(documents.size());
    BonusCalculatorPanel calculator;

    std::string statusOverride;
    double statusOverrideUntil = 0.0;

    while (!WindowShouldClose())
    {
        float width = static_cast<float>(GetScreenWidth());
        float height = static_cast<float>(GetScreenHeight());
        Rectangle toolbar{0.0f, 0.0f, width, 56.0f};
        float contentTop = toolbar.height + 8.0f;
        float contentHeight = height - contentTop - 12.0f;
        Rectangle panel{12.0f, contentTop, width - sidebarWidth - 36.0f, contentHeight};
        // The inspector is the long panel (a node can carry many modifiers);
        // the bonus total is usually a short list, so it gets the smaller share.
        Rectangle inspectorArea{width - sidebarWidth - 12.0f, contentTop, sidebarWidth, contentHeight * 0.70f};
        Rectangle calculatorArea{inspectorArea.x, inspectorArea.y + inspectorArea.height + 10.0f,
                                 sidebarWidth, contentHeight - inspectorArea.height - 10.0f};

        TreeView& view = views[activeDocument];
        TreeDocument& document = documents[activeDocument];

        Vector2 mouse = GetMousePosition();
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f && !DropdownWidget::IsAnyOpen() && view.ContainsTreeArea(panel, mouse))
        {
            if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
                view.AdjustScroll(wheel);
            else
                view.AdjustZoom(panel, mouse, wheel);
        }

        // Typing in a field must not also fire single-key shortcuts.
        bool typing = Inspector::IsEditing();
        bool reloadRequested = !typing && IsKeyPressed(KEY_F5);
        bool saveRequested = !typing && IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_S);

        if (!typing && !view.IsPlacing() && IsKeyPressed(KEY_TAB))
            activeDocument = (activeDocument + 1) % documents.size();
        if (!typing && !view.IsPlacing() && IsKeyPressed(KEY_DELETE) && !view.selectedNodeId.empty())
        {
            documents[activeDocument].DeleteNode(views[activeDocument].selectedNodeId);
            views[activeDocument].selectedNodeId.clear();
        }

        BeginDrawing();
        ClearBackground(UiTheme::Ink);

        // -- Toolbar ----------------------------------------------------------
        // Everything outside the tree draws with the plain face.
        UiText::SetRole(UiFontRole::Plain);
        DrawRectangleRec(toolbar, UiTheme::Bark);
        DrawLineEx({0.0f, toolbar.height}, {width, toolbar.height}, 1.0f, UiTheme::Bronze);

        float x = 12.0f;
        for (size_t i = 0; i < documents.size(); i++)
        {
            std::string label = documents[i].GetKind() == TreeKind::Focus ? "Decisions" : "Technologies";
            if (documents[i].IsDirty())
                label += " *";
            if (DrawToolbarButton({x, 11.0f, 150.0f, 34.0f}, label, i == activeDocument, UiTheme::AmberBright))
                activeDocument = i;
            x += 158.0f;
        }

        x += 16.0f;
        if (DrawToolbarButton({x, 11.0f, 130.0f, 34.0f}, "Save [Ctrl+S]", false, UiTheme::SageBright))
            saveRequested = true;
        x += 138.0f;
        if (DrawToolbarButton({x, 11.0f, 120.0f, 34.0f}, "Reload [F5]", false, UiTheme::AmberBright))
            reloadRequested = true;
        x += 128.0f;
        if (DrawToolbarButton({x, 11.0f, 150.0f, 34.0f}, "Clear selection", false, UiTheme::AmberBright))
            documents[activeDocument].ClearTaken();
        x += 158.0f;

        std::string status = GetTime() < statusOverrideUntil
            ? statusOverride
            : documents[activeDocument].GetStatus();
        Color statusColor = GetTime() < statusOverrideUntil ? UiTheme::SageBright : UiTheme::ParchmentDim;
        if (status.rfind("FILE NOT FOUND", 0) == 0 || status.rfind("Round-trip", 0) == 0 ||
            status.rfind("Refusing", 0) == 0 || status.rfind("Cannot open", 0) == 0)
            statusColor = UiTheme::RustBright;
        UiText::Draw(status, x + 8.0f, 21.0f, 14, statusColor);

        std::string fps = std::to_string(GetFPS()) + " fps";
        UiText::Draw(fps, width - UiText::Measure(fps, 13) - 14.0f, 22.0f, 13, UiTheme::ParchmentFaint);

        // -- Tree + sidebars --------------------------------------------------
        // The tree is the one place that keeps the game's display font: it is a
        // real UI component from the game, and it should look like it.
        UiText::SetRole(UiFontRole::Display);
        views[activeDocument].Draw(panel, documents[activeDocument]);

        UiText::SetRole(UiFontRole::Plain);
        std::string nextSelection = inspectors[activeDocument].Draw(
            inspectorArea, documents[activeDocument], views[activeDocument].selectedNodeId);
        views[activeDocument].selectedNodeId = nextSelection;

        calculator.Draw(calculatorArea, documents[activeDocument]);

        // Expanded dropdown lists paint last so they sit above every panel.
        DropdownWidget::DrawOpenList();
        UiText::SetRole(UiFontRole::Display);

        EndDrawing();

        // Mutating the document happens after EndDrawing: the draw pass holds
        // pointers into its definitions, which these replace wholesale.
        if (saveRequested)
        {
            SaveResult result = documents[activeDocument].Save();
            statusOverride = result.message;
            statusOverrideUntil = GetTime() + (result.ok ? 4.0 : 12.0);
        }
        if (reloadRequested)
        {
            views[activeDocument].selectedNodeId.clear();
            documents[activeDocument].Reload();
        }
    }

    UiTextFont::Unload();
    CloseWindow();
    return 0;
}
