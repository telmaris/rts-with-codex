#ifndef GAME_SNAPSHOT_H
#define GAME_SNAPSHOT_H

#include "BuildingConfig.h"
#include "Utils.h"
#include "raylib.h"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

struct GameSnapshotTile
{
    int terrainTextureId{0};
    bool hasOwner{false};
    Color ownerColor{BLANK};
    bool hasBuilding{false};
    BuildingType buildingType{BuildingType::Building};
    Vec2i buildingFootprint{1, 1};
};

inline bool operator==(const GameSnapshotTile& lhs, const GameSnapshotTile& rhs)
{
    return lhs.terrainTextureId == rhs.terrainTextureId &&
           lhs.hasOwner == rhs.hasOwner &&
           lhs.ownerColor.r == rhs.ownerColor.r &&
           lhs.ownerColor.g == rhs.ownerColor.g &&
           lhs.ownerColor.b == rhs.ownerColor.b &&
           lhs.ownerColor.a == rhs.ownerColor.a &&
           lhs.hasBuilding == rhs.hasBuilding &&
           lhs.buildingType == rhs.buildingType &&
           lhs.buildingFootprint.x == rhs.buildingFootprint.x &&
           lhs.buildingFootprint.y == rhs.buildingFootprint.y;
}

inline bool operator!=(const GameSnapshotTile& lhs, const GameSnapshotTile& rhs)
{
    return !(lhs == rhs);
}

inline void SerializeSnapshotTile(std::ostringstream& out, const GameSnapshotTile& tile)
{
    out << tile.terrainTextureId << ' '
        << (tile.hasOwner ? 1 : 0) << ' '
        << static_cast<int>(tile.ownerColor.r) << ' '
        << static_cast<int>(tile.ownerColor.g) << ' '
        << static_cast<int>(tile.ownerColor.b) << ' '
        << static_cast<int>(tile.ownerColor.a) << ' '
        << (tile.hasBuilding ? 1 : 0) << ' '
        << static_cast<int>(tile.buildingType) << ' '
        << tile.buildingFootprint.x << ' '
        << tile.buildingFootprint.y << ' ';
}

inline bool TryDeserializeSnapshotTile(std::istringstream& in, GameSnapshotTile& tile)
{
    int hasOwner = 0;
    int hasBuilding = 0;
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 0;
    int buildingType = 0;
    if (!(in >> tile.terrainTextureId >> hasOwner >> r >> g >> b >> a >> hasBuilding >> buildingType >> tile.buildingFootprint.x >> tile.buildingFootprint.y))
        return false;
    tile.hasOwner = hasOwner != 0;
    tile.ownerColor = Color{
        static_cast<unsigned char>(std::clamp(r, 0, 255)),
        static_cast<unsigned char>(std::clamp(g, 0, 255)),
        static_cast<unsigned char>(std::clamp(b, 0, 255)),
        static_cast<unsigned char>(std::clamp(a, 0, 255))};
    tile.hasBuilding = hasBuilding != 0;
    tile.buildingType = static_cast<BuildingType>(buildingType);
    return true;
}

struct GameSnapshot
{
    std::uint64_t simulationTick{0};
    int localPlayerId{0};
    Vec2i mapSize{0, 0};
    std::vector<GameSnapshotTile> tiles;

    bool IsValid() const
    {
        return mapSize.x > 0 && mapSize.y > 0 && tiles.size() == static_cast<size_t>(mapSize.x * mapSize.y);
    }

    std::string Serialize() const
    {
        std::ostringstream out;
        out << simulationTick << ' ' << localPlayerId << ' ' << mapSize.x << ' ' << mapSize.y << ' ';
        for (const auto& tile : tiles)
            SerializeSnapshotTile(out, tile);
        return out.str();
    }

    static bool TryDeserialize(const std::string& payload, GameSnapshot& snapshot)
    {
        std::istringstream in(payload);
        GameSnapshot parsed;
        if (!(in >> parsed.simulationTick >> parsed.localPlayerId >> parsed.mapSize.x >> parsed.mapSize.y))
            return false;
        if (parsed.mapSize.x <= 0 || parsed.mapSize.y <= 0)
            return false;

        parsed.tiles.reserve(static_cast<size_t>(parsed.mapSize.x * parsed.mapSize.y));
        for (int i = 0; i < parsed.mapSize.x * parsed.mapSize.y; i++)
        {
            GameSnapshotTile tile;
            if (!TryDeserializeSnapshotTile(in, tile))
                return false;
            parsed.tiles.push_back(tile);
        }

        snapshot = std::move(parsed);
        return true;
    }
};

struct GameSnapshotDeltaTile
{
    size_t index{0};
    GameSnapshotTile tile;
};

struct GameSnapshotDelta
{
    std::uint64_t simulationTick{0};
    Vec2i mapSize{0, 0};
    std::vector<GameSnapshotDeltaTile> changes;

    bool IsValidFor(const GameSnapshot& snapshot) const
    {
        return snapshot.IsValid() && mapSize.x == snapshot.mapSize.x && mapSize.y == snapshot.mapSize.y;
    }

    std::string Serialize() const
    {
        std::ostringstream out;
        out << simulationTick << ' ' << mapSize.x << ' ' << mapSize.y << ' ' << changes.size() << ' ';
        for (const auto& change : changes)
        {
            out << change.index << ' ';
            SerializeSnapshotTile(out, change.tile);
        }
        return out.str();
    }

    static bool TryDeserialize(const std::string& payload, GameSnapshotDelta& delta)
    {
        std::istringstream in(payload);
        GameSnapshotDelta parsed;
        size_t changeCount = 0;
        if (!(in >> parsed.simulationTick >> parsed.mapSize.x >> parsed.mapSize.y >> changeCount))
            return false;
        if (parsed.mapSize.x <= 0 || parsed.mapSize.y <= 0)
            return false;

        parsed.changes.reserve(changeCount);
        size_t tileCount = static_cast<size_t>(parsed.mapSize.x * parsed.mapSize.y);
        for (size_t i = 0; i < changeCount; i++)
        {
            GameSnapshotDeltaTile change;
            if (!(in >> change.index))
                return false;
            if (change.index >= tileCount)
                return false;
            if (!TryDeserializeSnapshotTile(in, change.tile))
                return false;
            parsed.changes.push_back(change);
        }

        delta = std::move(parsed);
        return true;
    }

    bool ApplyTo(GameSnapshot& snapshot) const
    {
        if (!IsValidFor(snapshot))
            return false;
        for (const auto& change : changes)
        {
            if (change.index >= snapshot.tiles.size())
                return false;
            snapshot.tiles[change.index] = change.tile;
        }
        snapshot.simulationTick = simulationTick;
        return true;
    }
};

#endif
