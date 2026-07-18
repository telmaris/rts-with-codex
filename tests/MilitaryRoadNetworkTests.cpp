#include "core/GameWorld.h"
#include "simulation/MilitaryRoadNetwork.h"
#include "simulation/MapGenerator.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <set>

namespace
{
    MapParameters MakeParams(int aiOpponentCount, unsigned int seed)
    {
        MapParameters params;
        params.sizePreset = MapSizePreset::S;
        params.aiOpponentCount = aiOpponentCount;
        params.seed = seed;
        return params;
    }

    // Verifies the ring is a single connected cycle touching every player
    // exactly once, by walking neighbor links starting from player 0.
    void ExpectSingleConnectedRing(const MilitaryRoadNetwork& roads, int playerCount)
    {
        std::set<int> visited;
        int current = 0;
        int previous = -1;
        for (int step = 0; step < playerCount; step++)
        {
            visited.insert(current);
            auto neighbors = roads.GetNeighbors(current);
            ASSERT_EQ(neighbors.size(), playerCount == 2 ? 1u : 2u) << "player " << current;
            int next = -1;
            for (int n : neighbors)
                if (n != previous)
                {
                    next = n;
                    break;
                }
            if (next == -1)
                next = neighbors.front();
            previous = current;
            current = next;
        }
        EXPECT_EQ(visited.size(), static_cast<size_t>(playerCount));
    }
}

TEST(MilitaryRoadNetworkTests, TwoPlayersGetExactlyOneMutualRoute)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeParams(1, 111));

    const auto& roads = world.GetMilitaryRoads();
    ASSERT_EQ(roads.GetRoutes().size(), 1u);
    EXPECT_TRUE(roads.AreConnected(0, 1));
    EXPECT_EQ(roads.GetNeighbors(0), std::vector<int>{1});
    EXPECT_EQ(roads.GetNeighbors(1), std::vector<int>{0});

    const MilitaryRoute* route = roads.FindRoute(0, 1);
    ASSERT_NE(route, nullptr);
    EXPECT_FALSE(route->tiles.empty());
}

TEST(MilitaryRoadNetworkTests, ThreeFourAndFivePlayersFormASingleRing)
{
    for (int aiOpponentCount : {2, 3, 4})
    {
        GameWorld world;
        world.InitWorld("test", nullptr, nullptr, MakeParams(aiOpponentCount, 222));

        int playerCount = aiOpponentCount + 1;
        const auto& roads = world.GetMilitaryRoads();
        EXPECT_EQ(roads.GetRoutes().size(), static_cast<size_t>(playerCount))
            << "playerCount=" << playerCount;
        ExpectSingleConnectedRing(roads, playerCount);
    }
}

// B2 (docs/work_plan_2026-07-13.md): ring edges are carved sequentially and
// avoid tiles already claimed by an earlier edge, so no two routes should
// ever run alongside or cross each other. The two gate tiles at each route's
// ends are allowed to coincide with a neighboring edge's gate at a shared HQ
// (that's the same physical doorway both routes pass through) — only each
// route's INTERIOR tiles (everything but its first/last tile) are checked
// for global uniqueness across the whole ring.
TEST(MilitaryRoadNetworkTests, RingRoutesDoNotOverlapExceptAtSharedGates)
{
    for (int aiOpponentCount : {2, 3, 4})
    {
        GameWorld world;
        world.InitWorld("test", nullptr, nullptr, MakeParams(aiOpponentCount, 555));

        const auto& routes = world.GetMilitaryRoads().GetRoutes();
        std::map<int, int> interiorTileUseCount;
        for (const auto& route : routes)
        {
            ASSERT_GE(route.tiles.size(), 2u);
            for (size_t i = 1; i + 1 < route.tiles.size(); i++)
                interiorTileUseCount[route.tiles[i]]++;
        }
        for (const auto& [tileId, count] : interiorTileUseCount)
            EXPECT_EQ(count, 1) << "tile " << tileId << " shared by " << count
                                 << " routes' interiors (aiOpponentCount=" << aiOpponentCount << ")";
    }
}

