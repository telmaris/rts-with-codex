#include "core/GameWorld.h"
#include "simulation/MapGenerator.h"
#include "ai/AIActions.h"

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

// User request 2026-07-19: iron is often missing near spawn. COAL and
// IRON_ORE starting patches were added alongside WOOD/STONE, on a wider ring
// (26..32 vs 17..23) so all four fit around the HQ without collisions. Every
// HQ should end up with a reachable, non-track patch of each within the
// starting zone.
TEST(MapGeneratorTests, EveryHqGetsStartingCoalAndIronOrePatches)
{
    for (unsigned int seed : {11u, 222u, 3333u, 44444u})
    {
        MapParameters params;
        params.sizePreset = MapSizePreset::S;
        params.aiOpponentCount = 1;
        params.seed = seed;

        GameWorld world;
        world.InitWorld("test", nullptr, nullptr, params);
        TileMap& map = world.GetTileMap();

        for (auto& [playerId, player] : world.GetPlayerHandler().players)
        {
            Building* hq = nullptr;
            for (auto* building : player->GetTrackedBuildings())
                if (building != nullptr && building->buildingType == BuildingType::Headquarters)
                    hq = building;
            ASSERT_NE(hq, nullptr) << "seed=" << seed << " player=" << playerId;
            Vec2i hqCenter = map.GetCoordsFromId(hq->positionId);

            int coalTiles = 0;
            int ironTiles = 0;
            int coalOnTrack = 0;
            int ironOnTrack = 0;
            constexpr int kSearchRadius = 35;
            for (int y = -kSearchRadius; y <= kSearchRadius; y++)
            {
                for (int x = -kSearchRadius; x <= kSearchRadius; x++)
                {
                    Vec2i pos{hqCenter.x + x, hqCenter.y + y};
                    if (!map.IsInside(pos))
                        continue;
                    const Tile& tile = map[pos];
                    if (tile.tileType == TileType::COAL)
                    {
                        coalTiles++;
                        if (tile.isMilitaryRoad) coalOnTrack++;
                    }
                    else if (tile.tileType == TileType::IRON_ORE)
                    {
                        ironTiles++;
                        if (tile.isMilitaryRoad) ironOnTrack++;
                    }
                }
            }

            EXPECT_GE(coalTiles, 10) << "seed=" << seed << " player=" << playerId << " too few COAL tiles near HQ";
            EXPECT_GE(ironTiles, 10) << "seed=" << seed << " player=" << playerId << " too few IRON_ORE tiles near HQ";
            EXPECT_EQ(coalOnTrack, 0) << "seed=" << seed << " player=" << playerId << " COAL patch overlaps the track";
            EXPECT_EQ(ironOnTrack, 0) << "seed=" << seed << " player=" << playerId << " IRON_ORE patch overlaps the track";
        }
    }
}

// Playtest report (2026-07-20): the starting Village's actual ROAD path to
// HQ (not straight-line distance) could end up much longer than intended
// once BuildStartRoad detours around the military track. Village placement
// now measures the real road path up front and re-rolls away from
// candidates that would exceed the budget (GameWorld.Init.cpp).
TEST(MapGeneratorTests, StartingVillageRoadStaysWithinBudget)
{
    constexpr int kMaxVillageRoadTiles = 15;
    for (unsigned int seed : {11u, 222u, 3333u, 44444u, 55555u, 66666u})
    {
        MapParameters params;
        params.sizePreset = MapSizePreset::S;
        params.aiOpponentCount = 1;
        params.seed = seed;

        GameWorld world;
        world.InitWorld("test", nullptr, nullptr, params);

        for (auto& [playerId, player] : world.GetPlayerHandler().players)
        {
            Building* hq = nullptr;
            Building* village = nullptr;
            for (auto* building : player->GetTrackedBuildings())
            {
                if (building == nullptr)
                    continue;
                if (building->buildingType == BuildingType::Headquarters)
                    hq = building;
                if (building->buildingType == BuildingType::Village)
                    village = building;
            }
            ASSERT_NE(hq, nullptr) << "seed=" << seed << " player=" << playerId;
            // A Village should always be placeable on a freshly generated
            // map; if this ever fires it means placement failed outright,
            // not just "farther than budget".
            ASSERT_NE(village, nullptr) << "seed=" << seed << " player=" << playerId << " no starting village placed";

            // The budget is on actual ROAD tiles (what BuildStartRoad places
            // between the two footprints), not RoadNetwork::CalculatePath's
            // footprint-inclusive convention (which always counts 2 more —
            // one tile from each endpoint's own footprint). At world-init
            // time the only roads a fresh player owns are this start road.
            int roadTiles = AIActions::CountOwnedBuildings(player.get(), BuildingType::Road);
            EXPECT_LE(roadTiles, kMaxVillageRoadTiles)
                << "seed=" << seed << " player=" << playerId << " village road is " << roadTiles << " tiles";
        }
    }
}
