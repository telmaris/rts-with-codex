#ifndef TVORIN_UI_ASSET_CONTEXT_H
#define TVORIN_UI_ASSET_CONTEXT_H

#include "ui/RaylibResource.h"

#include <map>
#include <memory>
#include <string>

namespace tvorin::ui
{
// Window-owned lifetime boundary for UI/GPU/audio resources. GameWindow keeps
// this context alive while scenes are active and closes it before the native
// raylib contexts are destroyed.
class UiAssetContext
{
public:
    UiAssetContext()
        : lifetime(std::make_shared<RaylibResourceLifetime>())
    {
    }

    ~UiAssetContext() { Close(); }

    UiAssetContext(const UiAssetContext&) = delete;
    UiAssetContext& operator=(const UiAssetContext&) = delete;
    UiAssetContext(UiAssetContext&&) noexcept = default;
    UiAssetContext& operator=(UiAssetContext&&) noexcept = default;

    std::shared_ptr<RaylibResourceLifetime> Token() const noexcept
    {
        return lifetime;
    }

    // Returns a borrowed shared view of a cached texture. The context keeps
    // the sole owning handle; callers cannot accidentally copy ownership into
    // a widget. Failed loads are not inserted, so a later retry can recover.
    std::shared_ptr<const TextureHandle> LoadTexture(const std::string& path)
    {
        if (!IsOpen() || !FileExists(path.c_str()))
            return {};

        auto existing = textures.find(path);
        if (existing != textures.end())
            return existing->second;

        TextureHandle loaded{::LoadTexture(path.c_str()), lifetime};
        if (!loaded)
            return {};

        auto shared = std::make_shared<TextureHandle>(std::move(loaded));
        textures.emplace(path, shared);
        return shared;
    }

    // Drops cached assets while the raylib context is still available.
    void Reset() noexcept
    {
        textures.clear();
    }

    void Close() noexcept
    {
        Reset();
        if (lifetime != nullptr)
            lifetime->Close();
    }

    bool IsOpen() const noexcept
    {
        return lifetime != nullptr && lifetime->IsAlive();
    }

private:
    std::shared_ptr<RaylibResourceLifetime> lifetime;
    std::map<std::string, std::shared_ptr<TextureHandle>> textures;
};
}

#endif
