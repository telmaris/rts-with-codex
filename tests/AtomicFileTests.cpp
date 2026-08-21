#include "platform/AtomicFile.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    std::filesystem::path TestPath(const char* name)
    {
        return std::filesystem::temp_directory_path() / (std::string("rts_atomic_") + name);
    }
}

TEST(AtomicFileTests, FailedWriterLeavesExistingTargetUntouchedAndCleansTemp)
{
    const auto path = TestPath("failure.txt");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::ofstream(path) << "old";

    std::string error;
    EXPECT_FALSE(AtomicFileTransaction::Write(path,
        [](std::ostream& out)
        {
            out << "new";
            return false;
        }, &error));

    std::ifstream input(path);
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "old");
    EXPECT_FALSE(error.empty());
    std::filesystem::remove(path, ignored);
}

TEST(AtomicFileTests, SuccessfulWriterReplacesTarget)
{
    const auto path = TestPath("success.txt");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    ASSERT_TRUE(AtomicFileTransaction::Write(path,
        [](std::ostream& out)
        {
            out << "new content";
            return true;
        }));

    std::ifstream input(path);
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "new content");
    std::filesystem::remove(path, ignored);
}
