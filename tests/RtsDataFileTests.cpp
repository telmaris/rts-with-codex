#include "data/RtsDataFile.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

TEST(RtsDataFileTests, StrictTokenizerRejectsMalformedQuotes)
{
    std::vector<std::string> tokens;
    std::string error;

    EXPECT_FALSE(TokenizeRtsDataLineStrict("name \"unterminated", tokens, &error));
    EXPECT_EQ(error, "unterminated quote");
    EXPECT_TRUE(tokens.empty());

    EXPECT_FALSE(TokenizeRtsDataLineStrict("name \"quoted\"tail", tokens, &error));
    EXPECT_EQ(error, "quoted token must be followed by whitespace or comment");
    EXPECT_TRUE(tokens.empty());
}

TEST(RtsDataFileTests, DocumentReportsMissingAndEmptyFiles)
{
    const auto missing = std::filesystem::temp_directory_path() / "rts_data_file_missing.rtsdata";
    std::error_code ignored;
    std::filesystem::remove(missing, ignored);

    const RtsDataDocument missingDocument = ReadRtsDataDocument(missing.string());
    ASSERT_FALSE(missingDocument.IsValid());
    ASSERT_EQ(missingDocument.diagnostics.size(), 1u);
    EXPECT_EQ(missingDocument.diagnostics.front().code, RtsDataDiagnosticCode::FileNotFound);

    const auto empty = std::filesystem::temp_directory_path() / "rts_data_file_empty.rtsdata";
    {
        std::ofstream file(empty);
        file << "# comments only\n\n";
    }
    const RtsDataDocument emptyDocument = ReadRtsDataDocument(empty.string());
    ASSERT_FALSE(emptyDocument.IsValid());
    ASSERT_EQ(emptyDocument.diagnostics.size(), 1u);
    EXPECT_EQ(emptyDocument.diagnostics.front().code, RtsDataDiagnosticCode::EmptyDocument);
    std::filesystem::remove(empty, ignored);
}

TEST(RtsDataFileTests, NumericParserRejectsTrailingGarbageAndNonFiniteValues)
{
    int integer = 0;
    double real = 0.0;

    EXPECT_TRUE(ParseRtsDataInt("-42", integer));
    EXPECT_EQ(integer, -42);
    EXPECT_FALSE(ParseRtsDataInt("42ms", integer));

    EXPECT_TRUE(ParseRtsDataDouble("0.125", real));
    EXPECT_DOUBLE_EQ(real, 0.125);
    EXPECT_FALSE(ParseRtsDataDouble("0.125s", real));
    EXPECT_FALSE(ParseRtsDataDouble("nan", real));
    EXPECT_FALSE(ParseRtsDataDouble("inf", real));
    EXPECT_EQ(RtsDataIntOr("invalid", 7), 7);
    EXPECT_DOUBLE_EQ(RtsDataDoubleOr("invalid", 1.5), 1.5);
}
