#ifndef RENDERER_H
#define RENDERER_H

#include "Utils.h"
#include "raylib.h"
#include "Gui.h"

struct CanvasLayer
{
    CanvasLayer()
    {
        fbo = LoadRenderTexture(1920, 1080);
    }

    RenderTexture2D fbo;    // frame buffer object
};

struct TextureAtlas
{
    inline void LoadTextureAtlas(const char* path)
    {
        tex = LoadTexture(path);
        size = {64,64};
        dim = {tex.width / size.x, tex.height / size.y};

        Log::Msg("[Texture Atlas]", "Loaded. Size: [", tex.width, ", ", tex.height, "] Dimensions: [", dim.x, ", ", dim.y, "]");

    }

    Rectangle GetRectFromId(int id)
    {
        Rectangle rect;

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
        camera.target = {0*64,0*64};
        camera.zoom = 1.0f;
        camera.rotation = 0.0f;
    }

    void Draw(std::vector<UiWidget*> ui = {}, double dt = 0);
    void DrawOnLayer(int, Texture2D, Vec2i);
    void DrawOnLayer(int, int, int, Vec2f);
    void ClearLayers();


    std::vector<CanvasLayer> layers{4};
    std::map<int, TextureAtlas> atlasMap;

    Camera2D camera;
};



#endif