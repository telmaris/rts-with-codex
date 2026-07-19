#ifndef GAMEWORLD_INTERNAL_H
#define GAMEWORLD_INTERNAL_H

#include "core/GameWorld.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <set>

namespace GameWorldInternal
{
    // Creates a concrete building instance while loading save data.
    inline std::unique_ptr<Building> CreateBuildingFromType(BuildingType type, int id)
    {
        switch (type)
        {
            case BuildingType::Headquarters: return std::make_unique<Headquarters>(id);
            case BuildingType::Village: return std::make_unique<Village>(id);
            case BuildingType::StorageBuilding: return std::make_unique<StorageBuilding>(id);
            case BuildingType::Woodcutter: return std::make_unique<Woodcutter>(id);
            case BuildingType::HuntersHut: return std::make_unique<HuntersHut>(id);
            case BuildingType::LumberMill: return std::make_unique<LumberMill>(id);
            case BuildingType::Mine: return std::make_unique<Mine>(id);
            case BuildingType::Foundry: return std::make_unique<Foundry>(id);
            case BuildingType::Well: return std::make_unique<Well>(id);
            case BuildingType::WheatFarm: return std::make_unique<WheatFarm>(id);
            case BuildingType::Windmill: return std::make_unique<Windmill>(id);
            case BuildingType::Bakery: return std::make_unique<Bakery>(id);
            case BuildingType::Inn: return std::make_unique<Inn>(id);
            case BuildingType::Paperworks: return std::make_unique<Paperworks>(id);
            case BuildingType::Smith: return std::make_unique<Smith>(id);
            case BuildingType::Mint: return std::make_unique<Mint>(id);
            case BuildingType::Glassworks: return std::make_unique<Glassworks>(id);
            case BuildingType::Powderworks: return std::make_unique<Powderworks>(id);
            case BuildingType::University: return std::make_unique<University>(id);
            case BuildingType::Barracks: return std::make_unique<Barracks>(id);
            case BuildingType::Road: return std::make_unique<Road>(id);
            case BuildingType::DefenseTower: return std::make_unique<DefenseTower>(id);
            case BuildingType::Bridge: return std::make_unique<Bridge>(id);
            default: return nullptr;
        }
    }

    // Writes one resource buffer snapshot to a save stream.
    inline void SaveResourceBuffer(std::ostream& out, const char* tag, const ResourceBuffer& buffer)
    {
        out << tag << ' ' << static_cast<int>(buffer.type) << ' '
            << buffer.bufferSize << ' ' << buffer.buffer.size() << '\n';
    }

    // Restores one resource buffer from saved capacity and amount values.
    inline void LoadResourceBuffer(ResourceBuffer& buffer, ResourceType type, int capacity, int amount)
    {
        buffer.type = type;
        buffer.bufferSize = capacity;
        buffer.SetStoredAmount(amount);
    }

    // Deferred connection restored after all saved buildings are placed.
    struct PendingConnection
    {
        int sourcePosition{-1};
        ResourceType resource{ResourceType::Null};
        int targetPosition{-1};
        bool receiver{false};
        bool alternative{false};
    };

    // Returns the tile-space center of a footprint.
    inline Vec2i FootprintCenter(Vec2i anchor, Vec2i footprint)
    {
        return Vec2i{
            anchor.x + footprint.x / 2,
            anchor.y + footprint.y / 2};
    }

    inline Vec2i ClampAnchor(Vec2i anchor, Vec2i footprint, const MapParameters& params)
    {
        return Vec2i{
            std::clamp(anchor.x, 1, std::max(1, params.sizeX - footprint.x - 2)),
            std::clamp(anchor.y, 1, std::max(1, params.sizeY - footprint.y - 2))};
    }

    inline void SetFootprintTerrain(TileMap& tilemap, Vec2i anchor, Vec2i footprint, TileType type, std::mt19937& rng, int padding = 0)
    {
        for (int y = -padding; y < footprint.y + padding; y++)
        {
            for (int x = -padding; x < footprint.x + padding; x++)
            {
                Vec2i pos{anchor.x + x, anchor.y + y};
                if (!tilemap.IsInside(pos))
                    continue;

                auto& tile = tilemap[pos];
                tile.tileType = type;
                tile.terrainTextureId = tilemap.PickTerrainTexture(type, rng);
                tile.resourceRichness = type == TileType::GRASS ? 0 : std::max(1, tilemap.params.resourceRichness);
            }
        }
        tilemap.terrainDirty = true;
    }

    inline bool FootprintsOverlap(Vec2i a, Vec2i aSize, Vec2i b, Vec2i bSize, int padding = 0)
    {
        return a.x - padding < b.x + bSize.x + padding &&
               a.x + aSize.x + padding > b.x - padding &&
               a.y - padding < b.y + bSize.y + padding &&
               a.y + aSize.y + padding > b.y - padding;
    }

