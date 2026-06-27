#include "../inc/Renderer.h"
#include "../inc/Building.h"

// Draws all cached world layers and UI widgets to the window.
void Renderer::Draw(std::vector<UiWidget*> ui, double dt)
{
    BeginDrawing();
    ClearBackground(BLACK);

    float scale = std::min(GetScreenWidth() / static_cast<float>(RENDER_WIDTH),
                           GetScreenHeight() / static_cast<float>(RENDER_HEIGHT));
    float width = RENDER_WIDTH * scale;
    float height = RENDER_HEIGHT * scale;
    Vector2 offset{
        (GetScreenWidth() - width) * 0.5f,
        (GetScreenHeight() - height) * 0.5f};

    Rectangle src{0.0f, 0.0f, static_cast<float>(RENDER_WIDTH), static_cast<float>(RENDER_HEIGHT)};
    Rectangle dest{offset.x, offset.y, width, height};

    for(auto& layer : layers)
    {
        DrawTexturePro(layer.fbo.texture, src, dest, {0.0f, 0.0f}, 0.0f, WHITE);
    }

    for(auto ptr : ui)
    {
        ptr->Update(dt);
    }

    EndDrawing();
}

// Draws a standalone texture onto one render layer.
void Renderer::DrawOnLayer(int layer, Texture2D tex, Vec2i pos)
{
    BeginLayer(layer);

    Rectangle src = {0,0,tex.width*1.0f, -tex.height*1.0f};
    Rectangle dest = {static_cast<float>(pos.x), static_cast<float>(RENDER_HEIGHT - tex.height - pos.y), tex.width*1.0f, tex.height*1.0f};
    DrawTexturePro(tex, src, dest, {0,0}, 0, WHITE);

    EndLayer();
}

// Draws one atlas tile directly onto a render layer.
void Renderer::DrawOnLayer(int layer, int atlas, int tex, Vec2f pos)
{
    BeginLayer(layer);
    DrawAtlasTile(atlas, tex, pos);
    EndLayer();
}

// Initializes Renderer::BeginLayer.
void Renderer::BeginLayer(int layer)
{
    BeginTextureMode(layers[layer].fbo);
    BeginMode2D(camera);
}

// Initializes Renderer::EndLayer.
void Renderer::EndLayer()
{
    EndMode2D();
    EndTextureMode();
}

// Clears this runtime state.
void Renderer::ClearLayer(int layer)
{
    BeginTextureMode(layers[layer].fbo);
    ClearBackground(BLANK);
    EndTextureMode();
}

// Draws one atlas tile at its native tile size.
void Renderer::DrawAtlasTile(int atlas, int tex, Vec2f pos)
{
    auto& at = atlasMap[atlas];
    DrawAtlasTile(atlas, tex, pos, {static_cast<float>(at.size.x), static_cast<float>(at.size.y)});
}

// Draws one atlas tile stretched to a target world size.
void Renderer::DrawAtlasTile(int atlas, int tex, Vec2f pos, Vec2f drawSize)
{
    auto& at = atlasMap[atlas];
    if (tex < 0 || tex >= at.dim.x * at.dim.y)
        tex = std::max(0, at.dim.x * at.dim.y - 1);

    Rectangle src = at.GetRectFromId(tex);
    src.height *= -1.0;

    Rectangle dest = {pos.x, RENDER_HEIGHT - drawSize.y - pos.y, drawSize.x, drawSize.y};
    DrawTexturePro(at.tex, src, dest, {0,0}, 0, WHITE);
}

// Loads the requested data into runtime state.
void Renderer::LoadBuildingTexture(BuildingType type, const std::string& path)
{
    if (!FileExists(path.c_str()))
        return;

    Texture2D texture = LoadTexture(path.c_str());
    if (texture.id != 0)
        buildingTextures[type] = texture;
}

// Draws a building texture sized to its footprint.
void Renderer::DrawBuildingTexture(Building* building, Vec2f pos)
{
    if (building == nullptr)
        return;

    auto footprint = building->GetFootprint();
    Vec2f drawSize{
        static_cast<float>(footprint.x * TILE_SIZE),
        static_cast<float>(footprint.y * TILE_SIZE)};

    auto textureIt = buildingTextures.find(building->buildingType);
    if (textureIt != buildingTextures.end())
    {
        Texture2D texture = textureIt->second;
        Rectangle src{0.0f, 0.0f, static_cast<float>(texture.width), -static_cast<float>(texture.height)};
        Rectangle dest{pos.x, RENDER_HEIGHT - drawSize.y - pos.y, drawSize.x, drawSize.y};
        DrawTexturePro(texture, src, dest, {0.0f, 0.0f}, 0.0f, WHITE);
        return;
    }

    Rectangle dest{pos.x, RENDER_HEIGHT - drawSize.y - pos.y, drawSize.x, drawSize.y};
    DrawRectangleRounded(dest, 0.04f, 8, Color{90, 96, 108, 255});
    DrawRectangleRoundedLines(dest, 0.04f, 8, Color{170, 180, 196, 255});
}

