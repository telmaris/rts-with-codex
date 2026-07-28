#include "ui/Renderer.h"
#include "data/TextureConfig.h"

#include <gtest/gtest.h>

// ResolveAnimationFrame is a pure function of clip data + elapsed time, so it
// is testable without a raylib window (unlike texture loading/drawing).

TEST(RendererLifecycleTests, ConstructorDoesNotAllocateWorldLayers)
{
    Renderer renderer;

    EXPECT_FALSE(renderer.HasWorldLayers());
}

TEST(RendererLifecycleTests, RenderSettingsAreVisualOnlyAndConfigurableWithoutGpuResources)
{
    Renderer renderer;
    RenderSettings settings;
    settings.teamColors = false;
    settings.dayNightCycle = false;
    settings.dynamicLights = false;
    settings.contactShadows = false;
    settings.fogOfWar = true;
    settings.colorGrading = false;
    settings.retroFilter = true;
    settings.localLightBloom = true;
    settings.rainOverlay = true;
    settings.logisticsOverlay = true;
    settings.debugView = RenderDebugView::LightMap;

    renderer.SetRenderSettings(settings);

    EXPECT_FALSE(renderer.HasWorldLayers());
    EXPECT_FALSE(renderer.AreTeamColorsEnabled());
    EXPECT_FALSE(renderer.IsDayNightCycleEnabled());
    EXPECT_FALSE(renderer.AreDynamicLightsEnabled());
    EXPECT_FALSE(renderer.AreContactShadowsEnabled());
    EXPECT_TRUE(renderer.IsFogOfWarEnabled());
    EXPECT_FALSE(renderer.IsColorGradingEnabled());
    EXPECT_TRUE(renderer.IsRetroFilterEnabled());
    EXPECT_TRUE(renderer.IsLocalLightBloomEnabled());
    EXPECT_TRUE(renderer.IsRainOverlayEnabled());
    EXPECT_TRUE(renderer.IsLogisticsOverlayEnabled());
    EXPECT_EQ(renderer.GetRenderSettings().debugView, RenderDebugView::LightMap);
}

TEST(RendererLifecycleTests, FogPreferenceCanBeToggledWithoutWorldOrGpuState)
{
    SetFogOfWarPreferenceEnabled(false);
    EXPECT_FALSE(IsFogOfWarPreferenceEnabled());

    SetFogOfWarPreferenceEnabled(true);
    EXPECT_TRUE(IsFogOfWarPreferenceEnabled());

    SetFogOfWarPreferenceEnabled(false);
    EXPECT_FALSE(IsFogOfWarPreferenceEnabled());
}

TEST(RendererLifecycleTests, WorldPostProcessPreferencesCanBeChangedWithoutGpuState)
{
    SetColorGradingPreferenceEnabled(true);
    SetRetroFilterPreferenceEnabled(false);
    EXPECT_TRUE(IsColorGradingPreferenceEnabled());
    EXPECT_FALSE(IsRetroFilterPreferenceEnabled());

    SetColorGradingPreferenceEnabled(false);
    SetRetroFilterPreferenceEnabled(true);
    EXPECT_FALSE(IsColorGradingPreferenceEnabled());
    EXPECT_TRUE(IsRetroFilterPreferenceEnabled());

    // Restore application defaults for other renderer tests.
    SetColorGradingPreferenceEnabled(true);
    SetRetroFilterPreferenceEnabled(false);
}

TEST(RendererLifecycleTests, LocalLightBloomPreferenceCanBeChangedWithoutGpuState)
{
    SetLocalLightBloomPreferenceEnabled(false);
    EXPECT_FALSE(IsLocalLightBloomPreferenceEnabled());

    SetLocalLightBloomPreferenceEnabled(true);
    EXPECT_TRUE(IsLocalLightBloomPreferenceEnabled());

    SetLocalLightBloomPreferenceEnabled(false);
}

TEST(RendererLifecycleTests, RainOverlayPreferenceCanBeChangedWithoutGpuState)
{
    SetRainOverlayPreferenceEnabled(false);
    EXPECT_FALSE(IsRainOverlayPreferenceEnabled());

    SetRainOverlayPreferenceEnabled(true);
    EXPECT_TRUE(IsRainOverlayPreferenceEnabled());

    SetRainOverlayPreferenceEnabled(false);
}

TEST(RendererLifecycleTests, LogisticsOverlayPreferenceCanBeChangedWithoutGpuState)
{
    SetLogisticsOverlayPreferenceEnabled(false);
    EXPECT_FALSE(IsLogisticsOverlayPreferenceEnabled());

    SetLogisticsOverlayPreferenceEnabled(true);
    EXPECT_TRUE(IsLogisticsOverlayPreferenceEnabled());

    SetLogisticsOverlayPreferenceEnabled(false);
}

