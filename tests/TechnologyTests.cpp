#include "../inc/Technology.h"
#include "../inc/RtsDataFile.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>

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

    bool ContainsTag(const TechnologyDefinition& definition, const std::string& tag)
    {
        return std::find(definition.tags.begin(), definition.tags.end(), tag) != definition.tags.end();
    }

    bool IsAllowedResearchTag(const std::string& tag)
    {
        return tag == "production" ||
               tag == "logistics" ||
               tag == "manpower" ||
               tag == "expansion" ||
               tag == "military" ||
               tag == "government" ||
               tag == "construction";
    }

    void ExpectEveryDefinitionBlockHasExplicitTags(const std::string& path)
    {
        const auto lines = ReadRtsDataLines(path);
        bool inDefinition = false;
        bool hasTags = false;
        int definitionCount = 0;

        for (const auto& tokens : lines)
        {
            if (tokens[0] == "technology")
            {
                if (inDefinition)
                    ADD_FAILURE() << "Nested technology block before end in " << path;
                inDefinition = true;
                hasTags = false;
                definitionCount++;
                continue;
            }

            if (!inDefinition)
                continue;

            if (tokens[0] == "tag" || tokens[0] == "tags")
                hasTags = true;
            else if (tokens[0] == "end")
            {
                EXPECT_TRUE(hasTags) << "Missing explicit tags before definition ending in " << path;
                inDefinition = false;
            }
        }

        EXPECT_FALSE(inDefinition) << "Unclosed technology block in " << path;
        EXPECT_GT(definitionCount, 0);
    }
}

TEST(TechnologyTests, DefinitionsAreLoadedWithCategoriesCostsAndModifiers)
{
    const auto& definitions = GetTechnologyDefinitions();
    ASSERT_FALSE(definitions.empty());

    const TechnologyDefinition* forestry = FindTechnologyDefinition("forestry");
    ASSERT_NE(forestry, nullptr);
    EXPECT_EQ(forestry->name, "Mathematics");
    EXPECT_EQ(forestry->category, "SCIENCE");
    EXPECT_EQ(forestry->layoutLane, "Core Sciences");
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
    cost BRONZE_SWORD 1
    modifier RecruitmentTime multiplier 0.75 building Barracks unit Archer
    modifier ProductionOutputAmount additive 2 multiplier 1.25 building Woodcutter resource WOOD
    tags WARFARE military, archers government
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
    ASSERT_EQ(archery.costs.size(), 3u);
    EXPECT_EQ(archery.costs[0].type, ResourceType::PAPER);
    EXPECT_EQ(archery.costs[1].type, ResourceType::IRON_SWORD);
    EXPECT_EQ(archery.costs[2].type, ResourceType::BRONZE_SWORD);

    ASSERT_EQ(archery.modifiers.size(), 2u);
    EXPECT_EQ(archery.modifiers[0].stat, BalanceStat::RecruitmentTime);
    EXPECT_FALSE(archery.modifiers[0].buildingType.has_value());
    ASSERT_TRUE(archery.modifiers[0].unitType.has_value());
    EXPECT_EQ(archery.modifiers[0].unitType.value(), MilitaryUnitType::Archer);
    EXPECT_EQ(archery.modifiers[1].resourceType, ResourceType::WOOD);
    EXPECT_DOUBLE_EQ(archery.modifiers[1].additive, 2.0);
    EXPECT_DOUBLE_EQ(archery.modifiers[1].multiplier, 1.25);
    EXPECT_TRUE(ContainsTag(archery, "military"));
    EXPECT_TRUE(ContainsTag(archery, "government"));
    EXPECT_FALSE(ContainsTag(archery, "warfare"));
    EXPECT_FALSE(ContainsTag(archery, "archers"));
    EXPECT_EQ(std::count(archery.tags.begin(), archery.tags.end(), "military"), 1);

    EXPECT_EQ(definitions[1].id, "invalid_only_name");
    EXPECT_EQ(definitions[1].name, "invalid_only_name");
}

TEST(TechnologyTests, AssetTechnologyAndFocusDefinitionsDeclareExplicitTags)
{
    ExpectEveryDefinitionBlockHasExplicitTags("assets/data/technologies.rtsdata");
    ExpectEveryDefinitionBlockHasExplicitTags("assets/data/focuses.rtsdata");
}

