#include "simulation/ResourceShipment.h"

#include <gtest/gtest.h>

TEST(ResourceShipmentTests, IndexStoresValueRecordsInStableIdOrder)
{
    ResourceShipmentIndex index;

    ResourceShipment later;
    later.id = 20;
    later.type = ResourceType::IRON;
    later.sourceBuildingId = 101;
    later.targetBuildingId = 202;
    later.pathTileIds = {4, 5, 6};

    ResourceShipment earlier;
    earlier.id = 10;
    earlier.type = ResourceType::WOOD;
    earlier.sourceBuildingId = 11;
    earlier.targetBuildingId = 22;

    ASSERT_TRUE(index.Insert(later));
    ASSERT_TRUE(index.Insert(earlier));
    EXPECT_EQ(index.Size(), 2u);

    const ResourceShipment* found = index.Find(20);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type, ResourceType::IRON);
    EXPECT_EQ(found->pathTileIds, (std::vector<int>{4, 5, 6}));

    EXPECT_FALSE(index.Insert(later));
}

TEST(ResourceShipmentTests, InvalidRecordsAreRejectedWithoutChangingIndex)
{
    ResourceShipmentIndex index;
    ResourceShipment invalid;

    EXPECT_FALSE(index.Insert(invalid));
    invalid.id = 1;
    invalid.type = ResourceType::WOOD;
    invalid.quantity = 0;
    EXPECT_FALSE(index.Insert(invalid));
    EXPECT_EQ(index.Size(), 0u);
}

TEST(ResourceShipmentTests, RecordLifecycleIsIndependentOfPointerPayloads)
{
    ResourceShipmentIndex index;
    ResourceShipment record;
    record.id = 7;
    record.type = ResourceType::BREAD;
    record.sourceBuildingId = 3;
    record.targetBuildingId = 9;

    ASSERT_TRUE(index.Insert(record));
    ResourceShipment* mutableRecord = index.Find(7);
    ASSERT_NE(mutableRecord, nullptr);
    mutableRecord->currentPathStep = 2;
    mutableRecord->state = ResourceShipmentState::HandedOff;

    EXPECT_EQ(index.Find(7)->currentPathStep, 2);
    EXPECT_EQ(index.Find(7)->state, ResourceShipmentState::HandedOff);
    EXPECT_TRUE(index.Erase(7));
    EXPECT_EQ(index.Find(7), nullptr);
}
