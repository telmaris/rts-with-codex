#ifndef WORLD_LIGHTING_H
#define WORLD_LIGHTING_H

#include "raylib.h"

#include <cstdint>

// Purely visual time-of-day configuration. It deliberately lives outside the
// simulation state: the frame is derived from the already synchronized tick.
struct DayNightConfig
{
    std::uint64_t ticksPerDay{120000}; // 20 minutes at 100 Hz.
    float startPhase{0.50f};           // Noon by default.
    // Keep strategy readable when fog and night are both enabled.
    float minAmbient{0.58f};
};

struct WorldLightingFrame
{
    float phase{0.0f};
    Vector3 ambientColor{1.0f, 1.0f, 1.0f};
    float ambientIntensity{1.0f};
    float exposure{1.0f};
    float saturation{1.0f};
    float contrast{1.0f};
    Vector2 sunDirection{-0.7f, 0.7f};
    float shadowLength{0.0f};
    float localLightVisibility{0.0f};
};

// Render-only description of a local light. It never stores a gameplay
// pointer and its optional flicker is derived from simulationTick + stableId.
struct LightEmitterView
{
    Vector2 worldPosition{};
    Color color{WHITE};
    float radiusWorld{96.0f};
    float intensity{1.0f};
    float softness{0.65f};
    float flickerAmount{0.0f};
    int stableId{0};
    int priority{0};
};

// Uses no wall clock, randomness, or GPU state. The same tick always yields
// the same frame in single-player, on the host, and on a snapshot client.
WorldLightingFrame ComputeWorldLighting(std::uint64_t simulationTick,
                                        const DayNightConfig& config = {});

#endif
