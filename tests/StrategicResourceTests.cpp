#include "../inc/StrategicResource.h"

#include <gtest/gtest.h>

TEST(StrategicResourceTests, NamesExposeReadableLabels)
{
    EXPECT_EQ(StrategicResourceName(StrategicResourceType::Manpower), "Manpower");
    EXPECT_EQ(StrategicResourceName(StrategicResourceType::Workers), "Workers");
    EXPECT_EQ(StrategicResourceName(StrategicResourceType::SupplyPackages), "Supply packages");
}

TEST(StrategicResourceTests, PoolAddsConsumesAndClampsValues)
{
    StrategicResourcePool pool;

    EXPECT_DOUBLE_EQ(pool.Get(StrategicResourceType::Manpower), 0.0);

    pool.Add(StrategicResourceType::Manpower, 12.5);
    EXPECT_DOUBLE_EQ(pool.Get(StrategicResourceType::Manpower), 12.5);

    EXPECT_TRUE(pool.Consume(StrategicResourceType::Manpower, 5.0));
    EXPECT_DOUBLE_EQ(pool.Get(StrategicResourceType::Manpower), 7.5);

    EXPECT_FALSE(pool.Consume(StrategicResourceType::Manpower, 99.0));
    EXPECT_DOUBLE_EQ(pool.Get(StrategicResourceType::Manpower), 7.5);

    pool.Set(StrategicResourceType::Manpower, -10.0);
    EXPECT_DOUBLE_EQ(pool.Get(StrategicResourceType::Manpower), 0.0);
}