// B7 (docs/work_plan_2026-07-13.md, user report with screenshot 2026-07-14):
// a ring vertex's two ring edges used to both aim PickGateTile at their own
// neighbor's HQ center, which frequently resolved to the SAME face of the
// footprint — collapsing both corridors onto the same exit tile right
// outside the base. The fix forces the second edge's gate onto the OPPOSITE
// face whenever the natural pick would collide with the first. Any two
// DIFFERENT faces of a square footprint are at least (footprint-1) tiles
// apart (Chebyshev) by construction — opposite faces are (footprint+1) apart
// — so asserting that floor catches a same-face collapse (which would be 0)
// without depending on which specific pair of faces was chosen.
TEST(MilitaryRoadNetworkTests, PlayerGatesAreSpreadAcrossDifferentSidesOfHq)
{
    const int minSeparation = MapGenerator::HeadquartersFootprint().x - 1;

    for (unsigned int seed : {111u, 222u, 333u, 555u})
    {
        for (int aiOpponentCount : {2, 3})
        {
            GameWorld world;
            world.InitWorld("test", nullptr, nullptr, MakeParams(aiOpponentCount, seed));

            int playerCount = aiOpponentCount + 1;
            const auto& routes = world.GetMilitaryRoads().GetRoutes();
            const TileMap& map = world.GetTileMap();

            for (int playerId = 0; playerId < playerCount; playerId++)
            {
                std::vector<Vec2i> gates;
                for (const auto& route : routes)
                {
                    if (route.playerA == playerId)
                        gates.push_back(map.GetCoordsFromId(route.tiles.front()));
                    else if (route.playerB == playerId)
                        gates.push_back(map.GetCoordsFromId(route.tiles.back()));
                }

                if (gates.size() < 2)
                    continue; // n=2 ring: exactly one gate per player, nothing to separate

                ASSERT_EQ(gates.size(), 2u) << "player " << playerId << " seed=" << seed;
                int chebyshev = std::max(std::abs(gates[0].x - gates[1].x), std::abs(gates[0].y - gates[1].y));
                EXPECT_GE(chebyshev, minSeparation)
                    << "player " << playerId << " gates too close: (" << gates[0].x << "," << gates[0].y
                    << ") vs (" << gates[1].x << "," << gates[1].y << ") seed=" << seed
                    << " aiOpponentCount=" << aiOpponentCount;
            }
        }
    }
}

// User report 2026-07-14: the unit track was still landing on wood/stone
// fields. Two enforced invariants: (1) a carved route tile is always plain
// GRASS with zero richness — even when a relaxed fallback tier had to route
// through a deposit, the carve resets the terrain; (2) the starting resource
// patches placed AFTER the road (PlaceStartingResourcePatch) never paint
// over an isMilitaryRoad tile.
// User request (2026-07-16): the track must leave each HQ as a straight
// line — the serpentine wiggle belongs mid-route only (near-gate
// straightening in MilitaryRoadNetwork's FindRouteTiles). Checks the first
// and last few steps of every route stay colinear.
TEST(MilitaryRoadNetworkTests, RoutesLeaveEveryGateStraight)
{
    constexpr int StraightSteps = 5;  // conservative slice of the 14-tile straight zone
    for (unsigned int seed : {111u, 555u, 777u})
    {
        for (int aiOpponentCount : {1, 3})
        {
            GameWorld world;
            world.InitWorld("test", nullptr, nullptr, MakeParams(aiOpponentCount, seed));
            TileMap& map = world.GetTileMap();

            for (const auto& route : world.GetMilitaryRoads().GetRoutes())
            {
                ASSERT_GE(route.tiles.size(), static_cast<size_t>(StraightSteps));
                auto expectColinear = [&](std::vector<Vec2i> segment, const char* which)
                {
                    bool sameX = true;
                    bool sameY = true;
                    for (const Vec2i& pos : segment)
                    {
                        sameX = sameX && pos.x == segment.front().x;
                        sameY = sameY && pos.y == segment.front().y;
                    }
                    EXPECT_TRUE(sameX || sameY)
                        << which << " end of route " << route.playerA << "-" << route.playerB
                        << " bends within its straight zone (seed " << seed
                        << ", players " << aiOpponentCount + 1 << ")";
                };

                std::vector<Vec2i> head;
                std::vector<Vec2i> tail;
                for (int i = 0; i < StraightSteps; i++)
                {
                    head.push_back(map.GetCoordsFromId(route.tiles[i]));
                    tail.push_back(map.GetCoordsFromId(route.tiles[route.tiles.size() - 1 - i]));
                }
                expectColinear(head, "gate-A");
                expectColinear(tail, "gate-B");
            }
        }
    }
}

// Playtest 2026-07-17 #3 ("kolanka"): a 90° elbow with 4+ straight tiles on
// BOTH sides reads as a hard square corner — mid-route those must not
// survive (the direction-aware search turn-penalizes them and RoundCorners
// opens the rest into staircase arcs). Gate stubs and their opening cones
// are exempt via the margin.
TEST(MilitaryRoadNetworkTests, RoutesHaveNoSquareElbowsMidRoute)
{
    constexpr int Margin = 12;  // skip the stub + opening cone at each end
    constexpr int Leg = 4;
    for (unsigned int seed : {111u, 555u, 777u})
    {
        for (int aiOpponentCount : {1, 3})
        {
            GameWorld world;
            world.InitWorld("test", nullptr, nullptr, MakeParams(aiOpponentCount, seed));
            TileMap& map = world.GetTileMap();

            for (const auto& route : world.GetMilitaryRoads().GetRoutes())
            {
                if (route.tiles.size() < static_cast<size_t>(2 * (Margin + Leg)))
                    continue;
                auto dirAt = [&](size_t idx)
                {
                    Vec2i a = map.GetCoordsFromId(route.tiles[idx]);
                    Vec2i b = map.GetCoordsFromId(route.tiles[idx + 1]);
                    return Vec2i{b.x - a.x, b.y - a.y};
                };
                for (size_t i = Margin + Leg; i + Margin + Leg < route.tiles.size(); i++)
                {
                    Vec2i d1 = dirAt(i - 1);
                    Vec2i d2 = dirAt(i);
                    if (d1.x == d2.x && d1.y == d2.y)
                        continue;  // no elbow here

                    bool longBack = true;
                    for (int k = 2; k <= Leg && longBack; k++)
                    {
                        Vec2i d = dirAt(i - k);
                        longBack = d.x == d1.x && d.y == d1.y;
                    }
                    bool longAhead = true;
                    for (int k = 1; k < Leg && longAhead; k++)
                    {
                        Vec2i d = dirAt(i + k);
                        longAhead = d.x == d2.x && d.y == d2.y;
                    }
                    EXPECT_FALSE(longBack && longAhead)
                        << "square elbow on route " << route.playerA << "-" << route.playerB
                        << " at index " << i << " (seed " << seed
                        << ", players " << aiOpponentCount + 1 << ")";
                }
            }
        }
    }
}