TEST(TechnologyTests, LoadedAssetTagsAreNormalizedAndUnique)
{
    auto expectNormalizedTags = [](const std::vector<TechnologyDefinition>& definitions)
    {
        for (const auto& definition : definitions)
        {
            ASSERT_FALSE(definition.tags.empty()) << definition.id;
            for (const auto& tag : definition.tags)
            {
                EXPECT_FALSE(tag.empty()) << definition.id;
                EXPECT_TRUE(std::all_of(tag.begin(), tag.end(), [](unsigned char c)
                {
                    return !std::isupper(c);
                })) << definition.id << " tag " << tag;
                EXPECT_TRUE(IsAllowedResearchTag(tag)) << definition.id << " tag " << tag;
                EXPECT_EQ(std::count(definition.tags.begin(), definition.tags.end(), tag), 1)
                    << definition.id << " duplicate tag " << tag;
            }
        }
    };

    expectNormalizedTags(LoadTechnologyDefinitionsFromFile("assets/data/technologies.rtsdata"));
    expectNormalizedTags(LoadFocusDefinitionsFromFile("assets/data/focuses.rtsdata"));
}

TEST(TechnologyTests, AssetTechnologyTreeUsesScientificTrunkAndBranches)
{
    const auto definitions = LoadTechnologyDefinitionsFromFile("assets/data/technologies.rtsdata");
    ASSERT_FALSE(definitions.empty());

    std::set<std::string> ids;
    std::vector<std::string> roots;
    for (const auto& definition : definitions)
    {
        ids.insert(definition.id);
        EXPECT_EQ(definition.category, "SCIENCE") << definition.id;
        EXPECT_FALSE(definition.layoutLane.empty()) << definition.id;
    }

    for (const auto& definition : definitions)
    {
        if (definition.prerequisites.empty())
            roots.push_back(definition.id);
        for (const auto& prerequisite : definition.prerequisites)
            EXPECT_TRUE(ids.contains(prerequisite)) << definition.id << " missing prerequisite " << prerequisite;
    }

    EXPECT_EQ(roots, std::vector<std::string>{"forestry"});

    auto find = [&](const std::string& id) -> const TechnologyDefinition*
    {
        auto it = std::find_if(definitions.begin(), definitions.end(), [&](const TechnologyDefinition& definition)
        {
            return definition.id == id;
        });
        return it == definitions.end() ? nullptr : &*it;
    };

    const auto* mathematics = find("forestry");
    const auto* physics = find("masonry");
    const auto* chemistry = find("specialized_foundries");
    const auto* engineering = find("engineering_corps");
    const auto* medicine = find("communal_health");
    ASSERT_NE(mathematics, nullptr);
    ASSERT_NE(physics, nullptr);
    ASSERT_NE(chemistry, nullptr);
    ASSERT_NE(engineering, nullptr);
    ASSERT_NE(medicine, nullptr);

    EXPECT_EQ(mathematics->name, "Mathematics");
    EXPECT_EQ(physics->name, "Physics");
    EXPECT_EQ(chemistry->name, "Chemistry");
    EXPECT_EQ(engineering->name, "Engineering");
    EXPECT_EQ(medicine->name, "Medicine");
    EXPECT_EQ(chemistry->layoutLane, "Core Sciences");
    EXPECT_EQ(engineering->layoutLane, "Engineering");
    EXPECT_EQ(medicine->layoutLane, "Medicine");
    EXPECT_TRUE(std::find(physics->prerequisites.begin(), physics->prerequisites.end(), "forestry") != physics->prerequisites.end());
    EXPECT_TRUE(std::find(chemistry->prerequisites.begin(), chemistry->prerequisites.end(), "deep_mining") != chemistry->prerequisites.end());
    EXPECT_TRUE(std::find(engineering->prerequisites.begin(), engineering->prerequisites.end(), "specialized_foundries") != engineering->prerequisites.end());
}

TEST(TechnologyTests, MissingTechnologyDataUsesBuiltInDefaults)
{
    const auto definitions = LoadTechnologyDefinitionsFromFile("missing_technology_fixture.rtsdata");

    EXPECT_NE(std::find_if(definitions.begin(), definitions.end(), [](const TechnologyDefinition& definition)
    {
        return definition.id == "forestry";
    }), definitions.end());
}
