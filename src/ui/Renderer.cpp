#include "ui/Renderer.h"
#include "core/FogOfWar.h"
#include "core/Log.h"
#include "economy/Building.h"
#include "economy/Player.h"

#include <algorithm>
#include <cmath>

namespace
{
    bool fogOfWarPreferenceEnabled = true;
    bool colorGradingPreferenceEnabled = true;
    bool retroFilterPreferenceEnabled = true;
    bool localLightBloomPreferenceEnabled = true;
    bool rainOverlayPreferenceEnabled = false;
    bool logisticsOverlayPreferenceEnabled = false;

    // Terrain is composed from individual TILE_SIZE-square quads. Keeping a
    // tile an integral number of render pixels wide prevents adjacent quads
    // from falling on opposite sides of a pixel when zoomed out.
    float SnapZoomToTileGrid(float zoom)
    {
        const float pixelsPerTile = std::max(1.0f,
            std::round(zoom * static_cast<float>(TILE_SIZE)));
        return pixelsPerTile / static_cast<float>(TILE_SIZE);
    }

    void SnapCameraTargetToRenderPixels(Camera2D& camera)
    {
        if (camera.zoom <= 0.0f)
            return;

        camera.target.x = std::round(camera.target.x * camera.zoom) / camera.zoom;
        camera.target.y = std::round(camera.target.y * camera.zoom) / camera.zoom;
    }

    // Bakery smoke is deliberately a separate visual effect instead of an
    // animated sprite sheet: the building remains completely stable while
    // only the smoke rises and disperses above its chimney.
    void DrawBakerySmoke(Vec2f position, Vec2i footprint, float elapsedTime)
    {
        const float width = static_cast<float>(footprint.x * TILE_SIZE);
        const float height = static_cast<float>(footprint.y * TILE_SIZE);
        const float chimneyX = position.x + width * 0.76f;
        const float chimneyY = RENDER_HEIGHT - position.y - height * 0.86f;
        constexpr float SmokeCycleSeconds = 2.4f;
        const float phaseOffset = std::fmod(position.x * 0.0031f + position.y * 0.0017f,
                                             SmokeCycleSeconds);

        for (int puff = 0; puff < 3; ++puff)
        {
            float age = std::fmod(elapsedTime + phaseOffset + puff * 0.72f,
                                  SmokeCycleSeconds) / SmokeCycleSeconds;
            if (age < 0.0f)
                age += 1.0f;

            const float drift = std::sin((age + static_cast<float>(puff) * 0.31f) * 5.2f) *
                                (2.0f + age * 4.0f);
            const float x = chimneyX + drift;
            const float y = chimneyY - 3.0f - age * 30.0f;
            const float radius = 3.0f + age * 5.0f;
            const float fade = 1.0f - age;
            const unsigned char alpha = static_cast<unsigned char>(42.0f + fade * 68.0f);
            DrawCircleGradient(static_cast<int>(std::round(x)), static_cast<int>(std::round(y)), radius,
                               Color{196, 202, 212, alpha}, Color{156, 165, 178, 0});
        }
    }

    void DrawDamageFlashOverlay(Vec2f position, Vec2i footprint, float remainingSeconds)
    {
        if (remainingSeconds <= 0.0f)
            return;
        const float width = footprint.x * TILE_SIZE;
        const float height = footprint.y * TILE_SIZE;
        const float top = RENDER_HEIGHT - position.y - height;
        const float pulse = 0.45f + 0.55f * std::abs(std::sin(static_cast<float>(GetTime()) * 10.0f));
        const unsigned char fillAlpha = static_cast<unsigned char>(18.0f + pulse * 28.0f);
        const unsigned char lineAlpha = static_cast<unsigned char>(125.0f + pulse * 110.0f);
        DrawRectangle(static_cast<int>(position.x), static_cast<int>(top),
                      static_cast<int>(width), static_cast<int>(height), Color{255, 58, 32, fillAlpha});
        DrawRectangleLinesEx({position.x - 2.0f, top - 2.0f, width + 4.0f, height + 4.0f},
                             2.5f, Color{255, 110, 72, lineAlpha});
    }

    void DrawRoadUtilizationOverlay(Vec2f position, float utilization,
                                    bool left, bool right, bool up, bool down)
    {
        utilization = std::clamp(utilization, 0.0f, 1.0f);
        if (utilization <= 0.01f)
            return;

        Color color{};
        if (utilization < 0.55f)
        {
            float t = utilization / 0.55f;
            color = Color{static_cast<unsigned char>(68.0f + t * 150.0f),
                          static_cast<unsigned char>(172.0f + t * 18.0f), 102, 255};
        }
        else
        {
            float t = (utilization - 0.55f) / 0.45f;
            color = Color{218, static_cast<unsigned char>(190.0f - t * 112.0f),
                          static_cast<unsigned char>(88.0f - t * 30.0f), 255};
        }
        const float top = RENDER_HEIGHT - position.y - TILE_SIZE;
        const Vector2 center{position.x + TILE_SIZE * 0.5f, top + TILE_SIZE * 0.5f};
        const float thickness = 10.0f + utilization * 8.0f;
        const float bloomScale = IsLocalLightBloomPreferenceEnabled() ? 1.25f : 1.0f;
        const Color outerGlow{color.r, color.g, color.b,
                              static_cast<unsigned char>(11.0f + utilization * 15.0f)};
        const Color innerGlow{color.r, color.g, color.b,
                              static_cast<unsigned char>(22.0f + utilization * 20.0f)};
        const Color fill{color.r, color.g, color.b,
                         static_cast<unsigned char>(58.0f + utilization * 42.0f)};
        BeginBlendMode(BLEND_ADDITIVE);
        const auto drawSegment = [&](Vector2 end)
        {
            DrawLineEx(center, end, thickness * 5.0f * bloomScale, outerGlow);
            DrawLineEx(center, end, thickness * 2.5f, innerGlow);
            DrawLineEx(center, end, thickness, fill);
        };

        if (left)  drawSegment({position.x, center.y});
        if (right) drawSegment({position.x + TILE_SIZE, center.y});
        if (up)    drawSegment({center.x, top + TILE_SIZE});
        if (down)  drawSegment({center.x, top});
        DrawCircleV(center, thickness * 2.50f * bloomScale, outerGlow);
        DrawCircleV(center, thickness * 1.25f, innerGlow);
        if (!left && !right && !up && !down)
            DrawCircleV(center, thickness * 0.55f, fill);
        else
            DrawCircleV(center, thickness * 0.50f, fill);
        EndBlendMode();
    }

    void DrawRoadSaturationIndicator(Vec2f position, bool saturated)
    {
        if (!saturated)
            return;
        const float top = RENDER_HEIGHT - position.y - TILE_SIZE;
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 6.0f);
        const Vector2 center{position.x + TILE_SIZE - 6.0f, top + 6.0f};
        DrawCircleV(center, 3.5f + pulse * 1.0f, Color{232, 91, 48, 255});
        DrawCircleV(center, 1.5f, Color{255, 222, 128, 255});
    }
}

bool IsFogOfWarPreferenceEnabled()
{
    return fogOfWarPreferenceEnabled;
}

void SetFogOfWarPreferenceEnabled(bool enabled)
{
    fogOfWarPreferenceEnabled = enabled;
}

bool IsColorGradingPreferenceEnabled()
{
    return colorGradingPreferenceEnabled;
}

void SetColorGradingPreferenceEnabled(bool enabled)
{
    colorGradingPreferenceEnabled = enabled;
}

bool IsRetroFilterPreferenceEnabled()
{
    return retroFilterPreferenceEnabled;
}

