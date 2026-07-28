#include "ui/ShaderLibrary.h"

#include "core/Log.h"

bool ShaderLibrary::LoadFragment(ShaderId id, const std::string& fragmentPath)
{
    if (!IsWindowReady())
    {
        Log::Msg("[Shaders] Cannot load ", fragmentPath, " before InitWindow().");
        return false;
    }
    if (!FileExists(fragmentPath.c_str()))
    {
        Log::Msg("[Shaders] Fragment shader not found: ", fragmentPath);
        return false;
    }

    auto existing = shaders.find(id);
    if (existing != shaders.end())
    {
        UnloadShader(existing->second);
        shaders.erase(existing);
        locations.erase(id);
    }

    Shader shader = LoadShader(nullptr, fragmentPath.c_str());
    if (shader.id == 0)
    {
        Log::Msg("[Shaders] Failed to load fragment shader: ", fragmentPath);
        return false;
    }

    shaders.emplace(id, shader);
    Log::Msg("[Shaders] Loaded fragment shader: ", fragmentPath);
    return true;
}

bool ShaderLibrary::IsAvailable(ShaderId id) const
{
    return shaders.find(id) != shaders.end();
}

const Shader* ShaderLibrary::Find(ShaderId id) const
{
    auto it = shaders.find(id);
    return it != shaders.end() ? &it->second : nullptr;
}

int ShaderLibrary::GetLocation(ShaderId id, const std::string& uniformName)
{
    const Shader* shader = Find(id);
    if (shader == nullptr)
        return -1;

    auto& byName = locations[id];
    auto existing = byName.find(uniformName);
    if (existing != byName.end())
        return existing->second;

    int location = GetShaderLocation(*shader, uniformName.c_str());
    byName.emplace(uniformName, location);
    return location;
}

void ShaderLibrary::Shutdown()
{
    if (IsWindowReady())
    {
        for (const auto& [id, shader] : shaders)
        {
            if (shader.id != 0)
                UnloadShader(shader);
        }
    }
    shaders.clear();
    locations.clear();
}
