#include "economy/Building.h"
#include "warfare/Equipment.h"
#include "economy/SupplyPackage.h"
#include "warfare/UnitStats.h"
#include "economy/BalanceModifiers.h"
#include "simulation/MapGenerator.h"
#include "economy/Player.h"
#include "warfare/DivisionSector.h"
#include "warfare/MovementPlanner.h"
#include "simulation/SectorGraph.h"
#include "core/GameCommand.h"
#include "core/GameWorld.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
    // Adds a division to a building's garrison. Divisions are owned by the Player
    // now, so an owned building routes through Player::forces (the real ownership).
    // A player-less test building keeps the division alive in a static store and
    // pushes the raw pointer into the non-owning garrison view directly.
    SoldierDivision* GarrisonAdd(Building& b, std::unique_ptr<SoldierDivision> d)
    {
        if (b.owner != nullptr)
            return b.owner->AddForce(std::move(d), b.positionId);
        static std::vector<std::unique_ptr<SoldierDivision>> store;
        d->garrisonBuildingId = b.positionId;
        SoldierDivision* raw = d.get();
        store.push_back(std::move(d));
        if (auto* g = b.GetComponent<GarrisonComponent>())
            g->divisions.push_back(raw);
        return raw;
    }
}

// ─── Equipment taxonomy ───────────────────────────────────────────────────────

TEST(EquipmentTaxonomy, SwordMaterialProgressionIncreasesQuality)
{
    const EquipmentProfile* copper = FindEquipmentProfile(ResourceType::COPPER_SWORD);
    const EquipmentProfile* bronze = FindEquipmentProfile(ResourceType::BRONZE_SWORD);
    const EquipmentProfile* iron   = FindEquipmentProfile(ResourceType::IRON_SWORD);
    const EquipmentProfile* steel  = FindEquipmentProfile(ResourceType::STEEL_SWORD);

    ASSERT_NE(copper, nullptr);
    ASSERT_NE(bronze, nullptr);
    ASSERT_NE(iron, nullptr);
    ASSERT_NE(steel, nullptr);

    EXPECT_EQ(copper->category, EquipmentCategory::Sword);
    EXPECT_LT(copper->quality, bronze->quality);
    EXPECT_LT(bronze->quality, iron->quality);
    EXPECT_LT(iron->quality, steel->quality);
}

TEST(EquipmentTaxonomy, NonEquipmentResourcesHaveNoProfile)
{
    EXPECT_FALSE(IsEquipment(ResourceType::WOOD));
    EXPECT_FALSE(IsEquipment(ResourceType::BREAD));
    EXPECT_TRUE(IsEquipment(ResourceType::IRON_SHIELD));
    EXPECT_TRUE(IsEquipment(ResourceType::LEATHER_ARMOR));
}

// ─── UnitStats ────────────────────────────────────────────────────────────────

TEST(UnitStats, DefaultsDifferByUnitType)
{
    UnitStats militia   = MakeDefaultUnitStats(MilitaryUnitType::Militia);
    UnitStats swordsman = MakeDefaultUnitStats(MilitaryUnitType::Swordsman);

    EXPECT_GT(swordsman.lightAttack.GetBase(), militia.lightAttack.GetBase());
    EXPECT_GT(swordsman.maxStrength.GetBase(), militia.maxStrength.GetBase());
    EXPECT_EQ(swordsman.lightAttack.GetStatId(), BalanceStat::UnitLightAttack);
}

TEST(UnitStats, ResolveWithoutModifiersReturnsBase)
{
    UnitStats stats = MakeDefaultUnitStats(MilitaryUnitType::Swordsman);
    float value = ResolveUnitStat(stats.lightAttack, MilitaryUnitType::Swordsman, nullptr);
    EXPECT_FLOAT_EQ(value, stats.lightAttack.GetBase());
}

TEST(UnitStats, ModifierAppliesToMatchingUnitType)
{
    UnitStats stats = MakeDefaultUnitStats(MilitaryUnitType::Swordsman);

    BalanceModifierSet mods;
    BalanceModifier mod;
    mod.stat = BalanceStat::UnitLightAttack;
    mod.additive = 5.0;
    mod.multiplier = 1.0;
    mod.unitType = MilitaryUnitType::Swordsman;
    mod.source = "tech.test";
    mods.AddModifier(mod);

    float forSwordsman = ResolveUnitStat(stats.lightAttack, MilitaryUnitType::Swordsman, &mods);
    float forArcher    = ResolveUnitStat(stats.lightAttack, MilitaryUnitType::Archer, &mods);

    EXPECT_FLOAT_EQ(forSwordsman, stats.lightAttack.GetBase() + 5.0f);
    EXPECT_FLOAT_EQ(forArcher, stats.lightAttack.GetBase());  // wrong unit type → unaffected
}

// ─── War Phase 2 — Phase A: division model scale ─────────────────────────────

TEST(WarSystem, UnitTypeManpowerScaleIsInHundreds)
{
    MilitiaDivision militia;
    SwordsmanDivision swordsman;
    ArcherDivision archer;
    SpearmanDivision spearman;
    CavalryDivision cavalry;

    std::vector<const SoldierDivision*> divisions{&militia, &swordsman, &archer, &spearman, &cavalry};
    for (const SoldierDivision* d : divisions)
    {
        EXPECT_GE(d->strength, 50);
        EXPECT_LE(d->strength, 300);
    }

    std::set<int> distinctStrengths{militia.strength, swordsman.strength, archer.strength,
                                     spearman.strength, cavalry.strength};
    EXPECT_GT(distinctStrengths.size(), 1u);
}

TEST(WarSystem, RecruitmentManpowerCostMatchesEstablishment)
{
    EXPECT_EQ(GetBaseRecruitmentManpowerCost(MilitaryUnitType::Swordsman),
              static_cast<int>(std::lround(MakeDefaultUnitStats(MilitaryUnitType::Swordsman).maxStrength.GetBase())));
    EXPECT_GT(GetBaseRecruitmentManpowerCost(MilitaryUnitType::Swordsman),
              GetBaseRecruitmentManpowerCost(MilitaryUnitType::Militia));
}

TEST(WarSystem, VillageGeneratesHundredsOfManpower)
{
    TileMap map;
    Player player{0, map};
    map.params.sizeX = 8;
    map.params.sizeY = 8;
    map.tilemap.clear();
    for (int i = 0; i < map.params.sizeX * map.params.sizeY; i++)
    {
        Tile tile{i};
        tile.owner = &player;
        tile.tileType = TileType::GRASS;
        map.tilemap.push_back(std::move(tile));
    }

    auto* village = dynamic_cast<Village*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Village>(1)));
    ASSERT_NE(village, nullptr);
    village->constructionRemaining = 0.0;
    village->population.populationCap = 1000;
    village->population.manpowerRate = 5.0;
    village->population.foodSupplyLevel = 1.0;
    village->population.hasFood = true;

    for (int i = 0; i < 200; i++)
    {
        village->population.foodSupplyLevel = 1.0;  // fully fed for this test — food logistics covered elsewhere
        village->population.Update(*village, 1.0);
    }

    double manpower = player.strategicResources.Get(StrategicResourceType::Manpower);
    EXPECT_GT(manpower, 100.0);
    EXPECT_LE(manpower, 1000.0);
}

TEST(WarSystem, SwordsmanEstablishmentIsFortyWeapons)
{
    SwordsmanDivision swordsman;
    EXPECT_EQ(swordsman.weaponSupplyCapacity, 40);
}

TEST(WarSystem, MaxCohesionRespectsModifiers)
{
    SwordsmanDivision swordsman;
    float base = ResolveDivisionMaxCohesion(swordsman, nullptr);

    BalanceModifierSet mods;
    BalanceModifier mod;
    mod.stat = BalanceStat::UnitMaxCohesion;
    mod.additive = 0.0;
    mod.multiplier = 1.5;
    mod.unitType = MilitaryUnitType::Swordsman;
    mod.source = "tech.test";
    mods.AddModifier(mod);

    float modified = ResolveDivisionMaxCohesion(swordsman, &mods);
    EXPECT_GT(modified, base);
}

TEST(Persistence, DivisionCohesionRoundTrips)
{
    MapParameters params;
    params.seed = 4242;
    params.aiOpponentCount = 0;

    GameWorld world;
    world.InitMultiplayerWorld("cohesion-save-test", nullptr, nullptr, params, 0, true);
    Player* player = world.playerHandler.players.at(0).get();
    ASSERT_NE(player, nullptr);

    auto garrisons = player->GetTrackedBuildingsWithComponent<GarrisonComponent>();
    ASSERT_FALSE(garrisons.empty());
    Building* home = *garrisons.begin();

    auto division = std::make_unique<SwordsmanDivision>();
    division->cohesion = 17.5f;
    SoldierDivision* raw = player->AddForce(std::move(division), home->positionId);
    ASSERT_NE(raw, nullptr);
    int divisionId = raw->id;

    const std::string path = std::filesystem::temp_directory_path().string() + "/test_cohesion_roundtrip.rts_save";
    ASSERT_TRUE(world.SaveToFile(path));

    GameWorld loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path, nullptr, nullptr));
    std::remove(path.c_str());

    Player* loadedPlayer = loaded.playerHandler.players.at(0).get();
    ASSERT_NE(loadedPlayer, nullptr);
    SoldierDivision* found = nullptr;
    for (const auto& f : loadedPlayer->forces)
        if (f != nullptr && f->id == divisionId) { found = f.get(); break; }

    ASSERT_NE(found, nullptr);
    EXPECT_FLOAT_EQ(found->cohesion, 17.5f);
}

TEST(Persistence, DivisionMaterielRoundTrips)
{
    MapParameters params;
    params.seed = 4343;
    params.aiOpponentCount = 0;

    GameWorld world;
    world.InitMultiplayerWorld("materiel-save-test", nullptr, nullptr, params, 0, true);
    Player* player = world.playerHandler.players.at(0).get();
    ASSERT_NE(player, nullptr);

    auto garrisons = player->GetTrackedBuildingsWithComponent<GarrisonComponent>();
    ASSERT_FALSE(garrisons.empty());
    Building* home = *garrisons.begin();

    auto division = std::make_unique<SwordsmanDivision>();
    division->materielSupply = 33;
    division->materielSupplyCapacity = 77;
    SoldierDivision* raw = player->AddForce(std::move(division), home->positionId);
    ASSERT_NE(raw, nullptr);
    int divisionId = raw->id;

    const std::string path = std::filesystem::temp_directory_path().string() + "/test_materiel_roundtrip.rts_save";
    ASSERT_TRUE(world.SaveToFile(path));

    GameWorld loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path, nullptr, nullptr));
    std::remove(path.c_str());

    Player* loadedPlayer = loaded.playerHandler.players.at(0).get();
    ASSERT_NE(loadedPlayer, nullptr);
    SoldierDivision* found = nullptr;
    for (const auto& f : loadedPlayer->forces)
        if (f != nullptr && f->id == divisionId) { found = f.get(); break; }

    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->materielSupply, 33);
    EXPECT_EQ(found->materielSupplyCapacity, 77);
}

// ─── SupplyPackage ────────────────────────────────────────────────────────────

