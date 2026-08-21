#include "core/CheckedStateReader.h"

#include <charconv>
#include <cstdlib>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <utility>

StateReadError CheckedStateReader::MakeError(std::string message) const
{
    return StateReadError{section_, std::move(message)};
}

bool CheckedStateReader::ReadToken(std::string& token)
{
    return static_cast<bool>(stream_ >> token);
}

StateReadResult<std::string> CheckedStateReader::ReadTag(std::string_view expected)
{
    std::string token;
    if (!ReadToken(token))
        return MakeError("missing tag");
    if (token != expected)
        return MakeError("unexpected tag");
    section_ = token;
    return token;
}

StateReadResult<std::int64_t> CheckedStateReader::ReadInt(std::int64_t minimum,
                                                            std::int64_t maximum)
{
    std::string token;
    if (!ReadToken(token))
        return MakeError("missing integer");
    std::int64_t value = 0;
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() ||
        value < minimum || value > maximum)
        return MakeError("integer out of range");
    return value;
}

StateReadResult<std::size_t> CheckedStateReader::ReadCount(std::size_t maximum)
{
    auto value = ReadInt(0, static_cast<std::int64_t>(std::min(
        maximum, static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))));
    if (!value.HasValue())
        return value.Error();
    return static_cast<std::size_t>(value.Value());
}

StateReadResult<double> CheckedStateReader::ReadFiniteDouble(double minimum, double maximum)
{
    std::string token;
    if (!ReadToken(token))
        return MakeError("missing floating point value");
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size() || !std::isfinite(value) ||
        value < minimum || value > maximum)
        return MakeError("non-finite or out-of-range floating point value");
    return value;
}

StateReadResult<std::string> CheckedStateReader::ReadQuotedString(std::size_t maxBytes)
{
    std::string value;
    if (!(stream_ >> std::quoted(value)))
        return MakeError("missing quoted string");
    if (value.size() > maxBytes)
        return MakeError("quoted string exceeds limit");
    return value;
}

bool CheckedStateReader::AtEnd()
{
    stream_ >> std::ws;
    return stream_.eof();
}
