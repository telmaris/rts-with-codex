#ifndef SIMULATION_STATE_H
#define SIMULATION_STATE_H

#include <cstdint>
#include <string>

// Canonical, pointer-free encoding primitives for gameplay state.
//
// The writer deliberately exposes only fixed-width integers, quantized
// simulation doubles, booleans and byte strings. Callers are responsible for
// traversing containers in a deterministic order (normally std::map order or
// an explicit stable-id sort). It is shared by checksum now and is the schema
// boundary for the save/snapshot visitor in AUD-03 follow-up work.
class CanonicalStateWriter
{
public:
    CanonicalStateWriter();

    void U64(std::uint64_t value);
    void I32(std::int32_t value);
    void Bool(bool value);
    // Matches the existing gameplay checksum precision: thousandths.
    void FixedDouble3(double value);
    void String(const std::string& value);

    std::uint64_t Finish() const noexcept { return hash; }

private:
    void Mix(std::uint64_t value) noexcept;
    std::uint64_t hash;
};

#endif
