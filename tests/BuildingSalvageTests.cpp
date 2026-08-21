#include "economy/BuildingSalvage.h"
#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "simulation/MapGenerator.h"

#include <gtest/gtest.h>

namespace
{
void MakeGrassMap(TileMap& map, int width = 12, int height = 12)
{
    map.params.sizeX = width;
    map.params.sizeY = height;
    map.tilemap.clear();
    map.tilemap.reserve(width * height);
    for (int id = 0; id < width * height; ++id)
    {
        Tile tile{id};
        tile.tileType = TileType::GRASS;
        map.tilemap.push_back(std::move(tile));
    }
}
}

TEST(BuildingSalvageTests, MovesBuffersAndRefundsRecordedPayment)
{
    TileMap map;
    Player player{0, map};
    MakeGrassMap(map);

    auto* warehouse = dynamic_cast<StorageBuilding*>(map.PlaceLoadedBuilding(
        map.GetIdFromCoords({8, 8}), &player, std::make_unique<StorageBuilding>(10)));
    ASSERT_NE(warehouse, nullptr);
    warehouse->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 10};
    warehouse->storage.buffers[ResourceType::PLANKS] = ResourceBuffer{ResourceType::PLANKS, 10};

    auto* producer = dynamic_cast<Woodcutter*>(map.PlaceLoadedBuilding(
        map.GetIdFromCoords({1, 1}), &player, std::make_unique<Woodcutter>(11)));
    ASSERT_NE(producer, nullptr);
    producer->production.inputBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 4};
    producer->production.outputBuffers[ResourceType::PLANKS] = ResourceBuffer{ResourceType::PLANKS, 4};
    producer->production.inputBuffers[ResourceType::WOOD].GenerateResource(ResourceType::WOOD);
    producer->production.outputBuffers[ResourceType::PLANKS].GenerateResource(ResourceType::PLANKS);
    producer->buildCostRecordState = BuildCostRecordState::PaidRecorded;
    producer->buildCostWasPaid = true;
    producer->paidBuildCosts = {{ResourceType::WOOD, 5}};

    const int positionId = producer->positionId;
    const auto preview = BuildDemolitionPreview(map, *producer, player);
    ASSERT_TRUE(preview.allowed) << preview.reason;
    ASSERT_EQ(preview.resources.size(), 2u);

    EXPECT_TRUE(ExecuteDemolition(map, player, *producer));
    EXPECT_EQ(map.GetBuilding(positionId), nullptr);
    EXPECT_EQ(StockpileIndex::GetTotal(player, ResourceType::WOOD), 3);
    EXPECT_EQ(StockpileIndex::GetTotal(player, ResourceType::PLANKS), 1);
}

TEST(BuildingSalvageTests, RejectsWhenEvacuationCapacityIsInsufficient)
{
    TileMap map;
    Player player{0, map};
    MakeGrassMap(map);

    auto* warehouse = dynamic_cast<StorageBuilding*>(map.PlaceLoadedBuilding(
        map.GetIdFromCoords({8, 8}), &player, std::make_unique<StorageBuilding>(20)));
    ASSERT_NE(warehouse, nullptr);
    warehouse->storage.buffers.clear();
    warehouse->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 1};

    auto* producer = dynamic_cast<Woodcutter*>(map.PlaceLoadedBuilding(
        map.GetIdFromCoords({1, 1}), &player, std::make_unique<Woodcutter>(21)));
    ASSERT_NE(producer, nullptr);
    producer->production.outputBuffers[ResourceType::PLANKS] = ResourceBuffer{ResourceType::PLANKS, 1};
    producer->production.outputBuffers[ResourceType::PLANKS].GenerateResource(ResourceType::PLANKS);

    const int positionId = producer->positionId;
    const auto preview = BuildDemolitionPreview(map, *producer, player);
    EXPECT_FALSE(preview.allowed);
    EXPECT_NE(preview.reason.find("PLANKS"), std::string::npos);
    EXPECT_FALSE(ExecuteDemolition(map, player, *producer));
    EXPECT_NE(map.GetBuilding(positionId), nullptr);
    EXPECT_EQ(map.GetBuilding(positionId)->GetComponent<ProductionComponent>()
                  ->outputBuffers[ResourceType::PLANKS].buffer.size(), 1u);
}