// Initializes Renderer::ScreenToRender.
Vec2f Renderer::ScreenToRender(Vector2 screen)
{
    float scale = std::min(GetScreenWidth() / static_cast<float>(RENDER_WIDTH),
                           GetScreenHeight() / static_cast<float>(RENDER_HEIGHT));
    float width = RENDER_WIDTH * scale;
    float height = RENDER_HEIGHT * scale;
    float offsetX = (GetScreenWidth() - width) * 0.5f;
    float offsetY = (GetScreenHeight() - height) * 0.5f;

    if (screen.x < offsetX || screen.x > offsetX + width ||
        screen.y < offsetY || screen.y > offsetY + height)
    {
        return Vec2f{-1.0f, -1.0f};
    }

    return Vec2f{
        (screen.x - offsetX) / scale,
        (screen.y - offsetY) / scale};
}

// Initializes Renderer::RenderToScreen.
Vec2f Renderer::RenderToScreen(Vec2f render)
{
    float scale = std::min(GetScreenWidth() / static_cast<float>(RENDER_WIDTH),
                           GetScreenHeight() / static_cast<float>(RENDER_HEIGHT));
    float width = RENDER_WIDTH * scale;
    float height = RENDER_HEIGHT * scale;
    float offsetX = (GetScreenWidth() - width) * 0.5f;
    float offsetY = (GetScreenHeight() - height) * 0.5f;

    return Vec2f{
        offsetX + render.x * scale,
        offsetY + render.y * scale};
}

// Initializes Renderer::RenderToWorld.
Vec2f Renderer::RenderToWorld(Vec2f render)
{
    return Vec2f{
        render.x / camera.zoom + camera.target.x,
        RENDER_HEIGHT - camera.target.y - (RENDER_HEIGHT - render.y) / camera.zoom};
}

// Initializes Renderer::WorldToRender.
Vec2f Renderer::WorldToRender(Vec2f world)
{
    return Vec2f{
        (world.x - camera.target.x) * camera.zoom,
        RENDER_HEIGHT - (RENDER_HEIGHT - world.y - camera.target.y) * camera.zoom};
}

// Initializes Renderer::ScreenToWorld.
Vec2f Renderer::ScreenToWorld(Vector2 screen)
{
    Vec2f render = ScreenToRender(screen);
    if (render.x < 0.0f || render.y < 0.0f)
        return {-1.0f, -1.0f};

    return RenderToWorld(render);
}

// Initializes Renderer::WorldToScreen.
Vec2f Renderer::WorldToScreen(Vec2f world)
{
    return RenderToScreen(WorldToRender(world));
}

// Adjusts camera or map-space geometry.
void Renderer::ClampCameraToMap(Vec2i mapSize)
{
    float mapW = static_cast<float>(mapSize.x * TILE_SIZE);
    float mapH = static_cast<float>(mapSize.y * TILE_SIZE);
    if (mapW <= 0.0f || mapH <= 0.0f)
        return;

    float minZoom = std::max(RENDER_WIDTH / mapW, RENDER_HEIGHT / mapH);
    camera.zoom = std::clamp(camera.zoom, minZoom, 2.5f);

    float visibleW = RENDER_WIDTH / camera.zoom;
    float visibleH = RENDER_HEIGHT / camera.zoom;
    float maxX = std::max(0.0f, mapW - visibleW);
    float minY = RENDER_HEIGHT - mapH;
    float maxY = RENDER_HEIGHT - visibleH;

    camera.target.x = std::clamp(camera.target.x, 0.0f, maxX);
    camera.target.y = std::clamp(camera.target.y, minY, maxY);
}

// Adjusts camera or map-space geometry.
void Renderer::CenterCameraOnWorld(Vec2f worldPoint, Vec2i mapSize)
{
    camera.target.x = worldPoint.x - (RENDER_WIDTH * 0.5f) / camera.zoom;
    camera.target.y = RENDER_HEIGHT - worldPoint.y - (RENDER_HEIGHT * 0.5f) / camera.zoom;
    ClampCameraToMap(mapSize);
}

// Adjusts camera or map-space geometry.
void Renderer::ZoomAtScreenPoint(Vector2 screen, float wheel, Vec2i mapSize)
{
    if (wheel == 0.0f)
        return;

    Vec2f render = ScreenToRender(screen);
    if (render.x < 0.0f || render.y < 0.0f)
        return;

    Vec2f worldBefore = RenderToWorld(render);
    camera.zoom += wheel * 0.12f;
    ClampCameraToMap(mapSize);

    camera.target.x = worldBefore.x - render.x / camera.zoom;
    camera.target.y = RENDER_HEIGHT - worldBefore.y - (RENDER_HEIGHT - render.y) / camera.zoom;
    ClampCameraToMap(mapSize);
}

// Clears this runtime state.
void Renderer::ClearLayers()
{
    for(auto& l : layers)
    {
        BeginTextureMode(l.fbo);
        ClearBackground(BLANK);
        EndTextureMode();
    }
}
