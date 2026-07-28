// Texture editor for assets/data/textures.rtsdata.
//
// Shell only: tab bar, document banner, status bar, and the modal overlay. It
// knows nothing about any individual tab — the full list is BuildTabs() below,
// and that is the only place to touch when adding another one.

#include "EditorTheme.h"
#include "EditorWidgets.h"
#include "Tabs.h"

#include "ui/UiText.h"

#include "raylib.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace
{
    constexpr const char* assetsDir = RTS_ASSETS_DIR;

    std::string AssetPath(const std::string& relative)
    {
        return std::string(assetsDir) + "/" + relative;
    }

    // The tab registry. One line per tab; order here is the order on screen.
    std::vector<std::unique_ptr<ITextureTab>> BuildTabs(ToolContext& context)
    {
        std::vector<std::unique_ptr<ITextureTab>> tabs;
        tabs.push_back(std::make_unique<TerrainTab>(context));
        tabs.push_back(std::make_unique<BuildingTab>(context));
        tabs.push_back(std::make_unique<ResourceTab>(context));
        tabs.push_back(std::make_unique<AssetTab>(context));
        return tabs;
    }

    bool TabButton(Rectangle rect, const std::string& label, bool active)
    {
        Vector2 mouse = GetMousePosition();
        bool hover = !Ed::InputBlocked() && CheckCollisionPointRec(mouse, rect);

        DrawRectangleRounded(rect, 0.16f, 8, active ? Ed::Surface : hover ? Ed::Hover : Ed::Void);
        DrawRectangleRoundedLines(rect, 0.16f, 8, 1.0f, active ? Ed::Accent : hover ? Ed::Border : Ed::BorderSoft);
        UiText::DrawFit(label, Rectangle{rect.x + 12.0f, rect.y + 6.0f, rect.width - 24.0f, rect.height - 12.0f},
                        Ed::FontBody, active ? Ed::TextPrimary : Ed::TextMuted);

        return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    }
}

