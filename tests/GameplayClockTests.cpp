#include "ui/GameplayClock.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

TEST(GameplayClockTests, FormatsDurationsWithoutWrappingHours)
{
    EXPECT_EQ(FormatGameplayDuration(0), "0:00:00");
    EXPECT_EQ(FormatGameplayDuration(59), "0:00:59");
    EXPECT_EQ(FormatGameplayDuration(60), "0:01:00");
    EXPECT_EQ(FormatGameplayDuration(3599), "0:59:59");
    EXPECT_EQ(FormatGameplayDuration(3600), "1:00:00");
    EXPECT_EQ(FormatGameplayDuration(25 * 3600 + 2 * 60 + 7), "25:02:07");
}

TEST(GameplayClockTests, HandlesLargeValuesWithoutOverflow)
{
    EXPECT_EQ(FormatGameplayDuration(std::numeric_limits<std::uint64_t>::max()),
              "5124095576030431:00:15");
}
