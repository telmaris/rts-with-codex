#include "core/GameSnapshot.h"

#include <gtest/gtest.h>

namespace
{
    void ExpectColorEquals(Color actual, Color expected)
    {
        EXPECT_EQ(actual.r, expected.r);
        EXPECT_EQ(actual.g, expected.g);
        EXPECT_EQ(actual.b, expected.b);
        EXPECT_EQ(actual.a, expected.a);
    }
}

TEST(GameSnapshotTests, RoundTripPreservesPlayerPaletteAndBuildingOwner)
{
    GameSnapshot original;
    original.simulationTick = 1234;
    original.localPlayerId = 7;
    original.mapSize = {2, 1};
    original.players = {
        {7, Color{31, 163, 255, 255}},
        {11, Color{220, 73, 91, 255}},
    };

    GameSnapshotTile ownedBuilding;
    ownedBuilding.terrainTextureId = 12;
    ownedBuilding.hasBuilding = true;
    ownedBuilding.buildingType = BuildingType::DefenseTower;
    ownedBuilding.buildingFootprint = {2, 3};
    ownedBuilding.buildingOwnerId = 11;
    ownedBuilding.isBuildingOperational = true;
    ownedBuilding.buildingDamageIndicator = 2.5f;
    ownedBuilding.roadUtilization = 0.8f;
    ownedBuilding.roadSaturated = true;
    original.tiles = {ownedBuilding, GameSnapshotTile{}};

    GameSnapshot restored;
    ASSERT_TRUE(GameSnapshot::TryDeserialize(original.Serialize(), restored));

    EXPECT_EQ(restored.simulationTick, original.simulationTick);
    EXPECT_EQ(restored.localPlayerId, original.localPlayerId);
    EXPECT_EQ(restored.mapSize.x, original.mapSize.x);
    EXPECT_EQ(restored.mapSize.y, original.mapSize.y);
    EXPECT_EQ(restored.players, original.players);
    EXPECT_EQ(restored.tiles, original.tiles);
}

TEST(GameSnapshotTests, DeltaUpdatesBuildingOwnerWithoutReplacingPalette)
{
    GameSnapshot snapshot;
    snapshot.simulationTick = 10;
    snapshot.mapSize = {1, 1};
    snapshot.players = {
        {1, RED},
        {2, BLUE},
    };
    snapshot.tiles.resize(1);

    GameSnapshotDelta delta;
    delta.simulationTick = 20;
    delta.mapSize = snapshot.mapSize;
    delta.changes = {{0, GameSnapshotTile{}}};
    delta.changes.front().tile.hasBuilding = true;
    delta.changes.front().tile.buildingType = BuildingType::Barracks;
    delta.changes.front().tile.buildingOwnerId = 2;
    delta.changes.front().tile.isBuildingOperational = true;
    delta.changes.front().tile.buildingDamageIndicator = 3.0f;
    delta.changes.front().tile.roadUtilization = 0.6f;
    delta.changes.front().tile.roadSaturated = true;

    GameSnapshotDelta restored;
    ASSERT_TRUE(GameSnapshotDelta::TryDeserialize(delta.Serialize(), restored));
    ASSERT_TRUE(restored.ApplyTo(snapshot));

    EXPECT_EQ(snapshot.simulationTick, 20);
    EXPECT_EQ(snapshot.players[0], (GameSnapshotPlayer{1, RED}));
    EXPECT_EQ(snapshot.players[1], (GameSnapshotPlayer{2, BLUE}));
    EXPECT_EQ(snapshot.tiles[0].buildingOwnerId, 2);
    EXPECT_TRUE(snapshot.tiles[0].isBuildingOperational);
    EXPECT_FLOAT_EQ(snapshot.tiles[0].buildingDamageIndicator, 3.0f);
    EXPECT_FLOAT_EQ(snapshot.tiles[0].roadUtilization, 0.6f);
    EXPECT_TRUE(snapshot.tiles[0].roadSaturated);
}

TEST(GameSnapshotTests, RejectsAnOlderSnapshotVersion)
{
    GameSnapshot snapshot;
    snapshot.mapSize = {1, 1};
    snapshot.tiles.resize(1);

    std::string payload = snapshot.Serialize();
    payload.replace(0, std::to_string(SerializationVersion::GameSnapshotVersion).size(), "7");

    GameSnapshot parsed;
    EXPECT_FALSE(GameSnapshot::TryDeserialize(payload, parsed));
}

TEST(GameSnapshotTests, ResolvesOwnerColorsAndFallsBackForNeutralBuildings)
{
    GameSnapshot snapshot;
    snapshot.players = {
        {3, Color{80, 120, 210, 255}},
    };

    ExpectColorEquals(ResolveSnapshotPlayerColor(snapshot, 3), Color{80, 120, 210, 255});
    ExpectColorEquals(ResolveSnapshotPlayerColor(snapshot, -1), WHITE);
    ExpectColorEquals(ResolveSnapshotPlayerColor(snapshot, 999, MAGENTA), MAGENTA);
}