void SetRetroFilterPreferenceEnabled(bool enabled)
{
    retroFilterPreferenceEnabled = enabled;
}

bool IsLocalLightBloomPreferenceEnabled()
{
    return localLightBloomPreferenceEnabled;
}

void SetLocalLightBloomPreferenceEnabled(bool enabled)
{
    localLightBloomPreferenceEnabled = enabled;
}

bool IsRainOverlayPreferenceEnabled()
{
    return rainOverlayPreferenceEnabled;
}

void SetRainOverlayPreferenceEnabled(bool enabled)
{
    rainOverlayPreferenceEnabled = enabled;
}

bool IsLogisticsOverlayPreferenceEnabled()
{
    return logisticsOverlayPreferenceEnabled;
}

void SetLogisticsOverlayPreferenceEnabled(bool enabled)
{
    logisticsOverlayPreferenceEnabled = enabled;
}

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

void CanvasLayer::Initialize(int width, int height)
{
    Shutdown();
    fbo = LoadRenderTexture(width, height);
}

void CanvasLayer::Shutdown()
{
    if (fbo.id != 0 && IsWindowReady())
        UnloadRenderTexture(fbo);
    fbo = {};
}

void TextureAtlas::LoadTextureAtlas(const char* path, Vec2i tileSize)
{
    tex = LoadTexture(path);
    // World atlases contain neighbouring, unrelated cells. Point sampling
    // keeps their pixel-art edges discrete instead of blending from a
    // neighbour while the map is minified.
    if (tex.id != 0)
        SetTextureFilter(tex, TEXTURE_FILTER_POINT);
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

bool Renderer::InitializeWorldLayers()
{
    if (worldLayersInitialized)
        return true;

    if (!IsWindowReady())
    {
        Log::Msg("[Renderer]", "Cannot initialize world layers before InitWindow().");
        return false;
    }

    for (auto& layer : layers)
    {
        layer.Initialize();
        if (!layer.IsInitialized())
        {
            Log::Msg("[Renderer]", "Failed to allocate a world render layer.");
            Shutdown();
            return false;
        }
    }

    worldComposite.Initialize();
    if (!worldComposite.IsInitialized())
    {
        Log::Msg("[Renderer]", "Failed to allocate the world composite target.");
        Shutdown();
        return false;
    }

    litWorld.Initialize();
    if (!litWorld.IsInitialized())
    {
        Log::Msg("[Renderer] Failed to allocate the lit world target.");
        Shutdown();
        return false;
    }

    // Keep the light buffer at the same resolution as the world composite.
    // When this was half-resolution, the additive falloff was resampled at a
    // different pixel grid from the building sprite.  A light could therefore
    // look brighter or dimmer after a small pan/zoom even though neither its
    // world position nor intensity had changed.
    lightMap.Initialize(RENDER_WIDTH, RENDER_HEIGHT);
    if (!lightMap.IsInitialized())
    {
        Log::Msg("[Renderer] Failed to allocate the light map target.");
        Shutdown();
        return false;
    }

    fogMask.Initialize(RENDER_WIDTH / 2, RENDER_HEIGHT / 2);
    if (!fogMask.IsInitialized())
    {
        Log::Msg("[Renderer] Failed to allocate the fog mask target.");
        Shutdown();
        return false;
    }

    foggedWorld.Initialize();
    if (!foggedWorld.IsInitialized())
    {
        Log::Msg("[Renderer] Failed to allocate the fog compositing target.");
        Shutdown();
        return false;
    }

    postProcessedWorld.Initialize();
    if (!postProcessedWorld.IsInitialized())
    {
        Log::Msg("[Renderer] Failed to allocate the world postprocess target.");
        Shutdown();
        return false;
    }

    // Team colors remain optional until an individual material provides a
    // matching mask texture. A shader compile error must not prevent gameplay.
    shaderLibrary.LoadFragment(ShaderId::TeamColor, "assets/shaders/team_color.fs");
    shaderLibrary.LoadFragment(ShaderId::WorldLighting, "assets/shaders/world_lighting.fs");
    shaderLibrary.LoadFragment(ShaderId::RadialLight, "assets/shaders/radial_light.fs");
    shaderLibrary.LoadFragment(ShaderId::FogOfWar, "assets/shaders/fog_of_war.fs");
    shaderLibrary.LoadFragment(ShaderId::FogRoad, "assets/shaders/fog_road.fs");
    shaderLibrary.LoadFragment(ShaderId::WorldPostProcess, "assets/shaders/world_postprocess.fs");

    constexpr const char* RadialLightMaskPath = "assets/light/radial_light_mask_1024.png";
    if (FileExists(RadialLightMaskPath))
    {
        radialLightMask = LoadTexture(RadialLightMaskPath);
        if (radialLightMask.id == 0)
            Log::Msg("[Renderer] Failed to load radial light mask: ", RadialLightMaskPath);
    }
    else
    {
        Log::Msg("[Renderer] Radial light mask not found: ", RadialLightMaskPath);
    }

    worldLayersInitialized = true;
    return true;
}

void Renderer::Shutdown()
{
    layerActive = false;

    shaderLibrary.Shutdown();

    for (auto& layer : layers)
        layer.Shutdown();
    worldComposite.Shutdown();
    litWorld.Shutdown();
    lightMap.Shutdown();
    fogMask.Shutdown();
    foggedWorld.Shutdown();
    postProcessedWorld.Shutdown();
    worldLayersInitialized = false;

    if (IsWindowReady())
    {
        for (auto& [type, texture] : buildingTextures)
        {
            if (texture.id != 0)
                UnloadTexture(texture);
        }

        for (auto& [atlasId, atlas] : atlasMap)
        {
            if (atlas.tex.id != 0)
                UnloadTexture(atlas.tex);
        }

        if (radialLightMask.id != 0)
            UnloadTexture(radialLightMask);
    }

    buildingTextures.clear();
    buildingAnimations.clear();
    atlasMap.clear();
    radialLightMask = {};
    dynamicLights.clear();
    fogReveals.clear();
    cachedSnapshotTick = std::numeric_limits<std::uint64_t>::max();
    cachedSnapshotCameraTarget = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    cachedSnapshotCameraZoom = -1.0f;
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

void Renderer::ClearDynamicLights()
{
    dynamicLights.clear();
}

void Renderer::ClearFogReveals()
{
    fogReveals.clear();
}

void Renderer::QueueFogReveal(const FogRevealView& reveal)
{
    if (reveal.radiusWorld <= 0.0f)
        return;

    // Cull by the whole influence circle, never by source position. A building
    // just outside the camera can still reveal a large portion of the screen.
    const Vec2f renderPosition = WorldToRender({reveal.worldPosition.x, reveal.worldPosition.y});
    // Keep a small guard for the shader's animated edge deformation.
    const float radiusRender = reveal.radiusWorld * std::max(0.0f, camera.zoom) * 1.05f;
    if (renderPosition.x + radiusRender < 0.0f ||
        renderPosition.x - radiusRender > static_cast<float>(RENDER_WIDTH) ||
        renderPosition.y + radiusRender < 0.0f ||
        renderPosition.y - radiusRender > static_cast<float>(RENDER_HEIGHT))
        return;

    fogReveals.push_back(reveal);
}

void Renderer::QueueBuildingFogReveal(BuildingType type, Vec2i footprint, Vec2f pos)
{
    const float radius = FogOfWar::BuildingRevealRadiusWorld(type, footprint);
    QueueFogReveal({{pos.x + footprint.x * TILE_SIZE * 0.50f,
                     pos.y + footprint.y * TILE_SIZE * 0.50f}, radius});
}

void Renderer::CycleDebugView()
{
    switch (renderSettings.debugView)
    {
        case RenderDebugView::Final:
            renderSettings.debugView = RenderDebugView::WorldAlbedo;
            break;
        case RenderDebugView::WorldAlbedo:
            renderSettings.debugView = RenderDebugView::LightMap;
            break;
        case RenderDebugView::LightMap:
            renderSettings.debugView = RenderDebugView::FogMask;
            break;
        case RenderDebugView::FogMask:
            renderSettings.debugView = RenderDebugView::Final;
            break;
    }
}

void Renderer::QueueDynamicLight(const LightEmitterView& light)
{
    constexpr size_t MaxDynamicLights = 192;
    if (light.radiusWorld <= 0.0f || light.intensity <= 0.0f)
        return;

    const Vec2f renderPosition = WorldToRender({light.worldPosition.x, light.worldPosition.y});
    const float radiusRender = light.radiusWorld * std::max(0.0f, camera.zoom) * 1.05f;
    if (renderPosition.x + radiusRender < 0.0f ||
        renderPosition.x - radiusRender > static_cast<float>(RENDER_WIDTH) ||
        renderPosition.y + radiusRender < 0.0f ||
        renderPosition.y - radiusRender > static_cast<float>(RENDER_HEIGHT))
        return;

    if (dynamicLights.size() < MaxDynamicLights)
    {
        dynamicLights.push_back(light);
        return;
    }

    auto lowestPriority = std::min_element(dynamicLights.begin(), dynamicLights.end(),
                                           [](const LightEmitterView& lhs, const LightEmitterView& rhs) {
                                               return lhs.priority < rhs.priority;
                                           });
    if (lowestPriority != dynamicLights.end() && light.priority > lowestPriority->priority)
        *lowestPriority = light;
}

void Renderer::QueueBuildingLight(BuildingType type, Vec2i footprint, Vec2f pos, int stableId, bool isOperational)
{
    if (!isOperational || IsRoadLike(type))
        return;

    LightEmitterView light;
    switch (type)
    {
        case BuildingType::Foundry:
            // The furnace itself is animated and brightly coloured. Anchor
            // the environmental glow at the chimney instead, so the sprite's
            // coloured work area is not mistaken for a flashing light source.
            light = {{pos.x + footprint.x * TILE_SIZE * 0.49f, pos.y + footprint.y * TILE_SIZE * 0.86f},
                     Color{255, 164, 88, 255}, 272.0f, 1.75f, 0.70f, 0.0f, stableId, 30};
            break;
        case BuildingType::Smith:
            // The smithy is entered from the lower facade; keep the glow at
            // that doorway rather than on the team-coloured roof details.
            light = {{pos.x + footprint.x * TILE_SIZE * 0.50f, pos.y + footprint.y * TILE_SIZE * 0.22f},
                     Color{255, 184, 112, 255}, 208.0f, 1.30f, 0.68f, 0.0f, stableId, 20};
            break;
        case BuildingType::Inn:
            // A warm inn light reads as lamplight from the upper chimney,
            // not as an emission from its painted trim.
            light = {{pos.x + footprint.x * TILE_SIZE * 0.76f, pos.y + footprint.y * TILE_SIZE * 0.86f},
                     Color{255, 196, 126, 255}, 192.0f, 1.10f, 0.65f, 0.0f, stableId, 10};
            break;
        default:
        {
            // Every finished building needs a small, steady night-time pool
            // of light so it remains legible. This is an environmental
            // fill/light from its occupied plot, not an emissive sample of
            // any coloured sprite pixel, so painted roofs and trims cannot
            // start appearing to blink.
            const float largestDimension = static_cast<float>(std::max(footprint.x, footprint.y));
            light = {{pos.x + footprint.x * TILE_SIZE * 0.50f,
                      pos.y + footprint.y * TILE_SIZE * 0.54f},
                     Color{255, 200, 132, 255}, 96.0f + largestDimension * 40.0f,
                     0.90f, 0.64f, 0.0f, stableId, 5};
            break;
        }
    }
    QueueDynamicLight(light);
}

void Renderer::DrawLightMap(const WorldLightingFrame& lighting)
{
    if (!lightMap.IsInitialized())
        return;

    BeginTextureMode(lightMap.fbo);
    ClearBackground(BLACK);

    const Shader* radialLightShader = shaderLibrary.Find(ShaderId::RadialLight);
    if (radialLightShader != nullptr && radialLightMask.id != 0 && lighting.localLightVisibility > 0.0f)
    {
        const float mapScale = lightMap.fbo.texture.width / static_cast<float>(RENDER_WIDTH);
        int colorLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "lightColor");
        int intensityLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "intensity");
        int animationTimeLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "animationTime");
        int stablePhaseLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "stablePhase");
        int animationAmountLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "animationAmount");
        int maskOnlyLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "maskOnly");
        const float animationTime = static_cast<float>(simulationTick) * 0.01f;
        const int maskOnly = 0;

        BeginBlendMode(BLEND_ADDITIVE);
        BeginShaderMode(*radialLightShader);
        if (animationTimeLocation >= 0)
            SetShaderValue(*radialLightShader, animationTimeLocation, &animationTime, SHADER_UNIFORM_FLOAT);
        if (maskOnlyLocation >= 0)
            SetShaderValue(*radialLightShader, maskOnlyLocation, &maskOnly, SHADER_UNIFORM_INT);
        for (const LightEmitterView& light : dynamicLights)
        {
            Vec2f renderPosition = WorldToRender({light.worldPosition.x, light.worldPosition.y});
            float radius = std::max(1.0f, light.radiusWorld * camera.zoom * mapScale);
            float flickerPhase = static_cast<float>(simulationTick % 100000) * 0.071f +
                                 static_cast<float>(light.stableId) * 0.618f;
            float flicker = 1.0f + std::sin(flickerPhase) * std::clamp(light.flickerAmount, 0.0f, 1.0f);
            float color[4]{light.color.r / 255.0f, light.color.g / 255.0f, light.color.b / 255.0f, 1.0f};
            float intensity = light.intensity * lighting.localLightVisibility * flicker;
            float stablePhase = static_cast<float>(light.stableId) * 0.6180339f;
            // Buildings are deliberately steady. Only explicitly flickering
            // emitters (currently projectiles) animate their radial edge.
            float animationAmount = std::clamp(light.flickerAmount, 0.0f, 1.0f) * 2.0f;
            if (colorLocation >= 0)
                SetShaderValue(*radialLightShader, colorLocation, color, SHADER_UNIFORM_VEC4);
            if (intensityLocation >= 0)
                SetShaderValue(*radialLightShader, intensityLocation, &intensity, SHADER_UNIFORM_FLOAT);
            if (stablePhaseLocation >= 0)
                SetShaderValue(*radialLightShader, stablePhaseLocation, &stablePhase, SHADER_UNIFORM_FLOAT);
            if (animationAmountLocation >= 0)
                SetShaderValue(*radialLightShader, animationAmountLocation, &animationAmount, SHADER_UNIFORM_FLOAT);
            // WorldToRender uses the world's bottom-up Y convention, while a
            // render texture is written in top-down framebuffer coordinates.
            // Flip once here; sampling the light map in the world composite
            // then follows the same FBO orientation as worldComposite.
            float lightMapY = (static_cast<float>(RENDER_HEIGHT) - renderPosition.y) * mapScale;
            Rectangle source{0.0f, 0.0f, static_cast<float>(radialLightMask.width),
                             -static_cast<float>(radialLightMask.height)};
            Rectangle destination{(renderPosition.x * mapScale) - radius,
                                  lightMapY - radius,
                                  radius * 2.0f,
                                  radius * 2.0f};
            DrawTexturePro(radialLightMask, source, destination, {0.0f, 0.0f}, 0.0f, WHITE);
        }
        EndShaderMode();
        EndBlendMode();
    }
    EndTextureMode();
}