TEST(RendererLifecycleTests, HeadquartersFogRevealIsMuchLargerThanAnOrdinaryBuilding)
{
    Renderer renderer;

    renderer.QueueBuildingFogReveal(BuildingType::Headquarters, {3, 3}, {0.0f, 0.0f});
    renderer.QueueBuildingFogReveal(BuildingType::Woodcutter, {1, 1}, {0.0f, 0.0f});

    ASSERT_EQ(renderer.fogReveals.size(), 2u);
    EXPECT_NEAR(renderer.fogReveals[0].radiusWorld, 3072.0f, 0.001f);
    EXPECT_GT(renderer.fogReveals[0].radiusWorld, renderer.fogReveals[1].radiusWorld * 4.0f);
}

TEST(RendererLifecycleTests, FogSourceOutsideCameraIsKeptWhileItsRadiusTouchesTheView)
{
    Renderer renderer;
    renderer.camera.zoom = 1.0f;

    renderer.QueueFogReveal({{-100.0f, 540.0f}, 150.0f});
    renderer.QueueFogReveal({{-1000.0f, 540.0f}, 150.0f});

    ASSERT_EQ(renderer.fogReveals.size(), 1u);
    EXPECT_FLOAT_EQ(renderer.fogReveals.front().worldPosition.x, -100.0f);
}

TEST(RendererLifecycleTests, OperationalBuildingLightExtendsBeyondItsFootprint)
{
    Renderer renderer;
    renderer.camera.zoom = 1.0f;

    renderer.QueueBuildingLight(BuildingType::Village, {4, 4}, {100.0f, 100.0f}, 17, true);

    ASSERT_EQ(renderer.dynamicLights.size(), 1u);
    EXPECT_GT(renderer.dynamicLights.front().radiusWorld, 4.0f * TILE_SIZE);
    EXPECT_GT(renderer.dynamicLights.front().flickerAmount, 0.0f);
}

TEST(RendererLifecycleTests, DebugViewCyclesThroughAllAvailableRenderTargets)
{
    Renderer renderer;

    EXPECT_EQ(renderer.GetRenderSettings().debugView, RenderDebugView::Final);
    renderer.CycleDebugView();
    EXPECT_EQ(renderer.GetRenderSettings().debugView, RenderDebugView::WorldAlbedo);
    renderer.CycleDebugView();
    EXPECT_EQ(renderer.GetRenderSettings().debugView, RenderDebugView::LightMap);
    renderer.CycleDebugView();
    EXPECT_EQ(renderer.GetRenderSettings().debugView, RenderDebugView::FogMask);
    renderer.CycleDebugView();
    EXPECT_EQ(renderer.GetRenderSettings().debugView, RenderDebugView::Final);
}

TEST(RendererAnimationTests, StaticClipAlwaysReturnsStartFrame)
{
    AnimationClip clip{5, 1, 0.1f, true};

    EXPECT_EQ(ResolveAnimationFrame(clip, 0.0f), 5);
    EXPECT_EQ(ResolveAnimationFrame(clip, 100.0f), 5);
}

TEST(RendererAnimationTests, LoopingClipAdvancesThroughFrames)
{
    AnimationClip clip{0, 4, 0.1f, true};

    EXPECT_EQ(ResolveAnimationFrame(clip, 0.0f), 0);
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.15f), 1);
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.25f), 2);
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.35f), 3);
}

TEST(RendererAnimationTests, LoopingClipWrapsAroundTotalDuration)
{
    AnimationClip clip{0, 4, 0.1f, true};  // Total duration = 0.4s

    // 0.45s should wrap to the same frame as 0.05s (frame 0).
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.45f), ResolveAnimationFrame(clip, 0.05f));
}

TEST(RendererAnimationTests, NonLoopingClipClampsAtLastFrame)
{
    AnimationClip clip{0, 4, 0.1f, false};

    EXPECT_EQ(ResolveAnimationFrame(clip, 10.0f), 3);  // Well past total duration, stays on last frame.
}

TEST(RendererAnimationTests, StartFrameIdOffsetsAllFrames)
{
    AnimationClip clip{10, 3, 0.1f, true};

    EXPECT_EQ(ResolveAnimationFrame(clip, 0.0f), 10);
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.15f), 11);
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.25f), 12);
}

TEST(RendererAnimationTests, RegisteredBuildingAnimationAdvancesLeftToRight)
{
    Renderer renderer;

    EXPECT_FALSE(renderer.HasBuildingAnimation(BuildingType::Headquarters));
    renderer.RegisterBuildingAnimation(BuildingType::Headquarters, AnimationClip{0, 3, 0.18f, true});
    EXPECT_TRUE(renderer.HasBuildingAnimation(BuildingType::Headquarters));

    const auto& clip = renderer.buildingAnimations.at(BuildingType::Headquarters);
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.00f), 0);
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.18f), 1);
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.36f), 2);
    EXPECT_EQ(ResolveAnimationFrame(clip, 0.54f), 0);
}

