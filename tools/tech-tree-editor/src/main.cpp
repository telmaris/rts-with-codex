// Research-tree editor for assets/data/technologies.rtsdata and focuses.rtsdata.
//
// Layout: toolbar strip on top, the game's research panel on the left, and a
// right column split between the node inspector and the bonus total.
// See README.md for what is shared with the game and what was copied.

#include "BonusCalculator.h"
#include "EditorTheme.h"
#include "Inspector.h"
#include "TreeModel.h"
#include "TreeView.h"

#include "ui/UiText.h"
#include "ui/UiWidgets.h"

#include "raylib.h"

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    constexpr const char* assetsDir = RTS_ASSETS_DIR;
    constexpr float sidebarWidth = 540.0f;

    std::string AssetPath(const std::string& relative)
    {
        return std::string(assetsDir) + "/" + relative;
    }

    // Draws one toolbar button and reports whether it was clicked this frame.
    bool DrawToolbarButton(Rectangle rect, const std::string& label, bool active, Color accent)
    {
        Vector2 mouse = GetMousePosition();
        bool hover = !DropdownWidget::IsAnyOpen() && CheckCollisionPointRec(mouse, rect);
        Color fill = active ? EditorTheme::SurfaceFocus : hover ? EditorTheme::SurfaceHover : EditorTheme::Surface;
        Color border = active ? EditorTheme::Accent : hover ? accent : EditorTheme::Border;
        DrawRectangleRounded(rect, 0.18f, 8, fill);
        DrawRectangleRoundedLines(rect, 0.18f, 8, 1.0f, border);
        UiText::DrawFit(label,
            Rectangle{rect.x + 10.0f, rect.y + 5.0f, rect.width - 20.0f, rect.height - 10.0f},
            15,
            active ? EditorTheme::Text : EditorTheme::TextMuted);
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

    const std::vector<const std::vector<std::string>*> dropdownLists{
        &RtsDataNames::BalanceStats(), &RtsDataNames::BuildingTypes(),
        &RtsDataNames::ResourceTypes(), &RtsDataNames::ResourceCategories(),
        &RtsDataNames::Categories(), &RtsDataNames::Tags()};
    const bool dropdownsSorted = std::all_of(dropdownLists.begin(), dropdownLists.end(), [](const auto* options)
    {
        return std::is_sorted(options->begin(), options->end());
    });

    TreeDocument focusOptionsDocument(TreeKind::Focus, AssetPath("data/focuses.rtsdata"));
    const auto buildingOptions = focusOptionsDocument.GetBuildingUnlockOptions("bathhouses");
    const bool buildingsSorted = std::is_sorted(buildingOptions.begin(), buildingOptions.end());
    const bool universityAvailable = std::find(buildingOptions.begin(), buildingOptions.end(), "University") !=
                                     buildingOptions.end();
    printf("%-24s %s: %s\n", "dropdown options",
           dropdownsSorted && buildingsSorted && universityAvailable ? "OK  " : "FAIL",
           dropdownsSorted && buildingsSorted && universityAvailable
               ? "Alphabetical; University available"
               : "Order or building availability mismatch");
    if (!dropdownsSorted || !buildingsSorted || !universityAvailable)
        failures++;

    const double slowdownPercent = -5.0;
    const double expectedSlowdownMultiplier = 1.0 / 0.95;
    const double slowdownMultiplier = ModifierMultiplierFromEffectPercent(
        BalanceStat::ProductionCycleTime, slowdownPercent);
    const bool negativeCycleEffect = std::abs(slowdownMultiplier - expectedSlowdownMultiplier) < 1e-12 &&
                                     std::abs(ModifierEffectPercent(
                                         BalanceStat::ProductionCycleTime, slowdownMultiplier) - slowdownPercent) < 1e-9;
    BalanceModifier slowdownModifier;
    slowdownModifier.stat = BalanceStat::ProductionCycleTime;
    slowdownModifier.multiplier = slowdownMultiplier;
    const bool slowdownSerialized = SerializeModifier(slowdownModifier) ==
                                    "modifier ProductionCycleTime multiplier 1.05263157895";
    TechnologyDefinition slowdownProbe;
    slowdownProbe.id = "editor_negative_cycle_probe";
    slowdownProbe.name = "Editor negative cycle probe";
    slowdownProbe.researchTime = 1.0;
    slowdownProbe.modifiers.push_back(slowdownModifier);
    const std::string slowdownProbePath =
        (std::filesystem::path(outputDir) / "negative_cycle_probe.rtsdata").string();
    const SaveResult slowdownProbeResult = SaveTree(slowdownProbePath, {slowdownProbe}, false);
    const auto rereadSlowdownProbe = LoadTechnologyDefinitionsFromFile(slowdownProbePath);
    const bool slowdownParsedByGame = slowdownProbeResult.ok && rereadSlowdownProbe.size() == 1 &&
                                      rereadSlowdownProbe.front().modifiers.size() == 1 &&
                                      std::abs(rereadSlowdownProbe.front().modifiers.front().multiplier -
                                               expectedSlowdownMultiplier) < 1e-9;
    printf("%-24s %s: %s\n", "negative cycle effect",
           negativeCycleEffect && slowdownSerialized && slowdownParsedByGame ? "OK  " : "FAIL",
           negativeCycleEffect && slowdownSerialized && slowdownParsedByGame
               ? "-5% speed -> precise game cycle multiplier; parser verified"
               : "Effect conversion mismatch");
    if (!negativeCycleEffect || !slowdownSerialized || !slowdownParsedByGame)
        failures++;

    // Building unlocks deliberately live in buildings.rtsdata. Exercise that
    // second save path in an isolated copy so an editor change can never leave
    // a tooltip-only pseudo effect that the game build gate does not respect.
    const std::filesystem::path sourceData = std::filesystem::path(assetsDir) / "data";
    const std::filesystem::path unlockData = std::filesystem::path(outputDir) / "unlock_check";
    std::filesystem::create_directories(unlockData);
    std::filesystem::copy_file(sourceData / "technologies.rtsdata", unlockData / "technologies.rtsdata",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(sourceData / "buildings.rtsdata", unlockData / "buildings.rtsdata",
                               std::filesystem::copy_options::overwrite_existing);
    TreeDocument unlockDocument(TreeKind::Technology, (unlockData / "technologies.rtsdata").string());
    auto unlocks = unlockDocument.GetUnlockedBuildingIds("standard_coinage");
    unlocks.push_back("Armorer");
    unlockDocument.SetBuildingUnlocks("standard_coinage", unlocks);
    SaveResult unlockResult = unlockDocument.Save();
    TreeDocument rereadUnlockDocument(TreeKind::Technology, (unlockData / "technologies.rtsdata").string());
    const auto rereadUnlocks = rereadUnlockDocument.GetUnlockedBuildings("standard_coinage");
    const bool armorerUnlocked = std::find(rereadUnlocks.begin(), rereadUnlocks.end(), "Armorer") != rereadUnlocks.end();
    printf("%-24s %s: %s\n", "building unlocks", unlockResult.ok && armorerUnlocked ? "OK  " : "FAIL",
           unlockResult.ok && armorerUnlocked ? "Saved and reloaded" : unlockResult.message.c_str());
    if (!unlockResult.ok || !armorerUnlocked)
        failures++;

    // Command-history smoke test: additions, bulk delete, undo and redo all
    // operate on a single transaction snapshot rather than per-frame state.
    const std::filesystem::path historyData = std::filesystem::path(outputDir) / "history_check";
    std::filesystem::create_directories(historyData);
    std::filesystem::copy_file(sourceData / "technologies.rtsdata", historyData / "technologies.rtsdata",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(sourceData / "buildings.rtsdata", historyData / "buildings.rtsdata",
                               std::filesystem::copy_options::overwrite_existing);
    TreeDocument historyDocument(TreeKind::Technology, (historyData / "technologies.rtsdata").string());
    const size_t originalCount = historyDocument.GetNodeCount();
    historyDocument.BeginHistoryTransaction();
    const std::string historyNode = historyDocument.AddNode("field_herbals", "Biology", 9000);
    historyDocument.CommitHistoryTransaction();
    const bool addUndoRedo = historyDocument.GetNodeCount() == originalCount + 1 &&
                             historyDocument.Undo() && historyDocument.Find(historyNode) == nullptr &&
                             historyDocument.Redo() && historyDocument.Find(historyNode) != nullptr;
    historyDocument.BeginHistoryTransaction();
    historyDocument.DeleteNodes({historyNode, "comparative_anatomy"});
    historyDocument.CommitHistoryTransaction();
    const bool bulkDeleteUndo = historyDocument.Find(historyNode) == nullptr &&
                                historyDocument.Undo() && historyDocument.Find(historyNode) != nullptr &&
                                historyDocument.Find("comparative_anatomy") != nullptr;
    printf("%-24s %s: %s\n", "undo / redo", addUndoRedo && bulkDeleteUndo ? "OK  " : "FAIL",
           addUndoRedo && bulkDeleteUndo ? "Transactions restored" : "History state mismatch");
    if (!addUndoRedo || !bulkDeleteUndo)
        failures++;

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
    SetUiWidgetPalette(EditorTheme::Widgets);

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
        const bool ctrlHeld = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        bool reloadRequested = !typing && IsKeyPressed(KEY_F5);
        bool saveRequested = !typing && ctrlHeld && IsKeyPressed(KEY_S);

        if (!typing && !view.IsPlacing() && ctrlHeld && IsKeyPressed(KEY_Z))
        {
            if (document.Undo())
                view.ClearNodeSelection();
        }
        if (!typing && !view.IsPlacing() && ctrlHeld && IsKeyPressed(KEY_Y))
        {
            if (document.Redo())
                view.ClearNodeSelection();
        }

        if (!typing && !view.IsPlacing() && IsKeyPressed(KEY_TAB))
            activeDocument = (activeDocument + 1) % documents.size();
        if (!typing && !view.IsPlacing() && IsKeyPressed(KEY_DELETE) &&
            (!view.selectedNodeIds.empty() || !view.selectedNodeId.empty()))
        {
            std::vector<std::string> ids(view.selectedNodeIds.begin(), view.selectedNodeIds.end());
            if (ids.empty())
                ids.push_back(view.selectedNodeId);
            document.BeginHistoryTransaction();
            document.DeleteNodes(ids);
            document.CommitHistoryTransaction();
            view.ClearNodeSelection();
        }

        document.BeginHistoryFrame();

        BeginDrawing();
        ClearBackground(EditorTheme::Canvas);

        // -- Toolbar ----------------------------------------------------------
        // Everything outside the tree draws with the plain face.
        UiText::SetRole(UiFontRole::Plain);
        DrawRectangleRec(toolbar, EditorTheme::PanelHeader);
        DrawLineEx({0.0f, toolbar.height}, {width, toolbar.height}, 1.0f, EditorTheme::Border);

        float x = 12.0f;
        for (size_t i = 0; i < documents.size(); i++)
        {
            std::string label = documents[i].GetKind() == TreeKind::Focus ? "Decisions" : "Technologies";
            if (documents[i].IsDirty())
                label += " *";
            if (DrawToolbarButton({x, 11.0f, 150.0f, 34.0f}, label, i == activeDocument, EditorTheme::Accent))
                activeDocument = i;
            x += 158.0f;
        }

        x += 16.0f;
        if (DrawToolbarButton({x, 11.0f, 130.0f, 34.0f}, "Save [Ctrl+S]", false, EditorTheme::Positive))
            saveRequested = true;
        x += 138.0f;
        if (DrawToolbarButton({x, 11.0f, 120.0f, 34.0f}, "Reload [F5]", false, EditorTheme::Accent))
            reloadRequested = true;
        x += 128.0f;
        if (DrawToolbarButton({x, 11.0f, 150.0f, 34.0f}, "Clear selection", false, EditorTheme::Accent))
            documents[activeDocument].ClearTaken();
        x += 158.0f;

        std::string status = GetTime() < statusOverrideUntil
            ? statusOverride
            : documents[activeDocument].GetStatus();
        Color statusColor = GetTime() < statusOverrideUntil ? EditorTheme::Positive : EditorTheme::TextMuted;
        if (status.rfind("FILE NOT FOUND", 0) == 0 || status.rfind("Round-trip", 0) == 0 ||
            status.rfind("Refusing", 0) == 0 || status.rfind("Cannot open", 0) == 0)
            statusColor = EditorTheme::Negative;
        UiText::Draw(status, x + 8.0f, 21.0f, 14, statusColor);

        std::string fps = std::to_string(GetFPS()) + " fps";
        UiText::Draw(fps, width - UiText::Measure(fps, 13) - 14.0f, 22.0f, 13, EditorTheme::TextFaint);

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
        document.CommitHistoryFrame();

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
            views[activeDocument].ClearNodeSelection();
            documents[activeDocument].Reload();
        }
    }

    UiTextFont::Unload();
    CloseWindow();
    return 0;
}
