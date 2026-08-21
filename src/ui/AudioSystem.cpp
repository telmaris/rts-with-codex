#include "ui/AudioSystem.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

// raylib 5.0 lacks IsMusicValid/IsSoundValid (added in 5.5); use struct fields instead.
static bool MusicOk(const Music& m)  { return m.ctxData != nullptr; }
static bool SoundOk(const Sound& s)  { return s.frameCount > 0; }

namespace
{
    constexpr float HalfPi = 1.57079632679f;
    constexpr float RotationLeadSeconds = 2.5f;

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
    tracks.clear();
    rotations.clear();
    sounds.clear();
    currentId.clear();
    nextId.clear();
    activeRotationId.clear();
    initialized = false;
}

void AudioSystem::RegisterMusic(const std::string& id, const std::string& filepath)
{
    if (!initialized) return;
    tracks[id].filepath = filepath;
}

void AudioSystem::RegisterMusicRotation(const std::string& rotationId,
                                        std::vector<std::string> trackIds)
{
    if (!initialized || rotationId.empty() || trackIds.empty())
        return;

    rotations[rotationId].trackIds = std::move(trackIds);
}

bool AudioSystem::EnsureMusicLoaded(MusicEntry& entry)
{
    if (entry.loaded)
        return MusicOk(entry.music.Get());

    entry.music  = tvorin::ui::MusicHandle{LoadMusicStream(entry.filepath.c_str())};
    entry.loaded = true;
    if (!MusicOk(entry.music.Get()))
        return false;

    entry.music.Get().looping = true;
    ::SetMusicVolume(entry.music.Get(), 0.0f);
    return true;
}

bool AudioSystem::PreloadMusic(const std::string& id)
{
    if (!initialized)
        return false;

    auto it = tracks.find(id);
    return it != tracks.end() && EnsureMusicLoaded(it->second);
}

void AudioSystem::RegisterSound(const std::string& id, const std::string& filepath)
{
    if (!initialized) return;
    tvorin::ui::SoundHandle sound{LoadSound(filepath.c_str())};
    if (SoundOk(sound.Get()))
        sounds[id] = std::move(sound);
    // Missing/unsupported file: silently skip; PlaySound("id") will be a no-op.
}

void AudioSystem::PlayMusic(const std::string& id, float fadeSecs)
{
    activeRotationId.clear();
    PlayMusicInternal(id, fadeSecs);
}

void AudioSystem::PlayMusicRotation(const std::string& rotationId,
                                    const std::string& firstTrackId,
                                    float fadeSecs)
{
    if (!initialized)
        return;

    auto rotationIt = rotations.find(rotationId);
    if (rotationIt == rotations.end() || rotationIt->second.trackIds.empty())
        return;

    if (std::find(rotationIt->second.trackIds.begin(), rotationIt->second.trackIds.end(),
                  firstTrackId) == rotationIt->second.trackIds.end())
        return;

    activeRotationId = rotationId;
    PlayMusicInternal(firstTrackId, fadeSecs);
}

void AudioSystem::PlayMusicInternal(const std::string& id, float fadeSecs)
{
    if (!initialized) return;
    auto it = tracks.find(id);
    if (it == tracks.end()) return;

    fadeSpeed       = (fadeSecs > 0.0f) ? (1.0f / fadeSecs) : 1000.0f;
    fadingToSilence = false;

    if (id == currentId)
    {
        if (!nextId.empty())
        {
            auto nextIt = tracks.find(nextId);
            if (nextIt != tracks.end() && nextIt->second.loaded && MusicOk(nextIt->second.music.Get()))
            {
                StopMusicStream(nextIt->second.music.Get());
                ::SetMusicVolume(nextIt->second.music.Get(), 0.0f);
            }
        }
        nextId.clear();
        nextVol = 0.0f;
        crossfadeProgress = 0.0f;
        return;
    }

    // A transition may request the same destination once before and once
    // after scene activation. Do not restart an already running crossfade.
    if (id == nextId)
        return;

    // Missing/unsupported file: mark as inactive and bail out.
    if (!EnsureMusicLoaded(it->second))
    {
        if (currentId == id) currentId.clear();
        if (nextId    == id) nextId.clear();
        return;
    }

    if (currentId.empty())
    {
        currentId  = id;
        currentVol = 0.0f;
        PlayMusicStream(it->second.music.Get());
    }
    else
    {
        if (!nextId.empty())
        {
            auto nextIt = tracks.find(nextId);
            if (nextIt != tracks.end() && nextIt->second.loaded && MusicOk(nextIt->second.music.Get()))
            {
                StopMusicStream(nextIt->second.music.Get());
                ::SetMusicVolume(nextIt->second.music.Get(), 0.0f);
            }
        }
        nextId  = id;
        nextVol = 0.0f;
        crossfadeProgress = 0.0f;
        crossfadeSourceVol = std::clamp(currentVol, 0.0f, 1.0f);
        PlayMusicStream(it->second.music.Get());
    }
}

