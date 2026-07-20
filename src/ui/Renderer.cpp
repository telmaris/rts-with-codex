#include "ui/Renderer.h"
#include "economy/Building.h"

#include <algorithm>
#include <cmath>

int ResolveAnimationFrame(const AnimationClip& clip, float elapsedTime)
{
    if (clip.frameCount <= 1)
        return clip.startFrameId;  // Static: always the same frame.

    float totalDuration = clip.frameCount * clip.frameTime;
    float normalizedTime = clip.looping ? std::fmod(elapsedTime, totalDuration)
                                         : std::min(elapsedTime, totalDuration);

    int frameIndex = static_cast<int>(normalizedTime / clip.frameTime);
    frameIndex = std::clamp(frameIndex, 0, clip.frameCount - 1);
    return clip.startFrameId + frameIndex;
}

CanvasLayer::CanvasLayer()
{
    fbo = LoadRenderTexture(RENDER_WIDTH, RENDER_HEIGHT);
}

void TextureAtlas::LoadTextureAtlas(const char* path, Vec2i tileSize)
{
    tex = LoadTexture(path);
    size = tileSize;
    dim = {tex.width / size.x, tex.height / size.y};

    Log::Msg("[Texture Atlas]", "Loaded. Size: [", tex.width, ", ", tex.height, "] Dimensions: [", dim.x, ", ", dim.y, "]");
}

Rectangle TextureAtlas::GetRectFromId(int id)
{
    Rectangle rect;
    id = std::clamp(id, 0, std::max(0, dim.x * dim.y - 1));

    rect.height = size.y;
    rect.width = size.x;

    rect.x = (id % dim.x) * rect.width;
    rect.y = (id / dim.x) * rect.height;

    return rect;
}

void TextureAtlas::RegisterAnimation(int clipId, const AnimationClip& clip)
{
    animations[clipId] = clip;
}

AnimationClip TextureAtlas::GetAnimation(int clipId) const
{
    auto it = animations.find(clipId);
    return it != animations.end() ? it->second : AnimationClip{};
}

int TextureAtlas::GetFrameForAnimation(int clipId, float elapsedTime) const
{
    return ResolveAnimationFrame(GetAnimation(clipId), elapsedTime);
}

Renderer::Renderer()
{
    camera.offset = {0, 0};
    camera.target = {0 * TILE_SIZE, 0 * TILE_SIZE};
    camera.zoom = 1.25f;
    camera.rotation = 0.0f;
}

namespace
{
    float ScreenTopPaddingToRender(float screenPadding)
    {
        float scale = std::min(GetScreenWidth() / static_cast<float>(RENDER_WIDTH),
                               GetScreenHeight() / static_cast<float>(RENDER_HEIGHT));
        if (scale <= 0.0f)
            return 0.0f;

        float height = RENDER_HEIGHT * scale;
        float offsetY = (GetScreenHeight() - height) * 0.5f;
        return std::clamp((screenPadding - offsetY) / scale, 0.0f, static_cast<float>(RENDER_HEIGHT - 1));
    }
}

// Draws all cached world layers and UI widgets to the window, then presents.
void Renderer::Draw(std::vector<UiWidget*> ui, double dt)
{
    DrawContent(std::move(ui), dt);
    PresentFrame();
}

// Issues all draw calls but does not present. See header for the locking rationale.
void Renderer::DrawContent(std::vector<UiWidget*> ui, double dt)
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
}