TEST(SupplyPackage, AddMergesSameType)
{
    SupplyPackage pkg;
    pkg.Add(ResourceType::IRON_SWORD, 4);
    pkg.Add(ResourceType::IRON_SWORD, 6);
    pkg.Add(ResourceType::IRON_SHIELD, 3);

    EXPECT_EQ(pkg.items.size(), 2u);
    EXPECT_EQ(pkg.CountCategory(EquipmentCategory::Sword), 10);
    EXPECT_EQ(pkg.CountCategory(EquipmentCategory::Shield), 3);
    EXPECT_EQ(pkg.TotalItems(), 13);
}

TEST(SupplyPackage, AverageQualityIsManpowerWeighted)
{
    SupplyPackage pkg;
    pkg.Add(ResourceType::STEEL_SWORD, 1);
    pkg.Add(ResourceType::COPPER_SWORD, 1);

    const EquipmentProfile* steel  = FindEquipmentProfile(ResourceType::STEEL_SWORD);
    const EquipmentProfile* copper = FindEquipmentProfile(ResourceType::COPPER_SWORD);
    float expected = (steel->quality + copper->quality) / 2.0f;
    EXPECT_FLOAT_EQ(pkg.AverageQuality(), expected);
}

// ─── Package planning (pure) ──────────────────────────────────────────────────

namespace
{
    const std::vector<EquipmentCategory> kDefaultCats{
        EquipmentCategory::Sword, EquipmentCategory::Shield,
        EquipmentCategory::Armor, EquipmentCategory::Ammo};
}

TEST(PlanSupplyPackage, RequiresWeaponAndRations)
{
    SupplyPackage pkg;

    // Shield but no weapon → no package.
    std::map<ResourceType, int> noWeapon{
        {ResourceType::IRON_SHIELD, 20}, {ResourceType::FOOD_PROVISIONS, 50}};
    EXPECT_FALSE(PlanSupplyPackage(noWeapon, kDefaultCats, 10, 10, pkg));

    // Weapon but no rations → no package.
    std::map<ResourceType, int> noRations{{ResourceType::IRON_SWORD, 20}};
    EXPECT_FALSE(PlanSupplyPackage(noRations, kDefaultCats, 10, 10, pkg));

    // Weapon + rations → package.
    std::map<ResourceType, int> ok{
        {ResourceType::IRON_SWORD, 20}, {ResourceType::FOOD_PROVISIONS, 10}};
    ASSERT_TRUE(PlanSupplyPackage(ok, kDefaultCats, 10, 10, pkg));
    EXPECT_EQ(pkg.rations, 10);
    EXPECT_EQ(pkg.soldierCapacity, 10);
}

TEST(PlanSupplyPackage, PicksBestGearPerCategory)
{
    std::map<ResourceType, int> available{
        {ResourceType::COPPER_SWORD, 100},
        {ResourceType::IRON_SWORD, 100},   // better sword preferred
        {ResourceType::IRON_SHIELD, 100},
        {ResourceType::IRON_ARMOR, 100},
        {ResourceType::ARROWS, 100},
        {ResourceType::FOOD_PROVISIONS, 100}};

    SupplyPackage pkg;
    ASSERT_TRUE(PlanSupplyPackage(available, kDefaultCats, 10, 10, pkg));

    EXPECT_EQ(pkg.CountCategory(EquipmentCategory::Sword), 10);
    EXPECT_EQ(pkg.BestOfCategory(EquipmentCategory::Sword), ResourceType::IRON_SWORD);
    EXPECT_EQ(pkg.CountCategory(EquipmentCategory::Shield), 10);
    EXPECT_EQ(pkg.CountCategory(EquipmentCategory::Armor), 10);
    EXPECT_EQ(pkg.CountCategory(EquipmentCategory::Ammo), 10);
}

TEST(PlanSupplyPackage, LimitedStockCapsPackageContents)
{
    std::map<ResourceType, int> available{
        {ResourceType::IRON_SWORD, 4},   // only 4 swords on hand
        {ResourceType::FOOD_PROVISIONS, 10}};

    SupplyPackage pkg;
    ASSERT_TRUE(PlanSupplyPackage(available, kDefaultCats, 10, 10, pkg));
    EXPECT_EQ(pkg.CountCategory(EquipmentCategory::Sword), 4);
}

TEST(SupplyPackage, BestOfCategoryPicksHighestQuality)
{
    SupplyPackage pkg;
    pkg.Add(ResourceType::COPPER_SWORD, 3);
    pkg.Add(ResourceType::STEEL_SWORD, 1);

    EXPECT_EQ(pkg.BestOfCategory(EquipmentCategory::Sword), ResourceType::STEEL_SWORD);
    EXPECT_EQ(pkg.BestOfCategory(EquipmentCategory::Bow), ResourceType::Null);
}

// ─── War Phase 2 — Phase B: three supply categories ──────────────────────────

TEST(Supply, CategoryOfResourceClassifiesCorrectly)
{
    EXPECT_EQ(CategoryOfResource(ResourceType::FOOD_PROVISIONS), SupplyCategory::Food);
    EXPECT_EQ(CategoryOfResource(ResourceType::WOOD), SupplyCategory::Materiel);
    EXPECT_EQ(CategoryOfResource(ResourceType::PLANKS), SupplyCategory::Materiel);
    EXPECT_EQ(CategoryOfResource(ResourceType::TOOLS), SupplyCategory::Materiel);
    EXPECT_EQ(CategoryOfResource(ResourceType::IRON_SWORD), SupplyCategory::Weapons);
    EXPECT_EQ(CategoryOfResource(ResourceType::ARROWS), SupplyCategory::Weapons);
}

TEST(PlanCategoryPackage, BuildsEachKind)
{
    std::map<ResourceType, int> available{
        {ResourceType::FOOD_PROVISIONS, 50},
        {ResourceType::WOOD, 30},
        {ResourceType::TOOLS, 10},
        {ResourceType::IRON_SWORD, 40}};

    SupplyPackage food;
    ASSERT_TRUE(PlanCategoryPackage(available, SupplyCategory::Food, 40, food));
    EXPECT_EQ(food.category, SupplyCategory::Food);
    EXPECT_EQ(food.rations, 40);

    SupplyPackage materiel;
    ASSERT_TRUE(PlanCategoryPackage(available, SupplyCategory::Materiel, 40, materiel));
    EXPECT_EQ(materiel.category, SupplyCategory::Materiel);
    EXPECT_EQ(materiel.TotalItems(), 40);   // 30 WOOD + 10 TOOLS

    SupplyPackage weapons;
    ASSERT_TRUE(PlanCategoryPackage(available, SupplyCategory::Weapons, 40, weapons));
    EXPECT_EQ(weapons.category, SupplyCategory::Weapons);
    EXPECT_EQ(weapons.rations, 0);   // food travels separately now
    EXPECT_EQ(weapons.CountCategory(EquipmentCategory::Sword), 40);
}

TEST(PlanCategoryPackage, FailsWhenNothingAvailable)
{
    std::map<ResourceType, int> empty;
    SupplyPackage out;
    EXPECT_FALSE(PlanCategoryPackage(empty, SupplyCategory::Food, 40, out));
    EXPECT_FALSE(PlanCategoryPackage(empty, SupplyCategory::Materiel, 40, out));
    EXPECT_FALSE(PlanCategoryPackage(empty, SupplyCategory::Weapons, 40, out));
}

// ─── Division combat stats (gear-weighted) ────────────────────────────────────

// ─── Field combat (division duels) ───────────────────────────────────────────

// ─── War Phase 2 — Phase B: per-tick supply consumption (B8/B9) ──────────────

TEST(Supply, DeployedHoldingDivisionConsumesFoodAndMateriel)
{
    // Deployed in the field but holding position (not marching, not fighting):
    // rations are eaten and materiel trickles for equipment maintenance, but
    // weapons/ammunition are only spent on the march or in battle.
    SwordsmanDivision div;
    int foodBefore = div.foodSupply;
    int weaponBefore = div.weaponSupply;
    int materielBefore = div.materielSupply;

    for (int i = 0; i < 50; i++)
        ConsumeDivisionSupply(div, 1.0, /*engaged=*/false, /*deployed=*/true);

    EXPECT_LT(div.foodSupply, foodBefore);
    EXPECT_EQ(div.weaponSupply, weaponBefore);   // static while holding
    EXPECT_LT(div.materielSupply, materielBefore);
}

TEST(Supply, EngagedDivisionConsumesWeaponsAndMateriel)
{
    // In combat all three pools drain.
    SwordsmanDivision div;
    int foodBefore = div.foodSupply;
    int weaponBefore = div.weaponSupply;
    int materielBefore = div.materielSupply;

    for (int i = 0; i < 10; i++)
        ConsumeDivisionSupply(div, 1.0, /*engaged=*/true, /*deployed=*/true);

    EXPECT_LT(div.foodSupply, foodBefore);
    EXPECT_LT(div.weaponSupply, weaponBefore);
    EXPECT_LT(div.materielSupply, materielBefore);
}

TEST(Supply, EngagedDivisionConsumesFaster)
{
    // Combat food consumption outpaces field upkeep (kFoodCombatMul > kFoodFieldMul).
    SwordsmanDivision engaged;
    SwordsmanDivision idle;

    for (int i = 0; i < 10; i++)
    {
        ConsumeDivisionSupply(engaged, 1.0, /*engaged=*/true, /*deployed=*/true);
        ConsumeDivisionSupply(idle, 1.0, /*engaged=*/false, /*deployed=*/true);
    }

    EXPECT_LT(engaged.foodSupply, idle.foodSupply);
}

TEST(Supply, StarvingDivisionLosesStrength)
{
    SwordsmanDivision div;
    div.foodSupply = 0;
    int strengthBefore = div.strength;

    for (int i = 0; i < 500; i++)
        ConsumeDivisionSupply(div, 1.0, /*engaged=*/false, /*deployed=*/true);

    EXPECT_LT(div.strength, strengthBefore);
}

TEST(Supply, WellFedDivisionDoesNotStarve)
{
    SwordsmanDivision div;
    int strengthBefore = div.strength;

    for (int i = 0; i < 50; i++)
        ConsumeDivisionSupply(div, 1.0, /*engaged=*/false, /*deployed=*/true);

    EXPECT_EQ(div.strength, strengthBefore);
}

TEST(Supply, UnarmedDivisionFightsWorse)
{
    auto armed = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    auto unarmed = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 2);
    unarmed->weaponSupply = 0;

    DivisionCombatStats armedStats = ComputeDivisionCombatStats(*armed, nullptr);
    DivisionCombatStats unarmedStats = ComputeDivisionCombatStats(*unarmed, nullptr);

    // Gear quality (lightAttack) is identical — both carry the same sword — but the
    // out-of-supply division delivers a fraction of it: supply now gates the whole
    // damage output, not the per-hit stat. See ComputeDivisionCombatStats.
    EXPECT_FLOAT_EQ(unarmedStats.lightAttack, armedStats.lightAttack);
    EXPECT_LT(unarmedStats.supplyEfficiency, armedStats.supplyEfficiency);
    EXPECT_FLOAT_EQ(armedStats.supplyEfficiency, 1.0f);
}

// ─── Package delivery to the front ────────────────────────────────────────────