void Renderer::DrawFogMask()
{
    if (!fogMask.IsInitialized())
        return;

    BeginTextureMode(fogMask.fbo);
    ClearBackground(BLACK);

    const float mapScale = fogMask.fbo.texture.width / static_cast<float>(RENDER_WIDTH);
    const Shader* radialMaskShader = shaderLibrary.Find(ShaderId::RadialLight);
    int animationTimeLocation = -1;
    int stablePhaseLocation = -1;
    int animationAmountLocation = -1;
    int maskOnlyLocation = -1;
    float animationTime = static_cast<float>(simulationTick) * 0.01f;
    const float animationAmount = 0.85f;
    const int maskOnly = 1;
    if (radialMaskShader != nullptr && radialLightMask.id != 0)
    {
        animationTimeLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "animationTime");
        stablePhaseLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "stablePhase");
        animationAmountLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "animationAmount");
        maskOnlyLocation = shaderLibrary.GetLocation(ShaderId::RadialLight, "maskOnly");
        BeginShaderMode(*radialMaskShader);
        if (animationTimeLocation >= 0)
            SetShaderValue(*radialMaskShader, animationTimeLocation, &animationTime, SHADER_UNIFORM_FLOAT);
        if (animationAmountLocation >= 0)
            SetShaderValue(*radialMaskShader, animationAmountLocation, &animationAmount, SHADER_UNIFORM_FLOAT);
        if (maskOnlyLocation >= 0)
            SetShaderValue(*radialMaskShader, maskOnlyLocation, &maskOnly, SHADER_UNIFORM_INT);
    }

    for (const FogRevealView& reveal : fogReveals)
    {
        Vec2f renderPosition = WorldToRender({reveal.worldPosition.x, reveal.worldPosition.y});
        const float radius = std::max(1.0f, reveal.radiusWorld * camera.zoom * mapScale);
        const float centerX = renderPosition.x * mapScale;
        // Match the light-map convention: world rendering is bottom-up while
        // the mask is authored directly in framebuffer (top-down) space.
        const float centerY = (static_cast<float>(RENDER_HEIGHT) - renderPosition.y) * mapScale;

        if (radialLightMask.id != 0)
        {
            if (radialMaskShader != nullptr && stablePhaseLocation >= 0)
            {
                float stablePhase = reveal.worldPosition.x * 0.0137f +
                                    reveal.worldPosition.y * 0.0191f;
                SetShaderValue(*radialMaskShader, stablePhaseLocation, &stablePhase, SHADER_UNIFORM_FLOAT);
            }
            // The supplied alpha-gradient texture gives fog revealers a
            // natural falloff instead of the old concentric primitive discs.
            Rectangle source{0.0f, 0.0f, static_cast<float>(radialLightMask.width),
                             -static_cast<float>(radialLightMask.height)};
            Rectangle destination{centerX - radius, centerY - radius, radius * 2.0f, radius * 2.0f};
            DrawTexturePro(radialLightMask, source, destination, {0.0f, 0.0f}, 0.0f, WHITE);
        }
        else
        {
            // The mask is optional at runtime; retain a readable fallback.
            DrawCircle(static_cast<int>(centerX), static_cast<int>(centerY), radius, WHITE);
        }
    }
    if (radialMaskShader != nullptr && radialLightMask.id != 0)
        EndShaderMode();
    EndTextureMode();
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

    if (worldLayersInitialized)
    {
        // OptionsScene can change this while a GameScene remains constructed.
        // Synchronizing at draw time makes fog enable/disable immediately
        // reflect the current world-derived revealers.
        renderSettings.fogOfWar = IsFogOfWarPreferenceEnabled();
        renderSettings.colorGrading = IsColorGradingPreferenceEnabled();
        renderSettings.retroFilter = IsRetroFilterPreferenceEnabled();
        renderSettings.localLightBloom = IsLocalLightBloomPreferenceEnabled();
        renderSettings.rainOverlay = IsRainOverlayPreferenceEnabled();
        renderSettings.logisticsOverlay = IsLogisticsOverlayPreferenceEnabled();
        BeginTextureMode(worldComposite.fbo);
        ClearBackground(BLANK);

        // A render texture is vertically inverted when sampled. The negative
        // source height preserves the existing layer-to-window orientation
        // while composing one FBO into another.
        Rectangle layerSource{0.0f, 0.0f, static_cast<float>(RENDER_WIDTH), -static_cast<float>(RENDER_HEIGHT)};
        Rectangle layerDestination{0.0f, 0.0f, static_cast<float>(RENDER_WIDTH), static_cast<float>(RENDER_HEIGHT)};
        for (std::size_t index = 0; index < layers.size(); ++index)
            DrawTexturePro(layers[index].fbo.texture, layerSource, layerDestination, {0.0f, 0.0f}, 0.0f, WHITE);
        EndTextureMode();

        Texture2D presentedWorld = worldComposite.fbo.texture;
        Rectangle presentedSource = src;
        const Shader* lightingShader = shaderLibrary.Find(ShaderId::WorldLighting);
        const bool hasLightingPass = (renderSettings.dayNightCycle || renderSettings.dynamicLights) &&
                                     lightingShader != nullptr;
        if (hasLightingPass)
        {
            WorldLightingFrame lighting = renderSettings.dayNightCycle
                ? ComputeWorldLighting(simulationTick, dayNightConfig)
                : WorldLightingFrame{};
            if (!renderSettings.dayNightCycle)
            {
                lighting.ambientColor = {1.0f, 1.0f, 1.0f};
                lighting.ambientIntensity = 1.0f;
                lighting.exposure = 1.0f;
                lighting.saturation = 1.0f;
                lighting.contrast = 1.0f;
                lighting.localLightVisibility = 1.0f;
            }
            if (!renderSettings.dynamicLights)
                lighting.localLightVisibility = 0.0f;
            DrawLightMap(lighting);
            BeginTextureMode(litWorld.fbo);
            ClearBackground(BLANK);
            BeginShaderMode(*lightingShader);

            int ambientColorLocation = shaderLibrary.GetLocation(ShaderId::WorldLighting, "ambientColor");
            int ambientIntensityLocation = shaderLibrary.GetLocation(ShaderId::WorldLighting, "ambientIntensity");
            int exposureLocation = shaderLibrary.GetLocation(ShaderId::WorldLighting, "exposure");
            int saturationLocation = shaderLibrary.GetLocation(ShaderId::WorldLighting, "saturation");
            int contrastLocation = shaderLibrary.GetLocation(ShaderId::WorldLighting, "contrast");
            int lightMapLocation = shaderLibrary.GetLocation(ShaderId::WorldLighting, "lightMap");
            int lightMapTexelSizeLocation = shaderLibrary.GetLocation(ShaderId::WorldLighting, "lightMapTexelSize");
            int localLightBloomEnabledLocation = shaderLibrary.GetLocation(ShaderId::WorldLighting, "localLightBloomEnabled");
            if (ambientColorLocation >= 0)
                SetShaderValue(*lightingShader, ambientColorLocation, &lighting.ambientColor, SHADER_UNIFORM_VEC3);
            if (ambientIntensityLocation >= 0)
                SetShaderValue(*lightingShader, ambientIntensityLocation, &lighting.ambientIntensity, SHADER_UNIFORM_FLOAT);
            if (exposureLocation >= 0)
                SetShaderValue(*lightingShader, exposureLocation, &lighting.exposure, SHADER_UNIFORM_FLOAT);
            if (saturationLocation >= 0)
                SetShaderValue(*lightingShader, saturationLocation, &lighting.saturation, SHADER_UNIFORM_FLOAT);
            if (contrastLocation >= 0)
                SetShaderValue(*lightingShader, contrastLocation, &lighting.contrast, SHADER_UNIFORM_FLOAT);
            if (lightMapLocation >= 0)
                SetShaderValueTexture(*lightingShader, lightMapLocation, lightMap.fbo.texture);
            const Vector2 lightMapTexelSize{
                1.0f / static_cast<float>(lightMap.fbo.texture.width),
                1.0f / static_cast<float>(lightMap.fbo.texture.height)};
            const int localLightBloomEnabled = renderSettings.localLightBloom ? 1 : 0;
            if (lightMapTexelSizeLocation >= 0)
                SetShaderValue(*lightingShader, lightMapTexelSizeLocation,
                               &lightMapTexelSize, SHADER_UNIFORM_VEC2);
            if (localLightBloomEnabledLocation >= 0)
                SetShaderValue(*lightingShader, localLightBloomEnabledLocation,
                               &localLightBloomEnabled, SHADER_UNIFORM_INT);

            DrawTexturePro(worldComposite.fbo.texture, layerSource, layerDestination, {0.0f, 0.0f}, 0.0f, WHITE);
            EndShaderMode();
            EndTextureMode();
            presentedWorld = litWorld.fbo.texture;
        }

        const Shader* fogShader = renderSettings.fogOfWar
            ? shaderLibrary.Find(ShaderId::FogOfWar)
            : nullptr;
        if (fogShader != nullptr)
        {
            DrawFogMask();
            BeginTextureMode(foggedWorld.fbo);
            ClearBackground(BLANK);
            BeginShaderMode(*fogShader);
            int fogMaskLocation = shaderLibrary.GetLocation(ShaderId::FogOfWar, "fogMask");
            if (fogMaskLocation >= 0)
                SetShaderValueTexture(*fogShader, fogMaskLocation, fogMask.fbo.texture);
            DrawTexturePro(presentedWorld, layerSource, layerDestination, {0.0f, 0.0f}, 0.0f, WHITE);
            EndShaderMode();
            EndTextureMode();
            presentedWorld = foggedWorld.fbo.texture;

        }

        const Shader* postProcessShader =
            (renderSettings.colorGrading || renderSettings.retroFilter || renderSettings.rainOverlay)
                ? shaderLibrary.Find(ShaderId::WorldPostProcess)
                : nullptr;
        if (postProcessShader != nullptr)
        {
            BeginTextureMode(postProcessedWorld.fbo);
            ClearBackground(BLANK);
            BeginShaderMode(*postProcessShader);

            const int colorGradingEnabled = renderSettings.colorGrading ? 1 : 0;
            const int retroFilterEnabled = renderSettings.retroFilter ? 1 : 0;
            const int rainOverlayEnabled = renderSettings.rainOverlay ? 1 : 0;
            const float animationTime = static_cast<float>(simulationTick) * 0.01f;
            const Vector2 resolution{static_cast<float>(RENDER_WIDTH),
                                     static_cast<float>(RENDER_HEIGHT)};
            int colorGradingLocation = shaderLibrary.GetLocation(ShaderId::WorldPostProcess, "colorGradingEnabled");
            int retroFilterLocation = shaderLibrary.GetLocation(ShaderId::WorldPostProcess, "retroFilterEnabled");
            int rainOverlayLocation = shaderLibrary.GetLocation(ShaderId::WorldPostProcess, "rainOverlayEnabled");
            int animationTimeLocation = shaderLibrary.GetLocation(ShaderId::WorldPostProcess, "animationTime");
            int resolutionLocation = shaderLibrary.GetLocation(ShaderId::WorldPostProcess, "resolution");
            if (colorGradingLocation >= 0)
                SetShaderValue(*postProcessShader, colorGradingLocation,
                               &colorGradingEnabled, SHADER_UNIFORM_INT);
            if (retroFilterLocation >= 0)
                SetShaderValue(*postProcessShader, retroFilterLocation,
                               &retroFilterEnabled, SHADER_UNIFORM_INT);
            if (rainOverlayLocation >= 0)
                SetShaderValue(*postProcessShader, rainOverlayLocation,
                               &rainOverlayEnabled, SHADER_UNIFORM_INT);
            if (animationTimeLocation >= 0)
                SetShaderValue(*postProcessShader, animationTimeLocation,
                               &animationTime, SHADER_UNIFORM_FLOAT);
            if (resolutionLocation >= 0)
                SetShaderValue(*postProcessShader, resolutionLocation,
                               &resolution, SHADER_UNIFORM_VEC2);

            DrawTexturePro(presentedWorld, layerSource, layerDestination,
                           {0.0f, 0.0f}, 0.0f, WHITE);
            EndShaderMode();
            EndTextureMode();
            presentedWorld = postProcessedWorld.fbo.texture;
        }

        const char* debugLabel = nullptr;
        switch (renderSettings.debugView)
        {
            case RenderDebugView::Final:
                break;
            case RenderDebugView::WorldAlbedo:
                presentedWorld = worldComposite.fbo.texture;
                debugLabel = "DEBUG: world albedo (F8: next view)";
                break;
            case RenderDebugView::LightMap:
                presentedWorld = lightMap.fbo.texture;
                // This FBO is authored in framebuffer coordinates, unlike
                // the already-composed world target. Its direct debug view
                // therefore needs the standard FBO Y flip.
                presentedSource = {0.0f, 0.0f,
                                   static_cast<float>(lightMap.fbo.texture.width),
                                   -static_cast<float>(lightMap.fbo.texture.height)};
                debugLabel = "DEBUG: light map (F8: next view)";
                break;
            case RenderDebugView::FogMask:
                presentedWorld = fogMask.fbo.texture;
                presentedSource = {0.0f, 0.0f,
                                   static_cast<float>(fogMask.fbo.texture.width),
                                   -static_cast<float>(fogMask.fbo.texture.height)};
                debugLabel = "DEBUG: fog visibility mask (F8: next view)";
                break;
        }

        DrawTexturePro(presentedWorld, presentedSource, dest, {0.0f, 0.0f}, 0.0f, WHITE);
        if (debugLabel != nullptr)
            DrawText(debugLabel, static_cast<int>(offset.x + 18.0f),
                     static_cast<int>(offset.y + 18.0f), 22, Color{255, 236, 152, 255});
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
void Renderer::DrawOnLayer(WorldRenderLayer layer, Texture2D tex, Vec2i pos)
{
    if (!worldLayersInitialized)
        return;

    BeginLayer(layer);

    Rectangle src = {0,0,tex.width*1.0f, -tex.height*1.0f};
    Rectangle dest = {static_cast<float>(pos.x), static_cast<float>(RENDER_HEIGHT - tex.height - pos.y), tex.width*1.0f, tex.height*1.0f};
    DrawTexturePro(tex, src, dest, {0,0}, 0, WHITE);

    EndLayer();
}

// Draws one atlas tile directly onto a render layer.
void Renderer::DrawOnLayer(WorldRenderLayer layer, int atlas, int tex, Vec2f pos)
{
    if (!worldLayersInitialized)
        return;

    BeginLayer(layer);
    DrawAtlasTile(atlas, tex, pos);
    EndLayer();
}

// Initializes Renderer::BeginLayer.
void Renderer::BeginLayer(WorldRenderLayer layer)
{
    if (!worldLayersInitialized || layerActive)
        return;

    BeginTextureMode(layers[ToLayerIndex(layer)].fbo);
    BeginMode2D(camera);
    layerActive = true;
}

// Initializes Renderer::EndLayer.
void Renderer::EndLayer()
{
    if (!layerActive)
        return;

    EndMode2D();
    EndTextureMode();
    layerActive = false;
}

// Clears this runtime state.
void Renderer::ClearLayer(WorldRenderLayer layer)
{
    if (!worldLayersInitialized)
        return;

    BeginTextureMode(layers[ToLayerIndex(layer)].fbo);
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
    if (atlas == 0 && camera.zoom > 0.0f)
    {
        // At a distant zoom a tile can cover only a few render pixels. Two
        // adjacent quads may then round to neighbouring pixel columns/rows
        // and leave a transparent (black after composition) seam between
        // them. Extend only the right and top edges by one render pixel. The
        // next tile is drawn later and owns the overlap, so this closes the
        // raster gap without shifting the tile grid or changing atlas
        // sampling. Resource overlays deliberately do not use this because
        // their transparent edges must not spill into neighbouring tiles.
        const float renderPixelInWorld = 1.0f / camera.zoom;
        dest.width += renderPixelInWorld;
        dest.height += renderPixelInWorld;
    }
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
    {
        // Building sprites are sampled directly by the team-colour shader.
        // Point filtering is required for pixel art and prevents subpixel
        // camera movement from blending neighbouring pixels or animation
        // frames into a shimmering/floating silhouette.
        SetTextureFilter(texture, TEXTURE_FILTER_POINT);
        buildingTextures[type] = texture;
    }
}

Rectangle Renderer::GetBuildingTextureFirstFrameSource(BuildingType type) const
{
    if (IsRoadLike(type))
    {
        auto atlasIt = atlasMap.find(19);
        if (atlasIt != atlasMap.end())
        {
            const TextureAtlas& atlas = atlasIt->second;
            const int tileId = type == BuildingType::Bridge ? 16 : 0;
            const int clampedId = std::clamp(tileId, 0, std::max(0, atlas.dim.x * atlas.dim.y - 1));
            return {static_cast<float>((clampedId % atlas.dim.x) * atlas.size.x),
                    static_cast<float>((clampedId / atlas.dim.x) * atlas.size.y),
                    static_cast<float>(atlas.size.x), static_cast<float>(atlas.size.y)};
        }
    }

    auto textureIt = buildingTextures.find(type);
    if (textureIt == buildingTextures.end() || textureIt->second.id == 0)
        return {};

    const Texture2D& texture = textureIt->second;
    float frameWidth = static_cast<float>(texture.width);
    float frameHeight = static_cast<float>(texture.height);
    auto animationIt = buildingAnimations.find(type);
    if (animationIt != buildingAnimations.end() && animationIt->second.frameCount > 1)
    {
        frameWidth = texture.width / static_cast<float>(animationIt->second.frameCount);
    }
    return {0.0f, 0.0f, frameWidth, frameHeight};
}

void Renderer::RegisterBuildingAnimation(BuildingType type, const AnimationClip& clip)
{
    buildingAnimations[type] = clip;
}

bool Renderer::HasBuildingAnimation(BuildingType type) const
{
    // The Bakery is redrawn for its procedural chimney smoke even though its
    // base sprite is static; this prevents roof or wall flicker.
    if (type == BuildingType::Bakery)
        return true;

    const auto it = buildingAnimations.find(type);
    return it != buildingAnimations.end() && it->second.frameCount > 1;
}

// Draws a building texture sized to its footprint, animated by its lifetime
// (ETAP 5.4) if a clip is registered for its type — otherwise identical to
// the static overload.
void Renderer::DrawBuildingTexture(Building* building, Vec2f pos, Color tint)
{
    if (building == nullptr)
        return;

    Color ownerColor = building->owner != nullptr ? building->owner->color : WHITE;
    DrawBuildingTexture(building->buildingType, building->GetFootprint(), pos, tint,
                         static_cast<float>(building->GetLifetime()), ownerColor, building->owner != nullptr);
}

void Renderer::DrawBuildingSprite(Texture2D texture, Rectangle source, Rectangle destination, Color tint,
                                  Color ownerColor, bool applyTeamColor, BuildingType type)
{
    const Shader* teamColorShader = (applyTeamColor && renderSettings.teamColors)
        ? shaderLibrary.Find(ShaderId::TeamColor)
        : nullptr;
    if (teamColorShader == nullptr)
    {
        DrawTexturePro(texture, source, destination, {0.0f, 0.0f}, 0.0f, tint);
        return;
    }

    float primary[4]{ownerColor.r / 255.0f, ownerColor.g / 255.0f, ownerColor.b / 255.0f, ownerColor.a / 255.0f};
    float secondary[4]{
        primary[0] + (1.0f - primary[0]) * 0.25f,
        primary[1] + (1.0f - primary[1]) * 0.25f,
        primary[2] + (1.0f - primary[2]) * 0.25f,
        primary[3]};
    int hasMaterialMask = 0;
    int useAlbedoBlueKey = 1;
    // The refreshed HQ uses a darker, more cyan-leaning blue than the legacy
    // building sprites. Keep its broader fallback isolated until authored
    // material masks replace the temporary albedo keying.
    int blueKeyProfile = type == BuildingType::Headquarters ? 1 : 0;

    BeginShaderMode(*teamColorShader);
    int primaryLocation = shaderLibrary.GetLocation(ShaderId::TeamColor, "playerPrimary");
    int secondaryLocation = shaderLibrary.GetLocation(ShaderId::TeamColor, "playerSecondary");
    int hasMaskLocation = shaderLibrary.GetLocation(ShaderId::TeamColor, "hasMaterialMask");
    int blueKeyLocation = shaderLibrary.GetLocation(ShaderId::TeamColor, "useAlbedoBlueKey");
    int blueKeyProfileLocation = shaderLibrary.GetLocation(ShaderId::TeamColor, "blueKeyProfile");
    if (primaryLocation >= 0)
        SetShaderValue(*teamColorShader, primaryLocation, primary, SHADER_UNIFORM_VEC4);
    if (secondaryLocation >= 0)
        SetShaderValue(*teamColorShader, secondaryLocation, secondary, SHADER_UNIFORM_VEC4);
    if (hasMaskLocation >= 0)
        SetShaderValue(*teamColorShader, hasMaskLocation, &hasMaterialMask, SHADER_UNIFORM_INT);
    if (blueKeyLocation >= 0)
        SetShaderValue(*teamColorShader, blueKeyLocation, &useAlbedoBlueKey, SHADER_UNIFORM_INT);
    if (blueKeyProfileLocation >= 0)
        SetShaderValue(*teamColorShader, blueKeyProfileLocation, &blueKeyProfile, SHADER_UNIFORM_INT);
    DrawTexturePro(texture, source, destination, {0.0f, 0.0f}, 0.0f, tint);
    EndShaderMode();
}

// Draws a building snapshot with its standalone texture. `tint` modulates the
// sprite (WHITE = unchanged); a dim tint marks buildings still under construction.
void Renderer::DrawBuildingTexture(BuildingType type, Vec2i footprint, Vec2f pos, Color tint,
                                   Color ownerColor, bool applyTeamColor)
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
        DrawBuildingSprite(texture, src, dest, tint, ownerColor, applyTeamColor, type);
        return;
    }

    Rectangle dest{pos.x, RENDER_HEIGHT - drawSize.y - pos.y, drawSize.x, drawSize.y};
    DrawRectangleRounded(dest, 0.04f, 8, modulate(Color{96, 78, 56, 255}));
    DrawRectangleRoundedLines(dest, 0.04f, 8, 1.0f, modulate(Color{150, 108, 58, 255}));
}

