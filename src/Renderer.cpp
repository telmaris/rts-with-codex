#include "../inc/Renderer.h"

void Renderer::Draw(std::vector<UiWidget*> ui, double dt)
{
    BeginDrawing();
    ClearBackground(WHITE);

    for(auto& layer : layers)
    {
        DrawTexture(layer.fbo.texture, 0, 0, WHITE);
    }

    for(auto ptr : ui)
    {
        ptr->Update(dt);
    }

    EndDrawing();
}

void Renderer::DrawOnLayer(int layer, Texture2D tex, Vec2i pos)
{
    BeginTextureMode(layers[layer].fbo);
    BeginMode2D(camera);

    Rectangle src = {0,0,tex.width*1.0f, -tex.height*1.0f};
    Rectangle dest = {pos.x , 1080 - tex.height - pos.y, tex.width*1.0f, tex.height*1.0f};
    DrawTexturePro(tex, src, dest, {0,0}, 0, WHITE);

    EndMode2D();
    EndTextureMode();
}

void Renderer::DrawOnLayer(int layer, int atlas, int tex, Vec2f pos)
{
    BeginTextureMode(layers[layer].fbo);
    BeginMode2D(camera);

    auto& at = atlasMap[atlas];
    Rectangle src = at.GetRectFromId(tex);
    src.height *= -1.0;

    Rectangle dest = {pos.x , 1080 - at.size.y - pos.y, at.size.x*1.0f, at.size.y*1.0f};
    DrawTexturePro(at.tex, src, dest, {0,0}, 0, WHITE);

    EndMode2D();
    EndTextureMode();
}

void Renderer::ClearLayers()
{
    for(auto& l : layers)
    {
        BeginTextureMode(l.fbo);
        ClearBackground(BLANK);
        EndTextureMode();
    }
}