TEST(PackageDelivery, ApplyEquipsDivisionsAndRefillsSupply)
{
    GuardTower tower(1);
    auto div = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    div->weaponSupply = 0;
    div->foodSupply = 0;
    div->weaponSupplyCapacity = 10;
    div->foodSupplyCapacity = 10;
    div->equipment.weapon = ResourceType::COPPER_SWORD;  // outdated gear
    GarrisonAdd(tower, std::move(div));

    EXPECT_TRUE(MilitaryNeedsSupply(tower));

    SupplyPackage pkg;
    pkg.category = SupplyCategory::Weapons;
    pkg.Add(ResourceType::IRON_SWORD, 10);
    pkg.Add(ResourceType::IRON_ARMOR, 10);
    pkg.soldierCapacity = 10;

    ASSERT_TRUE(ApplyPackageToMilitary(pkg, tower));

    const SoldierDivision& equipped = *tower.garrison.divisions.front();
    EXPECT_EQ(equipped.equipment.weapon, ResourceType::IRON_SWORD);  // upgraded
    EXPECT_EQ(equipped.equipment.armor, ResourceType::IRON_ARMOR);
    EXPECT_EQ(equipped.weaponSupply, equipped.weaponSupplyCapacity);
}

TEST(PackageDelivery, ApplyFoodPackageFillsBufferThenDivisions)
{
    GuardTower tower(1);
    // Fill the building's own ration buffer first so the package's rations flow
    // entirely to the division (buffer top-up takes priority — "as-is" behaviour).
    tower.supplyBuffer.buffer.SetStoredAmount(tower.supplyBuffer.buffer.bufferSize);

    auto div = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    div->foodSupply = 0;
    div->foodSupplyCapacity = 10;
    GarrisonAdd(tower, std::move(div));

    SupplyPackage pkg;
    pkg.category = SupplyCategory::Food;
    pkg.rations = 10;
    pkg.soldierCapacity = 10;

    ASSERT_TRUE(ApplyPackageToMilitary(pkg, tower));

    const SoldierDivision& fed = *tower.garrison.divisions.front();
    EXPECT_EQ(fed.foodSupply, fed.foodSupplyCapacity);
}

TEST(Supply, ApplyMaterielFillsMaterielPool)
{
    GuardTower tower(1);
    auto div = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    div->materielSupply = 0;
    div->materielSupplyCapacity = 20;
    GarrisonAdd(tower, std::move(div));

    SupplyPackage pkg;
    pkg.category = SupplyCategory::Materiel;
    pkg.Add(ResourceType::WOOD, 15);
    pkg.soldierCapacity = 20;

    ASSERT_TRUE(ApplyPackageToMilitary(pkg, tower));

    const SoldierDivision& resupplied = *tower.garrison.divisions.front();
    EXPECT_EQ(resupplied.materielSupply, 15);
}

TEST(PackageDelivery, PrioritisesNeediestDivision)
{
    GuardTower tower(1);

    auto needy = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    needy->weaponSupplyCapacity = 10;
    needy->weaponSupply = 0;       // fully depleted
    auto stocked = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 2);
    stocked->weaponSupplyCapacity = 10;
    stocked->weaponSupply = 8;     // mostly supplied
    GarrisonAdd(tower, std::move(stocked));   // pushed first, but less needy
    GarrisonAdd(tower, std::move(needy));

    SupplyPackage pkg;
    pkg.Add(ResourceType::IRON_SWORD, 5);   // only 5 weapon-supply to hand out
    pkg.rations = 10;

    ASSERT_TRUE(ApplyPackageToMilitary(pkg, tower));

    // The depleted division is served first and consumes the whole pool.
    const SoldierDivision& served = *tower.garrison.divisions[1];  // 'needy'
    const SoldierDivision& skipped = *tower.garrison.divisions[0]; // 'stocked'
    EXPECT_EQ(served.weaponSupply, 5);    // 0 + 5
    EXPECT_EQ(skipped.weaponSupply, 8);   // unchanged — pool already empty
}

TEST(PackageDelivery, ApplyToBuildingWithoutGarrisonDoesNothing)
{
    StorageBuilding storage(1);
    SupplyPackage pkg;
    pkg.Add(ResourceType::IRON_SWORD, 5);
    pkg.rations = 5;

    EXPECT_FALSE(MilitaryNeedsSupply(storage));
    EXPECT_FALSE(ApplyPackageToMilitary(pkg, storage));
}

// ─── SupplyHub network integration ────────────────────────────────────────────

namespace
{
    // Builds a small owned grass map for SupplyHub network tests.
    void FillGrass(TileMap& map, Player* owner, int width, int height)
    {
        map.params.sizeX = width;
        map.params.sizeY = height;
        map.tilemap.clear();
        map.tilemap.reserve(width * height);
        for (int i = 0; i < width * height; i++)
        {
            Tile tile{i};
            tile.owner = owner;
            tile.tileType = TileType::GRASS;
            map.tilemap.push_back(std::move(tile));
        }
    }

    // The supply hub owns no storage component — it never stockpiles equipment.
    void ExpectHubHasNoStorage(const SupplyHub& hub)
    {
        EXPECT_EQ(hub.GetComponent<StorageComponent>(), nullptr);
        EXPECT_NE(hub.GetComponent<SupplyPackageComponent>(), nullptr);
    }
}

TEST(SupplyHub, OwnsNoStorageComponent)
{
    SupplyHub hub(1);
    ExpectHubHasNoStorage(hub);
}

TEST(SupplyHub, AssemblesFromNetworkPickingBestSwordAndLeavingTheRest)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 16, 16);

    auto* depot = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Headquarters>(1)));
    auto* hub = dynamic_cast<SupplyHub*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({8, 8}), &player, std::make_unique<SupplyHub>(2)));
    auto* tower = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({12, 12}), &player, std::make_unique<GuardTower>(3)));
    ASSERT_NE(depot, nullptr);
    ASSERT_NE(hub, nullptr);
    ASSERT_NE(tower, nullptr);

    depot->storage.buffers[ResourceType::COPPER_SWORD].SetStoredAmount(30);  // worse
    depot->storage.buffers[ResourceType::STEEL_SWORD].SetStoredAmount(10);   // best
    depot->storage.buffers[ResourceType::ARROWS].SetStoredAmount(30);
    depot->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(100);

    // Demand-driven: a swordsman garrison short on weapons asks for Swords, which
    // is what makes the hub pack any weapon package at all.
    auto swordsman = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    swordsman->weaponSupply = 0;
    GarrisonAdd(*tower, std::move(swordsman));

    ASSERT_TRUE(hub->packaging.AssemblePackage(*hub));

    const auto& weaponsQueue = hub->packaging.readyPackages[static_cast<size_t>(SupplyCategory::Weapons)];
    ASSERT_FALSE(weaponsQueue.empty());
    const SupplyPackage& pkg = weaponsQueue.front();
    EXPECT_EQ(pkg.BestOfCategory(EquipmentCategory::Sword), ResourceType::STEEL_SWORD);
    // Best sword drained from the network, the worse one left in the depot.
    EXPECT_TRUE(depot->storage.buffers[ResourceType::STEEL_SWORD].buffer.empty());
    EXPECT_EQ(depot->storage.buffers[ResourceType::COPPER_SWORD].buffer.size(), 30u);
}

TEST(SupplyHub, AssemblesAllThreeCategories)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 16, 16);

    auto* depot = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Headquarters>(1)));
    auto* hub = dynamic_cast<SupplyHub*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({8, 8}), &player, std::make_unique<SupplyHub>(2)));
    auto* tower = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({12, 12}), &player, std::make_unique<GuardTower>(3)));
    ASSERT_NE(depot, nullptr);
    ASSERT_NE(hub, nullptr);
    ASSERT_NE(tower, nullptr);

    depot->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(100);
    depot->storage.buffers[ResourceType::WOOD].SetStoredAmount(100);
    depot->storage.buffers[ResourceType::IRON_SWORD].SetStoredAmount(40);

    // A fully-depleted swordsman garrison generates demand for all three streams
    // (food, materiel, weapons) so one pass fills every queue.
    auto swordsman = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    swordsman->foodSupply = 0;
    swordsman->weaponSupply = 0;
    swordsman->materielSupply = 0;
    GarrisonAdd(*tower, std::move(swordsman));

    // One assembly pass should be able to fill all three queues at once.
    ASSERT_TRUE(hub->packaging.AssemblePackage(*hub));

    EXPECT_GE(hub->packaging.ReadyPackageCount(SupplyCategory::Food), 1);
    EXPECT_GE(hub->packaging.ReadyPackageCount(SupplyCategory::Materiel), 1);
    EXPECT_GE(hub->packaging.ReadyPackageCount(SupplyCategory::Weapons), 1);
}

// ─── Demand-driven packing (supply rework) ────────────────────────────────────

TEST(DemandDriven, IdleFullySuppliedFrontPacksNothing)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 16, 16);

    auto* depot = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Headquarters>(1)));
    auto* hub = dynamic_cast<SupplyHub*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({8, 8}), &player, std::make_unique<SupplyHub>(2)));
    auto* tower = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({12, 12}), &player, std::make_unique<GuardTower>(3)));
    ASSERT_NE(depot, nullptr);
    ASSERT_NE(hub, nullptr);
    ASSERT_NE(tower, nullptr);

    // Swords are on hand, but the swordsman garrison is at full supply, so there
    // is no weapon demand. Drain the HQ's default starting rations so no food
    // package can form either — leaving genuinely nothing to pack.
    depot->storage.buffers[ResourceType::STEEL_SWORD].SetStoredAmount(40);
    depot->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(0);

    auto swordsman = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    // Divisions are constructed at full supply — leave them so.
    GarrisonAdd(*tower, std::move(swordsman));

    EXPECT_FALSE(hub->packaging.AssemblePackage(*hub))
        << "A fully-supplied front must not pull packages just because gear exists";
    EXPECT_EQ(hub->packaging.ReadyPackageCount(SupplyCategory::Weapons), 0);
    // The steel swords stay in the warehouse — no weapon demand, no draw.
    EXPECT_EQ(depot->storage.buffers[ResourceType::STEEL_SWORD].buffer.size(), 40u);
}

TEST(DemandDriven, ArcherFrontPacksRangedGearNotSwords)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 16, 16);

    auto* depot = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Headquarters>(1)));
    auto* hub = dynamic_cast<SupplyHub*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({8, 8}), &player, std::make_unique<SupplyHub>(2)));
    auto* tower = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({12, 12}), &player, std::make_unique<GuardTower>(3)));
    ASSERT_NE(depot, nullptr);
    ASSERT_NE(hub, nullptr);
    ASSERT_NE(tower, nullptr);

    // Both swords and bows are on hand; only bows should be packed for archers.
    depot->storage.buffers[ResourceType::STEEL_SWORD].SetStoredAmount(40);
    depot->storage.buffers[ResourceType::BOW].SetStoredAmount(40);
    depot->storage.buffers[ResourceType::ARROWS].SetStoredAmount(80);

    auto archer = CreateMilitaryDivision(MilitaryUnitType::Archer, 1);
    archer->weaponSupply = 0;
    GarrisonAdd(*tower, std::move(archer));

    ASSERT_TRUE(hub->packaging.AssemblePackage(*hub));

    const auto& weapons = hub->packaging.readyPackages[static_cast<size_t>(SupplyCategory::Weapons)];
    ASSERT_FALSE(weapons.empty());
    const SupplyPackage& pkg = weapons.front();
    EXPECT_GT(pkg.CountCategory(EquipmentCategory::Bow), 0);
    EXPECT_EQ(pkg.CountCategory(EquipmentCategory::Sword), 0)
        << "An archer front must not pull swords it cannot use";
    // The steel swords stay untouched in the warehouse.
    EXPECT_EQ(depot->storage.buffers[ResourceType::STEEL_SWORD].buffer.size(), 40u);
}