void AudioSystem::QueueRandomRotationTrack(float fadeSecs)
{
    if (activeRotationId.empty() || !nextId.empty() || currentId.empty())
        return;

    auto rotationIt = rotations.find(activeRotationId);
    if (rotationIt == rotations.end())
        return;

    std::vector<std::string> candidates;
    candidates.reserve(rotationIt->second.trackIds.size());
    for (const std::string& trackId : rotationIt->second.trackIds)
    {
        if (trackId != currentId)
            candidates.push_back(trackId);
    }
    std::shuffle(candidates.begin(), candidates.end(), randomEngine);

    for (const std::string& candidate : candidates)
    {
        auto trackIt = tracks.find(candidate);
        if (trackIt == tracks.end() || !EnsureMusicLoaded(trackIt->second))
            continue;

        PlayMusicInternal(candidate, fadeSecs);
        return;
    }
}

void AudioSystem::StopMusic(float fadeSecs)
{
    activeRotationId.clear();
    if (!initialized || currentId.empty()) return;
    fadeSpeed       = (fadeSecs > 0.0f) ? (1.0f / fadeSecs) : 1000.0f;
    fadingToSilence = true;

    if (!nextId.empty())
    {
        auto nextIt = tracks.find(nextId);
        if (nextIt != tracks.end() && nextIt->second.loaded && MusicOk(nextIt->second.music.Get()))
        {
            StopMusicStream(nextIt->second.music.Get());
            ::SetMusicVolume(nextIt->second.music.Get(), 0.0f);
        }
    }
    nextId.clear();
    nextVol = 0.0f;
    crossfadeProgress = 0.0f;
}

void AudioSystem::Update(float dt)
{
    if (!initialized) return;

    for (auto& [id, entry] : tracks)
        if (entry.loaded && MusicOk(entry.music.Get()) && IsMusicStreamPlaying(entry.music.Get()))
            UpdateMusicStream(entry.music.Get());

    if (currentId.empty()) return;

    MusicEntry& current = tracks[currentId];
    if (!MusicOk(current.music.Get()))
    {
        currentId.clear();
        nextId.clear();
        return;
    }

    if (nextId.empty() && !fadingToSilence && !activeRotationId.empty())
    {
        const float length = GetMusicTimeLength(current.music.Get());
        const float played = GetMusicTimePlayed(current.music.Get());
        if (length > 0.0f && played > 0.0f && length - played <= RotationLeadSeconds)
            QueueRandomRotationTrack(1.6f);
    }

    if (!nextId.empty())
    {
        MusicEntry& next = tracks[nextId];
        if (!MusicOk(next.music.Get()))
        {
            nextId.clear();
            return;
        }

        // Equal-power curves keep the combined perceived energy stable in
        // the middle of the transition. Linear volume ramps sounded weak
        // there, especially when switching from menu to gameplay.
        crossfadeProgress = std::min(1.0f, crossfadeProgress + fadeSpeed * dt);
        const float angle = crossfadeProgress * HalfPi;
        currentVol = crossfadeSourceVol * std::cos(angle);
        nextVol    = std::sin(angle);

        ::SetMusicVolume(current.music.Get(), currentVol * EffectiveMusicVol());
        ::SetMusicVolume(next.music.Get(),    nextVol    * EffectiveMusicVol());

        if (crossfadeProgress >= 1.0f)
        {
            StopMusicStream(current.music.Get());
            currentId  = nextId;
            currentVol = 1.0f;
            nextId.clear();
            nextVol = 0.0f;
            crossfadeProgress = 0.0f;
            crossfadeSourceVol = 1.0f;
        }
    }
    else if (fadingToSilence)
    {
        currentVol = std::max(0.0f, currentVol - fadeSpeed * dt);
        ::SetMusicVolume(current.music.Get(), currentVol * EffectiveMusicVol());

        if (currentVol <= 0.0f)
        {
            StopMusicStream(current.music.Get());
            currentId.clear();
            fadingToSilence = false;
        }
    }
    else
    {
        currentVol = std::min(1.0f, currentVol + fadeSpeed * dt);
        ::SetMusicVolume(current.music.Get(), currentVol * EffectiveMusicVol());
    }
}

void AudioSystem::PlaySound(const std::string& id, float volume)
{
    if (!initialized) return;
    auto it = sounds.find(id);
    if (it == sounds.end()) return;
    SetSoundVolume(it->second.Get(), volume * EffectiveSfxVol());
    ::PlaySound(it->second.Get());
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
