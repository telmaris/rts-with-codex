#ifndef RENDERER_H
#define RENDERER_H

#include "core/Types.h"
#include "core/GameSnapshot.h"
#include "raylib.h"
#include "ui/Gui.h"
#include "ui/ShaderLibrary.h"
#include "ui/WorldLighting.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

constexpr int RENDER_WIDTH = 1920;
constexpr int RENDER_HEIGHT = 1080;
class Building;
enum class BuildingType : int;

// Ordered world layers. Tactical UI is intentionally not part of this list:
// it will be composed after world lighting in a later rendering pass.
enum class WorldRenderLayer : std::size_t
{
    Terrain,
    MilitaryRoads,
    WorldEffects,
    StaticObjects,
    DynamicObjects,
    Count
};

constexpr std::size_t ToLayerIndex(WorldRenderLayer layer)
{
    return static_cast<std::size_t>(layer);
}

// Debug views intentionally expose intermediate render targets without
// affecting the simulation. They are useful for validating FBO orientation
// and local-light placement at different camera zoom levels.
enum class RenderDebugView : std::uint8_t
{
    Final,
    WorldAlbedo,
    LightMap,
    FogMask
};

struct RenderSettings
{
    bool teamColors{true};
    bool dayNightCycle{true};
    bool dynamicLights{true};
    bool contactShadows{true};
    bool fogOfWar{false}; // Pilot feature: opt-in through OptionsScene.
    bool colorGrading{true};
    bool retroFilter{false};
    // A low-cost blur sampled from the light map only. It never touches UI or
    // bright terrain/albedo, unlike a bloom pass over the final frame.
    bool localLightBloom{false};
    // Procedural rain is a presentation-only postprocess. It is deliberately
    // disabled by default and has no weather/gameplay simulation behind it.
    bool rainOverlay{false};
    bool logisticsOverlay{false};
    RenderDebugView debugView{RenderDebugView::Final};
};

// Global presentation preference shared between the options scene and the
// active gameplay renderer. It is intentionally not a simulation setting and
// is therefore never serialized into saves, commands, or multiplayer state.
bool IsFogOfWarPreferenceEnabled();
void SetFogOfWarPreferenceEnabled(bool enabled);
bool IsColorGradingPreferenceEnabled();
void SetColorGradingPreferenceEnabled(bool enabled);
bool IsRetroFilterPreferenceEnabled();
void SetRetroFilterPreferenceEnabled(bool enabled);
bool IsLocalLightBloomPreferenceEnabled();
void SetLocalLightBloomPreferenceEnabled(bool enabled);
bool IsRainOverlayPreferenceEnabled();
void SetRainOverlayPreferenceEnabled(bool enabled);
bool IsLogisticsOverlayPreferenceEnabled();
void SetLogisticsOverlayPreferenceEnabled(bool enabled);

struct FogRevealView
{
    Vector2 worldPosition{};
    float radiusWorld{96.0f};
};

// Animation clip: sequence of frames from an atlas
struct AnimationClip
{
    int startFrameId = 0;
    int frameCount = 1;
    float frameTime = 0.1f;
    bool looping = true;
};

// Resolves which 0-based frame index of a clip should show at an elapsed
// time. Shared by TextureAtlas (atlas-tile animation) and Renderer's
// standalone building-texture animation so the timing math lives in one place.
int ResolveAnimationFrame(const AnimationClip& clip, float elapsedTime);

// Fixed-resolution render layer backed by a Raylib render texture.
struct CanvasLayer
{
    // GPU allocation is explicit so menu scenes do not allocate world-sized FBOs.
    void Initialize(int width = RENDER_WIDTH, int height = RENDER_HEIGHT);
    // Must normally be called while the raylib window/context is still alive.
    void Shutdown();
    bool IsInitialized() const { return fbo.id != 0; }

    RenderTexture2D fbo{};
};

// Tile atlas loader and id-to-source-rectangle mapper.
struct TextureAtlas
{
    // Loads atlas texture and derives grid dimensions from tile size.
    void LoadTextureAtlas(const char* path, Vec2i tileSize = {TILE_SIZE, TILE_SIZE});
    // Returns source rectangle for a tile id, clamped to atlas bounds.
    Rectangle GetRectFromId(int id);
    // Registers an animation clip under an id (ETAP 5.2).
    void RegisterAnimation(int clipId, const AnimationClip& clip);
    // Returns the clip for an id, or a default single-frame clip if unregistered.
    AnimationClip GetAnimation(int clipId) const;
    // Resolves which atlas frame a clip should show at a given elapsed time.
    int GetFrameForAnimation(int clipId, float elapsedTime) const;

    Texture2D tex;
    Vec2i size;
    Vec2i dim;
    std::map<int, AnimationClip> animations;
};

