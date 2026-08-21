#include "data/RtsDataFile.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace
{
    void SetError(std::string* outError, const char* message)
    {
        if (outError != nullptr)
            *outError = message;
    }
}

bool TokenizeRtsDataLineStrict(const std::string& line,
                               std::vector<std::string>& outTokens,
                               std::string* outError)
{
    outTokens.clear();
    std::string token;
    bool inQuote = false;
    bool tokenStarted = false;
    bool justClosedQuote = false;

    for (char c : line)
    {
        if (!inQuote && c == '#')
            break;

        if (c == '"')
        {
            if (inQuote)
            {
                inQuote = false;
                justClosedQuote = true;
                continue;
            }

            if (justClosedQuote || tokenStarted)
            {
                SetError(outError, "quote must start a separate token");
                outTokens.clear();
                return false;
            }

            inQuote = !inQuote;
            tokenStarted = true;
            justClosedQuote = false;
            continue;
        }

        if (!inQuote && std::isspace(static_cast<unsigned char>(c)))
        {
            if (justClosedQuote || !token.empty())
            {
                outTokens.push_back(token);
                token.clear();
            }
            tokenStarted = false;
            justClosedQuote = false;
            continue;
        }

        if (!inQuote && justClosedQuote)
        {
            SetError(outError, "quoted token must be followed by whitespace or comment");
            outTokens.clear();
            return false;
        }

        token.push_back(c);
        tokenStarted = true;
    }

    if (inQuote)
    {
        SetError(outError, "unterminated quote");
        outTokens.clear();
        return false;
    }

    if (justClosedQuote || !token.empty())
        outTokens.push_back(token);

    return true;
}

std::vector<std::string> TokenizeRtsDataLine(const std::string& line)
{
    std::vector<std::string> tokens;
    TokenizeRtsDataLineStrict(line, tokens);
    return tokens;
}

RtsDataDocument ReadRtsDataDocument(const std::string& path)
{
    RtsDataDocument document;
    std::ifstream file(path);
    if (!file.is_open())
    {
        document.diagnostics.push_back({RtsDataDiagnosticCode::FileNotFound, path, 0,
                                        "file could not be opened"});
        return document;
    }

    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(file, line))
    {
        ++lineNumber;
        RtsDataLine tokens;
        std::string error;
        if (!TokenizeRtsDataLineStrict(line, tokens, &error))
        {
            const RtsDataDiagnosticCode code = error == "unterminated quote"
                ? RtsDataDiagnosticCode::UnterminatedQuote
                : RtsDataDiagnosticCode::InvalidQuoteBoundary;
            document.diagnostics.push_back({code, path, lineNumber, error});
            continue;
        }
        if (!tokens.empty())
        {
            document.sourceLines.push_back(lineNumber);
            document.lines.push_back(std::move(tokens));
        }
    }

    if (file.bad())
        document.diagnostics.push_back({RtsDataDiagnosticCode::ReadError, path, lineNumber,
                                        "I/O error while reading file"});
    if (document.lines.empty() && document.diagnostics.empty())
        document.diagnostics.push_back({RtsDataDiagnosticCode::EmptyDocument, path, 0,
                                        "document contains no data lines"});

    return document;
}

bool ParseRtsDataInt(const std::string& text, int& outValue)
{
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, outValue);
    return result.ec == std::errc{} && result.ptr == last;
}

bool ParseRtsDataDouble(const std::string& text, double& outValue)
{
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(parsed))
        return false;
    outValue = parsed;
    return true;
}

int RtsDataIntOr(const std::string& text, int fallback)
{
    int value = 0;
    return ParseRtsDataInt(text, value) ? value : fallback;
}

double RtsDataDoubleOr(const std::string& text, double fallback)
{
    double value = 0.0;
    return ParseRtsDataDouble(text, value) ? value : fallback;
}

RtsDataLines ReadRtsDataLines(const std::string& path)
{
    return ReadRtsDataDocument(path).lines;
}
