#ifndef GAME_SNAPSHOT_H
#define GAME_SNAPSHOT_H

#include "core/Serialization.h"
#include "economy/BuildingConfig.h"
#include "core/Types.h"
#include "raylib.h"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// Render-only player information. A full snapshot transmits this immutable
// palette once; tile deltas refer to it through buildingOwnerId.
struct GameSnapshotPlayer
{
    int id{-1};
    Color color{WHITE};
};

inline bool operator==(const GameSnapshotPlayer& lhs, const GameSnapshotPlayer& rhs)
{
    return lhs.id == rhs.id &&
           lhs.color.r == rhs.color.r &&
           lhs.color.g == rhs.color.g &&
           lhs.color.b == rhs.color.b &&
           lhs.color.a == rhs.color.a;
}

inline void SerializeSnapshotPlayer(std::ostringstream& out, const GameSnapshotPlayer& player)
{
    out << player.id << ' '
        << static_cast<int>(player.color.r) << ' '
        << static_cast<int>(player.color.g) << ' '
        << static_cast<int>(player.color.b) << ' '
        << static_cast<int>(player.color.a) << ' ';
}

inline bool TryDeserializeSnapshotPlayer(std::istringstream& in, GameSnapshotPlayer& player)
{
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 0;
    if (!(in >> player.id >> r >> g >> b >> a))
        return false;

    player.color = Color{
        static_cast<unsigned char>(std::clamp(r, 0, 255)),
        static_cast<unsigned char>(std::clamp(g, 0, 255)),
        static_cast<unsigned char>(std::clamp(b, 0, 255)),
        static_cast<unsigned char>(std::clamp(a, 0, 255))};
    return true;
}

struct GameSnapshotTile
{
    int terrainTextureId{0};
    bool hasOwner{false};
    Color ownerColor{BLANK};
    bool hasBuilding{false};
    BuildingType buildingType{BuildingType::Building};
    Vec2i buildingFootprint{1, 1};
    int buildingOwnerId{-1};
    bool isBuildingOperational{false};
    float buildingDamageIndicator{0.0f};
    float roadUtilization{0.0f};
    bool roadSaturated{false};
    bool isMilitaryRoad{false};
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
           lhs.buildingFootprint.y == rhs.buildingFootprint.y &&
           lhs.buildingOwnerId == rhs.buildingOwnerId &&
           lhs.isBuildingOperational == rhs.isBuildingOperational &&
           lhs.buildingDamageIndicator == rhs.buildingDamageIndicator &&
           lhs.roadUtilization == rhs.roadUtilization &&
           lhs.roadSaturated == rhs.roadSaturated &&
           lhs.isMilitaryRoad == rhs.isMilitaryRoad;
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
        << tile.buildingFootprint.y << ' '
        << tile.buildingOwnerId << ' '
        << (tile.isBuildingOperational ? 1 : 0) << ' '
        << tile.buildingDamageIndicator << ' '
        << tile.roadUtilization << ' '
        << (tile.roadSaturated ? 1 : 0) << ' '
        << (tile.isMilitaryRoad ? 1 : 0) << ' ';
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
    int isBuildingOperational = 0;
    int roadSaturated = 0;
    int isMilitaryRoad = 0;
    if (!(in >> tile.terrainTextureId >> hasOwner >> r >> g >> b >> a >> hasBuilding >> buildingType >> tile.buildingFootprint.x >> tile.buildingFootprint.y >> tile.buildingOwnerId >> isBuildingOperational >> tile.buildingDamageIndicator >> tile.roadUtilization >> roadSaturated >> isMilitaryRoad))
        return false;
    tile.hasOwner = hasOwner != 0;
    tile.ownerColor = Color{
        static_cast<unsigned char>(std::clamp(r, 0, 255)),
        static_cast<unsigned char>(std::clamp(g, 0, 255)),
        static_cast<unsigned char>(std::clamp(b, 0, 255)),
        static_cast<unsigned char>(std::clamp(a, 0, 255))};
    tile.hasBuilding = hasBuilding != 0;
    tile.buildingType = static_cast<BuildingType>(buildingType);
    tile.isBuildingOperational = isBuildingOperational != 0;
    tile.buildingDamageIndicator = std::max(0.0f, tile.buildingDamageIndicator);
    tile.roadUtilization = std::clamp(tile.roadUtilization, 0.0f, 1.0f);
    tile.roadSaturated = roadSaturated != 0;
    tile.isMilitaryRoad = isMilitaryRoad != 0;
    return true;
}

struct GameSnapshot
{
    std::uint64_t simulationTick{0};
    int localPlayerId{0};
    Vec2i mapSize{0, 0};
    std::vector<GameSnapshotPlayer> players;
    std::vector<GameSnapshotTile> tiles;

    bool IsValid() const
    {
        return mapSize.x > 0 && mapSize.y > 0 && tiles.size() == static_cast<size_t>(mapSize.x * mapSize.y);
    }

    std::string Serialize() const
    {
        Archive ar(SerializationVersion::GameSnapshotVersion);
        ar << simulationTick << localPlayerId << mapSize.x << mapSize.y << static_cast<int>(players.size());

        std::string payload = ar.GetString();
        std::ostringstream out;
        out << payload << ' ';
        for (const auto& player : players)
            SerializeSnapshotPlayer(out, player);
        for (const auto& tile : tiles)
            SerializeSnapshotTile(out, tile);
        return out.str();
    }

    static bool TryDeserialize(const std::string& payload, GameSnapshot& snapshot)
    {
        std::istringstream in(payload);
        int version = 0;
        int playerCount = 0;
        GameSnapshot parsed;
        in >> version >> parsed.simulationTick >> parsed.localPlayerId >> parsed.mapSize.x >> parsed.mapSize.y >> playerCount;
        if (!in || version != SerializationVersion::GameSnapshotVersion)
            return false;
        constexpr int MaxSnapshotPlayers = 64;
        if (parsed.mapSize.x <= 0 || parsed.mapSize.y <= 0 || playerCount < 0 || playerCount > MaxSnapshotPlayers)
            return false;

        parsed.players.reserve(static_cast<size_t>(playerCount));
        for (int i = 0; i < playerCount; i++)
        {
            GameSnapshotPlayer player;
            if (!TryDeserializeSnapshotPlayer(in, player))
                return false;
            parsed.players.push_back(player);
        }
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

// A missing owner is intentionally rendered neutrally. This permits destroyed,
// neutral, and legacy buildings to keep their normal albedo while callers
// safely use the same lookup in single-player and snapshot rendering paths.
inline Color ResolveSnapshotPlayerColor(const GameSnapshot& snapshot, int playerId, Color fallback = WHITE)
{
    auto player = std::find_if(snapshot.players.begin(), snapshot.players.end(),
                               [playerId](const GameSnapshotPlayer& candidate) {
                                   return candidate.id == playerId;
                               });
    return player != snapshot.players.end() ? player->color : fallback;
}

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
