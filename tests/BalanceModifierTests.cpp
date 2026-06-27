#include "../inc/BalanceModifiers.h"

#include <gtest/gtest.h>

TEST(BalanceModifierTests, AppliesOnlyToMatchingStatAndBuilding)
{
    BalanceModifierSet modifiers;
    modifiers.AddModifier(BalanceModifier{
        BalanceStat::ProductionOutputAmount,
        1.0,
        1.5,
        BalanceModifierScope::Global(),
        BuildingType::Woodcutter,
        ResourceType::WOOD,
        std::nullopt,
        "test:wood_bonus"});

    BalanceModifierContext matching;
    matching.stat = BalanceStat::ProductionOutputAmount;
    matching.buildingType = BuildingType::Woodcutter;
    matching.resourceType = ResourceType::WOOD;
    BalanceModifierContext wrongBuilding = matching;
    wrongBuilding.buildingType = BuildingType::LumberMill;

    EXPECT_DOUBLE_EQ(modifiers.ModifyDouble(2.0, matching), 4.5);
    EXPECT_DOUBLE_EQ(modifiers.ModifyDouble(2.0, wrongBuilding), 2.0);
}

TEST(BalanceModifierTests, AreaScopeUsesCircularDistance)
{
    BalanceModifierSet modifiers;
    modifiers.AddModifier(BalanceModifier{
        BalanceStat::RoadSpeed,
        0.0,
        2.0,
        BalanceModifierScope::Area({10, 10}, 3),
        BuildingType::Road,
        std::nullopt,
        std::nullopt,
        "test:local_road_speed"});

    BalanceModifierContext inside;
    inside.stat = BalanceStat::RoadSpeed;
    inside.buildingType = BuildingType::Road;
    inside.position = Vec2i{12, 11};
    BalanceModifierContext outside = inside;
    outside.position = Vec2i{14, 10};

    EXPECT_DOUBLE_EQ(modifiers.ModifyDouble(1.0, inside), 2.0);
    EXPECT_DOUBLE_EQ(modifiers.ModifyDouble(1.0, outside), 1.0);
}

TEST(BalanceModifierTests, UnitTypeModifierRequiresUnitContext)
{
    BalanceModifierSet modifiers;
    modifiers.AddModifier(BalanceModifier{
        BalanceStat::RecruitmentTime,
        0.0,
        0.5,
        BalanceModifierScope::Global(),
        BuildingType::Barracks,
        std::nullopt,
        MilitaryUnitType::Archer,
        "test:archer_training"});

    BalanceModifierContext noUnitContext;
    noUnitContext.stat = BalanceStat::RecruitmentTime;
    noUnitContext.buildingType = BuildingType::Barracks;

    BalanceModifierContext archerContext = noUnitContext;
    archerContext.unitType = MilitaryUnitType::Archer;

    EXPECT_DOUBLE_EQ(modifiers.ModifyDouble(10.0, noUnitContext), 10.0);
    EXPECT_DOUBLE_EQ(modifiers.ModifyDouble(10.0, archerContext), 5.0);
}

TEST(BalanceModifierTests, ClearSourcePrefixRemovesTechnologyGroup)
{
    BalanceModifierSet modifiers;
    modifiers.AddModifier(BalanceModifier{BalanceStat::BuildTime, 1.0, 1.0, BalanceModifierScope::Global(), std::nullopt, std::nullopt, std::nullopt, "tech:a"});
    modifiers.AddModifier(BalanceModifier{BalanceStat::BuildTime, 2.0, 1.0, BalanceModifierScope::Global(), std::nullopt, std::nullopt, std::nullopt, "local:a"});

    modifiers.ClearSourcePrefix("tech:");

    BalanceModifierContext context{BalanceStat::BuildTime};
    EXPECT_DOUBLE_EQ(modifiers.ModifyDouble(10.0, context), 12.0);
    EXPECT_EQ(modifiers.GetModifiers().size(), 1u);
}
