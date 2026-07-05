#ifndef RENDERER_H
#define RENDERER_H

#include "core/Utils.h"
#include "core/GameSnapshot.h"
#include "raylib.h"
#include "ui/Gui.h"

#include <cstdint>
#include <limits>
#include <map>

constexpr int RENDER_WIDTH = 1920;
constexpr int RENDER_HEIGHT = 1080;
class Building;
enum class BuildingType : int;

// Animation clip: sequence of frames from an atlas
struct AnimationClip
{
    int startFrameId = 0;
    int frameCount = 1;
    float frameTime = 0.1f;
    bool looping = true;
};

// Fixed-resolution render layer backed by a Raylib render texture.
struct CanvasLayer
{
    CanvasLayer();
    RenderTexture2D fbo;
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

    // Draws all render layers and UI widgets.
    void Draw(std::vector<UiWidget*> ui = {}, double dt = 0);
    // Draws a full texture on a render layer at tile coordinates.
    void DrawOnLayer(int, Texture2D, Vec2i);
    // Draws one atlas tile on a render layer at tile coordinates.
    void DrawOnLayer(int, int, int, Vec2f);
    // Begins drawing to a render layer.
    void BeginLayer(int);
    // Ends drawing to the current render layer.
    void EndLayer();
    // Clears one render layer without changing camera state.
    void ClearLayer(int);
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
    // Draws a building with its standalone texture.
    void DrawBuildingTexture(Building*, Vec2f, Color tint = WHITE);
    // Draws a building snapshot with its standalone texture.
    void DrawBuildingTexture(BuildingType type, Vec2i footprint, Vec2f pos, Color tint = WHITE);
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


    std::vector<CanvasLayer> layers{4};
    std::map<int, TextureAtlas> atlasMap;
    std::map<BuildingType, Texture2D> buildingTextures;

    Camera2D camera;
    float topScreenPadding{0.0f};
    std::uint64_t cachedSnapshotTick{std::numeric_limits<std::uint64_t>::max()};
    Vec2f cachedSnapshotCameraTarget{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    float cachedSnapshotCameraZoom{-1.0f};
};



#endif
