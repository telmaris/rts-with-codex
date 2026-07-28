#include "ui/WorldLighting.h"

#include <cmath>

#include <gtest/gtest.h>

TEST(WorldLightingTests, UsesTheConfiguredStartingPhase)
{
    DayNightConfig config;
    config.ticksPerDay = 24000;
    config.startPhase = 0.0f;

    WorldLightingFrame frame = ComputeWorldLighting(0, config);

    EXPECT_NEAR(frame.phase, 0.0f, 0.0001f);
    EXPECT_NEAR(frame.ambientIntensity, 0.58f, 0.0001f);
    EXPECT_NEAR(frame.ambientColor.z, 0.80f, 0.0001f);
    EXPECT_NEAR(frame.localLightVisibility, 1.0f, 0.0001f);
}

TEST(WorldLightingTests, DefaultsToNoon)
{
    WorldLightingFrame frame = ComputeWorldLighting(0);

    EXPECT_NEAR(frame.phase, 0.50f, 0.0001f);
    EXPECT_GT(frame.ambientIntensity, 0.90f);
    EXPECT_GT(frame.shadowLength, 0.0f);
}

TEST(WorldLightingTests, DirectionalShadowVanishesAtNight)
{
    DayNightConfig config;
    config.startPhase = 0.0f;
    config.ticksPerDay = 24000;

    WorldLightingFrame night = ComputeWorldLighting(0, config);
    WorldLightingFrame morning = ComputeWorldLighting(7000, config);

    EXPECT_FLOAT_EQ(night.shadowLength, 0.0f);
    EXPECT_GT(morning.shadowLength, 0.0f);
}

TEST(WorldLightingTests, RepeatsExactlyAfterOneDay)
{
    DayNightConfig config;
    config.ticksPerDay = 24000;
    config.startPhase = 0.0f;

    WorldLightingFrame first = ComputeWorldLighting(5123, config);
    WorldLightingFrame repeated = ComputeWorldLighting(5123 + config.ticksPerDay, config);

    EXPECT_NEAR(repeated.phase, first.phase, 0.0001f);
    EXPECT_NEAR(repeated.ambientIntensity, first.ambientIntensity, 0.0001f);
    EXPECT_NEAR(repeated.ambientColor.x, first.ambientColor.x, 0.0001f);
    EXPECT_NEAR(repeated.localLightVisibility, first.localLightVisibility, 0.0001f);
}

TEST(WorldLightingTests, HonorsTheMinimumAmbientFloor)
{
    DayNightConfig config;
    config.startPhase = 0.0f;
    config.minAmbient = 0.80f;

    WorldLightingFrame frame = ComputeWorldLighting(0, config);

    EXPECT_NEAR(frame.ambientIntensity, 0.80f, 0.0001f);
}

TEST(WorldLightingTests, InterpolatesSmoothlyAcrossDawn)
{
    DayNightConfig config;
    config.ticksPerDay = 24000;
    config.startPhase = 0.0f;
    constexpr std::uint64_t dawnTick = 5500;

    WorldLightingFrame before = ComputeWorldLighting(dawnTick - 1, config);
    WorldLightingFrame after = ComputeWorldLighting(dawnTick + 1, config);

    EXPECT_LT(std::abs(after.ambientIntensity - before.ambientIntensity), 0.002f);
    EXPECT_LT(std::abs(after.ambientColor.y - before.ambientColor.y), 0.002f);
}