// Same, but reads the frame from the type's registered animation clip
// (elapsedTime is normally the building's lifetime). Types with no clip, or
// a clip with frameCount==1, fall through to the static overload unchanged.
void Renderer::DrawBuildingTexture(BuildingType type, Vec2i footprint, Vec2f pos, Color tint, float elapsedTime,
                                   Color ownerColor, bool applyTeamColor)
{
    auto animIt = buildingAnimations.find(type);
    auto textureIt = buildingTextures.find(type);
    if (animIt == buildingAnimations.end() || animIt->second.frameCount <= 1 || textureIt == buildingTextures.end())
    {
        if (type == BuildingType::Bakery && tint.r == 255 && tint.g == 255 && tint.b == 255)
            DrawBakerySmoke(pos, footprint, elapsedTime);
        DrawBuildingTexture(type, footprint, pos, tint, ownerColor, applyTeamColor);
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
    DrawBuildingSprite(texture, src, dest, tint, ownerColor, applyTeamColor, type);
}

// Draws terrain, territory and buildings from an immutable game snapshot.
void Renderer::DrawSnapshot(const GameSnapshot& snapshot)
{
    if (!worldLayersInitialized || !snapshot.IsValid())
        return;

    SetSimulationTick(snapshot.simulationTick);

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

    ClearDynamicLights();
    ClearFogReveals();
    // Snapshot rendering has no Player building registry, so scan its anchor
    // tiles once and let radius-aware queue culling reject irrelevant sources.
    // This is independent of camera tile bounds and fixes fog changing shape
    // when an owning building scrolls just outside the view.
    for (int y = 0; y < snapshot.mapSize.y; y++)
    {
        for (int x = 0; x < snapshot.mapSize.x; x++)
        {
            size_t tileIndex = static_cast<size_t>(y * snapshot.mapSize.x + x);
            const auto& tile = snapshot.tiles[tileIndex];
            if (tile.hasBuilding)
            {
                QueueBuildingLight(tile.buildingType, tile.buildingFootprint,
                                   {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)},
                                   static_cast<int>(tileIndex), tile.isBuildingOperational);
                if (tile.buildingOwnerId == snapshot.localPlayerId)
                    QueueBuildingFogReveal(tile.buildingType, tile.buildingFootprint,
                                           {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)});
            }
        }
    }

    ClearLayer(WorldRenderLayer::Terrain);
    BeginLayer(WorldRenderLayer::Terrain);
    for (int x = minTileX; x <= maxTileX; x++)
    {
        for (int y = minTileY; y <= maxTileY; y++)
        {
            const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
            Vec2f pos = {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};
            DrawAtlasTile(0, tile.terrainTextureId, pos);
        }
    }
    EndLayer();

    ClearLayer(WorldRenderLayer::ResourceOverlays);
    BeginLayer(WorldRenderLayer::ResourceOverlays);
    for (int x = minTileX; x <= maxTileX; x++)
    {
        for (int y = minTileY; y <= maxTileY; y++)
        {
            const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
            if (tile.resourceOverlayTextureId < 0)
                continue;
            DrawAtlasTile(41, tile.resourceOverlayTextureId,
                          {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)});
        }
    }
    EndLayer();

    ClearLayer(WorldRenderLayer::MilitaryRoads);
    BeginLayer(WorldRenderLayer::MilitaryRoads);
    for (int x = minTileX; x <= maxTileX; x++)
    {
        for (int y = minTileY; y <= maxTileY; y++)
        {
            const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
            // Keep the track in the normal world layer order so static bridge
            // sprites render above it and dynamic objects (units/projectiles)
            // render above both. A Bridge replaces neither the track flag nor
            // the track texture; it only adds its sprite on StaticObjects.
            if (tile.isMilitaryRoad)
            {
                const auto isTrackAt = [&](int checkX, int checkY)
                {
                    if (checkX < 0 || checkY < 0 || checkX >= snapshot.mapSize.x || checkY >= snapshot.mapSize.y)
                        return false;
                    return snapshot.tiles[static_cast<size_t>(checkY * snapshot.mapSize.x + checkX)].isMilitaryRoad;
                };
                int mask = 0;
                if (isTrackAt(x - 1, y)) mask |= 1;
                if (isTrackAt(x + 1, y)) mask |= 2;
                if (isTrackAt(x, y - 1)) mask |= 4;
                if (isTrackAt(x, y + 1)) mask |= 8;
                DrawMilitaryRoadTexture(
                    {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)}, mask);
            }
        }
    }
    EndLayer();

    ClearLayer(WorldRenderLayer::WorldEffects);
    if (renderSettings.contactShadows)
    {
        const WorldLightingFrame lighting = ComputeWorldLighting(snapshot.simulationTick, dayNightConfig);
        const unsigned char shadowAlpha = static_cast<unsigned char>(std::clamp(
            32.0f + (1.0f - lighting.ambientIntensity) * 42.0f, 32.0f, 74.0f));
        BeginLayer(WorldRenderLayer::WorldEffects);
        for (int x = minTileX; x <= maxTileX; x++)
        {
            for (int y = minTileY; y <= maxTileY; y++)
            {
                const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
                if (!tile.hasBuilding || IsRoadLike(tile.buildingType))
                    continue;

                float width = tile.buildingFootprint.x * TILE_SIZE;
                float height = tile.buildingFootprint.y * TILE_SIZE;
                const float baseX = x * TILE_SIZE + width * 0.50f;
                const float baseY = RENDER_HEIGHT - y * TILE_SIZE - height * 0.84f;
                const float directionalLength = std::min(
                    lighting.shadowLength * 0.30f, std::max(width, height) * 1.10f);
                if (directionalLength > 0.5f)
                {
                    constexpr float samples[] = {0.32f, 0.62f, 0.90f};
                    constexpr float widths[] = {0.30f, 0.24f, 0.18f};
                    constexpr float alphas[] = {0.32f, 0.22f, 0.14f};
                    for (int i = 0; i < 3; ++i)
                    {
                        const float shadowX = baseX - lighting.sunDirection.x * directionalLength * samples[i];
                        const float shadowY = baseY + lighting.sunDirection.y * directionalLength * samples[i];
                        DrawEllipse(static_cast<int>(shadowX), static_cast<int>(shadowY),
                                    width * widths[i], std::max(2.0f, height * 0.075f),
                                    Color{0, 0, 0, static_cast<unsigned char>(shadowAlpha * alphas[i])});
                    }
                }
                DrawEllipse(static_cast<int>(baseX), static_cast<int>(baseY),
                            width * 0.32f,
                            std::max(2.0f, height * 0.075f),
                            Color{0, 0, 0, static_cast<unsigned char>(shadowAlpha * 0.70f)});
            }
        }
        EndLayer();
    }

    if (renderSettings.logisticsOverlay)
    {
        BeginLayer(WorldRenderLayer::WorldEffects);
        for (int x = minTileX; x <= maxTileX; x++)
        {
            for (int y = minTileY; y <= maxTileY; y++)
            {
                const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
                if (!tile.hasBuilding || !IsRoadLike(tile.buildingType))
                    continue;
                const auto isConnectionAt = [&](int checkX, int checkY)
                {
                    if (checkX < 0 || checkY < 0 || checkX >= snapshot.mapSize.x || checkY >= snapshot.mapSize.y)
                        return false;
                    const auto& neighbor = snapshot.tiles[static_cast<size_t>(checkY * snapshot.mapSize.x + checkX)];
                    return neighbor.hasBuilding;
                };
                DrawRoadUtilizationOverlay(
                    {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)},
                    tile.roadUtilization,
                    isConnectionAt(x - 1, y), isConnectionAt(x + 1, y),
                    isConnectionAt(x, y - 1), isConnectionAt(x, y + 1));
            }
        }
        EndLayer();
    }

    ClearLayer(WorldRenderLayer::StaticObjects);
    BeginLayer(WorldRenderLayer::StaticObjects);
    for (int x = minTileX; x <= maxTileX; x++)
    {
        for (int y = minTileY; y <= maxTileY; y++)
        {
            const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
            if (!tile.hasBuilding)
                continue;

            Vec2f pos = {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};
            const bool hasOwner = tile.buildingOwnerId >= 0;
            Color ownerColor = ResolveSnapshotPlayerColor(snapshot, tile.buildingOwnerId);
            const Color tint = tile.isBuildingOperational
                ? WHITE
                : Color{118, 122, 132, 215};
            // Snapshots do not retain per-building lifetimes, so visual-only
            // building clips share the deterministic simulation clock. Frame
            // selection still advances strictly left-to-right through the strip.
            constexpr float SimulationTickSeconds = 0.01f;
            const float elapsedTime = static_cast<float>(snapshot.simulationTick) * SimulationTickSeconds;
            if (IsRoadLike(tile.buildingType))
            {
                const auto isConnectionAt = [&](int checkX, int checkY)
                {
                    if (checkX < 0 || checkY < 0 || checkX >= snapshot.mapSize.x || checkY >= snapshot.mapSize.y)
                        return false;
                    const auto& neighbour = snapshot.tiles[static_cast<size_t>(checkY * snapshot.mapSize.x + checkX)];
                    return neighbour.hasBuilding;
                };
                int mask = 0;
                if (isConnectionAt(x - 1, y)) mask |= 1;
                if (isConnectionAt(x + 1, y)) mask |= 2;
                if (isConnectionAt(x, y - 1)) mask |= 4;
                if (isConnectionAt(x, y + 1)) mask |= 8;
                DrawRoadTexture(tile.buildingType, pos, mask, tint);
            }
            else
            {
                DrawBuildingTexture(tile.buildingType, tile.buildingFootprint, pos, tint, elapsedTime,
                                    ownerColor, hasOwner);
            }
        }
    }
    EndLayer();

    ClearLayer(WorldRenderLayer::DynamicObjects);
    BeginLayer(WorldRenderLayer::DynamicObjects);
    for (int x = minTileX; x <= maxTileX; x++)
    {
        for (int y = minTileY; y <= maxTileY; y++)
        {
            const auto& tile = snapshot.tiles[static_cast<size_t>(y * snapshot.mapSize.x + x)];
            if (!tile.hasBuilding)
                continue;
            const Vec2f position{static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};
            DrawDamageFlashOverlay(position, tile.buildingFootprint, tile.buildingDamageIndicator);
            if (renderSettings.logisticsOverlay && IsRoadLike(tile.buildingType))
                DrawRoadSaturationIndicator(position, tile.roadSaturated);
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
    const float minZoom = std::max(RENDER_WIDTH / mapW, usableRenderHeight / mapH);
    constexpr float MaxZoom = 2.5f;
    // Round the lower bound upward so snapping never reveals outside the map.
    const float tileAlignedMinZoom = std::ceil(minZoom * TILE_SIZE) / TILE_SIZE;
    const float effectiveMinZoom = std::min(tileAlignedMinZoom, MaxZoom);
    camera.zoom = std::clamp(SnapZoomToTileGrid(camera.zoom), effectiveMinZoom, MaxZoom);

    float visibleW = RENDER_WIDTH / camera.zoom;
    float visibleH = usableRenderHeight / camera.zoom;
    float maxX = std::max(0.0f, mapW - visibleW);
    float minY = RENDER_HEIGHT - mapH;
    float maxY = RENDER_HEIGHT - visibleH;

    camera.target.x = std::clamp(camera.target.x, 0.0f, maxX);
    camera.target.y = std::clamp(camera.target.y, minY, maxY);
    SnapCameraTargetToRenderPixels(camera);
    // Snapping can cross a clamped map edge by less than one screen pixel.
    camera.target.x = std::clamp(camera.target.x, 0.0f, maxX);
    camera.target.y = std::clamp(camera.target.y, minY, maxY);
}

void Renderer::DrawRoadTexture(BuildingType type, Vec2f pos, int connectionMask, Color tint)
{
    connectionMask &= 0x0F;
    int atlasId = 19;
    int tileId = (type == BuildingType::Bridge ? 16 : 0) + connectionMask;
    if (type == BuildingType::Road && atlasMap.contains(145))
    {
        const int tileX = static_cast<int>(std::floor(pos.x / TILE_SIZE));
        const int tileY = static_cast<int>(std::floor(pos.y / TILE_SIZE));
        const unsigned int hash =
            static_cast<unsigned int>(tileX) * 73856093u ^
            static_cast<unsigned int>(tileY) * 19349663u ^
            static_cast<unsigned int>(connectionMask) * 83492791u;
        atlasId = 145;
        tileId = static_cast<int>(hash % 3u) * 16 + connectionMask;
    }

    auto atlasIt = atlasMap.find(atlasId);
    if (atlasIt == atlasMap.end() || atlasIt->second.tex.id == 0)
    {
        DrawRectangle(static_cast<int>(pos.x), RENDER_HEIGHT - TILE_SIZE - static_cast<int>(pos.y),
                      TILE_SIZE, TILE_SIZE, Color{112, 78, 48, tint.a});
        return;
    }

    TextureAtlas& atlas = atlasIt->second;
    Rectangle source = atlas.GetRectFromId(tileId);
    source.height *= -1.0f;
    Rectangle destination{pos.x, RENDER_HEIGHT - TILE_SIZE - pos.y,
                          static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE)};
    DrawTexturePro(atlas.tex, source, destination, {0.0f, 0.0f}, 0.0f, tint);
}

