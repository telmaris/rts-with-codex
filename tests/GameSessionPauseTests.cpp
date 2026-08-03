#include "core/GameSession.h"
#include "core/GameWorld.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

TEST(GameSessionPauseTests, HostStopsTicksUntilResumed)
{
    GameWorld world;
    HostSession session(world);
    auto readTick = [&]()
    {
        std::lock_guard<std::recursive_mutex> lock(*session.GetWorldMutex());
        return world.GetSimulationTick();
    };

    session.SetPaused(true);
    ASSERT_TRUE(session.IsPaused());
    const std::uint64_t pausedTick = readTick();

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_EQ(readTick(), pausedTick);

    session.SetPaused(false);
    ASSERT_FALSE(session.IsPaused());
    for (int attempt = 0; attempt < 25 && readTick() == pausedTick; ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT_GT(readTick(), pausedTick);
}