// Draws the world through a camera and composites UI over render layers.
class Renderer
{
    public:

    Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    // Allocates the world render layers. Only GameScene should call this;
    // UI-only scenes can keep their Renderer uninitialized.
    bool InitializeWorldLayers();
    // Explicitly frees all GPU resources owned by this renderer. Call before
    // CloseWindow(); this method is intentionally safe to call more than once.
    void Shutdown();
    bool HasWorldLayers() const { return worldLayersInitialized; }
    // The render clock is derived from the deterministic simulation tick, not
    // from wall-clock time. Both direct and snapshot render paths set it.
    void SetSimulationTick(std::uint64_t tick) { simulationTick = tick; }
    void SetRenderSettings(const RenderSettings& settings) { renderSettings = settings; }
    const RenderSettings& GetRenderSettings() const { return renderSettings; }
    void SetDayNightCycleEnabled(bool enabled) { renderSettings.dayNightCycle = enabled; }
    bool IsDayNightCycleEnabled() const { return renderSettings.dayNightCycle; }
    void SetDynamicLightsEnabled(bool enabled) { renderSettings.dynamicLights = enabled; }
    bool AreDynamicLightsEnabled() const { return renderSettings.dynamicLights; }
    void SetTeamColorsEnabled(bool enabled) { renderSettings.teamColors = enabled; }
    bool AreTeamColorsEnabled() const { return renderSettings.teamColors; }
    void SetContactShadowsEnabled(bool enabled) { renderSettings.contactShadows = enabled; }
    bool AreContactShadowsEnabled() const { return renderSettings.contactShadows; }
    bool IsFogOfWarEnabled() const { return renderSettings.fogOfWar; }
    void SetColorGradingEnabled(bool enabled) { renderSettings.colorGrading = enabled; }
    bool IsColorGradingEnabled() const { return renderSettings.colorGrading; }
    void SetRetroFilterEnabled(bool enabled) { renderSettings.retroFilter = enabled; }
    bool IsRetroFilterEnabled() const { return renderSettings.retroFilter; }
    void SetLocalLightBloomEnabled(bool enabled) { renderSettings.localLightBloom = enabled; }
    bool IsLocalLightBloomEnabled() const { return renderSettings.localLightBloom; }
    void SetRainOverlayEnabled(bool enabled) { renderSettings.rainOverlay = enabled; }
    bool IsRainOverlayEnabled() const { return renderSettings.rainOverlay; }
    void SetLogisticsOverlayEnabled(bool enabled) { renderSettings.logisticsOverlay = enabled; }
    bool IsLogisticsOverlayEnabled() const { return renderSettings.logisticsOverlay; }
    void CycleDebugView();
    // Lights are rebuilt from world sources and radius-culled against the
    // current render view; their data does not enter simulation state, saves,
    // or gameplay checksums.
    void ClearDynamicLights();
    void QueueDynamicLight(const LightEmitterView& light);
    void QueueBuildingLight(BuildingType type, Vec2i footprint, Vec2f pos, int stableId, bool isOperational);
    // Fog revealers are rebuilt from all owned world sources by
    // GameWorld::DrawMap/DrawSnapshot each frame. No state is retained, so
    // toggling fog cannot show stale visibility.
    void ClearFogReveals();
    void QueueFogReveal(const FogRevealView& reveal);
    void QueueBuildingFogReveal(BuildingType type, Vec2i footprint, Vec2f pos);