TEST(DemandDriven, ComputeMilitaryDemandBreaksDownByCategory)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 16, 16);
    auto* tower = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({4, 4}), &player, std::make_unique<GuardTower>(1)));
    ASSERT_NE(tower, nullptr);

    auto swordsman = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    swordsman->weaponSupply = 0;
    swordsman->materielSupply = 0;
    GarrisonAdd(*tower, std::move(swordsman));

    SupplyDemand demand = ComputeMilitaryDemand(*tower);
    EXPECT_GT(demand.weapons[EquipmentCategory::Sword], 0);
    EXPECT_EQ(demand.weapons.count(EquipmentCategory::Bow), 0u);
    EXPECT_GT(demand.materiel, 0);
}

TEST(DemandDriven, ServedCategoriesRestrictWhatAHubPacks)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 16, 16);

    auto* depot = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Headquarters>(1)));
    auto* hub = dynamic_cast<SupplyHub*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({8, 8}), &player, std::make_unique<SupplyHub>(2)));
    auto* tower = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({12, 12}), &player, std::make_unique<GuardTower>(3)));
    ASSERT_NE(depot, nullptr);
    ASSERT_NE(hub, nullptr);
    ASSERT_NE(tower, nullptr);

    depot->storage.buffers[ResourceType::WOOD].SetStoredAmount(100);
    depot->storage.buffers[ResourceType::IRON_SWORD].SetStoredAmount(40);

    auto swordsman = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    swordsman->weaponSupply = 0;
    swordsman->materielSupply = 0;
    GarrisonAdd(*tower, std::move(swordsman));

    // A materiel-only depot: serves Materiel, never weapons or food.
    hub->packaging.servedCategories = {false, true, false};

    ASSERT_TRUE(hub->packaging.AssemblePackage(*hub));
    EXPECT_GE(hub->packaging.ReadyPackageCount(SupplyCategory::Materiel), 1);
    EXPECT_EQ(hub->packaging.ReadyPackageCount(SupplyCategory::Weapons), 0);
    EXPECT_EQ(hub->packaging.ReadyPackageCount(SupplyCategory::Food), 0);
    // Swords left alone by a materiel depot.
    EXPECT_EQ(depot->storage.buffers[ResourceType::IRON_SWORD].buffer.size(), 40u);
}

// ─── Recruitment: equipment charged by category (any material/quality) ─────────

TEST(Recruitment, CountEquipmentCategorySumsAnyMaterial)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 16, 16);
    auto* depot = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Headquarters>(1)));
    ASSERT_NE(depot, nullptr);

    depot->storage.buffers[ResourceType::COPPER_SWORD].SetStoredAmount(10);
    depot->storage.buffers[ResourceType::STEEL_SWORD].SetStoredAmount(5);

    EXPECT_EQ(player.CountEquipmentCategory(EquipmentCategory::Sword), 15);
    EXPECT_EQ(player.CountEquipmentCategory(EquipmentCategory::Bow), 0);
}

TEST(Recruitment, TryPayEquipmentCategoryConsumesAllTiersProportionally)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 16, 16);
    auto* depot = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Headquarters>(1)));
    ASSERT_NE(depot, nullptr);

    depot->storage.buffers[ResourceType::COPPER_SWORD].SetStoredAmount(10);  // cheap
    depot->storage.buffers[ResourceType::STEEL_SWORD].SetStoredAmount(5);    // premium

    // Any sword of the right category satisfies the cost, and EVERY tier is drawn
    // down together (proportional) — not just the cheapest one.
    ResourceType rep = ResourceType::Null;
    ASSERT_TRUE(player.TryPayEquipmentCategory(EquipmentCategory::Sword, 10, &rep));

    int copperLeft = static_cast<int>(depot->storage.buffers[ResourceType::COPPER_SWORD].buffer.size());
    int steelLeft = static_cast<int>(depot->storage.buffers[ResourceType::STEEL_SWORD].buffer.size());
    EXPECT_LT(copperLeft, 10);          // copper was consumed
    EXPECT_LT(steelLeft, 5);            // steel was consumed too
    EXPECT_GT(steelLeft, 0);            // but not drained first
    EXPECT_EQ(copperLeft + steelLeft, 5);   // 15 stocked − 10 paid = 5 left
    EXPECT_EQ(rep, ResourceType::STEEL_SWORD);  // representative = best tier taken
}

TEST(Recruitment, TryPayEquipmentCategoryFailsWithoutEnoughAndConsumesNothing)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 16, 16);
    auto* depot = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Headquarters>(1)));
    ASSERT_NE(depot, nullptr);

    depot->storage.buffers[ResourceType::COPPER_SWORD].SetStoredAmount(5);
    ASSERT_EQ(player.CountEquipmentCategory(EquipmentCategory::Sword), 5);  // guard: setup stored 5

    EXPECT_FALSE(player.TryPayEquipmentCategory(EquipmentCategory::Sword, 10));
    EXPECT_EQ(depot->storage.buffers[ResourceType::COPPER_SWORD].buffer.size(), 5u);  // untouched
}

namespace
{
    // Places a Road building and registers it (and any building) in the player's
    // own road network, mirroring RoadNetworkTests' PlaceAndRegister helper.
    template <typename T>
    T* PlaceAndRegisterOnPlayer(TileMap& map, Player& player, Vec2i anchor, int id)
    {
        int tileId = map.GetIdFromCoords(anchor);
        auto* placed = dynamic_cast<T*>(map.PlaceLoadedBuilding(tileId, &player, std::make_unique<T>(id)));
        if (placed == nullptr)
            return nullptr;
        placed->constructionRemaining = 0.0;
        for (int occupiedTileId : map.GetBuildingTileIds(placed))
            player.roadNetwork->UpdateNavMap(occupiedTileId, placed);
        return placed;
    }
}

TEST(Supply, PackageTravelsOverRoadAndArrives)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 20, 10);
    // Player's RoadNetwork sized its nav map off the (then-empty) TileMap at
    // construction time; rebuild it now that FillGrass has sized the map.
    player.roadNetwork = std::make_unique<RoadNetwork>(map);

    auto* depot = PlaceAndRegisterOnPlayer<Headquarters>(map, player, {1, 1}, 1);
    auto* hub   = PlaceAndRegisterOnPlayer<SupplyHub>(map, player, {2, 5}, 2);
    auto* tower = PlaceAndRegisterOnPlayer<GuardTower>(map, player, {14, 5}, 3);
    ASSERT_NE(depot, nullptr);
    ASSERT_NE(hub, nullptr);
    ASSERT_NE(tower, nullptr);

    // hub occupies x2-4 (3x3 footprint), tower occupies x14-15 (2x2 footprint) —
    // roads fill the gap between them without overlapping either footprint.
    std::vector<Road*> roads;
    for (int x = 5; x <= 13; x++)
    {
        auto* road = PlaceAndRegisterOnPlayer<Road>(map, player, {x, 5}, 100 + x);
        ASSERT_NE(road, nullptr);
        roads.push_back(road);
    }

    // 40 = Swordsman's establishment (weaponSupplyCapacity, Phase A).
    depot->storage.buffers[ResourceType::IRON_SWORD].SetStoredAmount(40);

    auto div = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    div->weaponSupply = 0;
    GarrisonAdd(*tower, std::move(div));

    // Assemble and launch the shipment.
    ASSERT_TRUE(hub->packaging.AssemblePackage(*hub));
    hub->packaging.DeliverPackages(*hub);
    EXPECT_FALSE(hub->packaging.inFlight.empty());
    EXPECT_EQ(tower->garrison.divisions.front()->weaponSupply, 0);   // not arrived yet

    // Physically step just the transport chain (hub -> roads -> tower) so the
    // package hops along, without exercising unrelated building Update logic.
    for (int i = 0; i < 300 && tower->garrison.divisions.front()->weaponSupply == 0; i++)
    {
        hub->UpdateTransportables(1.0);
        for (Road* road : roads)
            road->UpdateTransportables(1.0);
        tower->UpdateTransportables(1.0);
    }

    EXPECT_GT(tower->garrison.divisions.front()->weaponSupply, 0);
}

TEST(Supply, PackageWithoutARouteStaysQueued)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 20, 10);
    // Player's RoadNetwork sized its nav map off the (then-empty) TileMap at
    // construction time; rebuild it now that FillGrass has sized the map.
    player.roadNetwork = std::make_unique<RoadNetwork>(map);

    auto* depot = PlaceAndRegisterOnPlayer<Headquarters>(map, player, {1, 1}, 1);
    auto* hub   = PlaceAndRegisterOnPlayer<SupplyHub>(map, player, {2, 5}, 2);
    auto* tower = PlaceAndRegisterOnPlayer<GuardTower>(map, player, {14, 5}, 3);
    ASSERT_NE(depot, nullptr);
    ASSERT_NE(hub, nullptr);
    ASSERT_NE(tower, nullptr);
    // No road connecting hub and tower.

    depot->storage.buffers[ResourceType::IRON_SWORD].SetStoredAmount(40);

    auto div = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    div->weaponSupply = 0;
    GarrisonAdd(*tower, std::move(div));

    ASSERT_TRUE(hub->packaging.AssemblePackage(*hub));
    hub->packaging.DeliverPackages(*hub);

    EXPECT_TRUE(hub->packaging.inFlight.empty());  // never launched — no route
    EXPECT_GE(hub->packaging.ReadyPackageCount(SupplyCategory::Weapons), 1);  // still queued, will retry
}

// ─── Division sectors (fixed 2x2 quadrant grid, border-clipped) ──────────────

TEST(DivisionSector, FixedCellGivesFull2x2AndSnapsToGrid)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 10, 10);

    // Any tile in cell (2,2) resolves to the same quadrant anchored at (4,4).
    DivisionSector a = ResolveDivisionSector(map, {4, 4});
    DivisionSector b = ResolveDivisionSector(map, {5, 5});
    EXPECT_EQ(a.cell.x, 2);
    EXPECT_EQ(a.cell.y, 2);
    EXPECT_EQ(a.anchor.x, 4);
    EXPECT_EQ(a.anchor.y, 4);
    EXPECT_EQ(b.anchor.x, a.anchor.x);   // snapped to the same fixed cell
    EXPECT_EQ(b.anchor.y, a.anchor.y);
    EXPECT_TRUE(a.IsFull());
    EXPECT_EQ(a.TileCount(), 4);
    EXPECT_EQ(a.Span().x, 2);
    EXPECT_EQ(a.Span().y, 2);
}

TEST(DivisionSector, RoadTilesDoNotBlockTheSector)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 10, 10);

    Building road;
    road.buildingType = BuildingType::Road;
    map.tilemap[map.GetIdFromCoords({4, 4})].buildingRef = &road;

    EXPECT_TRUE(ResolveDivisionSector(map, {4, 4}).IsFull());
}

