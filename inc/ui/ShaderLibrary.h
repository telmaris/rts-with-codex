#ifndef SHADER_LIBRARY_H
#define SHADER_LIBRARY_H

#include "raylib.h"
#include "ui/RaylibResource.h"

#include <map>
#include <string>

// Keep shader handles in one context-owned object. A failed shader is optional:
// callers check IsAvailable() and use their normal raylib draw path instead.
enum class ShaderId
{
    TeamColor,
    ResourceOverlay,
    WorldLighting,
    RadialLight,
    FogOfWar,
    FogRoad,
    WorldPostProcess,
};

class ShaderLibrary
{
public:
    ShaderLibrary() = default;
    ShaderLibrary(const ShaderLibrary&) = delete;
    ShaderLibrary& operator=(const ShaderLibrary&) = delete;

    // Loads a fragment shader paired with raylib's default vertex shader.
    // Returns false without throwing when the file, context, or compilation is unavailable.
    bool LoadFragment(ShaderId id, const std::string& fragmentPath);
    bool IsAvailable(ShaderId id) const;
    const Shader* Find(ShaderId id) const;
    // Queries a uniform once per loaded shader; -1 means it is absent.
    int GetLocation(ShaderId id, const std::string& uniformName);

    // Must be called while the raylib context is alive. Safe to call repeatedly.
    void Shutdown();

private:
    std::map<ShaderId, tvorin::ui::ShaderHandle> shaders;
    std::map<ShaderId, std::map<std::string, int>> locations;
};

#endif
