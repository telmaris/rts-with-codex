#include "../inc/Technology.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace
{
    // Writes a temporary technology data file for parser-level tests.
    std::filesystem::path WriteTechnologyFixture(const std::string& text)
    {
        const auto path = std::filesystem::temp_directory_path() / "rts_technology_fixture.rtsdata";
        std::ofstream file(path);
        file << text;
        return path;
    }
}

TEST(TechnologyTests, DefinitionsAreLoadedWithCategoriesCostsAndModifiers)
{
    const auto& definitions = GetTechnologyDefinitions();
    ASSERT_FALSE(definitions.empty());

    const TechnologyDefinition* forestry = FindTechnologyDefinition("forestry");
    ASSERT_NE(forestry, nullptr);
    EXPECT_EQ(forestry->category, "PRODUCTION");
    EXPECT_GT(forestry->researchTime, 0.0);
    EXPECT_FALSE(forestry->modifiers.empty());
}

TEST(TechnologyTests, PrerequisitesGateUnlocks)
{
    TechnologyState state;

    ASSERT_TRUE(state.UnlockTechnology("forestry"));
    EXPECT_TRUE(state.HasTechnology("forestry"));

    if (FindTechnologyDefinition("sawmill_blades") != nullptr)
    {
        TechnologyState gatedState;
        EXPECT_FALSE(gatedState.CanUnlock("sawmill_blades"));

        gatedState.RestoreTechnology("forestry");
        EXPECT_TRUE(gatedState.CanUnlock("sawmill_blades"));
    }
}

TEST(TechnologyTests, CollectModifiersExportsUnlockedTechnologyEffects)
{
    TechnologyState state;
    ASSERT_TRUE(state.UnlockTechnology("forestry"));

    BalanceModifierSet modifiers;
    state.CollectModifiers(modifiers);

    EXPECT_FALSE(modifiers.GetModifiers().empty());
    for (const auto& modifier : modifiers.GetModifiers())
        EXPECT_EQ(modifier.source, "tech:forestry");
}

TEST(TechnologyTests, LoadsTechnologyDataFileWithQuotedTextPrerequisitesCostsAndModifiers)
{
    const auto path = WriteTechnologyFixture(R"DATA(
# parser fixture
technology archery
    name "Archery Training"
    description "Unlocks better ranged troops."
    category WARFARE
    research_time 7.5
    requires forestry
    cost PAPER 3
    cost IRON_SWORD 2
    modifier RecruitmentTime multiplier 0.75 building Barracks unit Archer
    modifier ProductionOutputAmount additive 2 multiplier 1.25 building Woodcutter resource WOOD
end
technology invalid_only_name
end
)DATA");

    const auto definitions = LoadTechnologyDefinitionsFromFile(path.string());
    ASSERT_EQ(definitions.size(), 2u);

    const auto& archery = definitions.front();
    EXPECT_EQ(archery.id, "archery");
    EXPECT_EQ(archery.name, "Archery Training");
    EXPECT_EQ(archery.description, "Unlocks better ranged troops.");
    EXPECT_EQ(archery.category, "WARFARE");
    EXPECT_DOUBLE_EQ(archery.researchTime, 7.5);
    ASSERT_EQ(archery.prerequisites.size(), 1u);
    EXPECT_EQ(archery.prerequisites.front(), "forestry");
    ASSERT_EQ(archery.costs.size(), 2u);
    EXPECT_EQ(archery.costs[0].type, ResourceType::PAPER);
    EXPECT_EQ(archery.costs[1].type, ResourceType::IRON_SWORD);

    ASSERT_EQ(archery.modifiers.size(), 2u);
    EXPECT_EQ(archery.modifiers[0].stat, BalanceStat::RecruitmentTime);
    EXPECT_EQ(archery.modifiers[0].buildingType, BuildingType::Barracks);
    ASSERT_TRUE(archery.modifiers[0].unitType.has_value());
    EXPECT_EQ(archery.modifiers[0].unitType.value(), MilitaryUnitType::Archer);
    EXPECT_EQ(archery.modifiers[1].resourceType, ResourceType::WOOD);
    EXPECT_DOUBLE_EQ(archery.modifiers[1].additive, 2.0);
    EXPECT_DOUBLE_EQ(archery.modifiers[1].multiplier, 1.25);

    EXPECT_EQ(definitions[1].id, "invalid_only_name");
    EXPECT_EQ(definitions[1].name, "invalid_only_name");
}

TEST(TechnologyTests, MissingTechnologyDataUsesBuiltInDefaults)
{
    const auto definitions = LoadTechnologyDefinitionsFromFile("missing_technology_fixture.rtsdata");

    EXPECT_NE(std::find_if(definitions.begin(), definitions.end(), [](const TechnologyDefinition& definition)
    {
        return definition.id == "forestry";
    }), definitions.end());
}
