#ifndef GAMEPLAY_CLOCK_H
#define GAMEPLAY_CLOCK_H

#include <cstdint>
#include <string>

// Formats deterministic simulation time as H:MM:SS. Hours intentionally do
// not wrap at 24 so long sessions remain unambiguous.
std::string FormatGameplayDuration(std::uint64_t totalSeconds);

#endif
