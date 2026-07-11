#include "economy/BalanceModifiers.h"
#include "research/StateDevelopment.h"

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

TEST(BalanceModifierTests, CategoryModifierAppliesToEveryResourceInCategory)
{
    // "+10% Metal production" — one modifier, lifts every metal, ignores non-metals.
    BalanceModifier metalBonus;
    metalBonus.stat = BalanceStat::ProductionOutputAmount;
    metalBonus.multiplier = 1.10;
    metalBonus.resourceCategory = ResourceCategory::Metal;
    metalBonus.source = "test:metal_industry";

    BalanceModifierSet modifiers;
    modifiers.AddModifier(metalBonus);

    auto ctxFor = [](ResourceType type)
    {
        BalanceModifierContext ctx;
        ctx.stat = BalanceStat::ProductionOutputAmount;
        ctx.resourceType = type;
        return ctx;
    };

    EXPECT_DOUBLE_EQ(modifiers.ModifyDouble(10.0, ctxFor(ResourceType::IRON)), 11.0);
    EXPECT_DOUBLE_EQ(modifiers.ModifyDouble(10.0, ctxFor(ResourceType::COPPER)), 11.0);
    EXPECT_DOUBLE_EQ(modifiers.ModifyDouble(10.0, ctxFor(ResourceType::STEEL)), 11.0);
    // A plank is Timber, not Metal — untouched.
    EXPECT_DOUBLE_EQ(modifiers.ModifyDouble(10.0, ctxFor(ResourceType::PLANKS)), 10.0);
}

TEST(BalanceModifierTests, SwordCategoryBonusCoversEverySwordTier)
{
    // "+5% Sword power" boosts every sword material tier through the shared tag.
    BalanceModifier swordBonus;
    swordBonus.stat = BalanceStat::ProductionOutputAmount;
    swordBonus.multiplier = 1.05;
    swordBonus.resourceCategory = ResourceCategory::Sword;
    swordBonus.source = "test:swordsmithing";

    BalanceModifierSet modifiers;
    modifiers.AddModifier(swordBonus);

    auto ctxFor = [](ResourceType type)
    {
        BalanceModifierContext ctx;
        ctx.stat = BalanceStat::ProductionOutputAmount;
        ctx.resourceType = type;
        return ctx;
    };

    EXPECT_DOUBLE_EQ(modifiers.ModifyDouble(100.0, ctxFor(ResourceType::COPPER_SWORD)), 105.0);
    EXPECT_DOUBLE_EQ(modifiers.ModifyDouble(100.0, ctxFor(ResourceType::IRON_SWORD)), 105.0);
    EXPECT_DOUBLE_EQ(modifiers.ModifyDouble(100.0, ctxFor(ResourceType::STEEL_SWORD)), 105.0);
    // A bow is a different weapon category — unaffected by a Sword bonus.
    EXPECT_DOUBLE_EQ(modifiers.ModifyDouble(100.0, ctxFor(ResourceType::BOW)), 100.0);
}

TEST(BalanceModifierTests, ResourceCategoryClassificationIsStable)
{
    EXPECT_EQ(ResourceCategoryOf(ResourceType::IRON_ORE), ResourceCategory::Ore);
    EXPECT_EQ(ResourceCategoryOf(ResourceType::IRON), ResourceCategory::Metal);
    EXPECT_EQ(ResourceCategoryOf(ResourceType::BRONZE), ResourceCategory::Metal);
    EXPECT_EQ(ResourceCategoryOf(ResourceType::WOOD), ResourceCategory::Timber);
    EXPECT_EQ(ResourceCategoryOf(ResourceType::BREAD), ResourceCategory::Foodstuff);
    EXPECT_EQ(ResourceCategoryOf(ResourceType::IRON_SWORD), ResourceCategory::Sword);
    EXPECT_EQ(ResourceCategoryOf(ResourceType::BOW), ResourceCategory::Bow);
    EXPECT_EQ(ResourceCategoryOf(ResourceType::ARROWS), ResourceCategory::Ammunition);
    EXPECT_EQ(ResourceCategoryOf(ResourceType::IRON_ARMOR), ResourceCategory::Armor);
    EXPECT_EQ(ResourceCategoryOf(ResourceType::FOOD_PROVISIONS), ResourceCategory::MilitarySupply);
    // Every pooled resource must classify to something other than None.
    for (ResourceType type : resourceTypes)
        EXPECT_NE(ResourceCategoryOf(type), ResourceCategory::None)
            << "Unclassified resource: " << rt2s(type);
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

TEST(BalanceModifierTests, ClearSourcePrefixRemovesTechnologyGroup)
{
    BalanceModifierSet modifiers;
    modifiers.AddModifier(BalanceModifier{BalanceStat::BuildTime, 1.0, 1.0, BalanceModifierScope::Global(), std::nullopt, std::nullopt, "tech:a"});
    modifiers.AddModifier(BalanceModifier{BalanceStat::BuildTime, 2.0, 1.0, BalanceModifierScope::Global(), std::nullopt, std::nullopt, "local:a"});

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

TEST(BalanceModifierTests, StateDevelopmentManpowerModifiersAreSystemWide)
{
    for (const auto& definition : GetStateDevelopmentDefinitions())
    {
        for (const auto& modifier : definition.modifiers)
        {
            if (modifier.stat == BalanceStat::ManpowerRate)
            {
                EXPECT_FALSE(modifier.buildingType.has_value()) << definition.id;
            }
        }
    }
}
