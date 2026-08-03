#include "ui/ControlIcons.h"

#include <array>
#include <map>

namespace
{
    std::map<std::string, Texture2D> textures;

    constexpr std::array<const char*, 24> ActiveIconNames{
        "key_q", "key_r", "key_d", "key_e", "key_s", "key_f", "key_t", "key_u", "key_l",
        "key_space", "key_escape", "key_f6", "key_f7", "key_f8", "key_f10", "key_ctrl",
        "key_up", "key_down", "key_enter", "key_backspace", "mouse_lmb", "mouse_rmb",
        "mouse_mmb", "mouse_wheel"};
}

void UiControlIcons::Load(const std::string& directory)
{
    Unload();

    for (const char* name : ActiveIconNames)
    {
        const std::string path = directory + "/" + name + ".png";
        if (!FileExists(path.c_str()))
            continue;

        Texture2D texture = LoadTexture(path.c_str());
        if (texture.id == 0)
            continue;

        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        textures.emplace(name, texture);
    }
}

void UiControlIcons::Unload()
{
    for (auto& [name, texture] : textures)
        UnloadTexture(texture);
    textures.clear();
}

bool UiControlIcons::IsLoaded()
{
    return !textures.empty();
}

bool UiControlIcons::Draw(const std::string& name, Rectangle destination, Color tint)
{
    auto it = textures.find(name);
    if (it == textures.end())
        return false;

    const Texture2D texture = it->second;
    Rectangle source{0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)};
    DrawTexturePro(texture, source, destination, {0.0f, 0.0f}, 0.0f, tint);
    return true;
}