    inline void PlaceStartingResourcePatch(TileMap& tilemap, Vec2i hqAnchor, Vec2i hqFootprint,
                                    Vec2i villageAnchor, Vec2i villageFootprint,
                                    TileType type, std::mt19937& rng)
    {
        Vec2i center{hqAnchor.x + hqFootprint.x / 2, hqAnchor.y + hqFootprint.y / 2};
        int radius = 4;
        // User request (2026-07-17): patch centers live in a ring 17..23
        // tiles from the HQ center, so every patch tile (radius 4) sits at
        // 13+ — clear of the 10-tile HQ build apron (extractors must be
        // placeable ON the patch) while staying inside the starting zone.
        constexpr int kMinPatchCenterDist = 17;
        constexpr int kMaxPatchCenterDist = 23;
        Vec2i preferredOffset = type == TileType::WOOD ? Vec2i{-kMaxPatchCenterDist + radius, 0}
                                                       : Vec2i{kMaxPatchCenterDist - radius, 0};

        Vec2i bestCenter{-1, -1};
        int bestScore = std::numeric_limits<int>::min();
        for (int y = -kMaxPatchCenterDist; y <= kMaxPatchCenterDist; y++)
        {
            for (int x = -kMaxPatchCenterDist; x <= kMaxPatchCenterDist; x++)
            {
                Vec2i patchCenter{center.x + x, center.y + y};
                int distSq = x * x + y * y;
                if (distSq < kMinPatchCenterDist * kMinPatchCenterDist ||
                    distSq > kMaxPatchCenterDist * kMaxPatchCenterDist)
                    continue;

                Vec2i patchAnchor{patchCenter.x - radius, patchCenter.y - radius};
                Vec2i patchSize{radius * 2 + 1, radius * 2 + 1};
                if (!tilemap.IsInsideFootprint(patchAnchor, patchSize))
                    continue;
                if (FootprintsOverlap(patchAnchor, patchSize, hqAnchor, hqFootprint, 1) ||
                    FootprintsOverlap(patchAnchor, patchSize, villageAnchor, villageFootprint, 1))
                    continue;

                int paintableTiles = 0;
                for (int py = -radius; py <= radius; py++)
                {
                    for (int px = -radius; px <= radius; px++)
                    {
                        if (px * px + py * py > radius * radius)
                            continue;

                        Vec2i pos{patchCenter.x + px, patchCenter.y + py};
                        if (!tilemap.IsInside(pos))
                            continue;

                        const Tile& tile = tilemap[pos];
                        if (tile.tileType == TileType::GRASS && !tile.HasBuilding() && !tile.isMilitaryRoad)
                            paintableTiles++;
                    }
                }

                if (paintableTiles <= 0)
                    continue;

                int preferredDistance = std::abs(x - preferredOffset.x) + std::abs(y - preferredOffset.y);
                int score = paintableTiles * 100 - preferredDistance;
                if (score > bestScore)
                {
                    bestScore = score;
                    bestCenter = patchCenter;
                }
            }
        }

        if (bestCenter.x < 0)
        {
            for (int y = -kMaxPatchCenterDist; y <= kMaxPatchCenterDist && bestCenter.x < 0; y++)
            {
                for (int x = -kMaxPatchCenterDist; x <= kMaxPatchCenterDist && bestCenter.x < 0; x++)
                {
                    Vec2i pos{center.x + x, center.y + y};
                    if (!tilemap.IsInside(pos))
                        continue;

                    Tile& tile = tilemap[pos];
                    if (tile.tileType == TileType::GRASS && !tile.HasBuilding())
                        bestCenter = pos;
                }
            }
        }

        if (bestCenter.x < 0)
            return;

        int painted = 0;
        for (int y = -radius; y <= radius; y++)
        {
            for (int x = -radius; x <= radius; x++)
            {
                if (x * x + y * y > radius * radius)
                    continue;

                Vec2i pos{bestCenter.x + x, bestCenter.y + y};
                if (!tilemap.IsInside(pos))
                    continue;

                auto* building = tilemap.GetBuilding(pos);
                if (building != nullptr)
                    continue;

                Tile& tile = tilemap[pos];
                if (tile.tileType != TileType::GRASS)
                    continue;
                // Since the 2026-07-12 generation reorder these starting
                // patches run AFTER the military road is baked — a road tile
                // still has tileType GRASS (only isMilitaryRoad + richness=0
                // are set), so without this check the patch painted WOOD/
                // STONE straight over the unit track (user report 2026-07-14:
                // "tor jednostek na poletku kamienia czy drewna").
                if (tile.isMilitaryRoad)
                    continue;

                tile.tileType = type;
                tile.terrainTextureId = tilemap.PickTerrainTexture(type, rng);
                tile.resourceRichness = std::max(1, tilemap.params.resourceRichness);
                painted++;
            }
        }
        if (painted == 0)
            Log::Msg("[MapGenerator]", "Starting resource patch failed for tile type ", static_cast<int>(type));
        tilemap.terrainDirty = true;
    }