TEST(TextureConfigIntegrationTests, HeadquartersArtworkUsesTheAuthoredThreeFrameStrip)
{
    std::string error;
    const TextureConfig config = LoadTextureConfig("assets/data/textures.rtsdata", &error);
    ASSERT_TRUE(error.empty()) << error;

    const auto buildingIt = std::find_if(
        config.buildings.begin(), config.buildings.end(),
        [](const BuildingTextureDefinition& definition)
        {
            return definition.buildingType == "Headquarters";
        });
    ASSERT_NE(buildingIt, config.buildings.end());

    const TextureAtlasDefinition* atlas = config.FindAtlas(buildingIt->sprite.atlasId);
    ASSERT_NE(atlas, nullptr);
    EXPECT_EQ(atlas->path,
              "assets/textures/building/generated/headquarters_idle/headquarters_idle_sheet_3x1.png");
    EXPECT_EQ(atlas->cellWidth, 96);
    EXPECT_EQ(atlas->cellHeight, 96);
    EXPECT_EQ(buildingIt->sprite.textureId, 0);
    EXPECT_TRUE(buildingIt->animation.enabled);
    EXPECT_EQ(buildingIt->animation.frames, 3);
    EXPECT_DOUBLE_EQ(buildingIt->animation.frameTime, 0.18);
    EXPECT_TRUE(buildingIt->animation.looping);
}

TEST(TextureConfigIntegrationTests, TanneryArtworkUsesTheAuthoredTwoFrameStrip)
{
    std::string error;
    const TextureConfig config = LoadTextureConfig("assets/data/textures.rtsdata", &error);
    ASSERT_TRUE(error.empty()) << error;

    const auto buildingIt = std::find_if(
        config.buildings.begin(), config.buildings.end(),
        [](const BuildingTextureDefinition& definition)
        {
            return definition.buildingType == "Tannery";
        });
    ASSERT_NE(buildingIt, config.buildings.end());

    const TextureAtlasDefinition* atlas = config.FindAtlas(buildingIt->sprite.atlasId);
    ASSERT_NE(atlas, nullptr);
    EXPECT_EQ(atlas->path,
              "assets/textures/building/generated/tannery_idle/tannery_idle_sheet_2x1.png");
    EXPECT_EQ(atlas->cellWidth, 96);
    EXPECT_EQ(atlas->cellHeight, 96);
    EXPECT_EQ(buildingIt->sprite.textureId, 0);
    EXPECT_TRUE(buildingIt->animation.enabled);
    EXPECT_EQ(buildingIt->animation.frames, 2);
    EXPECT_DOUBLE_EQ(buildingIt->animation.frameTime, 0.30);
    EXPECT_TRUE(buildingIt->animation.looping);
}

TEST(TextureConfigIntegrationTests, TailorArtworkUsesTheAuthoredTwoFrameStrip)
{
    std::string error;
    const TextureConfig config = LoadTextureConfig("assets/data/textures.rtsdata", &error);
    ASSERT_TRUE(error.empty()) << error;

    const auto buildingIt = std::find_if(
        config.buildings.begin(), config.buildings.end(),
        [](const BuildingTextureDefinition& definition)
        {
            return definition.buildingType == "Tailor";
        });
    ASSERT_NE(buildingIt, config.buildings.end());

    const TextureAtlasDefinition* atlas = config.FindAtlas(buildingIt->sprite.atlasId);
    ASSERT_NE(atlas, nullptr);
    EXPECT_EQ(atlas->path,
              "assets/textures/building/generated/tailor_idle/tailor_idle_sheet_2x1.png");
    EXPECT_EQ(atlas->cellWidth, 96);
    EXPECT_EQ(atlas->cellHeight, 96);
    EXPECT_EQ(buildingIt->sprite.textureId, 0);
    EXPECT_TRUE(buildingIt->animation.enabled);
    EXPECT_EQ(buildingIt->animation.frames, 2);
    EXPECT_DOUBLE_EQ(buildingIt->animation.frameTime, 0.30);
    EXPECT_TRUE(buildingIt->animation.looping);
}

TEST(TextureConfigIntegrationTests, ArmorerArtworkUsesTheAuthoredTwoFrameStrip)
{
    std::string error;
    const TextureConfig config = LoadTextureConfig("assets/data/textures.rtsdata", &error);
    ASSERT_TRUE(error.empty()) << error;

    const auto buildingIt = std::find_if(
        config.buildings.begin(), config.buildings.end(),
        [](const BuildingTextureDefinition& definition)
        {
            return definition.buildingType == "Armorer";
        });
    ASSERT_NE(buildingIt, config.buildings.end());

    const TextureAtlasDefinition* atlas = config.FindAtlas(buildingIt->sprite.atlasId);
    ASSERT_NE(atlas, nullptr);
    EXPECT_EQ(atlas->path,
              "assets/textures/building/generated/armorer_idle/armorer_idle_sheet_2x1.png");
    EXPECT_EQ(atlas->cellWidth, 96);
    EXPECT_EQ(atlas->cellHeight, 96);
    EXPECT_EQ(buildingIt->sprite.textureId, 0);
    EXPECT_TRUE(buildingIt->animation.enabled);
    EXPECT_EQ(buildingIt->animation.frames, 2);
    EXPECT_DOUBLE_EQ(buildingIt->animation.frameTime, 0.30);
    EXPECT_TRUE(buildingIt->animation.looping);
}