TEST(DivisionSector, BorderClipsCellIntoLShape)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 10, 10);

    // Cell (1,1) covers tiles {2,2},{3,2},{2,3},{3,3}. Blocking one corner leaves
    // an L ("kolanko") of 3 tiles whose bounding span is still 2x2.
    Building wall;
    wall.buildingType = BuildingType::StorageBuilding;
    map.tilemap[map.GetIdFromCoords({3, 3})].buildingRef = &wall;

    DivisionSector sector = ResolveDivisionSector(map, {2, 2});
    ASSERT_TRUE(sector.IsValid());
    EXPECT_FALSE(sector.IsFull());
    EXPECT_EQ(sector.TileCount(), 3);
    EXPECT_EQ(sector.Span().x, 2);
    EXPECT_EQ(sector.Span().y, 2);
}

TEST(DivisionSector, BorderClipsCellIntoStripAndSingle)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 10, 10);

    Building wall;
    wall.buildingType = BuildingType::StorageBuilding;
    // Block the right column of cell (1,1) → 1x2 strip {2,2},{2,3}.
    map.tilemap[map.GetIdFromCoords({3, 2})].buildingRef = &wall;
    map.tilemap[map.GetIdFromCoords({3, 3})].buildingRef = &wall;

    DivisionSector strip = ResolveDivisionSector(map, {2, 3});
    ASSERT_TRUE(strip.IsValid());
    EXPECT_EQ(strip.TileCount(), 2);
    EXPECT_EQ(strip.Span().x, 1);
    EXPECT_EQ(strip.Span().y, 2);

    // Block one more → a single 1x1 tile.
    map.tilemap[map.GetIdFromCoords({2, 3})].buildingRef = &wall;
    DivisionSector single = ResolveDivisionSector(map, {2, 2});
    EXPECT_EQ(single.TileCount(), 1);
    EXPECT_EQ(single.Span().x, 1);
    EXPECT_EQ(single.Span().y, 1);
}

TEST(DivisionSector, MapEdgeClipsTheCell)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 5, 5);  // odd size → edge cells are clipped by bounds

    // Cell (2,2) anchored at (4,4); tiles (5,*) and (*,5) are out of bounds → 1x1.
    DivisionSector corner = ResolveDivisionSector(map, {4, 4});
    EXPECT_EQ(corner.TileCount(), 1);
    EXPECT_EQ(corner.Span().x, 1);

    // Cell (1,2) anchored at (2,4); bottom row out of bounds → 2x1 strip.
    DivisionSector edge = ResolveDivisionSector(map, {3, 4});
    EXPECT_EQ(edge.TileCount(), 2);
    EXPECT_EQ(edge.Span().x, 2);
    EXPECT_EQ(edge.Span().y, 1);
}

TEST(DivisionSector, TerritoryAndClipsCellToOwnedTiles)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 10, 10);  // FillGrass sets every tile's owner = &player

    // Disown one tile of cell (1,1): it drops out of the quadrant when AND-ed
    // with territory, even though it is perfectly walkable.
    map.tilemap[map.GetIdFromCoords({3, 3})].owner = nullptr;

    DivisionSector withTerritory = ResolveDivisionSector(map, {2, 2}, &player);
    EXPECT_EQ(withTerritory.TileCount(), 3);    // owned ∩ walkable

    DivisionSector ignoringTerritory = ResolveDivisionSector(map, {2, 2}, nullptr);
    EXPECT_EQ(ignoringTerritory.TileCount(), 4); // walkable only
}

TEST(DivisionSector, FullyBlockedCellIsInvalid)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 10, 10);

    Building wall;
    wall.buildingType = BuildingType::StorageBuilding;
    for (Vec2i t : {Vec2i{2, 2}, Vec2i{3, 2}, Vec2i{2, 3}, Vec2i{3, 3}})
        map.tilemap[map.GetIdFromCoords(t)].buildingRef = &wall;

    EXPECT_FALSE(ResolveDivisionSector(map, {3, 3}).IsValid());
}

// ─── Movement planner (road-aware pathfinding) ───────────────────────────────

namespace
{
    // Paints a horizontal road segment by pointing each tile at a road building.
    void PaintRoadRow(TileMap& map, Building& road, int y, int x0, int x1)
    {
        road.buildingType = BuildingType::Road;
        for (int x = x0; x <= x1; x++)
            map.tilemap[map.GetIdFromCoords({x, y})].buildingRef = &road;
    }

    bool PathUsesRoad(TileMap& map, const std::vector<int>& path)
    {
        for (int id : path)
        {
            const Building* b = map.tilemap[id].GetBuilding();
            if (b != nullptr && b->buildingType == BuildingType::Road)
                return true;
        }
        return false;
    }
}

TEST(MovementPlanner, StraightLineAcrossOpenGround)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 8, 8);

    std::vector<int> path = PlanDivisionPath(map, {1, 1}, {4, 1});
    ASSERT_GE(path.size(), 2u);
    EXPECT_EQ(path.front(), map.GetIdFromCoords({1, 1}));
    EXPECT_EQ(path.back(), map.GetIdFromCoords({4, 1}));
    EXPECT_FALSE(PathUsesRoad(map, path));  // no road around → cuts straight across
}

TEST(MovementPlanner, DetoursOntoRoadWhenItIsFaster)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 9, 5);

    Building road;
    PaintRoadRow(map, road, /*y*/ 0, /*x0*/ 0, /*x1*/ 8);  // road two rows above the line

    // Direct off-road row 2 costs 8*100=800; nipping up to the road and back is
    // cheaper (~720), so the optimal path should ride the road.
    std::vector<int> path = PlanDivisionPath(map, {0, 2}, {8, 2});
    ASSERT_GE(path.size(), 2u);
    EXPECT_TRUE(PathUsesRoad(map, path));
}

TEST(MovementPlanner, StaysOffRoadWhenDetourIsSlower)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 9, 9);

    Building road;
    PaintRoadRow(map, road, /*y*/ 0, /*x0*/ 0, /*x1*/ 8);  // road four rows away

    // Reaching the far road costs more than walking straight, so the planner keeps
    // to open ground.
    std::vector<int> path = PlanDivisionPath(map, {0, 4}, {8, 4});
    ASSERT_GE(path.size(), 2u);
    EXPECT_FALSE(PathUsesRoad(map, path));
}

TEST(MovementPlanner, UnreachableGoalReturnsEmptyPath)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 6, 6);

    // Wall off the goal tile entirely.
    Building wall;
    wall.buildingType = BuildingType::StorageBuilding;
    map.tilemap[map.GetIdFromCoords({4, 4})].buildingRef = &wall;

    EXPECT_TRUE(PlanDivisionPath(map, {1, 1}, {4, 4}).empty());
}

// ─── Division movement order ─────────────────────────────────────────────────

TEST(DivisionMovement, OrderMovesDivisionToTileAndArrives)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 16, 16);

    auto* tower = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({2, 2}), &player, std::make_unique<GuardTower>(1)));
    ASSERT_NE(tower, nullptr);

    GarrisonAdd(*tower, CreateMilitaryDivision(MilitaryUnitType::Swordsman, 7));

    const Vec2i targetTile{11, 11};
    ASSERT_TRUE(tower->garrison.MoveDivisionTo(7, targetTile, *tower));

    SoldierDivision& moving = *tower->garrison.divisions.front();
    EXPECT_TRUE(moving.inTransit);
    EXPECT_EQ(moving.occupiedTile.x, targetTile.x);   // bound to the target tile
    EXPECT_EQ(moving.occupiedTile.y, targetTile.y);
    ASSERT_FALSE(moving.travelPath.empty());
    EXPECT_EQ(moving.travelPath.back(), map.GetIdFromCoords(targetTile));

    // Step the simulation until the division arrives (bounded so a stuck unit fails).
    for (int i = 0; i < 5000 && moving.inTransit; i++)
        tower->garrison.Update(*tower, 0.1);

    EXPECT_FALSE(moving.inTransit);          // arrived
    EXPECT_TRUE(moving.travelPath.empty());  // path consumed
    EXPECT_GE(moving.worldPos.x, 0.0f);      // deployed on the map
}

// ─── Field combat is order-driven, then sticky ───────────────────────────────

namespace
{
    SoldierDivision* DeployDivision(GuardTower* tower, int id, Vec2i tile)
    {
        auto d = CreateMilitaryDivision(MilitaryUnitType::Swordsman, id);
        d->occupiedTile = tile;
        d->sectorCell = {tile.x / 2, tile.y / 2};
        d->worldPos = {(tile.x + 0.5f) * TILE_SIZE, (tile.y + 0.5f) * TILE_SIZE};
        d->inTransit = false;
        return GarrisonAdd(*tower, std::move(d));
    }
}

// Physical contact starts a battle by itself: two hostile divisions standing in
// the SAME quadrant engage automatically, no explicit attack order needed —
// entering the enemy's province IS the attack.
// A marching column cannot roll straight through a defended quadrant: combat
// tracks the PHYSICAL position of in-transit divisions, so the moment the
// column's body crosses a quadrant held by an enemy it is engaged, halted
// (intercepted) and the battle plays out there. Regression for "wojsko
// przeciwnika przejeżdża przez moje dywizje jak gdyby nigdy nic".
// Full command path: a field division that is NOT yet adjacent to an enemy
// military building, ordered via IssueMilitaryOrder(Attack), must march up to
// the building, engage, and siege it down — no manual order/tile poking.
// Attacking a building that sits on the ENEMY'S OWN territory must work from a
// cold start (no prior war): the attack order itself declares the war, and only
// then can the march enter enemy ground — movement is territory-gated on IsAtWar.
// Regression for the deadlock where the march was planned BEFORE the war was
// declared, so every path into enemy territory was blocked, the helper failed,
// the command was rejected, and the war declaration was never reached: the army
// simply did not react to attack orders on enemy land.
// An UNMANNED defensive work puts up no fight: an attack order on it captures it
// outright — no engagement, no battle. Only a manned garrison must be besieged.
// Capturing a garrison transfers the ground it projected: the defender's tiles
// are released and immediately re-claimed by the conqueror. Regression for the
// recompute ordering that left the radius NEUTRAL until an unrelated refresh.
// Capturing an enemy Headquarters eliminates its owner: the player is flagged
// defeated, every building it held (incl. civil ones) passes to the conqueror,
// and the game reports the conqueror as the victor.
// Overrunning enemy ground captures the CIVIL infrastructure on it (a village,
// production chain, roads), but military works still require a real siege.
// The Barracks is a CIVIL building: a direct attack order on it is rejected, and
// even a battle raging right beside it never sieges or captures it. Armies only
// besiege military targets — defensive works and the HQ.
// HoI4 flow: ordering a MOVE into a quadrant held by an enemy army converts into
// an attack on that army (declares war, marches to contact) instead of being
// rejected because the province is blocked.
// AttackTile (attack an enemy army standing on its own territory) must likewise
// declare the war itself before planning the march — it previously never
// declared war at all, so the route into enemy ground stayed blocked.
// ─── War Phase 2 — Phase C: HoI4-style deterministic combat ──────────────────

// Sanity-checks ResolveDivisionDuel against the documented formula (hits =
// min(attacks,defenses)*0.10 + max(0,attacks-defenses)*0.40) with hand-picked
// stats where attacks/defenses are exact round numbers, so the expected loss is
// computable by hand. See docs/war_system_phase2_design.md Phase C.
// A division whose organization breaks while badly outnumbered falls back to a
// rear quadrant instead of fighting to the last man (the soft-loss rule).
// A division surrounded on every side (no cardinal-neighbour quadrant is farther
// from the enemy than where it stands) cannot organize a retreat — the HoI4
// kocioł — and is destroyed outright once its strength runs out.
// ─── War Phase 2 — Phase C: Supply Conservation ──────────────────────────────

