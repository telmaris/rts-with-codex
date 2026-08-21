#include "core/Log.h"

#include <gtest/gtest.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
    class LogTests : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            Log::Shutdown();
            Log::ClearTestSink();
            Log::SetMinimumLevel(LogLevel::Info);
        }

        void TearDown() override
        {
            Log::Shutdown();
            Log::ClearTestSink();
        }

        std::vector<std::string> Capture()
        {
            std::lock_guard<std::mutex> lock(linesMutex);
            return lines;
        }

        std::mutex linesMutex;
        std::vector<std::string> lines;
    };
}

TEST_F(LogTests, FiltersMessagesBelowMinimumLevelBeforeFormatting)
{
    Log::SetTestSink([this](const std::string& line)
    {
        std::lock_guard<std::mutex> lock(linesMutex);
        lines.push_back(line);
    });
    Log::SetMinimumLevel(LogLevel::Info);

    Log::Debug("[test]", "hidden");
    Log::Msg("[test]", "visible");
    Log::Shutdown();

    const auto captured = Capture();
    ASSERT_EQ(captured.size(), 1u);
    EXPECT_NE(captured.front().find("visible"), std::string::npos);
    EXPECT_EQ(captured.front().find("hidden"), std::string::npos);
}

TEST_F(LogTests, ShutdownFlushesTheBoundedQueueToTheConfiguredSink)
{
    Log::SetTestSink([this](const std::string& line)
    {
        std::lock_guard<std::mutex> lock(linesMutex);
        lines.push_back(line);
    });

    Log::Msg("[test]", "one");
    Log::Warning("[test]", "two");
    Log::Error("[test]", "three");
    Log::Shutdown();

    const auto captured = Capture();
    ASSERT_EQ(captured.size(), 3u);
    EXPECT_NE(captured[0].find("one"), std::string::npos);
    EXPECT_NE(captured[1].find("two"), std::string::npos);
    EXPECT_NE(captured[2].find("three"), std::string::npos);
}

TEST(LogContractTests, QueueCapacityIsExplicitlyBounded)
{
    EXPECT_GT(Log::QueueCapacity(), 0u);
    EXPECT_LE(Log::QueueCapacity(), 4096u);
}

TEST_F(LogTests, DropsLowPriorityMessagesWhenTheQueueIsFull)
{
    std::atomic<bool> sinkEntered{false};
    std::atomic<bool> releaseSink{false};

    Log::SetMinimumLevel(LogLevel::Debug);
    Log::SetTestSink([&](const std::string& line)
    {
        if (line.find("block") == std::string::npos)
            return;

        sinkEntered.store(true);
        while (!releaseSink.load())
            std::this_thread::yield();
    });

    const auto droppedBefore = Log::DroppedLowPriorityCount();
    Log::Msg("[test]", "block");
    for (int attempt = 0; attempt < 10000 && !sinkEntered.load(); ++attempt)
        std::this_thread::yield();

    ASSERT_TRUE(sinkEntered.load());
    for (std::size_t index = 0; index < Log::QueueCapacity() * 2; ++index)
        Log::Debug("[test]", "queued debug ", index);

    releaseSink.store(true);
    Log::Shutdown();

    EXPECT_GT(Log::DroppedLowPriorityCount(), droppedBefore);
}
