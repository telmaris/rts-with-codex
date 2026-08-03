#include "ui/Renderer.h"
#include "core/FogOfWar.h"
#include "data/TextureConfig.h"

#include <cmath>

#include <gtest/gtest.h>

// ResolveAnimationFrame is a pure function of clip data + elapsed time, so it
// is testable without a raylib window (unlike texture loading/drawing).

TEST(RendererLifecycleTests, ConstructorDoesNotAllocateWorldLayers)
{
    Renderer renderer;

    EXPECT_FALSE(renderer.HasWorldLayers());
    EXPECT_TRUE(renderer.HasBuildingAnimation(BuildingType::Bakery));
    EXPECT_FALSE(renderer.HasBuildingAnimation(BuildingType::StorageBuilding));
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
    EXPECT_NEAR(renderer.fogReveals[0].radiusWorld,
                FogOfWar::HeadquartersRevealRadiusTiles * TILE_SIZE, 0.001f);
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

TEST(RendererLifecycleTests, CameraKeepsTileEdgesAndOriginOnRenderPixels)
{
    Renderer renderer;
    renderer.camera.zoom = 1.17f;
    renderer.camera.target = {123.456f, -456.789f};

    renderer.ClampCameraToMap({200, 200});

    const float pixelsPerTile = renderer.camera.zoom * static_cast<float>(TILE_SIZE);
    EXPECT_NEAR(pixelsPerTile, std::round(pixelsPerTile), 0.0001f);
    EXPECT_NEAR(renderer.camera.target.x * renderer.camera.zoom,
                std::round(renderer.camera.target.x * renderer.camera.zoom), 0.0001f);
    EXPECT_NEAR(renderer.camera.target.y * renderer.camera.zoom,
                std::round(renderer.camera.target.y * renderer.camera.zoom), 0.0001f);
}

TEST(RendererLifecycleTests, OperationalBuildingsQueueAStableLight)
{
    Renderer renderer;
    renderer.camera.zoom = 1.0f;

    renderer.QueueBuildingLight(BuildingType::Village, {4, 4}, {100.0f, 100.0f}, 17, true);
    ASSERT_EQ(renderer.dynamicLights.size(), 1u);
    EXPECT_GE(renderer.dynamicLights.front().radiusWorld, 4.0f * TILE_SIZE);
    EXPECT_FLOAT_EQ(renderer.dynamicLights.front().flickerAmount, 0.0f);

    renderer.QueueBuildingLight(BuildingType::Foundry, {3, 3}, {100.0f, 100.0f}, 18, true);

    ASSERT_EQ(renderer.dynamicLights.size(), 2u);
    EXPECT_GT(renderer.dynamicLights.back().radiusWorld, 4.0f * TILE_SIZE);
    EXPECT_FLOAT_EQ(renderer.dynamicLights.back().flickerAmount, 0.0f);
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

TEST(TextureConfigIntegrationTests, HeadquartersArtworkUsesTheAuthoredFiveFrame4x4Strip)
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
              "assets/textures/building/generated/headquarters_idle/headquarters_idle_sheet_5x1_4x4.png");
    EXPECT_EQ(atlas->cellWidth, 256);
    EXPECT_EQ(atlas->cellHeight, 256);
    EXPECT_EQ(buildingIt->sprite.textureId, 0);
    EXPECT_TRUE(buildingIt->animation.enabled);
    EXPECT_EQ(buildingIt->animation.frames, 5);
    EXPECT_DOUBLE_EQ(buildingIt->animation.frameTime, 0.18);
    EXPECT_TRUE(buildingIt->animation.looping);
}

TEST(TextureConfigIntegrationTests, StorageIsStaticAndBakeryUsesTheAuthoredFiveFrameStrip)
{
    std::string error;
    const TextureConfig config = LoadTextureConfig("assets/data/textures.rtsdata", &error);
    ASSERT_TRUE(error.empty()) << error;

    const auto expectStaticBuilding = [&](const char* buildingName, const char* expectedPath)
    {
        const auto buildingIt = std::find_if(
            config.buildings.begin(), config.buildings.end(),
            [&](const BuildingTextureDefinition& definition)
            {
                return definition.buildingType == buildingName;
            });
        ASSERT_NE(buildingIt, config.buildings.end());
        const TextureAtlasDefinition* atlas = config.FindAtlas(buildingIt->sprite.atlasId);
        ASSERT_NE(atlas, nullptr);
        EXPECT_EQ(atlas->path, expectedPath);
        EXPECT_FALSE(buildingIt->animation.enabled);
    };

    expectStaticBuilding("StorageBuilding",
                         "assets/textures/building/generated/storage_idle_v2/storage_style_v2_pixellab_3x3.png");

    const auto bakeryIt = std::find_if(
        config.buildings.begin(), config.buildings.end(),
        [](const BuildingTextureDefinition& definition)
        {
            return definition.buildingType == "Bakery";
        });
    ASSERT_NE(bakeryIt, config.buildings.end());
    const TextureAtlasDefinition* bakeryAtlas = config.FindAtlas(bakeryIt->sprite.atlasId);
    ASSERT_NE(bakeryAtlas, nullptr);
    EXPECT_EQ(bakeryAtlas->path,
              "assets/textures/building/generated/bakery_idle_v3/bakery_idle_sheet_5x1.png");
    EXPECT_TRUE(bakeryIt->animation.enabled);
    EXPECT_EQ(bakeryIt->animation.frames, 5);
    EXPECT_DOUBLE_EQ(bakeryIt->animation.frameTime, 0.18);
    EXPECT_TRUE(bakeryIt->animation.looping);
}

