#include "data/Resource.h"

#include <gtest/gtest.h>

TEST(ResourceBufferTests, AddResourceRespectsCapacity)
{
    Resource woodA{ResourceType::WOOD};
    Resource woodB{ResourceType::WOOD};
    Resource woodC{ResourceType::WOOD};
    ResourceBuffer buffer{ResourceType::WOOD, 2};

    buffer.AddResource(&woodA);
    buffer.AddResource(&woodB);
    buffer.AddResource(&woodC);

    EXPECT_EQ(buffer.buffer.size(), 2u);
}

TEST(ResourceBufferTests, GetResourceReturnsStoredResourcesLastInFirstOut)
{
    Resource first{ResourceType::WOOD};
    Resource second{ResourceType::WOOD};
    ResourceBuffer buffer{ResourceType::WOOD, 2};
    buffer.AddResource(&first);
    buffer.AddResource(&second);

    auto [hasSecond, secondPtr] = buffer.GetResource();
    auto [hasFirst, firstPtr] = buffer.GetResource();
    auto [hasNone, nonePtr] = buffer.GetResource();

    EXPECT_TRUE(hasSecond);
    EXPECT_EQ(secondPtr, &second);
    EXPECT_TRUE(hasFirst);
    EXPECT_EQ(firstPtr, &first);
    EXPECT_FALSE(hasNone);
    EXPECT_EQ(nonePtr, nullptr);
}

TEST(ResourceBufferTests, SetStoredAmountAllocatesLazilyAndClampsToCapacity)
{
    ResourceBuffer buffer{ResourceType::STONE, 3};

    buffer.SetStoredAmount(5);
    EXPECT_EQ(buffer.buffer.size(), 3u);
    for (auto* resource : buffer.buffer)
    {
        ASSERT_NE(resource, nullptr);
        EXPECT_EQ(resource->type, ResourceType::STONE);
    }

    buffer.Clear();
    EXPECT_TRUE(buffer.buffer.empty());
}

TEST(ResourceBufferTests, RepeatedShortLivedBuffersReleaseOwnedResources)
{
    for (int i = 0; i < 10000; ++i)
    {
        ResourceBuffer buffer{ResourceType::WOOD, 4};
        buffer.SetStoredAmount(4);
        ASSERT_EQ(buffer.buffer.size(), 4u);
    }
}

TEST(ResourceBufferTests, CopyingAResourceDoesNotTransferAllocationOwnership)
{
    Resource original{ResourceType::WOOD};
    original.shipmentId = 42;
    original.transportPath = {1, 2};

    Resource detachedCopy = original;
    EXPECT_FALSE(detachedCopy.ownedAllocation);
    EXPECT_EQ(detachedCopy.shipmentId, 0u);
    EXPECT_TRUE(detachedCopy.transportPath.empty());

    Resource assigned{ResourceType::STONE};
    assigned.shipmentId = 7;
    assigned = original;
    EXPECT_FALSE(assigned.ownedAllocation);
    EXPECT_EQ(assigned.shipmentId, 0u);
    EXPECT_TRUE(assigned.transportPath.empty());

    ResourceBuffer buffer{ResourceType::STONE, 1};
    buffer.GenerateResource(ResourceType::STONE);
    ASSERT_EQ(buffer.buffer.size(), 1u);

    Resource copy = *buffer.buffer.front();
    EXPECT_FALSE(copy.ownedAllocation);
    buffer.Clear();
    EXPECT_EQ(copy.type, ResourceType::STONE);
}
