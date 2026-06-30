#include "../inc/BalanceModifiers.h"
#include "../inc/StateDevelopment.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
    bool LowerIsBetter(BalanceStat stat)
    {
        switch (stat)
        {
            case BalanceStat::BuildTime:
            case BalanceStat::ProductionCycleTime:
            case BalanceStat::TransportTime:
            case BalanceStat::SupplyConsumption:
            case BalanceStat::RecruitmentTime:
            case BalanceStat::RecruitmentManpowerCost:
                return true;
            default:
                return false;
        }
    }

    bool IsPositiveModifier(const BalanceModifier& modifier)
    {
        if (LowerIsBetter(modifier.stat))
            return modifier.additive < 0.0 || modifier.multiplier < 1.0;
        return modifier.additive > 0.0 || modifier.multiplier > 1.0;
    }

    bool IsNegativeModifier(const BalanceModifier& modifier)
    {
        if (LowerIsBetter(modifier.stat))
            return modifier.additive > 0.0 || modifier.multiplier > 1.0;
        return modifier.additive < 0.0 || modifier.multiplier < 1.0;
    }
}

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

TEST(BalanceModifierTests, StateDevelopmentDefinitionsContainBuffsAndNerfs)
{
    for (const auto& definition : GetStateDevelopmentDefinitions())
    {
        EXPECT_FALSE(definition.modifiers.empty()) << definition.id;
        EXPECT_NE(std::find_if(definition.modifiers.begin(), definition.modifiers.end(), IsPositiveModifier),
                  definition.modifiers.end()) << definition.id;
        EXPECT_NE(std::find_if(definition.modifiers.begin(), definition.modifiers.end(), IsNegativeModifier),
                  definition.modifiers.end()) << definition.id;
    }
}

TEST(BalanceModifierTests, StateDevelopmentRefreshesFromUnlockedGovernmentIds)
{
    StateDevelopment state;
    state.RefreshFromGovernmentIds({});
    EXPECT_EQ(state.GetDefinition().id, "tribal_society");

    state.RefreshFromGovernmentIds({"chiefdom"});
    EXPECT_EQ(state.GetDefinition().id, "chiefdom");

    state.RefreshFromGovernmentIds({"chiefdom", "kingdom"});
    EXPECT_EQ(state.GetDefinition().id, "kingdom");

    state.RefreshFromGovernmentIds({"chiefdom", "kingdom", "aristocratic_state"});
    EXPECT_EQ(state.GetDefinition().id, "aristocratic_state");
}

TEST(BalanceModifierTests, StateDevelopmentModifiersApplyExpectedTradeoffs)
{
    StateDevelopment state;
    BalanceModifierSet modifiers;

    state.RefreshFromGovernmentIds({});
    state.CollectModifiers(modifiers);

    BalanceModifierContext build{BalanceStat::BuildTime};
    EXPECT_GT(modifiers.ModifyDouble(100.0, build), 100.0);

    BalanceModifierContext manpower{BalanceStat::ManpowerRate, BuildingType::Building};
    EXPECT_GT(modifiers.ModifyDouble(1.0, manpower), 1.0);

    modifiers.Clear();
    state.RefreshFromGovernmentIds({"chiefdom", "kingdom", "aristocratic_state"});
    state.CollectModifiers(modifiers);

    BalanceModifierContext production{BalanceStat::ProductionCycleTime};
    EXPECT_LT(modifiers.ModifyDouble(100.0, production), 100.0);
    EXPECT_LT(modifiers.ModifyDouble(1.0, manpower), 1.0);
}

TEST(BalanceModifierTests, StateDevelopmentManpowerAndRecruitmentModifiersAreSystemWide)
{
    for (const auto& definition : GetStateDevelopmentDefinitions())
    {
        for (const auto& modifier : definition.modifiers)
        {
            if (modifier.stat == BalanceStat::ManpowerRate ||
                modifier.stat == BalanceStat::RecruitmentTime)
            {
                EXPECT_FALSE(modifier.buildingType.has_value()) << definition.id;
            }
        }
    }
}