// Presents the frame. EndDrawing performs the vsync / frame-cap wait, so any
// world lock the caller held for drawing must already be released here.
void Renderer::PresentFrame()
{
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

// Draws one atlas tile, resolving the frame from an animation clip and elapsed time.
void Renderer::DrawAtlasTile(int atlas, int clipId, Vec2f pos, float elapsedTime)
{
    auto& at = atlasMap[atlas];
    DrawAtlasTile(atlas, at.GetFrameForAnimation(clipId, elapsedTime), pos);
}

// Same, stretched to a target world size.
void Renderer::DrawAtlasTile(int atlas, int clipId, Vec2f pos, Vec2f drawSize, float elapsedTime)
{
    auto& at = atlasMap[atlas];
    DrawAtlasTile(atlas, at.GetFrameForAnimation(clipId, elapsedTime), pos, drawSize);
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

void Renderer::RegisterBuildingAnimation(BuildingType type, const AnimationClip& clip)
{
    buildingAnimations[type] = clip;
}

// Draws a building texture sized to its footprint, animated by its lifetime
// (ETAP 5.4) if a clip is registered for its type — otherwise identical to
// the static overload.
void Renderer::DrawBuildingTexture(Building* building, Vec2f pos, Color tint)
{
    if (building == nullptr)
        return;

    DrawBuildingTexture(building->buildingType, building->GetFootprint(), pos, tint,
                         static_cast<float>(building->GetLifetime()));
}

// Draws a building snapshot with its standalone texture. `tint` modulates the
// sprite (WHITE = unchanged); a dim tint marks buildings still under construction.
void Renderer::DrawBuildingTexture(BuildingType type, Vec2i footprint, Vec2f pos, Color tint)
{
    Vec2f drawSize{
        static_cast<float>(footprint.x * TILE_SIZE),
        static_cast<float>(footprint.y * TILE_SIZE)};

    // Channel-wise multiply so the fallback shapes fade with the same tint the
    // GPU applies to textured sprites.
    auto modulate = [&](Color base)
    {
        return Color{
            static_cast<unsigned char>(base.r * tint.r / 255),
            static_cast<unsigned char>(base.g * tint.g / 255),
            static_cast<unsigned char>(base.b * tint.b / 255),
            static_cast<unsigned char>(base.a * tint.a / 255)};
    };

    auto textureIt = buildingTextures.find(type);
    if (textureIt != buildingTextures.end())
    {
        Texture2D texture = textureIt->second;
        Rectangle src{0.0f, 0.0f, static_cast<float>(texture.width), -static_cast<float>(texture.height)};
        Rectangle dest{pos.x, RENDER_HEIGHT - drawSize.y - pos.y, drawSize.x, drawSize.y};
        DrawTexturePro(texture, src, dest, {0.0f, 0.0f}, 0.0f, tint);
        return;
    }

    Rectangle dest{pos.x, RENDER_HEIGHT - drawSize.y - pos.y, drawSize.x, drawSize.y};
    DrawRectangleRounded(dest, 0.04f, 8, modulate(Color{96, 78, 56, 255}));
    DrawRectangleRoundedLines(dest, 0.04f, 8, 1.0f, modulate(Color{150, 108, 58, 255}));
}

// Same, but reads the frame from the type's registered animation clip
// (elapsedTime is normally the building's lifetime). Types with no clip, or
// a clip with frameCount==1, fall through to the static overload unchanged.
void Renderer::DrawBuildingTexture(BuildingType type, Vec2i footprint, Vec2f pos, Color tint, float elapsedTime)
{
    auto animIt = buildingAnimations.find(type);
    auto textureIt = buildingTextures.find(type);
    if (animIt == buildingAnimations.end() || animIt->second.frameCount <= 1 || textureIt == buildingTextures.end())
    {
        DrawBuildingTexture(type, footprint, pos, tint);
        return;
    }

    const AnimationClip& clip = animIt->second;
    Texture2D texture = textureIt->second;
    int frame = ResolveAnimationFrame(clip, elapsedTime);
    float frameWidth = static_cast<float>(texture.width) / clip.frameCount;

    Vec2f drawSize{
        static_cast<float>(footprint.x * TILE_SIZE),
        static_cast<float>(footprint.y * TILE_SIZE)};

    Rectangle src{frameWidth * frame, 0.0f, frameWidth, -static_cast<float>(texture.height)};
    Rectangle dest{pos.x, RENDER_HEIGHT - drawSize.y - pos.y, drawSize.x, drawSize.y};
    DrawTexturePro(texture, src, dest, {0.0f, 0.0f}, 0.0f, tint);
}

// Draws terrain, territory and buildings from an immutable game snapshot.
void Renderer::DrawSnapshot(const GameSnapshot& snapshot)
{
    if (!snapshot.IsValid())
        return;

    bool cameraChanged =
        cachedSnapshotCameraZoom != camera.zoom ||
        cachedSnapshotCameraTarget.x != camera.target.x ||
        cachedSnapshotCameraTarget.y != camera.target.y;
    bool snapshotChanged = cachedSnapshotTick != snapshot.simulationTick;
    if (!cameraChanged && !snapshotChanged)
        return;

    Vec2f worldA = RenderToWorld({0.0f, 0.0f});
    Vec2f worldB = RenderToWorld({static_cast<float>(RENDER_WIDTH), static_cast<float>(RENDER_HEIGHT)});
    float minWorldX = std::min(worldA.x, worldB.x);
    float maxWorldX = std::max(worldA.x, worldB.x);
    float minWorldY = std::min(worldA.y, worldB.y);
    float maxWorldY = std::max(worldA.y, worldB.y);

    int minTileX = std::clamp(static_cast<int>(std::floor(minWorldX / TILE_SIZE)) - 2, 0, snapshot.mapSize.x - 1);
    int maxTileX = std::clamp(static_cast<int>(std::ceil(maxWorldX / TILE_SIZE)) + 2, 0, snapshot.mapSize.x - 1);
    int minTileY = std::clamp(static_cast<int>(std::floor(minWorldY / TILE_SIZE)) - 2, 0, snapshot.mapSize.y - 1);
    int maxTileY = std::clamp(static_cast<int>(std::ceil(maxWorldY / TILE_SIZE)) + 2, 0, snapshot.mapSize.y - 1);

    ClearLayer(0);
    BeginLayer(0);
    for (int x = minTileX; x <= maxTileX; x++)
    {
        for (int y = minTileY; y <= maxTileY; y++)
        {
            const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
            Vec2f pos = {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};
            DrawAtlasTile(0, tile.terrainTextureId, pos);
            if (tile.isMilitaryRoad)
            {
                DrawRectangle(static_cast<int>(pos.x),
                              static_cast<int>(RENDER_HEIGHT - TILE_SIZE - pos.y),
                              TILE_SIZE,
                              TILE_SIZE,
                              Color{139, 90, 43, 150});
            }
        }
    }
    EndLayer();

    ClearLayer(1);
    BeginLayer(1);
    for (int x = minTileX; x <= maxTileX; x++)
    {
        for (int y = minTileY; y <= maxTileY; y++)
        {
            const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
            if (!tile.hasBuilding)
                continue;

            Vec2f pos = {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};
            DrawBuildingTexture(tile.buildingType, tile.buildingFootprint, pos);
        }
    }
    EndLayer();

    cachedSnapshotTick = snapshot.simulationTick;
    cachedSnapshotCameraTarget = {camera.target.x, camera.target.y};
    cachedSnapshotCameraZoom = camera.zoom;
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

    float topRenderPadding = ScreenTopPaddingToRender(topScreenPadding);
    float usableRenderHeight = std::max(1.0f, static_cast<float>(RENDER_HEIGHT) - topRenderPadding);
    float minZoom = std::max(RENDER_WIDTH / mapW, usableRenderHeight / mapH);
    camera.zoom = std::clamp(camera.zoom, minZoom, 2.5f);

    float visibleW = RENDER_WIDTH / camera.zoom;
    float visibleH = usableRenderHeight / camera.zoom;
    float maxX = std::max(0.0f, mapW - visibleW);
    float minY = RENDER_HEIGHT - mapH;
    float maxY = RENDER_HEIGHT - visibleH;

    camera.target.x = std::clamp(camera.target.x, 0.0f, maxX);
    camera.target.y = std::clamp(camera.target.y, minY, maxY);
}

void Renderer::SetTopScreenPadding(float padding)
{
    topScreenPadding = std::max(0.0f, padding);
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
