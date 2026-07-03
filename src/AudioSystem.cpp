#include "../inc/AudioSystem.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

// raylib 5.0 lacks IsMusicValid/IsSoundValid (added in 5.5); use struct fields instead.
static bool MusicOk(const Music& m)  { return m.ctxData != nullptr; }
static bool SoundOk(const Sound& s)  { return s.frameCount > 0; }

namespace
{
    float ParseClampedFloatOrDefault(const std::string& text, float fallback)
    {
        try
        {
            return std::clamp(std::stof(text), 0.0f, 1.0f);
        }
        catch (...)
        {
            return fallback;
        }
    }
}

AudioConfig LoadAudioConfig()
{
    AudioConfig config;
    std::ifstream file("config/audio.cfg");
    if (!file.is_open())
        return config;

    std::string line;
    while (std::getline(file, line))
    {
        auto split = line.find('=');
        if (split == std::string::npos)
            continue;

        std::string key = line.substr(0, split);
        std::string value = line.substr(split + 1);
        if (key == "master_volume")
            config.masterVolume = ParseClampedFloatOrDefault(value, config.masterVolume);
        else if (key == "music_volume")
            config.musicVolume = ParseClampedFloatOrDefault(value, config.musicVolume);
        else if (key == "sfx_volume")
            config.sfxVolume = ParseClampedFloatOrDefault(value, config.sfxVolume);
    }
    return config;
}

void SaveAudioConfig(const AudioConfig& config)
{
    std::error_code error;
    std::filesystem::create_directories("config", error);
    std::ofstream file("config/audio.cfg", std::ios::trunc);
    if (!file.is_open())
        return;

    file << "master_volume=" << config.masterVolume << '\n';
    file << "music_volume=" << config.musicVolume << '\n';
    file << "sfx_volume=" << config.sfxVolume << '\n';
}

void AudioSystem::Init()
{
    initialized = true;
}

void AudioSystem::Cleanup()
{
    for (auto& [id, entry] : tracks)
        if (entry.loaded && MusicOk(entry.music))
            UnloadMusicStream(entry.music);
    for (auto& [id, sound] : sounds)
        if (SoundOk(sound))
            UnloadSound(sound);
    tracks.clear();
    sounds.clear();
    currentId.clear();
    nextId.clear();
    initialized = false;
}

void AudioSystem::RegisterMusic(const std::string& id, const std::string& filepath)
{
    if (!initialized) return;
    tracks[id].filepath = filepath;
}

void AudioSystem::RegisterSound(const std::string& id, const std::string& filepath)
{
    if (!initialized) return;
    Sound s = LoadSound(filepath.c_str());
    if (SoundOk(s))
        sounds[id] = s;
    // Missing/unsupported file: silently skip; PlaySound("id") will be a no-op.
}

void AudioSystem::PlayMusic(const std::string& id, float fadeSecs)
{
    if (!initialized) return;
    auto it = tracks.find(id);
    if (it == tracks.end()) return;

    fadeSpeed       = (fadeSecs > 0.0f) ? (1.0f / fadeSecs) : 1000.0f;
    fadingToSilence = false;

    if (id == currentId)
    {
        nextId.clear();
        return;
    }

    if (!it->second.loaded)
    {
        it->second.music  = LoadMusicStream(it->second.filepath.c_str());
        it->second.loaded = true;
        if (MusicOk(it->second.music))
            ::SetMusicVolume(it->second.music, 0.0f);
    }

    // Missing/unsupported file: mark as inactive and bail out.
    if (!MusicOk(it->second.music))
    {
        if (currentId == id) currentId.clear();
        if (nextId    == id) nextId.clear();
        return;
    }

    if (currentId.empty())
    {
        currentId  = id;
        currentVol = 0.0f;
        PlayMusicStream(it->second.music);
    }
    else
    {
        nextId  = id;
        nextVol = 0.0f;
        PlayMusicStream(it->second.music);
    }
}

void AudioSystem::StopMusic(float fadeSecs)
{
    if (!initialized || currentId.empty()) return;
    fadeSpeed       = (fadeSecs > 0.0f) ? (1.0f / fadeSecs) : 1000.0f;
    fadingToSilence = true;
    nextId.clear();
}

void AudioSystem::Update(float dt)
{
    if (!initialized) return;

    for (auto& [id, entry] : tracks)
        if (entry.loaded && MusicOk(entry.music) && IsMusicStreamPlaying(entry.music))
            UpdateMusicStream(entry.music);

    if (currentId.empty()) return;

    MusicEntry& current = tracks[currentId];
    if (!MusicOk(current.music))
    {
        currentId.clear();
        nextId.clear();
        return;
    }

    if (!nextId.empty())
    {
        MusicEntry& next = tracks[nextId];
        if (!MusicOk(next.music))
        {
            nextId.clear();
            return;
        }

        currentVol = std::max(0.0f, currentVol - fadeSpeed * dt);
        nextVol    = std::min(1.0f, nextVol    + fadeSpeed * dt);

        ::SetMusicVolume(current.music, currentVol * EffectiveMusicVol());
        ::SetMusicVolume(next.music,    nextVol    * EffectiveMusicVol());

        if (currentVol <= 0.0f)
        {
            StopMusicStream(current.music);
            currentId  = nextId;
            currentVol = nextVol;
            nextId.clear();
            nextVol = 0.0f;
        }
    }
    else if (fadingToSilence)
    {
        currentVol = std::max(0.0f, currentVol - fadeSpeed * dt);
        ::SetMusicVolume(current.music, currentVol * EffectiveMusicVol());

        if (currentVol <= 0.0f)
        {
            StopMusicStream(current.music);
            currentId.clear();
            fadingToSilence = false;
        }
    }
    else
    {
        currentVol = std::min(1.0f, currentVol + fadeSpeed * dt);
        ::SetMusicVolume(current.music, currentVol * EffectiveMusicVol());
    }
}

void AudioSystem::PlaySound(const std::string& id, float volume)
{
    if (!initialized) return;
    auto it = sounds.find(id);
    if (it == sounds.end()) return;
    SetSoundVolume(it->second, volume * EffectiveSfxVol());
    ::PlaySound(it->second);
}

void AudioSystem::SetMasterVolume(float v)
{
    masterVolume = std::clamp(v, 0.0f, 1.0f);
}

void AudioSystem::SetMusicVolume(float v)
{
    musicVolume = std::clamp(v, 0.0f, 1.0f);
}

void AudioSystem::SetSfxVolume(float v)
{
    sfxVolume = std::clamp(v, 0.0f, 1.0f);
}