void Renderer::DrawMilitaryRoadTexture(Vec2f pos, int connectionMask, Color tint)
{
    constexpr int MilitaryRoadAtlasId = 144;
    connectionMask &= 0x0F;
    int atlasId = MilitaryRoadAtlasId;
    int tileId = connectionMask;
    if (atlasMap.contains(146))
    {
        const int tileX = static_cast<int>(std::floor(pos.x / TILE_SIZE));
        const int tileY = static_cast<int>(std::floor(pos.y / TILE_SIZE));
        const unsigned int hash =
            static_cast<unsigned int>(tileX) * 73856093u ^
            static_cast<unsigned int>(tileY) * 19349663u ^
            static_cast<unsigned int>(connectionMask) * 83492791u;
        atlasId = 146;
        tileId = static_cast<int>(hash % 3u) * 16 + connectionMask;
    }

    auto atlasIt = atlasMap.find(atlasId);
    if (atlasIt == atlasMap.end() || atlasIt->second.tex.id == 0)
    {
        DrawRectangle(static_cast<int>(pos.x), RENDER_HEIGHT - TILE_SIZE - static_cast<int>(pos.y),
                      TILE_SIZE, TILE_SIZE, Color{190, 178, 151, tint.a});
        return;
    }

    TextureAtlas& atlas = atlasIt->second;
    Rectangle source = atlas.GetRectFromId(tileId);
    source.height *= -1.0f;
    Rectangle destination{pos.x, RENDER_HEIGHT - TILE_SIZE - pos.y,
                          static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE)};
    DrawTexturePro(atlas.tex, source, destination, {0.0f, 0.0f}, 0.0f, tint);
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
    if (!worldLayersInitialized)
        return;

    for(auto& l : layers)
    {
        BeginTextureMode(l.fbo);
        ClearBackground(BLANK);
        EndTextureMode();
    }
}
