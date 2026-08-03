#include "core/SimulationState.h"

#include <cmath>
#include <limits>

namespace
{
    constexpr std::uint64_t FnvOffsetBasis = 1469598103934665603ull;
    constexpr std::uint64_t FnvPrime = 1099511628211ull;
}

CanonicalStateWriter::CanonicalStateWriter()
    : hash(FnvOffsetBasis)
{
}

void CanonicalStateWriter::Mix(std::uint64_t value) noexcept
{
    hash ^= value;
    hash *= FnvPrime;
}

void CanonicalStateWriter::U64(std::uint64_t value)
{
    Mix(value);
}

void CanonicalStateWriter::I32(std::int32_t value)
{
    Mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(value)));
}

void CanonicalStateWriter::Bool(bool value)
{
    I32(value ? 1 : 0);
}

void CanonicalStateWriter::FixedDouble3(double value)
{
    // Keep the old checksum contract while making quantization explicit in
    // the canonical schema. Non-finite values are normalized instead of
    // flowing through an implementation-defined integer conversion.
    const double normalized = std::isfinite(value) ? value : 0.0;
    const long double scaled = static_cast<long double>(normalized) * 1000.0L;
    constexpr long double minValue = static_cast<long double>(std::numeric_limits<std::int64_t>::min());
    constexpr long double maxValue = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    const auto quantized = scaled <= minValue
        ? std::numeric_limits<std::int64_t>::min()
        : scaled >= maxValue
            ? std::numeric_limits<std::int64_t>::max()
            : static_cast<std::int64_t>(scaled);
    Mix(static_cast<std::uint64_t>(quantized));
}

void CanonicalStateWriter::String(const std::string& value)
{
    U64(static_cast<std::uint64_t>(value.size()));
    for (unsigned char byte : value)
        I32(static_cast<std::int32_t>(byte));
}
