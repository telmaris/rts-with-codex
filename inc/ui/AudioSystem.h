#ifndef AUDIO_SYSTEM_H
#define AUDIO_SYSTEM_H

#include "raylib.h"
#include "ui/RaylibResource.h"

#include <map>
#include <random>
#include <string>
#include <vector>

inline constexpr float DefaultMusicCrossfadeSeconds = 1.6f;

// Manages background music (crossfade between named tracks) and one-shot sound effects.
// Call Init() after InitAudioDevice(), Update(dt) every frame, Cleanup() before CloseAudioDevice().
// All sounds and tracks are identified by a std::string id registered at startup.
class AudioSystem
{
public:
    void Init();
    void Cleanup();
    void Update(float dt);

    // Register asset paths before first playback. Must be called after Init().
    void RegisterMusic(const std::string& id, const std::string& filepath);
    void RegisterMusicRotation(const std::string& rotationId,
                               std::vector<std::string> trackIds);
    // Load a registered music stream ahead of time so a later transition does
    // not block the render loop while the old scene is still visible.
    bool PreloadMusic(const std::string& id);
    void RegisterSound(const std::string& id, const std::string& filepath);

    // Start (or crossfade to) a named music track. No-op if that track is already active.
    void PlayMusic(const std::string& id, float fadeSecs = DefaultMusicCrossfadeSeconds);
    // Starts a registered random rotation at the requested first track. The
    // following tracks are selected without immediately repeating the current
    // one and crossfaded shortly before it ends.
    void PlayMusicRotation(const std::string& rotationId, const std::string& firstTrackId,
                           float fadeSecs = DefaultMusicCrossfadeSeconds);
    // Fade the current track to silence.
    void StopMusic(float fadeSecs = DefaultMusicCrossfadeSeconds);
    // Trigger a one-shot sound effect; volume is a [0,1] per-call multiplier on top of sfxVolume.
    void PlaySound(const std::string& id, float volume = 1.0f);

    void SetMasterVolume(float v);
    void SetMusicVolume(float v);
    void SetSfxVolume(float v);

    float GetMasterVolume() const { return masterVolume; }
    float GetMusicVolume() const { return musicVolume; }
    float GetSfxVolume() const { return sfxVolume; }

private:
    float EffectiveMusicVol() const { return masterVolume * musicVolume; }
    float EffectiveSfxVol()  const { return masterVolume * sfxVolume; }

    struct MusicEntry
    {
        tvorin::ui::MusicHandle music{};
        std::string filepath;
        bool        loaded{false};
    };

    struct MusicRotation
    {
        std::vector<std::string> trackIds;
    };

    std::map<std::string, MusicEntry> tracks;
    std::map<std::string, MusicRotation> rotations;
    std::map<std::string, tvorin::ui::SoundHandle> sounds;

    // Crossfade state
    std::string currentId;
    std::string nextId;
    float currentVol{0.0f};   // interpolated [0,1] volume of current track
    float nextVol{0.0f};      // interpolated [0,1] volume of incoming track
    float crossfadeProgress{0.0f};
    float crossfadeSourceVol{0.0f};
    float fadeSpeed{1.0f};    // volume units per second (set from fadeSecs)
    bool  fadingToSilence{false};
    std::string activeRotationId;
    std::mt19937 randomEngine{std::random_device{}()};

    float masterVolume{0.8f};
    float musicVolume{0.8f};
    float sfxVolume{0.2f};
    bool  initialized{false};

    bool EnsureMusicLoaded(MusicEntry& entry);
    void PlayMusicInternal(const std::string& id, float fadeSecs);
    void QueueRandomRotationTrack(float fadeSecs);
};

// Persisted audio preferences, stored alongside other config/*.cfg files.
struct AudioConfig
{
    float masterVolume{0.5f};
    float musicVolume{0.5f};
    float sfxVolume{0.2f};
};

// Reads config/audio.cfg, returning defaults when missing or unreadable.
AudioConfig LoadAudioConfig();
// Writes config/audio.cfg, creating the config/ directory if needed.
void SaveAudioConfig(const AudioConfig& config);

#endif
