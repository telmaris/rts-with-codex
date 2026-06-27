#include "../inc/Resource.h"

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

TEST(ResourceBufferTests, SetStoredAmountUsesPoolAndClampsToCapacity)
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
