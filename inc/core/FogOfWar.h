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
    // Keep gameplay distances expressed in tiles. TILE_SIZE is a world-space
    // presentation constant and changed from 32 to 64 px; hard-coded pixel
    // radii silently halved the visible area after that migration.
    // Keep renderer visibility and gameplay visibility on the same radii.
    // These are roughly 54% of the original 192/20-tile values: shrinking only the
    // visual mask would let the player issue actions in apparently hidden
    // terrain, while shrinking only gameplay would do the opposite.
    // A further ~10% trim from the last pass: enough to make scouting and
    // expansion matter, while retaining a comfortable starting discovery area.
    constexpr float HeadquartersRevealRadiusTiles = 104.0f;
    constexpr float StandardRevealRadiusTiles = 10.8f;
    constexpr float HeadquartersRevealRadiusWorld = HeadquartersRevealRadiusTiles * TILE_SIZE;
    constexpr float UnitRevealRadiusWorld = StandardRevealRadiusTiles * TILE_SIZE;

    inline float BuildingRevealRadiusWorld(BuildingType type, Vec2i footprint)
    {
        if (type == BuildingType::Headquarters)
            return HeadquartersRevealRadiusWorld;

        // A standard one-tile building gets the same 10.8-tile minimum as a
        // unit. Larger footprints extend the source radius by their excess
        // footprint so the whole building remains inside its reveal circle.
        const int largestFootprint = std::max(1, std::max(footprint.x, footprint.y));
        return (StandardRevealRadiusTiles + static_cast<float>(largestFootprint - 1)) * TILE_SIZE;
    }
}

#endif
