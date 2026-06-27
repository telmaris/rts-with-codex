#ifndef RENDERER_H
#define RENDERER_H

#include "Utils.h"
#include "raylib.h"
#include "Gui.h"

constexpr int RENDER_WIDTH = 1920;
constexpr int RENDER_HEIGHT = 1080;
class Building;
enum class BuildingType : int;

// Fixed-resolution render layer backed by a Raylib render texture.
struct CanvasLayer
{
    CanvasLayer()
    {
        fbo = LoadRenderTexture(RENDER_WIDTH, RENDER_HEIGHT);
    }

    RenderTexture2D fbo;
};

// Tile atlas loader and id-to-source-rectangle mapper.
struct TextureAtlas
{
    // Loads atlas texture and derives grid dimensions from tile size.
    inline void LoadTextureAtlas(const char* path, Vec2i tileSize = {TILE_SIZE, TILE_SIZE})
    {
        tex = LoadTexture(path);
        size = tileSize;
        dim = {tex.width / size.x, tex.height / size.y};

        Log::Msg("[Texture Atlas]", "Loaded. Size: [", tex.width, ", ", tex.height, "] Dimensions: [", dim.x, ", ", dim.y, "]");

    }

    // Returns source rectangle for a tile id, clamped to atlas bounds.
    Rectangle GetRectFromId(int id)
    {
        Rectangle rect;
        id = std::clamp(id, 0, std::max(0, dim.x * dim.y - 1));

        rect.height = size.y;
        rect.width = size.x;

        rect.x = (id % dim.x) * rect.width;
        rect.y = (id / dim.x) * rect.height;

        return rect;
    }

    Texture2D tex;
    Vec2i size;
    Vec2i dim;
};

// Draws the world through a camera and composites UI over render layers.
class Renderer
{
    public:

    Renderer()
    {
        camera.offset = {0,0};
        camera.target = {0*TILE_SIZE,0*TILE_SIZE};
        camera.zoom = 1.25f;
        camera.rotation = 0.0f;
    }

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
    // Loads a standalone building texture for a building type.
    void LoadBuildingTexture(BuildingType, const std::string&);
    // Draws a building with its standalone texture.
    void DrawBuildingTexture(Building*, Vec2f);
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
};



#endif
