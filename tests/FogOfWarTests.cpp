#include "core/FogOfWar.h"

#include <gtest/gtest.h>

namespace
{
    TEST(FogOfWarTests, RevealsTilesFromWorldSpaceCircle)
    {
        FogOfWarState fog;
        fog.Initialize({20, 20});
        fog.RevealWorldCircle({10.5f * TILE_SIZE, 10.5f * TILE_SIZE}, 2.1f * TILE_SIZE);

        EXPECT_TRUE(fog.IsVisible({10, 10}));
        EXPECT_TRUE(fog.IsVisible({12, 10}));
        EXPECT_FALSE(fog.IsVisible({13, 10}));
        EXPECT_FALSE(fog.IsVisible({10, 13}));
    }

    TEST(FogOfWarTests, VisibilityIsCurrentAndDoesNotLeaveStaleTiles)
    {
        FogOfWarState fog;
        fog.Initialize({12, 12});
        fog.RevealWorldCircle({4.5f * TILE_SIZE, 4.5f * TILE_SIZE}, TILE_SIZE);
        EXPECT_TRUE(fog.IsVisible({4, 4}));

        fog.BeginVisibilityUpdate();
        EXPECT_FALSE(fog.IsVisible({4, 4}));

        fog.RevealWorldCircle({8.5f * TILE_SIZE, 8.5f * TILE_SIZE}, TILE_SIZE);
        EXPECT_FALSE(fog.IsVisible({4, 4}));
        EXPECT_TRUE(fog.IsVisible({8, 8}));
    }

    TEST(FogOfWarTests, BuildingFootprintMustBeEntirelyVisible)
    {
        FogOfWarState fog;
        fog.Initialize({20, 20});
        fog.RevealWorldCircle({5.5f * TILE_SIZE, 5.5f * TILE_SIZE}, 1.1f * TILE_SIZE);

        EXPECT_TRUE(fog.IsFootprintVisible({5, 5}, {1, 1}));
        EXPECT_FALSE(fog.IsFootprintVisible({5, 5}, {2, 2}));
    }

    TEST(FogOfWarTests, HeadquartersHasAStartingDiscoveryAdvantage)
    {
        EXPECT_FLOAT_EQ(FogOfWar::HeadquartersRevealRadiusTiles, 104.0f);
        EXPECT_FLOAT_EQ(FogOfWar::StandardRevealRadiusTiles, 10.8f);
        EXPECT_GT(FogOfWar::HeadquartersRevealRadiusWorld,
                  FogOfWar::BuildingRevealRadiusWorld(BuildingType::Village, {1, 1}));
        EXPECT_EQ(FogOfWar::HeadquartersRevealRadiusWorld,
                  FogOfWar::HeadquartersRevealRadiusTiles * TILE_SIZE);
        EXPECT_EQ(FogOfWar::UnitRevealRadiusWorld,
                  FogOfWar::StandardRevealRadiusTiles * TILE_SIZE);
        EXPECT_EQ(FogOfWar::BuildingRevealRadiusWorld(BuildingType::Village, {1, 1}),
                  FogOfWar::StandardRevealRadiusTiles * TILE_SIZE);
    }
}
