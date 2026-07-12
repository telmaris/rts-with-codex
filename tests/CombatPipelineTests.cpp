#include "warfare/CombatPipeline.h"

#include <gtest/gtest.h>

// TD(etap-5.1/5.3) — unit tests for the shape overlap tests and the shared
// damage formula, independent of any GameWorld/march/combat integration.
// Integration-level combat scenarios (road contact, no-zombie regression,
// etc.) live in UnitCombatSystemTests.cpp.

TEST(CombatPipelineTests, CircleShapeOverlapsWithinCombinedRadius)
{
    CircleShape shape(1.0f);
    EXPECT_TRUE(shape.Overlaps({0.0f, 0.0f}, {1.5f, 0.0f}, 0.5f));  // dist 1.5 == 1.0+0.5
    EXPECT_TRUE(shape.Overlaps({0.0f, 0.0f}, {0.0f, 0.0f}, 0.1f));  // concentric
}

TEST(CombatPipelineTests, CircleShapeMissesBeyondCombinedRadius)
{
    CircleShape shape(1.0f);
    EXPECT_FALSE(shape.Overlaps({0.0f, 0.0f}, {2.0f, 0.0f}, 0.5f)); // dist 2.0 > 1.5
    EXPECT_FALSE(shape.Overlaps({0.0f, 0.0f}, {10.0f, 10.0f}, 0.4f));
}

TEST(CombatPipelineTests, RectShapeOverlapsWithinHalfExtentsPlusTargetRadius)
{
    RectShape shape(2.0f, 1.0f);
    EXPECT_TRUE(shape.Overlaps({0.0f, 0.0f}, {2.4f, 0.9f}, 0.5f)); // 2.4<=2.5 and 0.9<=1.5
}

TEST(CombatPipelineTests, RectShapeMissesOutsideEitherAxis)
{
    RectShape shape(2.0f, 1.0f);
    EXPECT_FALSE(shape.Overlaps({0.0f, 0.0f}, {3.0f, 0.0f}, 0.4f)); // x: 3.0 > 2.4
    EXPECT_FALSE(shape.Overlaps({0.0f, 0.0f}, {0.0f, 2.0f}, 0.4f)); // y: 2.0 > 1.4
}

TEST(CombatPipelineTests, ResolveDamageFloorsAtOneWhenArmorExceedsAttack)
{
    std::map<DamageType, float> noResistances;
    for (std::uint64_t tick = 0; tick < 50; tick++)
    {
        double result = CombatResolver::ResolveDamage(1.0, 5.0, DamageType::Physical, noResistances,
                                                        /*worldSeed*/ 7, tick, /*sourceUnitInstanceId*/ 3);
        EXPECT_GE(result, 1.0) << "tick=" << tick;
    }
}

TEST(CombatPipelineTests, ResolveDamageAppliesResistanceReduction)
{
    std::map<DamageType, float> halfResist{{DamageType::Physical, 0.5f}};
    std::map<DamageType, float> noResistances;

    for (std::uint64_t tick = 0; tick < 20; tick++)
    {
        double resisted = CombatResolver::ResolveDamage(10.0, 0.0, DamageType::Physical, halfResist, 1, tick, 1);
        double unresisted = CombatResolver::ResolveDamage(10.0, 0.0, DamageType::Physical, noResistances, 1, tick, 1);
        // resisted result e (4.5,5.5], unresisted e (9,11] — resisted always
        // meaningfully lower, never floored to the same value.
        EXPECT_LT(resisted, unresisted) << "tick=" << tick;
        EXPECT_GE(resisted, 4.5) << "tick=" << tick;
        EXPECT_LE(resisted, 5.5) << "tick=" << tick;
    }
}

TEST(CombatPipelineTests, ResolveDamageIsDeterministicForSameInputs)
{
    std::map<DamageType, float> noResistances;
    double a = CombatResolver::ResolveDamage(6.0, 2.0, DamageType::Physical, noResistances, 123, 456, 789);
    double b = CombatResolver::ResolveDamage(6.0, 2.0, DamageType::Physical, noResistances, 123, 456, 789);
    EXPECT_DOUBLE_EQ(a, b);
}

TEST(CombatPipelineTests, ResolveDamageVarianceStaysWithinDocumentedRange)
{
    std::map<DamageType, float> noResistances;
    for (std::uint64_t tick = 0; tick < 200; tick++)
    {
        double result = CombatResolver::ResolveDamage(10.0, 0.0, DamageType::Physical, noResistances, 99, tick, 5);
        EXPECT_GE(result, 9.0) << "tick=" << tick;
        EXPECT_LE(result, 11.0) << "tick=" << tick;
    }
}
