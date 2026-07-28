#ifndef FOG_OF_WAR_H
#define FOG_OF_WAR_H

#include "core/Types.h"
#include "economy/Building.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// Deterministic gameplay counterpart of the renderer's fog mask. It stores
// current per-tile visibility in a generation-stamped array, avoiding an
// O(map size) clear every fixed simulation tick.
class FogOfWarState
{
public:
    void Initialize(Vec2i size)
    {
        mapSize = size;
        visibleRevision.assign(static_cast<size_t>(std::max(0, size.x) * std::max(0, size.y)), 0);
        revision = 1;
    }

    void BeginVisibilityUpdate()
    {
        if (revision == std::numeric_limits<std::uint32_t>::max())
        {
            std::fill(visibleRevision.begin(), visibleRevision.end(), 0);
            revision = 1;
        }
        else
            ++revision;
    }

    bool IsInitializedFor(Vec2i size) const
    {
        return mapSize == size && visibleRevision.size() == static_cast<size_t>(size.x * size.y);
    }

    void RevealWorldCircle(Vec2f center, float radiusWorld)
    {
        if (mapSize.x <= 0 || mapSize.y <= 0 || radiusWorld <= 0.0f)
            return;

        const float radiusTiles = radiusWorld / static_cast<float>(TILE_SIZE);
        const float centerX = center.x / static_cast<float>(TILE_SIZE);
        const float centerY = center.y / static_cast<float>(TILE_SIZE);
        const int minX = std::max(0, static_cast<int>(std::floor(centerX - radiusTiles)));
        const int maxX = std::min(mapSize.x - 1, static_cast<int>(std::ceil(centerX + radiusTiles)));
        const int minY = std::max(0, static_cast<int>(std::floor(centerY - radiusTiles)));
        const int maxY = std::min(mapSize.y - 1, static_cast<int>(std::ceil(centerY + radiusTiles)));
        const float radiusSquared = radiusWorld * radiusWorld;

        for (int y = minY; y <= maxY; ++y)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                const float dx = (static_cast<float>(x) + 0.5f) * TILE_SIZE - center.x;
                const float dy = (static_cast<float>(y) + 0.5f) * TILE_SIZE - center.y;
                if (dx * dx + dy * dy <= radiusSquared)
                    visibleRevision[static_cast<size_t>(y * mapSize.x + x)] = revision;
            }
        }
    }

    bool IsVisible(Vec2i tile) const
    {
        return tile.x >= 0 && tile.y >= 0 && tile.x < mapSize.x && tile.y < mapSize.y &&
               visibleRevision[static_cast<size_t>(tile.y * mapSize.x + tile.x)] == revision;
    }

    bool IsFootprintVisible(Vec2i anchor, Vec2i footprint) const
    {
        for (int y = 0; y < footprint.y; ++y)
            for (int x = 0; x < footprint.x; ++x)
                if (!IsVisible({anchor.x + x, anchor.y + y}))
                    return false;
        return true;
    }

private:
    Vec2i mapSize{};
    std::vector<std::uint32_t> visibleRevision;
    std::uint32_t revision{1};
};

namespace FogOfWar
{
    // Large starting visibility is intentional: resource discovery should be
    // a strategic decision, not a blind search immediately outside the HQ.
    constexpr float HeadquartersRevealRadiusWorld = 3072.0f;
    constexpr float UnitRevealRadiusWorld = 640.0f;

    inline float BuildingRevealRadiusWorld(BuildingType type, Vec2i footprint)
    {
        if (type == BuildingType::Headquarters)
            return HeadquartersRevealRadiusWorld;
        return 576.0f + static_cast<float>(std::max(footprint.x, footprint.y)) * 64.0f;
    }
}

#endif
