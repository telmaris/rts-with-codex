#include "core/CheckedStateReader.h"
#include "core/GameWorld.h"
#include "core/PersistenceLimits.h"

#include <gtest/gtest.h>

#include <limits>
#include <filesystem>
#include <fstream>
#include <string>

TEST(PersistenceValidationTests, CheckedReaderRejectsInvalidCountsAndNonFiniteNumbers)
{
    CheckedStateReader negative("-1", "count");
    EXPECT_FALSE(negative.ReadCount(10).HasValue());

    CheckedStateReader tooLarge("11", "count");
    EXPECT_FALSE(tooLarge.ReadCount(10).HasValue());

    CheckedStateReader nonFinite("nan", "double");
    EXPECT_FALSE(nonFinite.ReadFiniteDouble(-1.0, 1.0).HasValue());
}

TEST(PersistenceValidationTests, MapAreaIsCheckedBeforeAllocation)
{
    std::size_t area = 0;
    EXPECT_TRUE(PersistenceLimits::CheckedArea(401, 401, area));
    EXPECT_EQ(area, 401u * 401u);
    EXPECT_FALSE(PersistenceLimits::CheckedArea(1002, 1, area));
    EXPECT_TRUE(PersistenceLimits::CheckedArea(1001, 1001, area));
    EXPECT_FALSE(PersistenceLimits::CheckedArea(std::numeric_limits<int>::max(), 2, area));
}

TEST(PersistenceValidationTests, InvalidStateDoesNotMutateExistingWorld)
{
    MapParameters params;
    params.sizeX = 41;
    params.sizeY = 41;
    params.aiOpponentCount = 1;
    params.seed = 20260820;

    GameWorld world;
    world.InitWorld("persistence-validation", nullptr, nullptr, params);
    const std::uint64_t before = world.BuildChecksum();
    std::string payload = world.SerializeSimulationState();

    const std::string marker = "PARAMS 41 41";
    const std::size_t markerPos = payload.find(marker);
    ASSERT_NE(markerPos, std::string::npos);
    payload.replace(markerPos, marker.size(), "PARAMS 1002 1002");

    EXPECT_FALSE(world.RestoreSimulationState(payload));
    EXPECT_EQ(world.BuildChecksum(), before);
}

TEST(PersistenceValidationTests, SupportedSaveFixturesRemainLoadable)
{
    const std::filesystem::path fixtureRoot =
        std::filesystem::current_path() / "tests" / "fixtures";
    for (int version = 30; version <= 34; ++version)
    {
        SCOPED_TRACE(version);
        GameWorld world;
        EXPECT_TRUE(world.LoadFromFile(
            (fixtureRoot / ("save_v" + std::to_string(version) + ".rts")).string(),
            nullptr, nullptr));
    }
}

TEST(PersistenceValidationTests, TrailingStateTokensAreRejectedWithoutMutation)
{
    const std::filesystem::path fixture =
        std::filesystem::current_path() / "tests" / "fixtures" / "save_v34.rts";
    std::ifstream input(fixture);
    ASSERT_TRUE(input.is_open());
    std::string payload((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

    GameWorld world;
    world.InitWorld("trailing-token-guard", nullptr, nullptr, MapParameters{});
    const std::uint64_t before = world.BuildChecksum();
    payload += "\nUNEXPECTED_TRAILING_TOKEN\n";

    EXPECT_FALSE(world.RestoreSimulationState(payload));
    EXPECT_EQ(world.BuildChecksum(), before);
}
