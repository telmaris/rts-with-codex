#ifndef RENDERER_H
#define RENDERER_H

#include "Utils.h"
#include "raylib.h"
#include "Gui.h"
#include "MapGenerator.h"

constexpr int RENDER_WIDTH = 1920;
constexpr int RENDER_HEIGHT = 1080;

struct CanvasLayer
{
    CanvasLayer()
    {
        fbo = LoadRenderTexture(RENDER_WIDTH, RENDER_HEIGHT);
    }

    RenderTexture2D fbo;    // frame buffer object
};

struct TextureAtlas
{
    inline void LoadTextureAtlas(const char* path, Vec2i tileSize = {TILE_SIZE, TILE_SIZE})
    {
        tex = LoadTexture(path);
        size = tileSize;
        dim = {tex.width / size.x, tex.height / size.y};

        Log::Msg("[Texture Atlas]", "Loaded. Size: [", tex.width, ", ", tex.height, "] Dimensions: [", dim.x, ", ", dim.y, "]");

    }

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
    Vec2i dim;  // {columns, rows}
};

class Renderer
{
    public:

    Renderer()
    {
        camera.offset = {0,0};
        camera.target = {0*TILE_SIZE,0*TILE_SIZE};
        camera.zoom = 1.0f;
        camera.rotation = 0.0f;
    }

    void Draw(std::vector<UiWidget*> ui = {}, double dt = 0);
    void DrawOnLayer(int, Texture2D, Vec2i);
    void DrawOnLayer(int, int, int, Vec2f);
    void BeginLayer(int);
    void EndLayer();
    void DrawAtlasTile(int, int, Vec2f);
    void DrawAtlasTile(int, int, Vec2f, Vec2f);
    Vec2f ScreenToRender(Vector2);
    Vec2f RenderToScreen(Vec2f);
    void ClearLayers();


    std::vector<CanvasLayer> layers{4};
    std::map<int, TextureAtlas> atlasMap;

    Camera2D camera;
};



#endif
