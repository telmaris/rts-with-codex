#ifndef PERSISTENCE_LIMITS_H
#define PERSISTENCE_LIMITS_H

#include <cstddef>
#include <limits>

// Product limits shared by save loading and network state restoration. These
// are element limits, not a substitute for the serialized byte cap.
struct PersistenceLimits
{
    static constexpr int MaxMapDimension = 1001;
    static constexpr std::size_t MaxMapTiles = 1'002'001;
    static constexpr int MaxSupportedPlayers = 8;
    static constexpr std::size_t MaxBuildings = MaxMapTiles;
    static constexpr std::size_t MaxRouteCount =
        static_cast<std::size_t>(MaxSupportedPlayers) * (MaxSupportedPlayers - 1);
    static constexpr std::size_t MaxRouteTiles = MaxMapTiles;
    static constexpr std::size_t MaxUnits = MaxMapTiles;
    static constexpr std::size_t MaxProjectiles = MaxMapTiles;
    static constexpr std::size_t MaxQueueEntries = MaxMapTiles;
    static constexpr std::size_t MaxBufferEntries = 256;
    static constexpr std::size_t MaxStringBytes = 4096;

    static bool CheckedArea(int width, int height, std::size_t& result) noexcept
    {
        if (width <= 0 || height <= 0 || width > MaxMapDimension || height > MaxMapDimension)
            return false;
        const auto w = static_cast<std::size_t>(width);
        const auto h = static_cast<std::size_t>(height);
        if (w > MaxMapTiles / h)
            return false;
        result = w * h;
        return result <= MaxMapTiles;
    }

    static bool IsCountInRange(int count, std::size_t maximum) noexcept
    {
        return count >= 0 && static_cast<std::size_t>(count) <= maximum;
    }

    static bool IsCountInRange(std::size_t count, std::size_t maximum) noexcept
    {
        return count <= maximum;
    }
};

#endif