    // Draws all render layers and UI widgets, then presents the frame.
    // Convenience wrapper = DrawContent + PresentFrame.
    void Draw(std::vector<UiWidget*> ui = {}, double dt = 0);
    // Begins the frame and issues all draw calls (layers + widgets) but does NOT
    // present. Lets a caller draw under a lock and release it before the
    // vsync-blocking present. Must be paired with PresentFrame().
    void DrawContent(std::vector<UiWidget*> ui = {}, double dt = 0);
    // Presents the frame (EndDrawing). This is where vsync / frame-cap blocking
    // happens, so callers holding a lock should release it before calling this.
    void PresentFrame();
    // Draws a full texture on a render layer at tile coordinates.
    void DrawOnLayer(WorldRenderLayer, Texture2D, Vec2i);
    // Draws one atlas tile on a render layer at tile coordinates.
    void DrawOnLayer(WorldRenderLayer, int, int, Vec2f);
    // Begins drawing to a render layer.
    void BeginLayer(WorldRenderLayer);
    // Ends drawing to the current render layer.
    void EndLayer();
    // Clears one render layer without changing camera state.
    void ClearLayer(WorldRenderLayer);
    // Draws one atlas tile in world space.
    void DrawAtlasTile(int, int, Vec2f);
    // Draws one atlas tile in world space with scale.
    void DrawAtlasTile(int, int, Vec2f, Vec2f);
    // Draws one atlas tile, resolving the frame from an animation clip and elapsed time (ETAP 5.3).
    void DrawAtlasTile(int atlas, int clipId, Vec2f pos, float elapsedTime);
    // Same, stretched to a target world size.
    void DrawAtlasTile(int atlas, int clipId, Vec2f pos, Vec2f drawSize, float elapsedTime);
    // Loads a standalone building texture for a building type.
    void LoadBuildingTexture(BuildingType, const std::string&);
    // Registers an animation clip for a standalone building texture (ETAP 5.4).
    // Frames are read as a horizontal strip inside the loaded texture (frame
    // width = texture width / frameCount). Types with no registered clip (or
    // frameCount==1) keep drawing the full texture — fully backward compatible.
    void RegisterBuildingAnimation(BuildingType type, const AnimationClip& clip);
    // True when a type has a multi-frame strip registered. This lets callers
    // redraw otherwise cached building layers while the clip advances.
    bool HasBuildingAnimation(BuildingType type) const;
    // Draws a building with its standalone texture.
    void DrawBuildingTexture(Building*, Vec2f, Color tint = WHITE);
    // Draws a building snapshot with its standalone texture.
    void DrawBuildingTexture(BuildingType type, Vec2i footprint, Vec2f pos, Color tint = WHITE,
                             Color ownerColor = WHITE, bool applyTeamColor = false);
    // Same, picking the frame from the type's registered animation clip and elapsed time.
    void DrawBuildingTexture(BuildingType type, Vec2i footprint, Vec2f pos, Color tint, float elapsedTime,
                             Color ownerColor = WHITE, bool applyTeamColor = false);
    // Draws terrain, territory and buildings from an immutable game snapshot.
    void DrawSnapshot(const GameSnapshot& snapshot);
    // Converts OS screen coordinates to fixed render coordinates.
    Vec2f ScreenToRender(Vector2);
    // Converts fixed render coordinates to OS screen coordinates.
    Vec2f RenderToScreen(Vec2f);
    // Converts fixed render coordinates to world coordinates.
    Vec2f RenderToWorld(Vec2f);
    // Converts world coordinates to fixed render coordinates.
    Vec2f WorldToRender(Vec2f);
    // Converts OS screen coordinates to world coordinates.
    Vec2f ScreenToWorld(Vector2);
    // Converts world coordinates to OS screen coordinates.
    Vec2f WorldToScreen(Vec2f);
    // Keeps camera view within map bounds when possible.
    void ClampCameraToMap(Vec2i mapSize);
    // Reserves screen-space pixels at the top when clamping the camera.
    void SetTopScreenPadding(float padding);
    // Centers the camera on a world-space point and clamps it to map bounds.
    void CenterCameraOnWorld(Vec2f worldPoint, Vec2i mapSize);
    // Applies cursor-centered zoom and clamps camera afterwards.
    void ZoomAtScreenPoint(Vector2 screen, float wheel, Vec2i mapSize);
    // Clears all render layers.
    void ClearLayers();


    std::array<CanvasLayer, ToLayerIndex(WorldRenderLayer::Count)> layers{};
    // Composite target for every world layer. UI is deliberately drawn after it.
    CanvasLayer worldComposite;
    CanvasLayer litWorld;
    CanvasLayer lightMap;
    CanvasLayer fogMask;
    CanvasLayer foggedWorld;
    // Final world-only grading/retro target. UI is drawn afterwards and never
    // receives palette quantization, scanlines, or contrast correction.
    CanvasLayer postProcessedWorld;
    std::map<int, TextureAtlas> atlasMap;
    std::map<BuildingType, Texture2D> buildingTextures;
    std::map<BuildingType, AnimationClip> buildingAnimations;
    // White radial alpha mask shared by additive lights and soft fog revealers.
    Texture2D radialLightMask{};
    ShaderLibrary shaderLibrary;

    Camera2D camera;
    float topScreenPadding{0.0f};
    bool worldLayersInitialized{false};
    bool layerActive{false};
    RenderSettings renderSettings{};
    std::uint64_t simulationTick{0};
    DayNightConfig dayNightConfig{};
    std::vector<LightEmitterView> dynamicLights;
    std::vector<FogRevealView> fogReveals;
    std::uint64_t cachedSnapshotTick{std::numeric_limits<std::uint64_t>::max()};
    Vec2f cachedSnapshotCameraTarget{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    float cachedSnapshotCameraZoom{-1.0f};

private:
    void DrawBuildingSprite(Texture2D texture, Rectangle source, Rectangle destination, Color tint,
                            Color ownerColor, bool applyTeamColor, BuildingType type);
    void DrawLightMap(const WorldLightingFrame& lighting);
    void DrawFogMask();
};



#endif