TEST(MilitaryRoadNetworkTests, RouteTilesAreNeverResourceTiles)
{
    for (unsigned int seed : {111u, 222u, 555u, 888u})
    {
        for (int aiOpponentCount : {1, 3})
        {
            GameWorld world;
            world.InitWorld("test", nullptr, nullptr, MakeParams(aiOpponentCount, seed));

            const TileMap& map = world.GetTileMap();
            for (const Tile& tile : map.tilemap)
            {
                if (!tile.isMilitaryRoad)
                    continue;
                EXPECT_EQ(tile.tileType, TileType::GRASS)
                    << "military road tile " << tile.id << " sits on terrain type "
                    << static_cast<int>(tile.tileType) << " (seed=" << seed
                    << " ai=" << aiOpponentCount << ")";
                EXPECT_EQ(tile.resourceRichness, 0)
                    << "military road tile " << tile.id << " still has resource richness (seed="
                    << seed << " ai=" << aiOpponentCount << ")";
            }
        }
    }
}

TEST(MilitaryRoadNetworkTests, GenerationIsDeterministicForSameSeedAndParams)
{
    GameWorld worldA;
    GameWorld worldB;
    worldA.InitWorld("test", nullptr, nullptr, MakeParams(3, 777));
    worldB.InitWorld("test", nullptr, nullptr, MakeParams(3, 777));

    const auto& routesA = worldA.GetMilitaryRoads().GetRoutes();
    const auto& routesB = worldB.GetMilitaryRoads().GetRoutes();
    ASSERT_EQ(routesA.size(), routesB.size());

    for (const auto& routeA : routesA)
    {
        const MilitaryRoute* matched = worldB.GetMilitaryRoads().FindRoute(routeA.playerA, routeA.playerB);
        ASSERT_NE(matched, nullptr);
        EXPECT_EQ(routeA.tiles, matched->tiles) << "route " << routeA.playerA << "-" << routeA.playerB;
    }
}

TEST(MilitaryRoadNetworkTests, NoBuildingOrResourceRoadCanBePlacedOnMilitaryRoadTile)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeParams(1, 333));

    const auto& roads = world.GetMilitaryRoads();
    ASSERT_FALSE(roads.GetRoutes().empty());
    ASSERT_FALSE(roads.GetRoutes().front().tiles.empty());
    int militaryTileId = roads.GetRoutes().front().tiles[roads.GetRoutes().front().tiles.size() / 2];

    TileMap& map = world.GetTileMap();
    Vec2i pos = map.GetCoordsFromId(militaryTileId);
    EXPECT_TRUE(map[pos].isMilitaryRoad);

    Player* player = world.GetPlayerHandler().players.at(0).get();
    EXPECT_FALSE(map.CanPlaceBuilding(BuildingType::Road, pos, {1, 1}, player));
    EXPECT_FALSE(map.CanPlaceBuilding(BuildingType::StorageBuilding, pos, GetBuildingDefinition(BuildingType::StorageBuilding).footprint, player));
}

TEST(MilitaryRoadNetworkTests, SaveAndLoadPreservesRoutesAndTileFlags)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeParams(2, 444));

    const auto path = (std::filesystem::temp_directory_path() / "rts_military_road_test.save").string();
    ASSERT_TRUE(world.SaveToFile(path));

    GameWorld loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path, nullptr, nullptr));

    const auto& originalRoutes = world.GetMilitaryRoads().GetRoutes();
    const auto& loadedRoutes = loaded.GetMilitaryRoads().GetRoutes();
    ASSERT_EQ(originalRoutes.size(), loadedRoutes.size());

    for (const auto& route : originalRoutes)
    {
        const MilitaryRoute* matched = loaded.GetMilitaryRoads().FindRoute(route.playerA, route.playerB);
        ASSERT_NE(matched, nullptr);
        EXPECT_EQ(route.tiles, matched->tiles);
    }

    for (const auto& route : loadedRoutes)
        for (int tileId : route.tiles)
            EXPECT_TRUE(loaded.GetTileMap().tilemap[tileId].isMilitaryRoad);

    std::filesystem::remove(path);
}
