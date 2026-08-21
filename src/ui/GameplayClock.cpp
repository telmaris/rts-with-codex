#include "ui/GameplayClock.h"

#include <iomanip>
#include <sstream>

std::string FormatGameplayDuration(std::uint64_t totalSeconds)
{
    const std::uint64_t hours = totalSeconds / 3600u;
    const std::uint64_t minutes = (totalSeconds / 60u) % 60u;
    const std::uint64_t seconds = totalSeconds % 60u;
    std::ostringstream out;
    out << hours << ':' << std::setfill('0') << std::setw(2) << minutes
        << ':' << std::setw(2) << seconds;
    return out.str();
}