TEST(TextureConfigIntegrationTests, TanneryArtworkUsesTheAuthoredStaticSprite)
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
              "assets/textures/building/generated/tannery_idle_v2/tannery_idle_4x4.png");
    EXPECT_EQ(atlas->cellWidth, 256);
    EXPECT_EQ(atlas->cellHeight, 256);
    EXPECT_EQ(buildingIt->sprite.textureId, 0);
    EXPECT_FALSE(buildingIt->animation.enabled);
}

TEST(TextureConfigIntegrationTests, TailorArtworkUsesTheAuthoredStaticSprite)
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
              "assets/textures/building/generated/tailor_idle_v2/tailor_idle_3x3.png");
    EXPECT_EQ(atlas->cellWidth, 192);
    EXPECT_EQ(atlas->cellHeight, 192);
    EXPECT_EQ(buildingIt->sprite.textureId, 0);
    EXPECT_FALSE(buildingIt->animation.enabled);
}

TEST(TextureConfigIntegrationTests, ArmorerArtworkUsesTheAuthoredStaticSprite)
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
              "assets/textures/building/generated/armorer_idle_v2/armorer_idle_3x3.png");
    EXPECT_EQ(atlas->cellWidth, 192);
    EXPECT_EQ(atlas->cellHeight, 192);
    EXPECT_EQ(buildingIt->sprite.textureId, 0);
    EXPECT_FALSE(buildingIt->animation.enabled);
}

TEST(TextureConfigIntegrationTests, RoadAndBridgeUseCanonical64PixelAutotileAtlas)
{
    std::string error;
    const TextureConfig config = LoadTextureConfig("assets/data/textures.rtsdata", &error);
    ASSERT_TRUE(error.empty()) << error;

    const auto findBuilding = [&](const char* buildingName)
    {
        return std::find_if(
            config.buildings.begin(), config.buildings.end(),
            [&](const BuildingTextureDefinition& definition)
            {
                return definition.buildingType == buildingName;
            });
    };

    const auto roadIt = findBuilding("Road");
    const auto bridgeIt = findBuilding("Bridge");
    ASSERT_NE(roadIt, config.buildings.end());
    ASSERT_NE(bridgeIt, config.buildings.end());
    EXPECT_EQ(roadIt->sprite.atlasId, 19);
    EXPECT_EQ(bridgeIt->sprite.atlasId, 19);

    const TextureAtlasDefinition* atlas = config.FindAtlas(19);
    ASSERT_NE(atlas, nullptr);
    EXPECT_EQ(atlas->path,
              "assets/textures/roads/generated/roads_bridges_4x8_64px.png");
    EXPECT_EQ(atlas->cellWidth, 64);
    EXPECT_EQ(atlas->cellHeight, 64);
}

TEST(TextureConfigIntegrationTests, MilitaryRoadUsesCanonical64PixelAutotileAtlas)
{
    std::string error;
    const TextureConfig config = LoadTextureConfig("assets/data/textures.rtsdata", &error);
    ASSERT_TRUE(error.empty()) << error;

    const TextureAtlasDefinition* atlas = config.FindAtlas(144);
    ASSERT_NE(atlas, nullptr);
    EXPECT_EQ(atlas->path,
              "assets/textures/military_roads/generated/military_road_autotiles_4x4_64px.png");
    EXPECT_EQ(atlas->cellWidth, 64);
    EXPECT_EQ(atlas->cellHeight, 64);
}

TEST(TextureConfigIntegrationTests, RoadsAndMilitaryRoadsExposeDeterministicMaterialVariants)
{
    std::string error;
    const TextureConfig config = LoadTextureConfig("assets/data/textures.rtsdata", &error);
    ASSERT_TRUE(error.empty()) << error;

    const TextureAtlasDefinition* roadVariants = config.FindAtlas(145);
    ASSERT_NE(roadVariants, nullptr);
    EXPECT_EQ(roadVariants->path,
              "assets/textures/roads/generated/roads_autotile_variants_3x4_64px.png");
    EXPECT_EQ(roadVariants->cellWidth, 64);
    EXPECT_EQ(roadVariants->cellHeight, 64);

    const TextureAtlasDefinition* militaryRoadVariants = config.FindAtlas(146);
    ASSERT_NE(militaryRoadVariants, nullptr);
    EXPECT_EQ(militaryRoadVariants->path,
              "assets/textures/military_roads/generated/military_road_variants_3x4_64px.png");
    EXPECT_EQ(militaryRoadVariants->cellWidth, 64);
    EXPECT_EQ(militaryRoadVariants->cellHeight, 64);
}