    // Builds an orthogonal road between two starting buildings, routing
    // around the military track (user report 2026-07-19: "road do village
    // nachodzi na tor jednostek"). The original version walked a straight
    // Manhattan path (horizontal leg then vertical leg) between ONE fixed
    // point on each building's nearest side, with no awareness of the track
    // at all — CanBuildFootprint (via Player::Build) already refuses to
    // place a Road on an isMilitaryRoad tile, so a track tile on that
    // straight line was silently skipped, but the walk kept going past it
    // regardless, leaving a road that runs up against (or gaps across) the
    // track instead of avoiding it.
    //
    // A first BFS-based fix here still picked ONE fixed point per side as
    // the mandatory start/goal — which failed just as badly whenever that
    // one point happened to land ON the track itself (very possible: the
    // track is only GUARANTEED straight for kGateStub tiles out of each HQ,
    // MilitaryRoadNetwork.cpp, and is free to wiggle after that). A 60-seed
    // sweep caught 9/120 villages this way: the BFS correctly found no path
    // to an unbuildable single tile and visited ~99% of the map proving it.
    //
    // Fixed properly by treating EVERY tile adjacent to each building's
    // footprint as a valid start/goal (same pattern AIActions::SubmitRoadPath
    // already uses for the same reason) — a multi-source, multi-target BFS
    // that finds the nearest REACHABLE pair of perimeter tiles instead of
    // gambling that one specific point is buildable.
    inline void BuildStartRoad(Player* player, Vec2i fromAnchor, Vec2i fromFootprint, Vec2i toAnchor, Vec2i toFootprint)
    {
        if (player == nullptr)
            return;

        TileMap& tilemap = player->tilemap;

        const auto& roadDefinition = GetBuildingDefinition(BuildingType::Road);
        auto passable = [&](int tileId)
        {
            Vec2i pos = tilemap.GetCoordsFromId(tileId);
            return tilemap.CanBuildFootprint(pos, roadDefinition.footprint, player, BuildingType::Road);
        };

        std::vector<int> fromAdjacent = tilemap.GetAdjacentTileIds(fromAnchor, fromFootprint);
        std::vector<int> toAdjacent = tilemap.GetAdjacentTileIds(toAnchor, toFootprint);
        if (fromAdjacent.empty() || toAdjacent.empty())
            return;

        std::set<int> goalSet(toAdjacent.begin(), toAdjacent.end());

        // Unweighted 4-directional BFS — deterministic given fixed map
        // state (no RNG involved in world-gen pathing).
        int maxIndex = tilemap.params.sizeX * tilemap.params.sizeY;
        std::vector<int> parent(maxIndex, -1);
        std::vector<bool> visited(maxIndex, false);
        std::queue<int> frontier;
        for (int tileId : fromAdjacent)
        {
            if (visited[tileId] || !passable(tileId))
                continue;
            visited[tileId] = true;
            frontier.push(tileId);
        }

        int reached = -1;
        while (!frontier.empty())
        {
            int current = frontier.front();
            frontier.pop();
            if (goalSet.contains(current))
            {
                reached = current;
                break;
            }

            Vec2i pos = tilemap.GetCoordsFromId(current);
            const std::array<Vec2i, 4> neighbours{
                Vec2i{pos.x + 1, pos.y}, Vec2i{pos.x - 1, pos.y},
                Vec2i{pos.x, pos.y + 1}, Vec2i{pos.x, pos.y - 1}
            };
            for (Vec2i next : neighbours)
            {
                if (!tilemap.IsInside(next))
                    continue;
                int nextId = tilemap.GetIdFromCoords(next);
                if (visited[nextId] || !passable(nextId))
                    continue;
                visited[nextId] = true;
                parent[nextId] = current;
                frontier.push(nextId);
            }
        }

        // No track-avoiding route exists between any pair of perimeter
        // tiles — shouldn't happen (the ring doesn't enclose a single HQ's
        // local area) short of a building being fully boxed in; leave the
        // village unconnected rather than place a Road the placement rules
        // would refuse anyway (the old straight-walk fallback did exactly
        // that, silently, for the same net result).
        if (reached < 0)
            return;

        std::vector<int> path;
        for (int cursor = reached; cursor >= 0; cursor = parent[cursor])
            path.push_back(cursor);
        std::reverse(path.begin(), path.end());

        for (int tileId : path)
            player->Build<Road>(tilemap.GetCoordsFromId(tileId), false);
    }
}


#endif
