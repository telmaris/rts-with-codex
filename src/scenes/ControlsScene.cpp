#include "scenes/Scenes.h"

// Initializes ControlsScene::ControlsScene.
ControlsScene::ControlsScene()
{
    backButton.ChangeText("Back");
    backButton.ChangePositionAnchor(Vec2f{0.44f, 0.88f});
    backButton.ChangeSizeAnchor(Vec2f{0.12f, 0.055f});
    backButton.func = std::bind(&ControlsScene::OnBackPressed, this);

    Vec2i windowSize{GetScreenWidth(), GetScreenHeight()};
    backButton.UpdateSize(windowSize);
}

namespace
{
    void DrawControlsSection(float x, float& y, const char* header,
                             const std::vector<std::pair<const char*, const char*>>& rows)
    {
        UiText::Draw(header, x, y, 22, Color{210, 220, 240, 255});
        y += 30.0f;
        for (const auto& [key, desc] : rows)
        {
            float keyW = 130.0f;
            UiText::Draw(key, x + 8.0f, y, 18, Color{255, 220, 120, 255});
            UiText::Draw(desc, x + keyW, y, 18, Color{190, 200, 216, 255});
            y += 24.0f;
        }
        y += 10.0f;
    }
}

// Advances this object's state for one frame.
void ControlsScene::Update(double dt)
{
    BeginDrawing();
    ClearBackground(BLACK);

    float sw = static_cast<float>(GetScreenWidth());
    float sh = static_cast<float>(GetScreenHeight());

    float panelW = sw * 0.70f;
    float panelH = sh * 0.82f;
    float panelX = (sw - panelW) * 0.5f;
    float panelY = sh * 0.07f;

    DrawRectangleRounded({panelX, panelY, panelW, panelH}, 0.03f, 8, Color{18, 22, 30, 242});
    DrawRectangleRoundedLines({panelX, panelY, panelW, panelH}, 0.03f, 8, 1.0f, Color{90, 106, 130, 255});

    UiText::Draw("Controls", panelX + 24.0f, panelY + 16.0f, 30, RAYWHITE);
    DrawLineEx({panelX + 24.0f, panelY + 54.0f}, {panelX + panelW - 24.0f, panelY + 54.0f}, 1.0f, Color{70, 84, 106, 200});

    float col1X = panelX + 32.0f;
    float col2X = panelX + panelW * 0.5f + 16.0f;
    float startY = panelY + 68.0f;

    float y1 = startY;
    DrawControlsSection(col1X, y1, "Camera & navigation", {
        {"Space",       "Center on Headquarters"},
        {"RMB drag",    "Pan camera"},
        {"Scroll",      "Zoom in / out"},
        {"ESC",         "Open game menu / cancel mode"},
    });

    DrawControlsSection(col1X, y1, "Panels", {
        {"Q",           "Build panel"},
        {"R",           "Road build mode"},
        {"D",           "Destroy mode"},
        {"E",           "Headquarters panel"},
        {"S",           "Statistics & economy"},
        {"F",           "Political focus tree"},
    });

    DrawControlsSection(col1X, y1, "Selection", {
        {"LMB",         "Select building on map"},
        {"LMB (build)", "Confirm building placement"},
        {"LMB drag (road)", "Paint road tiles"},
    });

    float y2 = startY;
    DrawControlsSection(col2X, y2, "Military", {
        {"LMB military", "Open garrison / division bar"},
        {"LMB division", "Select division"},
        {"RMB building", "Assign attack / move order"},
    });

    DrawControlsSection(col2X, y2, "Build panel", {
        {"LMB on card",  "Select building to place"},
        {"LMB on map",   "Place selected building"},
        {"Scroll",       "Scroll build list"},
        {"ESC / Q",      "Cancel build mode"},
    });

    DrawControlsSection(col2X, y2, "Research & focus", {
        {"LMB node",     "Research technology / take focus"},
        {"RMB drag",     "Pan technology / focus tree"},
        {"Scroll",       "Zoom tree view"},
    });

    UiText::Draw("Tip: hover over widgets in the New Game screen for detailed descriptions.",
                 panelX + 24.0f, panelY + panelH - 32.0f, 16, Color{150, 162, 180, 220});

    backButton.Update(dt);

    EndDrawing();
}

// Handles the UI action represented by OnBackPressed.
void ControlsScene::OnBackPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = previousSceneName;
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

// Handles the requested event or transfer.
void ControlsScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
        backButton.UpdateSize(ptr->windowSize);
}