// ─── BUG 4: Combat finite-time resolution + deterministic RNG ────────────────

// The Barracks is a training FACTORY, not a garrison: a freshly trained division
// deploys straight onto a free tile beside the building instead of stationing
// inside, and deployed divisions do not consume the barracks' training capacity.
TEST(Recruitment, TrainedDivisionDeploysNextToBarracks)
{
    GameWorld world;
    auto player = std::make_unique<Player>(0, world.tilemap);
    Player* playerPtr = player.get();
    world.playerHandler.players[0] = std::move(player);
    FillGrass(world.tilemap, playerPtr, 20, 20);

    auto* hq = dynamic_cast<Headquarters*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), playerPtr, std::make_unique<Headquarters>(1)));
    ASSERT_NE(hq, nullptr);
    hq->constructionRemaining = 0.0;
    hq->storage.buffers[ResourceType::FOOD_PROVISIONS] = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 50};
    hq->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(50);
    hq->storage.buffers[ResourceType::WEAPON_SUPPLY] = ResourceBuffer{ResourceType::WEAPON_SUPPLY, 50};
    hq->storage.buffers[ResourceType::WEAPON_SUPPLY].SetStoredAmount(50);

    auto* barracks = dynamic_cast<Barracks*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({10, 10}), playerPtr, std::make_unique<Barracks>(2)));
    ASSERT_NE(barracks, nullptr);
    barracks->constructionRemaining = 0.0;
    barracks->garrison.cap = 50;
    // Manpower covers one Militia division's full establishment (Phase A).
    playerPtr->strategicResources.Set(StrategicResourceType::Manpower, 120);

    ASSERT_TRUE(barracks->QueueRecruitment(MilitaryUnitType::Militia));
    barracks->Update(1000.0);

    ASSERT_EQ(barracks->garrison.divisions.size(), 1u);
    const SoldierDivision& recruit = *barracks->garrison.divisions.front();
    EXPECT_GE(recruit.occupiedTile.x, 0);          // deployed, not garrisoned inside
    EXPECT_FALSE(recruit.inTransit);
    // Beside the building: within the 3-ring search area around the footprint.
    Vec2i anchor = world.tilemap.GetCoordsFromId(barracks->positionId);
    Vec2i footprint = barracks->GetFootprint();
    EXPECT_GE(recruit.occupiedTile.x, anchor.x - 3);
    EXPECT_LE(recruit.occupiedTile.x, anchor.x + footprint.x + 2);
    EXPECT_GE(recruit.occupiedTile.y, anchor.y - 3);
    EXPECT_LE(recruit.occupiedTile.y, anchor.y + footprint.y + 2);
    // A deployed division does not occupy training capacity.
    EXPECT_EQ(barracks->garrison.GetFreeDivisionSpace(*barracks),
              barracks->garrison.GetDivisionCap(*barracks));
}

// Divisions garrison only defensive works — sending a Defend order into the
// Barracks factory (or the HQ) is rejected: those are neutral buildings.
TEST(Recruitment, DefendOrderIntoBarracksIsRejected)
{
    GameWorld world;
    auto player = std::make_unique<Player>(0, world.tilemap);
    Player* playerPtr = player.get();
    world.playerHandler.players[0] = std::move(player);
    FillGrass(world.tilemap, playerPtr, 20, 20);

    auto* tower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), playerPtr, std::make_unique<GuardTower>(1)));
    auto* barracks = dynamic_cast<Barracks*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({10, 10}), playerPtr, std::make_unique<Barracks>(2)));
    ASSERT_NE(tower, nullptr);
    ASSERT_NE(barracks, nullptr);
    GarrisonAdd(*tower, CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1));

    world.SubmitCommand(GameCommand::IssueMilitaryOrder(
        playerPtr->id, MilitaryOrderType::Defend,
        tower->positionId, barracks->positionId, /*divisionId*/ 1));
    world.UpdateSimulation(0.01);
    auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results.front().accepted);
}

// AssignToArmy transfers selected divisions from one army into another existing
// army (the RMB-on-army-card action), pruning the emptied source army.
TEST(ArmyManagement, AssignToArmyTransfersDivisionsBetweenArmies)
{
    GameWorld world;
    auto player = std::make_unique<Player>(0, world.tilemap);
    Player* playerPtr = player.get();
    world.playerHandler.players[0] = std::move(player);
    FillGrass(world.tilemap, playerPtr, 20, 20);

    auto* tower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), playerPtr, std::make_unique<GuardTower>(1)));
    ASSERT_NE(tower, nullptr);
    GarrisonAdd(*tower, CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1));
    GarrisonAdd(*tower, CreateMilitaryDivision(MilitaryUnitType::Archer, 2));

    // Two separate armies, one division each.
    world.SubmitCommand(GameCommand::FormArmy(0, tower->positionId, {1}));
    world.SubmitCommand(GameCommand::FormArmy(0, tower->positionId, {2}));
    world.UpdateSimulation(0.01);
    world.ConsumeCommandResults();
    ASSERT_EQ(playerPtr->armyGroups.GetArmies().size(), 2u);

    int armyWithOne = playerPtr->armyGroups.FindArmyByDivision(1)->id;
    int armyWithTwo = playerPtr->armyGroups.FindArmyByDivision(2)->id;
    ASSERT_NE(armyWithOne, armyWithTwo);

    // Transfer division 2 into the army that holds division 1.
    world.SubmitCommand(GameCommand::AssignToArmy(0, armyWithOne, tower->positionId, {2}));
    world.UpdateSimulation(0.01);
    auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results.front().accepted);

    const ArmyGroup* merged = playerPtr->armyGroups.FindArmy(armyWithOne);
    ASSERT_NE(merged, nullptr);
    EXPECT_TRUE(merged->HasDivision(1));
    EXPECT_TRUE(merged->HasDivision(2));
    EXPECT_EQ(playerPtr->armyGroups.FindArmy(armyWithTwo), nullptr);  // emptied army pruned
    EXPECT_EQ(playerPtr->armyGroups.GetArmies().size(), 1u);
}

// Enemy-held tiles are impassable to the pathfinder: an enemy army physically
// blocks the way instead of being walked through.
TEST(MovementBlocking, EnemyOccupiedTilesArePassableOnlyAround)
{
    GameWorld world;
    auto player = std::make_unique<Player>(0, world.tilemap);
    Player* p = player.get();
    world.playerHandler.players[0] = std::move(player);
    FillGrass(world.tilemap, p, 12, 12);

    // Baseline: an open path exists on clear ground.
    auto open = PlanDivisionPath(world.tilemap, {2, 2}, {8, 2});
    ASSERT_GE(open.size(), 2u);

    // A goal held by an enemy division is unreachable (must be fought, not entered).
    std::set<int> blockedGoal{world.tilemap.GetIdFromCoords({8, 2})};
    EXPECT_TRUE(PlanDivisionPath(world.tilemap, {2, 2}, {8, 2}, {}, &blockedGoal).empty());

    // A single enemy tile on the straight line is routed around, never through.
    const int wallTile = world.tilemap.GetIdFromCoords({5, 2});
    std::set<int> wall{wallTile};
    auto detour = PlanDivisionPath(world.tilemap, {2, 2}, {8, 2}, {}, &wall);
    ASSERT_GE(detour.size(), 2u);
    EXPECT_EQ(std::count(detour.begin(), detour.end(), wallTile), 0);
}

// ─── Sector graph: adjacency & occupancy ─────────────────────────────────────

TEST(DivisionMovement, CommandsOverflowFullTargetSectorIntoAdjacentSector)
{
    GameWorld world;
    auto player = std::make_unique<Player>(0, world.tilemap);
    Player* playerPtr = player.get();
    world.playerHandler.players[0] = std::move(player);
    FillGrass(world.tilemap, playerPtr, 20, 20);

    auto* tower = dynamic_cast<GuardTower*>(
        world.tilemap.PlaceLoadedBuilding(
            world.tilemap.GetIdFromCoords({2, 2}), playerPtr, std::make_unique<GuardTower>(1)));
    ASSERT_NE(tower, nullptr);

    for (int id = 1; id <= 5; id++)
        GarrisonAdd(*tower, CreateMilitaryDivision(MilitaryUnitType::Swordsman, id));

    const Vec2i targetTile{12, 12};
    const int targetTileId = world.tilemap.GetIdFromCoords(targetTile);
    for (int id = 1; id <= 5; id++)
    {
        world.SubmitCommand(GameCommand::MoveDivision(
            playerPtr->id, tower->positionId, id, targetTileId));
    }

    world.UpdateSimulation(0.01);
    auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 5u);
    for (const auto& result : results)
        EXPECT_TRUE(result.accepted);

    const Vec2i targetCell = SectorCellOf(targetTile);
    int inTargetSector = 0;
    int inAdjacentSector = 0;
    for (const auto& division : tower->garrison.divisions)
    {
        EXPECT_GE(division->occupiedTile.x, 0);
        if (division->sectorCell == targetCell)
            inTargetSector++;
        else if (SectorsAdjacent(targetCell, division->sectorCell))
            inAdjacentSector++;
    }

    EXPECT_EQ(inTargetSector, 4);
    EXPECT_EQ(inAdjacentSector, 1);
}

TEST(SectorGraph, AdjacencyAndConnectivity)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 12, 12);

    EXPECT_TRUE(SectorsAdjacent({1, 1}, {2, 1}));
    EXPECT_TRUE(SectorsAdjacent({1, 1}, {2, 2}));   // diagonal counts as adjacent
    EXPECT_FALSE(SectorsAdjacent({1, 1}, {3, 1}));
    EXPECT_FALSE(SectorsAdjacent({1, 1}, {1, 1}));  // same cell is not adjacent

    // Two open neighbouring cells are walk-connected.
    EXPECT_TRUE(AreSectorsConnected(map, {1, 1}, {2, 1}));
    // Diagonal cells share no crossable edge.
    EXPECT_FALSE(AreSectorsConnected(map, {1, 1}, {2, 2}));
}

TEST(SectorGraph, WallBetweenCellsBreaksConnectivity)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 12, 12);

    // Cells (1,1) [tiles x2-3] and (2,1) [tiles x4-5] share the edge between x=3
    // and x=4. Block the whole right column of (1,1) so nothing can cross.
    Building wall;
    wall.buildingType = BuildingType::StorageBuilding;
    map.tilemap[map.GetIdFromCoords({3, 2})].buildingRef = &wall;
    map.tilemap[map.GetIdFromCoords({3, 3})].buildingRef = &wall;

    EXPECT_FALSE(AreSectorsConnected(map, {1, 1}, {2, 1}));
}

