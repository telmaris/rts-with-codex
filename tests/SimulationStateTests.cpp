#include "core/SimulationState.h"

#include <gtest/gtest.h>

#include <limits>

TEST(SimulationStateTests, EncodingIsStableForIdenticalPrimitiveSequence)
{
    CanonicalStateWriter first;
    first.U64(17);
    first.I32(-4);
    first.Bool(true);
    first.FixedDouble3(1.2349);
    first.String("WOOD");

    CanonicalStateWriter second;
    second.U64(17);
    second.I32(-4);
    second.Bool(true);
    second.FixedDouble3(1.2341);
    second.String("WOOD");

    EXPECT_EQ(first.Finish(), second.Finish());
}

TEST(SimulationStateTests, EncodingIncludesStringLengthAndBytes)
{
    CanonicalStateWriter first;
    first.String("AB");
    CanonicalStateWriter second;
    second.String("A");

    EXPECT_NE(first.Finish(), second.Finish());
}

TEST(SimulationStateTests, NonFiniteDoublesAreNormalized)
{
    CanonicalStateWriter nanState;
    nanState.FixedDouble3(std::numeric_limits<double>::quiet_NaN());
    CanonicalStateWriter zeroState;
    zeroState.FixedDouble3(0.0);

    EXPECT_EQ(nanState.Finish(), zeroState.Finish());
}
