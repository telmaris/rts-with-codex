#include "simulation/MapGenerator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <set>

namespace
{
    MapParameters MakeParams(unsigned int seed)
    {
        MapParameters params;
        params.sizePreset = MapSizePreset::S;
        params.seed = seed;
        return params;
    }

    // Cross product of (b-a) and (c-a) — zero means a/b/c are collinear.
    long long Cross(Vec2i a, Vec2i b, Vec2i c)
    {
        long long abx = b.x - a.x, aby = b.y - a.y;
        long long acx = c.x - a.x, acy = c.y - a.y;
        return abx * acy - aby * acx;
    }
}

// B1 (docs/work_plan_2026-07-13.md): replaces the old "host always at exact
// map center, opponents randomly maximin-placed" scheme with a single
// deterministic n-gon covering every player, including the host.

TEST(MapGeneratorTests, SinglePlayerReturnsOneAnchorNearMapCenter)
{
    MapParameters params = MakeParams(1);
    auto anchors = MapGenerator::PickHeadquartersAnchors(params, 1);
    ASSERT_EQ(anchors.size(), 1u);
    Vec2i footprint = MapGenerator::HeadquartersFootprint();
    EXPECT_NEAR(anchors[0].x, params.sizeX / 2 - footprint.x / 2, 2);
    EXPECT_NEAR(anchors[0].y, params.sizeY / 2 - footprint.y / 2, 2);
}

TEST(MapGeneratorTests, MultiplePlayersGetDistinctNonCollinearAnchors)
{
    for (int playerCount : {2, 3, 4, 5, 6})
    {
        MapParameters params = MakeParams(1000 + static_cast<unsigned int>(playerCount));
        auto anchors = MapGenerator::PickHeadquartersAnchors(params, playerCount);
        ASSERT_EQ(anchors.size(), static_cast<size_t>(playerCount)) << "playerCount=" << playerCount;

        // No two anchors coincide.
        std::set<std::pair<int, int>> seen;
        for (Vec2i anchor : anchors)
            EXPECT_TRUE(seen.insert({anchor.x, anchor.y}).second)
                << "duplicate anchor at (" << anchor.x << "," << anchor.y << ") playerCount=" << playerCount;

        if (playerCount < 3)
            continue;

        // Not every anchor collinear (the reported bug: players placed on a
        // line, so ring edges cross). At least one triple must have a
        // non-zero cross product.
        bool foundNonCollinearTriple = false;
        for (size_t i = 0; i < anchors.size() && !foundNonCollinearTriple; i++)
            for (size_t j = i + 1; j < anchors.size() && !foundNonCollinearTriple; j++)
                for (size_t k = j + 1; k < anchors.size() && !foundNonCollinearTriple; k++)
                    if (Cross(anchors[i], anchors[j], anchors[k]) != 0)
                        foundNonCollinearTriple = true;
        EXPECT_TRUE(foundNonCollinearTriple) << "all anchors collinear, playerCount=" << playerCount;
    }
}

TEST(MapGeneratorTests, AnchorsStayWithinMapBounds)
{
    Vec2i footprint = MapGenerator::HeadquartersFootprint();
    for (int playerCount : {2, 6})
    {
        MapParameters params = MakeParams(42);
        auto anchors = MapGenerator::PickHeadquartersAnchors(params, playerCount);
        for (Vec2i anchor : anchors)
        {
            EXPECT_GE(anchor.x, 0);
            EXPECT_GE(anchor.y, 0);
            EXPECT_LE(anchor.x + footprint.x, params.sizeX);
            EXPECT_LE(anchor.y + footprint.y, params.sizeY);
        }
    }
}

TEST(MapGeneratorTests, PlacementIsDeterministicForSameSeed)
{
    MapParameters params = MakeParams(777);
    auto anchorsA = MapGenerator::PickHeadquartersAnchors(params, 5);
    auto anchorsB = MapGenerator::PickHeadquartersAnchors(params, 5);
    EXPECT_EQ(anchorsA, anchorsB);
}