TEST(SectorGraph, OneDivisionPerTileIsEnforced)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 20, 20);

    auto* tower = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({2, 2}), &player, std::make_unique<GuardTower>(1)));
    ASSERT_NE(tower, nullptr);

    GarrisonAdd(*tower, CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1));
    GarrisonAdd(*tower, CreateMilitaryDivision(MilitaryUnitType::Swordsman, 2));

    const Vec2i tile{12, 12};

    // First division claims the tile.
    ASSERT_TRUE(tower->garrison.MoveDivisionTo(1, tile, *tower));
    EXPECT_FALSE(IsTileFree(player, tile, /*excluding*/ -1));
    EXPECT_EQ(DivisionOnTile(player, tile, -1), 1);

    // Clicking the occupied tile targets the whole 2x2 sector, so the second
    // division takes another free tile in that quadrant.
    EXPECT_TRUE(tower->garrison.MoveDivisionTo(2, tile, *tower));
    EXPECT_EQ(DivisionOnTile(player, Vec2i{13, 12}, -1), 2);
}

// ─── Bug fixes: Capture system ───────────────────────────────────────────────

TEST(WarSystem, CapturedBuildingClearsEnemySupplierConnections)
{
    // When a building with enemy supplier connections is captured, those connections
    // should be severed so the attacker doesn't keep pulling from enemy storage.
    TileMap map;
    Player attacker{0, map}, defender{1, map};
    FillGrass(map, &attacker, 30, 30);
    map.RecalculateTerritory(&attacker);
    map.RecalculateTerritory(&defender);

    // Defender's storage and production in one quadrant.
    auto* defenderStorage = dynamic_cast<StorageBuilding*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({5, 5}), &defender,
                                std::make_unique<StorageBuilding>(1)));
    auto* defenderMill = dynamic_cast<LumberMill*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({7, 7}), &defender,
                                std::make_unique<LumberMill>(2)));
    ASSERT_NE(defenderStorage, nullptr);
    ASSERT_NE(defenderMill, nullptr);

    // Connect them: Mill receives from Storage.
    defenderMill->SetSupplier(ResourceType::WOOD, defenderStorage);
    ASSERT_TRUE(defenderMill->HasSupplier(ResourceType::WOOD));

    // Attacker captures the Mill.
    map.PlaceLoadedBuilding(map.GetIdFromCoords({7, 7}), &attacker,
                            std::make_unique<LumberMill>(3));
    auto* capturedMill = dynamic_cast<LumberMill*>(map.GetBuilding(map.GetIdFromCoords({7, 7})));
    ASSERT_EQ(capturedMill->owner, &attacker);

    // After capture, the Mill should NO LONGER have the enemy supplier.
    EXPECT_FALSE(capturedMill->HasSupplier(ResourceType::WOOD));
}

TEST(WarSystem, RehomingDivisionClearsStaleOrders)
{
    // When a division's home garrison is captured/destroyed and it's re-homed to a
    // fallback garrison, its military orders (Attack, Defend, etc.) should be cleared
    // to prevent it from executing orders against its new owner's allies.
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 30, 30);

    auto* hq = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({2, 2}), &player,
                                std::make_unique<Headquarters>(1)));
    auto* tower = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({15, 15}), &player,
                                std::make_unique<GuardTower>(2)));
    ASSERT_NE(hq, nullptr);
    ASSERT_NE(tower, nullptr);

    auto* div = GarrisonAdd(*tower, CreateMilitaryDivision(MilitaryUnitType::Swordsman, 10));
    ASSERT_NE(div, nullptr);
    ASSERT_EQ(div->garrisonBuildingId, tower->positionId);

    // Give the division an attack order on tower (before it's destroyed).
    div->currentOrder = MilitaryOrderType::Attack;
    div->orderTargetPositionId = tower->positionId;
    EXPECT_EQ(div->currentOrder, MilitaryOrderType::Attack);

    // Manually remove the tower from the map (simulating capture).
    map.tilemap[map.GetIdFromCoords({15, 15})].building.reset();
    player.UnregisterBuilding(tower);

    // Rebuild garrison views: the division should be re-homed to HQ and orders cleared.
    player.RebuildGarrisonViews();

    EXPECT_EQ(div->garrisonBuildingId, hq->positionId);  // Re-homed to HQ
    EXPECT_EQ(div->currentOrder, MilitaryOrderType::None);  // Orders cleared
    EXPECT_EQ(div->orderTargetPositionId, -1);
}

TEST(WarSystem, OrphanedDivisionIsRemovedWhenAllGarrisonsGone)
{
    // When all garrison buildings are captured/destroyed and a player has divisions,
    // those divisions should be removed (representing surrender/dissolution) rather
    // than sitting invisible in limbo.
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 20, 20);

    auto* tower = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({10, 10}), &player,
                                std::make_unique<GuardTower>(1)));
    ASSERT_NE(tower, nullptr);

    // Add some divisions to the tower.
    auto* div1 = GarrisonAdd(*tower, CreateMilitaryDivision(MilitaryUnitType::Swordsman, 10));
    auto* div2 = GarrisonAdd(*tower, CreateMilitaryDivision(MilitaryUnitType::Archer, 11));
    ASSERT_EQ(player.forces.size(), 2);

    // Unregister the tower (simulating it being captured by the enemy).
    player.UnregisterBuilding(tower);
    map.tilemap[map.GetIdFromCoords({10, 10})].building.reset();

    // Rebuild garrison views: divisions without a home should be removed.
    player.RebuildGarrisonViews();

    // Both divisions should be gone.
    EXPECT_EQ(player.forces.size(), 0);
}

TEST(WarSystem, DivisionsArePreservedWhenHQExists)
{
    // If the player has an HQ, divisions can always be re-homed there, so they
    // should be preserved even if other garrisons are lost.
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 30, 30);

    auto* hq = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({2, 2}), &player,
                                std::make_unique<Headquarters>(1)));
    auto* tower = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({20, 20}), &player,
                                std::make_unique<GuardTower>(2)));
    ASSERT_NE(hq, nullptr);
    ASSERT_NE(tower, nullptr);

    auto* div = GarrisonAdd(*tower, CreateMilitaryDivision(MilitaryUnitType::Swordsman, 10));
    ASSERT_EQ(player.forces.size(), 1);

    // Destroy the tower.
    player.UnregisterBuilding(tower);
    map.tilemap[map.GetIdFromCoords({20, 20})].building.reset();

    // Rebuild: division should survive, re-homed to HQ.
    player.RebuildGarrisonViews();

    EXPECT_EQ(player.forces.size(), 1);
    EXPECT_EQ(div->garrisonBuildingId, hq->positionId);
}

// ─── BUG 5A: Mixed-tier equipment recruitment ─────────────────────────────────

TEST(Recruitment, SwordsmanSucceedsWithMixedCopperAndIronSwords)
{
    // BUG 5A confirmation: TryPayEquipmentCategory allocates proportionally across
    // all sword tiers. With 20 COPPER_SWORD + 20 IRON_SWORD in storage, recruiting
    // a Swordsman (which costs 40 swords of any tier) must succeed and consume ~20
    // of each type.
    GameWorld world;
    auto player = std::make_unique<Player>(0, world.tilemap);
    Player* playerPtr = player.get();
    world.playerHandler.players[0] = std::move(player);
    FillGrass(world.tilemap, playerPtr, 20, 20);

    auto* hq = dynamic_cast<Headquarters*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), playerPtr, std::make_unique<Headquarters>(1)));
    ASSERT_NE(hq, nullptr);
    hq->constructionRemaining = 0.0;

    // 20 of each sword tier = 40 total, exactly the swordsman establishment.
    hq->storage.buffers[ResourceType::COPPER_SWORD] = ResourceBuffer{ResourceType::COPPER_SWORD, 30};
    hq->storage.buffers[ResourceType::COPPER_SWORD].SetStoredAmount(20);
    hq->storage.buffers[ResourceType::IRON_SWORD] = ResourceBuffer{ResourceType::IRON_SWORD, 30};
    hq->storage.buffers[ResourceType::IRON_SWORD].SetStoredAmount(20);

    // Plain resource costs for a swordsman (FOOD_PROVISIONS=20, WEAPON_SUPPLY=20).
    hq->storage.buffers[ResourceType::FOOD_PROVISIONS] = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 50};
    hq->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(20);
    hq->storage.buffers[ResourceType::WEAPON_SUPPLY] = ResourceBuffer{ResourceType::WEAPON_SUPPLY, 50};
    hq->storage.buffers[ResourceType::WEAPON_SUPPLY].SetStoredAmount(20);

    auto* barracks = dynamic_cast<Barracks*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({10, 10}), playerPtr, std::make_unique<Barracks>(2)));
    ASSERT_NE(barracks, nullptr);
    barracks->constructionRemaining = 0.0;
    barracks->garrison.cap = 50;

    // Manpower cost for a Swordsman = maxStrength = 200.
    playerPtr->strategicResources.Set(StrategicResourceType::Manpower, 250);

    // QueueRecruitment should succeed (has 40 total swords across two tiers).
    ASSERT_TRUE(barracks->QueueRecruitment(MilitaryUnitType::Swordsman))
        << "Recruitment should succeed when 20 COPPER_SWORD + 20 IRON_SWORD are available";

    // Run training to completion.
    barracks->Update(1000.0);
    ASSERT_EQ(barracks->garrison.divisions.size(), 1u);

    // Both sword tiers should have been consumed proportionally (~20 each).
    int copperLeft = static_cast<int>(hq->storage.buffers[ResourceType::COPPER_SWORD].buffer.size());
    int ironLeft   = static_cast<int>(hq->storage.buffers[ResourceType::IRON_SWORD].buffer.size());
    EXPECT_EQ(copperLeft + ironLeft, 0) << "All 40 swords should have been consumed";
    // Each tier contributed roughly equally (proportional allocation).
    // With 20+20, expected is 20 each; allow ±1 for rounding.
    EXPECT_NEAR(copperLeft, 0, 1) << "Copper swords should be ~fully consumed";
    EXPECT_NEAR(ironLeft, 0, 1)   << "Iron swords should be ~fully consumed";
}

// ─── BUG 1: Road placement under own army ────────────────────────────────────

TEST(WarSystem, RoadCanBePlacedUnderFriendlyDivision)
{
    // BUG 1 regression: roads are traversable terrain; a deployed friendly
    // division must not block laying a road beneath it.
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 20, 20);

    auto* tower = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({2, 2}), &player,
                                std::make_unique<GuardTower>(1)));
    ASSERT_NE(tower, nullptr);

    // Deploy a division onto a tile away from the tower footprint.
    auto* div = GarrisonAdd(*tower, CreateMilitaryDivision(MilitaryUnitType::Militia, 5));
    const Vec2i occupiedTile{10, 10};
    ASSERT_TRUE(tower->garrison.MoveDivisionTo(5, occupiedTile, *tower));
    ASSERT_EQ(DivisionOnTile(player, occupiedTile, -1), 5);

    // Road placement on the occupied tile must succeed.
    const Vec2i roadFp = GetBuildingDefinition(BuildingType::Road).footprint;
    EXPECT_TRUE(map.CanPlaceBuilding(BuildingType::Road, occupiedTile, roadFp, &player))
        << "Road should be placeable under a friendly division";

    // Solid building (StorageBuilding) on the same tile must still be rejected.
    const Vec2i storageFp = GetBuildingDefinition(BuildingType::StorageBuilding).footprint;
    EXPECT_FALSE(map.CanPlaceBuilding(BuildingType::StorageBuilding, occupiedTile, storageFp, &player))
        << "Solid building should be blocked by a friendly division";

    (void)div;  // suppress unused-variable warning
}