int main()
{
    // No FLAG_VSYNC_HINT: with vsync on, SetTargetFPS's own wait loop fights the
    // driver's swap wait. One limiter only, same as tools/tech-tree-editor.
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1760, 1000, "RTS Texture Editor");
    SetTargetFPS(150);
    SetExitKey(KEY_NULL);

    // One plain UI face for the whole tool. The game's display face is small-caps
    // serif tuned for atmosphere; this is a dense form and needs to be read, not
    // admired. A system font is fine — nothing ships with the tool.
    UiTextFont::LoadPlain("C:/Windows/Fonts/segoeui.ttf", 40);
    UiText::SetRole(UiFontRole::Plain);

    auto context = std::make_unique<ToolContext>();
    context->Load(assetsDir);
    auto tabs = BuildTabs(*context);
    size_t activeTab = 0;

    double savedFlashUntil = 0.0;

    while (!WindowShouldClose())
    {
        float width = static_cast<float>(GetScreenWidth());
        float height = static_cast<float>(GetScreenHeight());

        bool modal = context->picker.IsOpen();
        bool saveRequested = false;
        bool reloadRequested = false;

        if (!modal)
        {
            if (IsKeyPressed(KEY_TAB))
                activeTab = (activeTab + 1) % tabs.size();
            for (size_t i = 0; i < tabs.size() && i < 9; i++)
            {
                if (IsKeyPressed(KEY_ONE + static_cast<int>(i)))
                    activeTab = i;
            }
            saveRequested = (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_S);
            reloadRequested = IsKeyPressed(KEY_F5);
        }

        BeginDrawing();
        ClearBackground(Ed::Void);

        // ── Toolbar ────────────────────────────────────────────────────────
        Ed::SetInputBlocked(modal);

        Rectangle toolbar{0.0f, 0.0f, width, 58.0f};
        DrawRectangleRec(toolbar, Ed::Void);
        DrawLineEx({0.0f, toolbar.height}, {width, toolbar.height}, 1.0f, Ed::Border);

        float x = 12.0f;
        for (size_t i = 0; i < tabs.size(); i++)
        {
            std::string label = std::to_string(i + 1) + "   " + tabs[i]->Title();
            float buttonWidth = static_cast<float>(UiText::Measure(label, Ed::FontBody)) + 36.0f;
            if (TabButton({x, 12.0f, buttonWidth, 34.0f}, label, i == activeTab))
                activeTab = i;
            x += buttonWidth + 8.0f;
        }

        x += 20.0f;
        std::string saveLabel = context->document.IsDirty() ? "Save *  [Ctrl+S]" : "Save  [Ctrl+S]";
        if (Ed::Button({x, 12.0f, 150.0f, 34.0f}, saveLabel, context->document.IsDirty()))
            saveRequested = true;
        x += 158.0f;
        if (Ed::Button({x, 12.0f, 118.0f, 34.0f}, "Reload  [F5]"))
            reloadRequested = true;

        std::string fps = std::to_string(GetFPS()) + " fps";
        UiText::Draw(fps, width - UiText::Measure(fps, Ed::FontSmall) - 14.0f, 22.0f, Ed::FontSmall, Ed::TextFaint);

        // ── Document banner ────────────────────────────────────────────────
        // Only shown when the document state is something you must not miss:
        // seeded-not-saved, a failed save, or a fresh success.
        float contentTop = toolbar.height + 10.0f;
        bool freshSave = GetTime() < savedFlashUntil;
        bool showBanner = context->document.WasSeeded() || context->document.StatusIsError() || freshSave;
        if (showBanner)
        {
            Color accent = context->document.StatusIsError() ? Ed::Danger
                           : context->document.WasSeeded()   ? Ed::Warn
                                                             : Ed::Ok;
            Rectangle banner{12.0f, contentTop, width - 24.0f, 32.0f};
            Color fill = accent;
            fill.a = 30;
            DrawRectangleRounded(banner, 0.3f, 6, fill);
            DrawRectangleRoundedLines(banner, 0.3f, 6, 1.0f, accent);
            UiText::DrawFit(context->document.Status(),
                            Rectangle{banner.x + 12.0f, banner.y + 7.0f, banner.width - 24.0f, 18.0f},
                            Ed::FontBody, accent);
            contentTop += 42.0f;
        }

        // ── Active tab ─────────────────────────────────────────────────────
        Rectangle content{12.0f, contentTop, width - 24.0f, height - contentTop - 44.0f};
        tabs[activeTab]->Draw(content);

        // ── Status bar ─────────────────────────────────────────────────────
        Rectangle statusBar{0.0f, height - 34.0f, width, 34.0f};
        DrawRectangleRec(statusBar, Ed::Void);
        DrawLineEx({0.0f, statusBar.y}, {width, statusBar.y}, 1.0f, Ed::Border);
        UiText::Draw(tabs[activeTab]->Status(), 14.0f, statusBar.y + 9.0f, Ed::FontBody,
                     tabs[activeTab]->StatusIsWarning() ? Ed::Warn : Ed::TextMuted);

        std::string target = context->document.Path();
        UiText::Draw(target, width - UiText::Measure(target, Ed::FontSmall) - 14.0f, statusBar.y + 11.0f,
                     Ed::FontSmall, Ed::TextFaint);

        // ── Modal overlay, painted over everything ─────────────────────────
        Ed::SetInputBlocked(false);
        std::string chosen = context->picker.Draw(Rectangle{0.0f, 0.0f, width, height});
        if (!chosen.empty())
            context->pickerResult = chosen;

        EndDrawing();

        // ── Deferred commands ──────────────────────────────────────────────
        if (saveRequested && context->document.Save())
            savedFlashUntil = GetTime() + 4.0;

        if (reloadRequested)
        {
            tabs.clear();
            context->Reload();
            tabs = BuildTabs(*context);
            activeTab = std::min(activeTab, tabs.size() - 1);
        }
    }

    tabs.clear();
    context.reset();
    UiTextFont::Unload();
    CloseWindow();
    return 0;
}
