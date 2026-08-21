#ifndef TVORIN_RAYLIB_RESOURCE_H
#define TVORIN_RAYLIB_RESOURCE_H

#include "raylib.h"

#include <memory>
#include <type_traits>

namespace tvorin::ui
{
// A token shared by owning handles and the window-level asset context. The
// token lets a late widget destructor become a no-op instead of calling into
// raylib after CloseWindow/CloseAudioDevice.
class RaylibResourceLifetime
{
public:
    void Close() noexcept { alive = false; }
    bool IsAlive() const noexcept { return alive; }

private:
    bool alive{true};
};

struct TextureResourceTraits
{
    static bool IsValid(const Texture2D& value) noexcept { return value.id != 0; }
    static void Release(Texture2D value) noexcept { UnloadTexture(value); }
};

struct RenderTextureResourceTraits
{
    static bool IsValid(const RenderTexture2D& value) noexcept { return value.id != 0; }
    static void Release(RenderTexture2D value) noexcept { UnloadRenderTexture(value); }
};

struct FontResourceTraits
{
    static bool IsValid(const Font& value) noexcept { return value.texture.id != 0; }
    static void Release(Font value) noexcept { UnloadFont(value); }
};

struct ShaderResourceTraits
{
    static bool IsValid(const Shader& value) noexcept { return value.id != 0; }
    static void Release(Shader value) noexcept { UnloadShader(value); }
};

struct SoundResourceTraits
{
    static bool IsValid(const Sound& value) noexcept { return value.frameCount > 0; }
    static void Release(Sound value) noexcept { UnloadSound(value); }
};

struct MusicResourceTraits
{
    static bool IsValid(const Music& value) noexcept { return value.ctxData != nullptr; }
    static void Release(Music value) noexcept { UnloadMusicStream(value); }
};

// Move-only owner for one raylib value. The traits parameter also makes the
// ownership contract testable with a fake resource/deleter without opening a
// real window or audio device.
template <typename Resource, typename Traits>
class RaylibResource
{
public:
    RaylibResource() noexcept = default;

    explicit RaylibResource(Resource value,
                             std::shared_ptr<RaylibResourceLifetime> lifetime = {}) noexcept
        : value(value), lifetime(std::move(lifetime)), owning(Traits::IsValid(value))
    {
    }

    ~RaylibResource() { Reset(); }

    RaylibResource(const RaylibResource&) = delete;
    RaylibResource& operator=(const RaylibResource&) = delete;

    RaylibResource(RaylibResource&& other) noexcept
        : value(other.value), lifetime(std::move(other.lifetime)), owning(other.owning)
    {
        other.value = Resource{};
        other.owning = false;
    }

    RaylibResource& operator=(RaylibResource&& other) noexcept
    {
        if (this == &other)
            return *this;

        Reset();
        value = other.value;
        lifetime = std::move(other.lifetime);
        owning = other.owning;
        other.value = Resource{};
        other.owning = false;
        return *this;
    }

    bool IsValid() const noexcept
    {
        return owning && Traits::IsValid(value);
    }

    explicit operator bool() const noexcept { return IsValid(); }

    // Explicit access is intentional: callers can pass a borrowed raylib
    // value to draw APIs without giving the raw value owning semantics.
    const Resource& Get() const noexcept { return value; }
    Resource& Get() noexcept { return value; }

    void Reset() noexcept
    {
        if (owning && Traits::IsValid(value) &&
            (lifetime == nullptr || lifetime->IsAlive()))
        {
            Traits::Release(value);
        }
        value = Resource{};
        lifetime.reset();
        owning = false;
    }

    // Drops ownership without calling raylib. Use only when the native
    // context has already been closed and an explicit lifetime token was not
    // available to guard this handle.
    void Forget() noexcept
    {
        value = Resource{};
        lifetime.reset();
        owning = false;
    }

private:
    Resource value{};
    std::shared_ptr<RaylibResourceLifetime> lifetime;
    bool owning{false};
};

using TextureHandle = RaylibResource<Texture2D, TextureResourceTraits>;
using RenderTextureHandle = RaylibResource<RenderTexture2D, RenderTextureResourceTraits>;
using FontHandle = RaylibResource<Font, FontResourceTraits>;
using ShaderHandle = RaylibResource<Shader, ShaderResourceTraits>;
using SoundHandle = RaylibResource<Sound, SoundResourceTraits>;
using MusicHandle = RaylibResource<Music, MusicResourceTraits>;
}

#endif
