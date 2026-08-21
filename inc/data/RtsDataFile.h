#ifndef RTS_DATA_FILE_H
#define RTS_DATA_FILE_H

#include <string>
#include <vector>

enum class RtsDataDiagnosticCode
{
    FileNotFound,
    ReadError,
    UnterminatedQuote,
    InvalidQuoteBoundary,
    EmptyDocument
};

struct RtsDataDiagnostic
{
    RtsDataDiagnosticCode code = RtsDataDiagnosticCode::ReadError;
    std::string path;
    std::size_t line = 0;
    std::string message;
};

// Tokenized representation of a lightweight .rtsdata file.
using RtsDataLine = std::vector<std::string>;
using RtsDataLines = std::vector<RtsDataLine>;

struct RtsDataDocument
{
    RtsDataLines lines;
    std::vector<std::size_t> sourceLines;
    std::vector<RtsDataDiagnostic> diagnostics;

    bool IsValid() const { return diagnostics.empty(); }
};

// Splits a .rtsdata line into tokens, preserving quoted text and trimming comments.
std::vector<std::string> TokenizeRtsDataLine(const std::string& line);

// Strict tokenizer used by validators and tools. It rejects malformed quotes
// instead of silently accepting a truncated or concatenated token.
bool TokenizeRtsDataLineStrict(const std::string& line,
                               std::vector<std::string>& outTokens,
                               std::string* outError = nullptr);

// Reads a complete document with source locations and diagnostics. Unlike the
// legacy convenience function below, missing files and malformed lines are
// observable by callers.
RtsDataDocument ReadRtsDataDocument(const std::string& path);

bool ParseRtsDataInt(const std::string& text, int& outValue);
bool ParseRtsDataDouble(const std::string& text, double& outValue);
int RtsDataIntOr(const std::string& text, int fallback = 0);
double RtsDataDoubleOr(const std::string& text, double fallback = 0.0);

// Reads non-empty tokenized .rtsdata lines from disk. Missing files return an empty list.
RtsDataLines ReadRtsDataLines(const std::string& path);

#endif