TEST(WarSystem, CanBuildFootprintAllowDivisionsFlag)
{
    // Unit test for the internal flag: with allowDivisions=true the footprint
    // check passes; with false (default) it rejects a division-occupied tile.
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 20, 20);

    auto* tower = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({2, 2}), &player,
                                std::make_unique<GuardTower>(1)));
    ASSERT_NE(tower, nullptr);

    GarrisonAdd(*tower, CreateMilitaryDivision(MilitaryUnitType::Militia, 7));
    const Vec2i tile{12, 12};
    ASSERT_TRUE(tower->garrison.MoveDivisionTo(7, tile, *tower));
    ASSERT_EQ(DivisionOnTile(player, tile, -1), 7);

    EXPECT_FALSE(map.CanBuildFootprint(tile, {1, 1}, &player, /*allowDivisions=*/false));
    EXPECT_TRUE(map.CanBuildFootprint(tile, {1, 1}, &player, /*allowDivisions=*/true));
}

// ─── BUG 2 regression: enemy division on player tile, tile reclaimed ─────────

// ─── BUG 3b/3d — ResupplyDeployedDivisions ────────────────────────────────────

// BUG 3b: a deployed division within SupplyRange of a friendly HQ's stockpile
// receives weaponSupply from that stockpile after ResupplyDeployedDivisions().
TEST(ResupplyDeployed, DeployedDivisionPullsWeaponsFromNearbyDepot)
{
    MapParameters params;
    params.seed = 9001;
    params.aiOpponentCount = 0;

    GameWorld world;
    world.InitMultiplayerWorld("resupply-test", nullptr, nullptr, params, 0, true);

    Player* player = world.playerHandler.players.at(0).get();
    ASSERT_NE(player, nullptr);

    // Find the HQ.
    Headquarters* hq = nullptr;
    for (const auto& tile : world.tilemap.tilemap)
    {
        if (tile.building != nullptr && tile.building->buildingType == BuildingType::Headquarters
            && tile.building->owner == player)
        {
            hq = dynamic_cast<Headquarters*>(tile.building.get());
            break;
        }
    }
    ASSERT_NE(hq, nullptr);

    // Stock the HQ's weapon stockpile.
    SupplyBufferComponent* sb = hq->GetComponent<SupplyBufferComponent>();
    ASSERT_NE(sb, nullptr);
    sb->weaponStock = 40;

    // Deploy a division at the HQ tile (within supply range).
    auto division = std::make_unique<SwordsmanDivision>();
    division->weaponSupply = 0;
    division->weaponSupplyCapacity = 40;
    Vec2i hqCoords = world.tilemap.GetCoordsFromId(hq->positionId);
    division->occupiedTile = hqCoords;   // deployed right at the HQ
    SoldierDivision* raw = player->AddForce(std::move(division), hq->positionId);
    ASSERT_NE(raw, nullptr);

    world.ResupplyDeployedDivisions();

    EXPECT_GT(raw->weaponSupply, 0) << "Deployed division in range of stocked HQ should be resupplied";
    EXPECT_EQ(raw->weaponSupply, 40);            // got full fill
    EXPECT_EQ(sb->weaponStock, 0);               // stockpile drained
}

// BUG 3b: a deployed division OUTSIDE SupplyRange receives nothing.
TEST(ResupplyDeployed, OutOfRangeDivisionNotResupplied)
{
    MapParameters params;
    params.seed = 9002;
    params.aiOpponentCount = 0;

    GameWorld world;
    world.InitMultiplayerWorld("resupply-range-test", nullptr, nullptr, params, 0, true);

    Player* player = world.playerHandler.players.at(0).get();
    ASSERT_NE(player, nullptr);

    Headquarters* hq = nullptr;
    for (const auto& tile : world.tilemap.tilemap)
    {
        if (tile.building != nullptr && tile.building->buildingType == BuildingType::Headquarters
            && tile.building->owner == player)
        {
            hq = dynamic_cast<Headquarters*>(tile.building.get());
            break;
        }
    }
    ASSERT_NE(hq, nullptr);

    SupplyBufferComponent* sb = hq->GetComponent<SupplyBufferComponent>();
    ASSERT_NE(sb, nullptr);
    sb->weaponStock = 40;

    // Deploy the division far away — well outside the default 20-tile SupplyRange.
    auto division = std::make_unique<SwordsmanDivision>();
    division->weaponSupply = 0;
    division->weaponSupplyCapacity = 40;
    Vec2i hqCoords = world.tilemap.GetCoordsFromId(hq->positionId);
    Vec2i farTile  = {hqCoords.x + 50, hqCoords.y + 50};
    if (!world.tilemap.IsInside(farTile))
        farTile = {world.tilemap.params.sizeX - 1, world.tilemap.params.sizeY - 1};
    division->occupiedTile = farTile;
    SoldierDivision* raw = player->AddForce(std::move(division), hq->positionId);
    ASSERT_NE(raw, nullptr);

    world.ResupplyDeployedDivisions();

    // Out of range → nothing transferred.
    EXPECT_EQ(raw->weaponSupply, 0) << "Out-of-range division must not receive supply";
    EXPECT_EQ(sb->weaponStock, 40) << "Stockpile must remain untouched";
}

// BUG 3d: deployed division with strength deficit recovers from Manpower pool
// when in range and has food+weapon supply. ReinforceDivisionStrength is called
// from RunFieldCombat, so here we test it directly via UnitStats.h interface.
TEST(ResupplyDeployed, ManpowerReinforcementRestoresStrengthWhenInRange)
{
    MapParameters params;
    params.seed = 9003;
    params.aiOpponentCount = 0;

    GameWorld world;
    world.InitMultiplayerWorld("reinforce-test", nullptr, nullptr, params, 0, true);

    Player* player = world.playerHandler.players.at(0).get();
    ASSERT_NE(player, nullptr);

    // Add significant Manpower to the player's pool.
    player->strategicResources.values[StrategicResourceType::Manpower] = 10000.0;

    // Craft a deployed division at reduced strength with food and weapon supply.
    Headquarters* hq = nullptr;
    for (const auto& tile : world.tilemap.tilemap)
    {
        if (tile.building != nullptr && tile.building->buildingType == BuildingType::Headquarters
            && tile.building->owner == player)
        {
            hq = dynamic_cast<Headquarters*>(tile.building.get());
            break;
        }
    }
    ASSERT_NE(hq, nullptr);

    auto division = std::make_unique<SwordsmanDivision>();
    const int maxStrength = static_cast<int>(division->stats.maxStrength.GetBase());
    division->strength     = maxStrength / 2;   // half strength → deficit
    division->foodSupply   = division->foodSupplyCapacity;
    division->weaponSupply = division->weaponSupplyCapacity;
    division->occupiedTile = world.tilemap.GetCoordsFromId(hq->positionId);
    SoldierDivision* raw = player->AddForce(std::move(division), hq->positionId);
    ASSERT_NE(raw, nullptr);

    const int strengthBefore = raw->strength;
    const double manpowerBefore = player->strategicResources.values[StrategicResourceType::Manpower];

    // Call ReinforceDivisionStrength directly (the same path RunFieldCombat uses).
    for (int i = 0; i < 10; i++)
        ReinforceDivisionStrength(*raw, *player, 1.0, &player->balanceModifiers);

    EXPECT_GT(raw->strength, strengthBefore) << "Strength should recover when Manpower pool is non-empty";
    EXPECT_LT(player->strategicResources.values[StrategicResourceType::Manpower], manpowerBefore)
        << "Manpower pool should have been consumed";
}

// BUG 3d: empty Manpower pool → no reinforcement.
TEST(ResupplyDeployed, EmptyManpowerPoolPreventsReinforcement)
{
    MapParameters params;
    params.seed = 9004;
    params.aiOpponentCount = 0;

    GameWorld world;
    world.InitMultiplayerWorld("reinforce-empty-test", nullptr, nullptr, params, 0, true);

    Player* player = world.playerHandler.players.at(0).get();
    ASSERT_NE(player, nullptr);

    // Drain Manpower completely.
    player->strategicResources.values[StrategicResourceType::Manpower] = 0.0;

    Headquarters* hq = nullptr;
    for (const auto& tile : world.tilemap.tilemap)
    {
        if (tile.building != nullptr && tile.building->buildingType == BuildingType::Headquarters
            && tile.building->owner == player)
        {
            hq = dynamic_cast<Headquarters*>(tile.building.get());
            break;
        }
    }
    ASSERT_NE(hq, nullptr);

    auto division = std::make_unique<SwordsmanDivision>();
    const int maxStrength = static_cast<int>(division->stats.maxStrength.GetBase());
    division->strength     = maxStrength / 2;
    division->foodSupply   = division->foodSupplyCapacity;
    division->weaponSupply = division->weaponSupplyCapacity;
    SoldierDivision* raw = player->AddForce(std::move(division), hq->positionId);
    ASSERT_NE(raw, nullptr);

    const int strengthBefore = raw->strength;

    for (int i = 0; i < 10; i++)
        ReinforceDivisionStrength(*raw, *player, 1.0, &player->balanceModifiers);

    EXPECT_EQ(raw->strength, strengthBefore) << "No Manpower → strength must not recover";
}

// ─── Supply Conservation (moved from the removed Combat suite) ────────────────
// These exercise ConsumeDivisionSupply / PlayerSupplyConservation directly — the
// supply upkeep system survives the removal of field combat.

TEST(Supply, ConservationReducesRequiredSupply)
{
    SwordsmanDivision baseline;
    SwordsmanDivision conserved;

    // Few enough ticks that neither pool bottoms out at 0 — a saturated pool
    // would flatten both sides to the same (capped) loss and hide the ratio.
    for (int i = 0; i < 5; i++)
    {
        ConsumeDivisionSupply(baseline, 1.0, /*engaged=*/true, /*deployed=*/true, /*conservation=*/0.0);
        ConsumeDivisionSupply(conserved, 1.0, /*engaged=*/true, /*deployed=*/true, /*conservation=*/0.5);
    }

    int baselineLoss = baseline.weaponSupplyCapacity - baseline.weaponSupply;
    int conservedLoss = conserved.weaponSupplyCapacity - conserved.weaponSupply;
    ASSERT_GT(baselineLoss, 0);
    ASSERT_LT(baselineLoss, baseline.weaponSupplyCapacity);   // sanity: didn't saturate
    EXPECT_NEAR(static_cast<double>(conservedLoss), baselineLoss * 0.5, baselineLoss * 0.15 + 1.0);
}

TEST(Supply, ConservationIsCapped)
{
    GameWorld world;
    Player player{0, world.tilemap};
    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::SupplyConservation, /*additive*/5.0, /*multiplier*/1.0, {}, {}, {}, {}, "test.overflow"});

    EXPECT_LE(PlayerSupplyConservation(player), kMaxSupplyConservation);
    EXPECT_DOUBLE_EQ(PlayerSupplyConservation(player), kMaxSupplyConservation);
}

TEST(Supply, ConservationFromTechApplies)
{
    GameWorld world;
    Player player{0, world.tilemap};
    EXPECT_DOUBLE_EQ(PlayerSupplyConservation(player), 0.0);

    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::SupplyConservation, /*additive*/0.15, /*multiplier*/1.0, {}, {}, {}, {}, "tech.field_logistics"});

    EXPECT_NEAR(PlayerSupplyConservation(player), 0.15, 1e-6);
}
