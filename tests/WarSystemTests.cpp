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
    Player* player = world.GetPlayerHandlerForTesting().players.at(0).get();
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

    Player* loadedPlayer = loaded.GetPlayerHandlerForTesting().players.at(0).get();
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
    Player* player = world.GetPlayerHandlerForTesting().players.at(0).get();
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

    Player* loadedPlayer = loaded.GetPlayerHandlerForTesting().players.at(0).get();
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
    // Rates are now realistic (~1 food/min for a Swordsman in the field, see
    // MakeDefaultUnitStats), so the observation window needs real minutes, not
    // the handful of seconds the old ~70x-faster rates needed.
    SwordsmanDivision div;
    int foodBefore = div.foodSupply;
    int weaponBefore = div.weaponSupply;
    int materielBefore = div.materielSupply;

    for (int i = 0; i < 200; i++)   // 200 simulated seconds (~3.3 min)
        ConsumeDivisionSupply(div, 1.0, /*engaged=*/false, /*deployed=*/true);

    EXPECT_LT(div.foodSupply, foodBefore);
    EXPECT_EQ(div.weaponSupply, weaponBefore);   // static while holding
    EXPECT_LT(div.materielSupply, materielBefore);
}

TEST(Supply, EngagedDivisionConsumesWeaponsAndMateriel)
{
    // In combat all three pools drain. See note above on realistic-rate windows.
    SwordsmanDivision div;
    int foodBefore = div.foodSupply;
    int weaponBefore = div.weaponSupply;
    int materielBefore = div.materielSupply;

    for (int i = 0; i < 90; i++)   // 90 simulated seconds of fighting
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

    for (int i = 0; i < 90; i++)
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
    EXPECT_EQ(copperLeft + steelLeft, 5);   // 15 stocked �?� 10 paid = 5 left
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
    EXPECT_EQ(withTerritory.TileCount(), 3);    // owned �?� walkable

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
// przeciwnika przejeLLdLLa przez moje dywizje jak gdyby nigdy nic".
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
// kocioL� — and is destroyed outright once its strength runs out.
// ─── War Phase 2 — Phase C: Supply Conservation ──────────────────────────────

// ─── BUG 4: Combat finite-time resolution + deterministic RNG ────────────────

// The Barracks is a training FACTORY, not a garrison: a freshly trained division
// deploys straight onto a free tile beside the building instead of stationing
// inside, and deployed divisions do not consume the barracks' training capacity.
TEST(Recruitment, TrainedDivisionDeploysNextToBarracks)
{
    GameWorld world;
    auto player = std::make_unique<Player>(0, world.GetTileMapForTesting());
    Player* playerPtr = player.get();
    world.GetPlayerHandlerForTesting().players[0] = std::move(player);
    FillGrass(world.GetTileMapForTesting(), playerPtr, 20, 20);

    auto* hq = dynamic_cast<Headquarters*>(world.GetTileMapForTesting().PlaceLoadedBuilding(
        world.GetTileMapForTesting().GetIdFromCoords({2, 2}), playerPtr, std::make_unique<Headquarters>(1)));
    ASSERT_NE(hq, nullptr);
    hq->constructionRemaining = 0.0;
    hq->storage.buffers[ResourceType::FOOD_PROVISIONS] = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 50};
    hq->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(50);
    hq->storage.buffers[ResourceType::WEAPON_SUPPLY] = ResourceBuffer{ResourceType::WEAPON_SUPPLY, 50};
    hq->storage.buffers[ResourceType::WEAPON_SUPPLY].SetStoredAmount(50);

    auto* barracks = dynamic_cast<Barracks*>(world.GetTileMapForTesting().PlaceLoadedBuilding(
        world.GetTileMapForTesting().GetIdFromCoords({10, 10}), playerPtr, std::make_unique<Barracks>(2)));
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
    Vec2i anchor = world.GetTileMapForTesting().GetCoordsFromId(barracks->positionId);
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
    auto player = std::make_unique<Player>(0, world.GetTileMapForTesting());
    Player* playerPtr = player.get();
    world.GetPlayerHandlerForTesting().players[0] = std::move(player);
    FillGrass(world.GetTileMapForTesting(), playerPtr, 20, 20);

    auto* tower = dynamic_cast<GuardTower*>(world.GetTileMapForTesting().PlaceLoadedBuilding(
        world.GetTileMapForTesting().GetIdFromCoords({2, 2}), playerPtr, std::make_unique<GuardTower>(1)));
    auto* barracks = dynamic_cast<Barracks*>(world.GetTileMapForTesting().PlaceLoadedBuilding(
        world.GetTileMapForTesting().GetIdFromCoords({10, 10}), playerPtr, std::make_unique<Barracks>(2)));
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
    auto player = std::make_unique<Player>(0, world.GetTileMapForTesting());
    Player* playerPtr = player.get();
    world.GetPlayerHandlerForTesting().players[0] = std::move(player);
    FillGrass(world.GetTileMapForTesting(), playerPtr, 20, 20);

    auto* tower = dynamic_cast<GuardTower*>(world.GetTileMapForTesting().PlaceLoadedBuilding(
        world.GetTileMapForTesting().GetIdFromCoords({2, 2}), playerPtr, std::make_unique<GuardTower>(1)));
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
    auto player = std::make_unique<Player>(0, world.GetTileMapForTesting());
    Player* p = player.get();
    world.GetPlayerHandlerForTesting().players[0] = std::move(player);
    FillGrass(world.GetTileMapForTesting(), p, 12, 12);

    // Baseline: an open path exists on clear ground.
    auto open = PlanDivisionPath(world.GetTileMapForTesting(), {2, 2}, {8, 2});
    ASSERT_GE(open.size(), 2u);

    // A goal held by an enemy division is unreachable (must be fought, not entered).
    std::set<int> blockedGoal{world.GetTileMapForTesting().GetIdFromCoords({8, 2})};
    EXPECT_TRUE(PlanDivisionPath(world.GetTileMapForTesting(), {2, 2}, {8, 2}, {}, &blockedGoal).empty());

    // A single enemy tile on the straight line is routed around, never through.
    const int wallTile = world.GetTileMapForTesting().GetIdFromCoords({5, 2});
    std::set<int> wall{wallTile};
    auto detour = PlanDivisionPath(world.GetTileMapForTesting(), {2, 2}, {8, 2}, {}, &wall);
    ASSERT_GE(detour.size(), 2u);
    EXPECT_EQ(std::count(detour.begin(), detour.end(), wallTile), 0);
}

// ─── Sector graph: adjacency & occupancy ─────────────────────────────────────

TEST(DivisionMovement, CommandsOverflowFullTargetSectorIntoAdjacentSector)
{
    GameWorld world;
    auto player = std::make_unique<Player>(0, world.GetTileMapForTesting());
    Player* playerPtr = player.get();
    world.GetPlayerHandlerForTesting().players[0] = std::move(player);
    FillGrass(world.GetTileMapForTesting(), playerPtr, 20, 20);

    auto* tower = dynamic_cast<GuardTower*>(
        world.GetTileMapForTesting().PlaceLoadedBuilding(
            world.GetTileMapForTesting().GetIdFromCoords({2, 2}), playerPtr, std::make_unique<GuardTower>(1)));
    ASSERT_NE(tower, nullptr);

    for (int id = 1; id <= 5; id++)
        GarrisonAdd(*tower, CreateMilitaryDivision(MilitaryUnitType::Swordsman, id));

    const Vec2i targetTile{12, 12};
    const int targetTileId = world.GetTileMapForTesting().GetIdFromCoords(targetTile);
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
    auto player = std::make_unique<Player>(0, world.GetTileMapForTesting());
    Player* playerPtr = player.get();
    world.GetPlayerHandlerForTesting().players[0] = std::move(player);
    FillGrass(world.GetTileMapForTesting(), playerPtr, 20, 20);

    auto* hq = dynamic_cast<Headquarters*>(world.GetTileMapForTesting().PlaceLoadedBuilding(
        world.GetTileMapForTesting().GetIdFromCoords({2, 2}), playerPtr, std::make_unique<Headquarters>(1)));
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

    auto* barracks = dynamic_cast<Barracks*>(world.GetTileMapForTesting().PlaceLoadedBuilding(
        world.GetTileMapForTesting().GetIdFromCoords({10, 10}), playerPtr, std::make_unique<Barracks>(2)));
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

    Player* player = world.GetPlayerHandlerForTesting().players.at(0).get();
    ASSERT_NE(player, nullptr);

    // Find the HQ.
    Headquarters* hq = nullptr;
    for (const auto& tile : world.GetTileMapForTesting().tilemap)
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
    Vec2i hqCoords = world.GetTileMapForTesting().GetCoordsFromId(hq->positionId);
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

    Player* player = world.GetPlayerHandlerForTesting().players.at(0).get();
    ASSERT_NE(player, nullptr);

    Headquarters* hq = nullptr;
    for (const auto& tile : world.GetTileMapForTesting().tilemap)
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
    Vec2i hqCoords = world.GetTileMapForTesting().GetCoordsFromId(hq->positionId);
    Vec2i farTile  = {hqCoords.x + 50, hqCoords.y + 50};
    if (!world.GetTileMapForTesting().IsInside(farTile))
        farTile = {world.GetTileMapForTesting().params.sizeX - 1, world.GetTileMapForTesting().params.sizeY - 1};
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

    Player* player = world.GetPlayerHandlerForTesting().players.at(0).get();
    ASSERT_NE(player, nullptr);

    // Add significant Manpower to the player's pool.
    player->strategicResources.values[StrategicResourceType::Manpower] = 10000.0;

    // Craft a deployed division at reduced strength with food and weapon supply.
    Headquarters* hq = nullptr;
    for (const auto& tile : world.GetTileMapForTesting().tilemap)
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
    division->occupiedTile = world.GetTileMapForTesting().GetCoordsFromId(hq->positionId);
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

    Player* player = world.GetPlayerHandlerForTesting().players.at(0).get();
    ASSERT_NE(player, nullptr);

    // Drain Manpower completely.
    player->strategicResources.values[StrategicResourceType::Manpower] = 0.0;

    Headquarters* hq = nullptr;
    for (const auto& tile : world.GetTileMapForTesting().tilemap)
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

    // Enough simulated seconds for the combat weapon rate to produce a
    // measurable loss without saturating the 40-point pool — a saturated pool
    // would flatten both sides to the same (capped) loss and hide the ratio.
    // Keep both divisions fed each tick: at the (visible) food upkeep rate a
    // deployed division would otherwise starve partway through this window,
    // dropping strength and coupling it back into EVERY drain rate — which
    // skews the pure conservation ratio this test is meant to isolate.
    for (int i = 0; i < 300; i++)
    {
        ConsumeDivisionSupply(baseline, 1.0, /*engaged=*/true, /*deployed=*/true, /*conservation=*/0.0);
        ConsumeDivisionSupply(conserved, 1.0, /*engaged=*/true, /*deployed=*/true, /*conservation=*/0.5);
        baseline.foodSupply = baseline.foodSupplyCapacity;
        conserved.foodSupply = conserved.foodSupplyCapacity;
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
    Player player{0, world.GetTileMapForTesting()};
    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::SupplyConservation, /*additive*/5.0, /*multiplier*/1.0, {}, {}, {}, {}, "test.overflow"});

    EXPECT_LE(PlayerSupplyConservation(player), kMaxSupplyConservation);
    EXPECT_DOUBLE_EQ(PlayerSupplyConservation(player), kMaxSupplyConservation);
}

TEST(Supply, ConservationFromTechApplies)
{
    GameWorld world;
    Player player{0, world.GetTileMapForTesting()};
    EXPECT_DOUBLE_EQ(PlayerSupplyConservation(player), 0.0);

    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::SupplyConservation, /*additive*/0.15, /*multiplier*/1.0, {}, {}, {}, {}, "tech.field_logistics"});

    EXPECT_NEAR(PlayerSupplyConservation(player), 0.15, 1e-6);
}

// ─── ETAP 11.2 follow-up: territory follows the army ──────────────────────────
// Restored from the pre-refactor combat system (git history): a deployed
// division claims the whole 2x2 quadrant it physically stands on for its owner,
// every tick — independent of whether a Battle is active there (an undefended
// enemy quadrant is simply walked into and annexed).
TEST(WarSystem, DivisionClaimsQuadrantItStandsOn)
{
    GameWorld world;
    auto attacker = std::make_unique<Player>(0, world.GetTileMapForTesting());
    Player* attackerPtr = attacker.get();
    world.GetPlayerHandlerForTesting().players[0] = std::move(attacker);
    auto defender = std::make_unique<Player>(1, world.GetTileMapForTesting());
    Player* defenderPtr = defender.get();
    world.GetPlayerHandlerForTesting().players[1] = std::move(defender);

    FillGrass(world.GetTileMapForTesting(), defenderPtr, 20, 20);
    // RebuildGarrisonViews (runs at the top of every UpdateSimulation tick) drops
    // any division with no garrison building to fall back to — give the attacker
    // a home so it survives the tick.
    world.GetTileMapForTesting().tilemap[world.GetTileMapForTesting().GetIdFromCoords({0, 0})].owner = attackerPtr;
    auto* hq = dynamic_cast<Headquarters*>(world.GetTileMapForTesting().PlaceLoadedBuilding(
        world.GetTileMapForTesting().GetIdFromCoords({0, 0}), attackerPtr, std::make_unique<Headquarters>(1)));
    ASSERT_NE(hq, nullptr);
    hq->constructionRemaining = 0.0;

    auto division = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    division->occupiedTile = {4, 4};
    division->strength = 200;
    division->garrisonBuildingId = hq->positionId;
    attackerPtr->forces.push_back(std::move(division));

    Vec2i cell = SectorCellOf({4, 4});
    int tileId = world.GetTileMapForTesting().GetIdFromCoords({cell.x * 2, cell.y * 2});
    ASSERT_EQ(world.GetTileMapForTesting().tilemap[tileId].owner, defenderPtr);

    world.UpdateSimulation(0.01);

    EXPECT_EQ(world.GetTileMapForTesting().tilemap[tileId].owner, attackerPtr);
    // The whole 2x2 quadrant flips, not just the tile the division stands on.
    int neighborId = world.GetTileMapForTesting().GetIdFromCoords({cell.x * 2 + 1, cell.y * 2 + 1});
    EXPECT_EQ(world.GetTileMapForTesting().tilemap[neighborId].owner, attackerPtr);
}

// ─── Combat observation harness (pilot) ──────────────────────────────────────
// A tick-by-tick observer over a running GameWorld: instead of asserting only the
// end state (the "after Update(1000.0)" pattern used above), it captures the
// evolution of every division's strength/cohesion/order/flags, the aggregate
// cohesion of each active Battle, and territory ownership — once per simulation
// tick. It prints a compact CSV trace (visible in gtest output) AND auto-detects
// suspicious transitions that have historically been real combat bugs:
//   * cohesion RISING while a division is engaged (regen fighting the Battle
//     system — the exact regression fixed in commit 3ad94c2);
//   * strength RISING while engaged (reinforcement leaking into a live fight);
//   * a division flagged engaged AND regrouping at once (inconsistent state);
//   * a Battle's aggregate cohesion INCREASING mid-fight (should be monotone
//     down until it resolves).
// This is the pilot the war-test enhancement is built around — one self-contained
// helper, reused by every future combat scenario, that turns "combat is buggy
// again" into "at tick N, field X did Y".
namespace
{
    struct DivFrame
    {
        int id{0};
        int owner{-1};
        int strength{0};
        float cohesion{0.0f};
        bool engaged{false};
        bool retreating{false};
        float regroupTimer{0.0f};
        MilitaryOrderType order{MilitaryOrderType::None};
        Vec2i tile{-1, -1};
    };

    struct BattleFrame
    {
        int id{0};
        int attackerCohesion{0};
        int defenderCohesion{0};
        bool resolved{false};
    };

    const char* OrderLabel(MilitaryOrderType o)
    {
        switch (o)
        {
            case MilitaryOrderType::Attack:  return "Attack";
            case MilitaryOrderType::Defend:  return "Defend";
            case MilitaryOrderType::Support: return "Support";
            default:                          return "None";
        }
    }

    class CombatObserver
    {
    public:
        explicit CombatObserver(GameWorld& world) : world_(world) {}

        // Advances one simulation tick and records a frame. Returns false if an
        // anomaly was detected on this tick (also recorded in anomalies()).
        bool Step(double dt)
        {
            world_.UpdateSimulation(dt);
            const std::uint64_t tick = world_.GetSimulationTick();

            std::map<int, DivFrame> divs;   // divisionId -> frame (deterministic order)
            for (auto& [pid, player] : world_.GetPlayerHandler().players)
            {
                if (player == nullptr) continue;
                for (const auto& f : player->forces)
                {
                    if (f == nullptr) continue;
                    DivFrame df;
                    df.id = f->id; df.owner = pid;
                    df.strength = f->strength; df.cohesion = f->cohesion;
                    df.engaged = f->engaged; df.retreating = f->retreating;
                    df.regroupTimer = f->regroupTimer; df.order = f->currentOrder;
                    df.tile = f->occupiedTile;
                    divs[df.id] = df;
                }
            }

            std::map<int, BattleFrame> bfs;
            for (const auto& [bid, b] : world_.GetBattles())
                bfs[bid] = BattleFrame{bid, b.attackerCohesion, b.defenderCohesion, b.resolved};

            bool clean = true;
            // Per-division invariants vs. the previous frame.
            for (const auto& [id, cur] : divs)
            {
                auto prevIt = lastDivs_.find(id);
                if (prevIt != lastDivs_.end())
                {
                    const DivFrame& prev = prevIt->second;
                    if (cur.engaged && prev.engaged && cur.cohesion > prev.cohesion + 1e-3f)
                        clean &= Flag(tick, "div " + std::to_string(id) +
                            " cohesion ROSE while engaged (" + Num(prev.cohesion) +
                            " -> " + Num(cur.cohesion) + ")");
                    if (cur.engaged && prev.engaged && cur.strength > prev.strength)
                        clean &= Flag(tick, "div " + std::to_string(id) +
                            " strength ROSE while engaged (" + std::to_string(prev.strength) +
                            " -> " + std::to_string(cur.strength) + ")");
                }
                if (cur.engaged && cur.regroupTimer > 0.0f)
                    clean &= Flag(tick, "div " + std::to_string(id) +
                        " is engaged AND regrouping (timer=" + Num(cur.regroupTimer) + ")");
            }
            // Per-battle invariant: aggregate cohesion is monotone down until resolve.
            for (const auto& [bid, cur] : bfs)
            {
                auto prevIt = lastBattles_.find(bid);
                if (prevIt != lastBattles_.end() && !cur.resolved && !prevIt->second.resolved)
                {
                    if (cur.attackerCohesion > prevIt->second.attackerCohesion)
                        clean &= Flag(tick, "battle " + std::to_string(bid) +
                            " attacker cohesion ROSE mid-fight");
                    if (cur.defenderCohesion > prevIt->second.defenderCohesion)
                        clean &= Flag(tick, "battle " + std::to_string(bid) +
                            " defender cohesion ROSE mid-fight");
                }
            }

            // Emit a trace row on the first tick, on any battle-set change, and
            // every `traceEvery_` ticks — keeps output bounded but never misses a
            // transition (battle start / resolve).
            const bool battleSetChanged = BattleIds(bfs) != BattleIds(lastBattles_);
            if (frameCount_ == 0 || battleSetChanged || (tick % traceEvery_) == 0)
                PrintRow(tick, divs, bfs);

            lastDivs_ = std::move(divs);
            lastBattles_ = std::move(bfs);
            frameCount_++;
            return clean;
        }

        bool SawBattle() const { return sawBattle_; }
        const std::vector<std::string>& anomalies() const { return anomalies_; }
        int TerritoryTiles(const Player* owner) const
        {
            int n = 0;
            for (const auto& t : world_.GetTileMap().tilemap)
                if (t.owner == owner) n++;
            return n;
        }

    private:
        static std::string Num(float v)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", v);
            return buf;
        }
        static std::set<int> BattleIds(const std::map<int, BattleFrame>& m)
        {
            std::set<int> ids;
            for (const auto& [id, b] : m) if (!b.resolved) ids.insert(id);
            return ids;
        }
        bool Flag(std::uint64_t tick, const std::string& msg)
        {
            anomalies_.push_back("[tick " + std::to_string(tick) + "] " + msg);
            std::printf("  !! ANOMALY [tick %llu] %s\n",
                        static_cast<unsigned long long>(tick), msg.c_str());
            return false;
        }
        void PrintRow(std::uint64_t tick,
                      const std::map<int, DivFrame>& divs,
                      const std::map<int, BattleFrame>& bfs)
        {
            if (!bfs.empty()) sawBattle_ = true;
            std::string line = "t=" + std::to_string(tick);
            for (const auto& [id, d] : divs)
            {
                line += " | P" + std::to_string(d.owner) + ".d" + std::to_string(id) +
                        " str=" + std::to_string(d.strength) +
                        " coh=" + Num(d.cohesion) +
                        " " + OrderLabel(d.order) +
                        (d.engaged ? " ENG" : "") +
                        (d.retreating ? " RET" : "");
            }
            for (const auto& [bid, b] : bfs)
                line += " | B" + std::to_string(bid) +
                        " atk=" + std::to_string(b.attackerCohesion) +
                        " def=" + std::to_string(b.defenderCohesion) +
                        (b.resolved ? " RESOLVED" : "");
            std::printf("%s\n", line.c_str());
        }

        GameWorld& world_;
        std::map<int, DivFrame> lastDivs_;
        std::map<int, BattleFrame> lastBattles_;
        std::vector<std::string> anomalies_;
        std::uint64_t traceEvery_{25};
        std::size_t frameCount_{0};
        bool sawBattle_{false};
    };

    // Places a headquarters at `anchor` owned by `player`, home for its divisions.
    Headquarters* PlaceHomeHq(GameWorld& world, Player* player, Vec2i anchor, int id)
    {
        // Own a small block around the HQ so retreat-home lands on friendly ground.
        for (int dy = 0; dy < 4; dy++)
            for (int dx = 0; dx < 4; dx++)
            {
                Vec2i t{anchor.x + dx, anchor.y + dy};
                if (world.GetTileMapForTesting().IsInside(t))
                    world.GetTileMapForTesting().tilemap[world.GetTileMapForTesting().GetIdFromCoords(t)].owner = player;
            }
        auto* hq = dynamic_cast<Headquarters*>(world.GetTileMapForTesting().PlaceLoadedBuilding(
            world.GetTileMapForTesting().GetIdFromCoords(anchor), player, std::make_unique<Headquarters>(id)));
        if (hq != nullptr) hq->constructionRemaining = 0.0;
        return hq;
    }
}

// Pilot: a full 1v1 field battle driven purely through UpdateSimulation, watched
// tick-by-tick. Attacker marches nowhere (already in contact), the Battle forms,
// resolves, the loser disengages/retreats, and the contested quadrant flips to
// the survivor. Every combat invariant the observer knows about must hold for the
// whole fight — so this doubles as a regression guard for the cohesion-regen and
// reinforcement-in-combat bugs.
TEST(CombatTrace, SwordsmanDuelResolvesCleanlyAndFlipsTerritory)
{
    GameWorld world;
    auto attacker = std::make_unique<Player>(0, world.GetTileMapForTesting());
    Player* attackerPtr = attacker.get();
    world.GetPlayerHandlerForTesting().players[0] = std::move(attacker);
    auto defender = std::make_unique<Player>(1, world.GetTileMapForTesting());
    Player* defenderPtr = defender.get();
    world.GetPlayerHandlerForTesting().players[1] = std::move(defender);

    // Whole map starts as defender's land — the attacker is invading.
    FillGrass(world.GetTileMapForTesting(), defenderPtr, 20, 20);

    // Homes in opposite corners so a defeated side retreats away from the front.
    auto* atkHq = PlaceHomeHq(world, attackerPtr, {0, 0}, 1);
    auto* defHq = PlaceHomeHq(world, defenderPtr, {16, 16}, 2);
    ASSERT_NE(atkHq, nullptr);
    ASSERT_NE(defHq, nullptr);

    // Declare war symmetrically (engagement is gated on IsAtWar).
    attackerPtr->diplomatic.DeclareWar(0, 1);
    defenderPtr->diplomatic.DeclareWar(0, 1);

    // Defender holds tile (4,4); attacker stands adjacent at (5,4) — same 2x2
    // quadrant — carrying a live Attack order onto the defender's tile. The
    // attacker is a Swordsman, the defender a weaker Militia, so the fight is
    // decisive (attacker wins) rather than a rounding-decided mirror match.
    auto defDiv = CreateMilitaryDivision(MilitaryUnitType::Militia, 100);
    defDiv->occupiedTile = {4, 4};
    defDiv->sectorCell = SectorCellOf(defDiv->occupiedTile);
    defDiv->garrisonBuildingId = defHq->positionId;
    defenderPtr->forces.push_back(std::move(defDiv));

    auto atkDiv = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 200);
    atkDiv->occupiedTile = {5, 4};
    atkDiv->sectorCell = SectorCellOf(atkDiv->occupiedTile);
    atkDiv->garrisonBuildingId = atkHq->positionId;
    atkDiv->currentOrder = MilitaryOrderType::Attack;
    atkDiv->orderTargetPositionId = world.GetTileMapForTesting().GetIdFromCoords({4, 4});
    attackerPtr->forces.push_back(std::move(atkDiv));

    const Vec2i contested = SectorCellOf({4, 4});
    const int contestedTileId = world.GetTileMapForTesting().GetIdFromCoords({contested.x * 2, contested.y * 2});

    std::printf("=== CombatTrace: Swordsman 1v1 (attacker P0 d200 vs defender P1 d100) ===\n");
    CombatObserver observer(world);

    const double dt = 1.0 / 100.0;   // FixedSimulationClock::FixedDt
    const int maxTicks = 20000;      // 200s sim — a duel must resolve well inside this
    bool battleStarted = false;
    int ticks = 0;
    for (; ticks < maxTicks; ticks++)
    {
        observer.Step(dt);
        const bool anyActive = !world.GetBattles().empty();
        if (anyActive) battleStarted = true;
        // Stop once the fight has both started and fully cleared.
        if (battleStarted && !anyActive) break;
    }

    // Let the field settle so the victor can assert its territorial claim and the
    // beaten division marches clear (ClaimTilesUnderDivisions runs every tick).
    // Off-road march speed is ~10-14 tiles/min (~0.17-0.23 tiles/sec), and a
    // retreat/advance covers a couple of tiles, so this needs real seconds of
    // sim time, not a handful of ticks — 4000 ticks = 40 simulated seconds.
    for (int s = 0; s < 4000; s++)
        observer.Step(dt);

    const SoldierDivision* atkSurvivor = nullptr;
    for (const auto& f : attackerPtr->forces)
        if (f != nullptr && f->id == 200) atkSurvivor = f.get();
    const SoldierDivision* defSurvivor = nullptr;
    for (const auto& f : defenderPtr->forces)
        if (f != nullptr && f->id == 100) defSurvivor = f.get();

    std::printf("=== resolved after %d ticks | P0 territory=%d P1 territory=%d | anomalies=%zu ===\n",
                ticks, observer.TerritoryTiles(attackerPtr), observer.TerritoryTiles(defenderPtr),
                observer.anomalies().size());

    // 1) A real Battle actually formed and was observed.
    EXPECT_TRUE(observer.SawBattle()) << "attack order never produced a Battle";
    EXPECT_LT(ticks, maxTicks) << "battle never resolved (possible infinite fight)";

    // 2) No combat invariant was ever violated during the fight.
    EXPECT_TRUE(observer.anomalies().empty())
        << "combat anomalies detected — see trace above (" << observer.anomalies().size() << ")";

    // 3) The victorious attacker survived with strength intact and disengaged.
    ASSERT_NE(atkSurvivor, nullptr) << "attacker should survive the duel";
    EXPECT_GT(atkSurvivor->strength, 0);
    EXPECT_FALSE(atkSurvivor->engaged) << "attacker still flagged engaged after the battle ended";

    // 4) The winner holds the ground: the attacker physically advances onto the
    //    contested quadrant (AdvanceIntoQuadrant, GameWorld.Battles.cpp) and
    //    ClaimTilesUnderDivisions picks it up. Previously this failed because
    //    (a) MoveDivisionTo's snapToSector path bailed out whenever the target
    //    sector's mask was empty — which is EVERY military building's own tile,
    //    since footprints are always >= one full 2x2 sector, so the defeated
    //    defender's retreat-to-home call was silently a no-op and it never
    //    vacated the quadrant; and (b) the attacker never had a move command
    //    onto the quadrant at all when it fought from an adjacent one. Both are
    //    fixed in GarrisonComponent::MoveDivisionTo / GameWorld.Battles.cpp.
    EXPECT_EQ(world.GetTileMapForTesting().tilemap[contestedTileId].owner, attackerPtr)
        << "attacker won the battle but never took the contested quadrant";

    // 5) The defeated defender physically fell back one quadrant deeper into its
    //    own territory instead of freezing in place with a stale `retreating` flag.
    ASSERT_NE(defSurvivor, nullptr) << "defender should survive (routed, not destroyed)";
    EXPECT_NE(defSurvivor->occupiedTile, Vec2i({4, 4}))
        << "defeated defender never physically left the contested tile";
}

// Cohesion-first combat, quantified: an even 1v1 (identical Swordsman divisions,
// both fully supplied) should break one side within ~10-20s of simulated
// combat, and — since cohesion is the sole damage channel while both sides stay
// fully supplied (ResolveOneSidedDamage: hpLoss = orgLoss * (1-supplyEfficiency))
// — HP should barely move. Regression guard for kConstantOrgFloor calibration
// (UnitStats.cpp): before Task 3's retune this mirror match took ~130s (with
// the pre-Task-1 supply rates crashing supplyEfficiency mid-fight and
// throttling orgLoss) and even after Task 1's rate fix alone still ran ~24s,
// above the intended window.
TEST(CombatTrace, EvenMatchupBreaksWithinTenToTwentySecondsWithMinimalHpLoss)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.GetTileMapForTesting());
    Player* p0Ptr = p0.get();
    world.GetPlayerHandlerForTesting().players[0] = std::move(p0);
    auto p1 = std::make_unique<Player>(1, world.GetTileMapForTesting());
    Player* p1Ptr = p1.get();
    world.GetPlayerHandlerForTesting().players[1] = std::move(p1);

    FillGrass(world.GetTileMapForTesting(), p1Ptr, 20, 20);
    auto* hq0 = PlaceHomeHq(world, p0Ptr, {0, 0}, 1);
    auto* hq1 = PlaceHomeHq(world, p1Ptr, {16, 16}, 2);
    ASSERT_NE(hq0, nullptr);
    ASSERT_NE(hq1, nullptr);

    p0Ptr->diplomatic.DeclareWar(0, 1);
    p1Ptr->diplomatic.DeclareWar(0, 1);

    auto div1 = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 100);
    div1->occupiedTile = {4, 4};
    div1->sectorCell = SectorCellOf(div1->occupiedTile);
    div1->garrisonBuildingId = hq1->positionId;
    int hp1Before = div1->health;
    p1Ptr->forces.push_back(std::move(div1));

    auto div0 = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 200);
    div0->occupiedTile = {5, 4};
    div0->sectorCell = SectorCellOf(div0->occupiedTile);
    div0->garrisonBuildingId = hq0->positionId;
    div0->currentOrder = MilitaryOrderType::Attack;
    div0->orderTargetPositionId = world.GetTileMapForTesting().GetIdFromCoords({4, 4});
    int hp0Before = div0->health;
    p0Ptr->forces.push_back(std::move(div0));

    CombatObserver observer(world);
    const double dt = 1.0 / 100.0;
    const int maxTicks = 6000;   // 60s ceiling — well above the 20s target
    bool battleStarted = false;
    int ticks = 0;
    for (; ticks < maxTicks; ticks++)
    {
        observer.Step(dt);
        const bool anyActive = !world.GetBattles().empty();
        if (anyActive) battleStarted = true;
        if (battleStarted && !anyActive) break;
    }

    const double seconds = ticks * dt;
    std::printf("=== Even Swordsman 1v1 resolved after %.2fs (%d ticks) | anomalies=%zu ===\n",
                seconds, ticks, observer.anomalies().size());

    EXPECT_TRUE(observer.SawBattle()) << "attack order never produced a Battle";
    EXPECT_TRUE(observer.anomalies().empty()) << "combat anomalies detected — see trace above";

    // Break window: 10-20s is the target for a roughly-equal duel; allow a
    // margin down to 8s / up to 25s for tick-level noise and variance rolls.
    EXPECT_GE(seconds, 8.0)  << "even duel broke suspiciously fast — org floor may be too high";
    EXPECT_LE(seconds, 25.0) << "even duel dragged on too long — org floor may be too low";

    const SoldierDivision* survivor0 = nullptr;
    for (const auto& f : p0Ptr->forces) if (f != nullptr && f->id == 200) survivor0 = f.get();
    const SoldierDivision* survivor1 = nullptr;
    for (const auto& f : p1Ptr->forces) if (f != nullptr && f->id == 100) survivor1 = f.get();

    // Whichever side broke, both started fully supplied, so neither should have
    // taken meaningful permanent (HP) losses — the fight should have been decided
    // on organization, not manpower.
    if (survivor0 != nullptr)
        EXPECT_GE(survivor0->health, static_cast<int>(hp0Before * 0.9))
            << "fully-supplied division lost significant HP in a short duel";
    if (survivor1 != nullptr)
        EXPECT_GE(survivor1->health, static_cast<int>(hp1Before * 0.9))
            << "fully-supplied division lost significant HP in a short duel";
}

// RegenerateDivisionCohesion: a division holding ground (no live Attack order)
// rallies organization faster than one still pressing an attack — Task 3's
// "regen w obronie > w ataku" requirement.
TEST(Cohesion, DefensivePostureRegeneratesFasterThanAttacking)
{
    SwordsmanDivision attacking;
    SwordsmanDivision defending;
    attacking.cohesion = 10.0f;
    defending.cohesion = 10.0f;
    attacking.currentOrder = MilitaryOrderType::Attack;
    defending.currentOrder = MilitaryOrderType::None;

    RegenerateDivisionCohesion(attacking, 5.0, /*inOwnTerritory=*/true, /*engaged=*/false, nullptr);
    RegenerateDivisionCohesion(defending, 5.0, /*inOwnTerritory=*/true, /*engaged=*/false, nullptr);

    EXPECT_GT(defending.cohesion, attacking.cohesion);
}

// ─── Z5: full supply chain, end to end ────────────────────────────────────────
// Now that combat is dominated by supply (Z1-Z4), the front is only as alive as
// the logistics chain feeding it. This drives the REAL pipeline through
// GameWorld::UpdateSimulation (not manually-stepped UpdateTransportables calls
// like Supply.PackageTravelsOverRoadAndArrives, and not a pre-filled
// SupplyBufferComponent like ResupplyDeployed.* — both of those each cover only
// half the chain) end to end: HQ.storage -> SupplyHub assembles+ships a package
// over the road network -> GuardTower's SupplyBufferComponent absorbs it ->
// ResupplyDeployedDivisions (1 Hz) pulls from the tower into a DEPLOYED (field)
// division standing nearby. If this breaks anywhere, the new supply-dominated
// combat model makes every division on the map starve out and die — not just
// fight worse — so this is load-bearing for the game being playable at all.
TEST(Supply, EndToEndChainFeedsDeployedDivision)
{
    GameWorld world;
    auto player = std::make_unique<Player>(0, world.GetTileMapForTesting());
    Player* playerPtr = player.get();
    world.GetPlayerHandlerForTesting().players[0] = std::move(player);
    FillGrass(world.GetTileMapForTesting(), playerPtr, 20, 10);
    // Player's RoadNetwork sized its nav map off the (then-empty) TileMap at
    // construction time; rebuild it now that FillGrass has sized the map (same
    // fix as Supply.PackageTravelsOverRoadAndArrives).
    playerPtr->roadNetwork = std::make_unique<RoadNetwork>(world.GetTileMapForTesting());

    auto* depot = PlaceAndRegisterOnPlayer<Headquarters>(world.GetTileMapForTesting(), *playerPtr, {1, 1}, 1);
    auto* hub   = PlaceAndRegisterOnPlayer<SupplyHub>(world.GetTileMapForTesting(), *playerPtr, {2, 5}, 2);
    auto* tower = PlaceAndRegisterOnPlayer<GuardTower>(world.GetTileMapForTesting(), *playerPtr, {14, 5}, 3);
    ASSERT_NE(depot, nullptr);
    ASSERT_NE(hub, nullptr);
    ASSERT_NE(tower, nullptr);

    std::vector<Road*> roads;
    for (int x = 5; x <= 13; x++)
    {
        auto* road = PlaceAndRegisterOnPlayer<Road>(world.GetTileMapForTesting(), *playerPtr, {x, 5}, 100 + x);
        ASSERT_NE(road, nullptr);
        roads.push_back(road);
    }

    // The HQ is stocked with raw materiel; nothing pre-fills the tower or the
    // division directly — everything downstream must arrive through the chain.
    depot->storage.buffers[ResourceType::IRON_SWORD].SetStoredAmount(40);
    depot->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(200);

    // A division deployed OUT IN THE FIELD (occupiedTile set — not garrisoned),
    // standing beside the tower, well within ResupplyDeployedDivisions' default
    // 20-tile range. Starts with nothing.
    auto division = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    division->weaponSupply = 0;
    division->foodSupply = 0;
    division->occupiedTile = {14, 3};
    division->garrisonBuildingId = tower->positionId;
    playerPtr->forces.push_back(std::move(division));

    const double dt = 1.0 / 100.0;   // FixedSimulationClock::FixedDt
    const int maxTicks = 12000;      // 120s ceiling: assembleInterval=5s + march + 1Hz pull
    bool fed = false;
    SoldierDivision* d = nullptr;
    int tick = 0;
    for (; tick < maxTicks; tick++)
    {
        world.UpdateSimulation(dt);
        ASSERT_FALSE(playerPtr->forces.empty()) << "division was removed from forces mid-test";
        d = playerPtr->forces.front().get();
        if (d->weaponSupply > 0 && d->foodSupply > 0)
        {
            fed = true;
            break;
        }
    }

    std::printf("=== EndToEndChainFeedsDeployedDivision: fed after %d ticks (%.1fs) ===\n",
                tick, tick * dt);

    ASSERT_NE(d, nullptr);
    EXPECT_TRUE(fed) << "deployed division never received supply through the full "
                        "HQ -> SupplyHub -> road -> tower -> pull chain "
                        "(weapon=" << d->weaponSupply << " food=" << d->foodSupply << ")";
    EXPECT_GT(d->weaponSupply, 0);
    EXPECT_GT(d->foodSupply, 0);
}

// ─── Stat-interaction unit tests (Z6) ─────────────────────────────────────────
// ResolveOneSidedDamage is file-local; these drive it through the public
// ResolveDivisionDuel(DivisionCombatStats, DivisionCombatStats, ...) API with
// hand-constructed stat blocks so each test isolates exactly ONE variable
// instead of comparing two named unit types (which differ in many stats at
// once and would leave the result ambiguous about which stat caused it).
// simTick/ids = 0/0/0 disables the deterministic variance roll (see
// ResolveDivisionDuel's doc comment), so results are exact, not "on average".
namespace
{
    DivisionCombatStats BaselineCombatStats()
    {
        DivisionCombatStats s;
        s.lightAttack = 10.0f;
        s.armoredAttack = 10.0f;
        s.shock = 0.0f;
        s.armor = 5.0f;
        s.piercing = 5.0f;
        s.defense = 5.0f;
        s.morale = 60.0f;
        s.speed = 10.0f;
        s.maxStrength = 100.0f;
        s.maxCohesion = 40.0f;
        s.equipmentQuality = 1.0f;
        s.strength = 100.0f;         // full strength -> hpScaling = 1.0
        s.armoredShare = 0.3f;
        s.isArmored = false;
        s.hpDamageMultiplier = 1.0f;
        s.orgDamageMultiplier = 1.0f;
        s.supplyEfficiency = 1.0f;   // fully supplied unless a test overrides it
        s.isDefending = true;        // "holding ground" unless a test overrides it
        return s;
    }
}

TEST(Combat, HighDefenseReducesOrgLossVsLowDefense)
{
    DivisionCombatStats attacker = BaselineCombatStats();
    DivisionCombatStats lowDef = BaselineCombatStats();
    DivisionCombatStats highDef = BaselineCombatStats();
    highDef.defense = 20.0f;   // only defense differs from lowDef

    DivisionDuelResult vsLow  = ResolveDivisionDuel(attacker, lowDef, 1.0, 0, 0, 0);
    DivisionDuelResult vsHigh = ResolveDivisionDuel(attacker, highDef, 1.0, 0, 0, 0);

    EXPECT_LT(vsHigh.defenderCohesionLoss, vsLow.defenderCohesionLoss);
}

TEST(Combat, HighShockBreaksDefendingTargetFaster)
{
    DivisionCombatStats lowShock = BaselineCombatStats();
    DivisionCombatStats highShock = BaselineCombatStats();
    highShock.shock = 20.0f;   // only shock differs from lowShock
    DivisionCombatStats defender = BaselineCombatStats();
    defender.isDefending = true;

    DivisionDuelResult viaLow  = ResolveDivisionDuel(lowShock, defender, 1.0, 0, 0, 0);
    DivisionDuelResult viaHigh = ResolveDivisionDuel(highShock, defender, 1.0, 0, 0, 0);

    EXPECT_GT(viaHigh.defenderCohesionLoss, viaLow.defenderCohesionLoss);
}

TEST(Combat, ShockOnlyHelpsAgainstADefendingTarget)
{
    // Same high-shock attacker; only the TARGET's posture differs. Shock must
    // NOT reward breaking another attacker (two divisions trading blows head-on)
    // — only breaking a division that is trying to HOLD ground.
    DivisionCombatStats attacker = BaselineCombatStats();
    attacker.shock = 20.0f;
    DivisionCombatStats defendingTarget = BaselineCombatStats();
    defendingTarget.isDefending = true;
    DivisionCombatStats attackingTarget = BaselineCombatStats();
    attackingTarget.isDefending = false;

    DivisionDuelResult vsDefending = ResolveDivisionDuel(attacker, defendingTarget, 1.0, 0, 0, 0);
    DivisionDuelResult vsAttacking = ResolveDivisionDuel(attacker, attackingTarget, 1.0, 0, 0, 0);

    EXPECT_GT(vsDefending.defenderCohesionLoss, vsAttacking.defenderCohesionLoss);
}

TEST(Combat, PiercingBelowArmorMitigatesHardAttack)
{
    DivisionCombatStats lowPierce = BaselineCombatStats();
    lowPierce.piercing = 1.0f;       // well below defender.armor(5) -> mostly bounces
    DivisionCombatStats highPierce = BaselineCombatStats();
    highPierce.piercing = 10.0f;     // exceeds defender.armor(5) -> full penetration
    DivisionCombatStats armoredDefender = BaselineCombatStats();
    armoredDefender.armoredShare = 1.0f;  // fully armored: armoredAttack*pierceRatio dominates firepower
    armoredDefender.armor = 5.0f;

    DivisionDuelResult viaLow  = ResolveDivisionDuel(lowPierce, armoredDefender, 1.0, 0, 0, 0);
    DivisionDuelResult viaHigh = ResolveDivisionDuel(highPierce, armoredDefender, 1.0, 0, 0, 0);

    EXPECT_GT(viaHigh.defenderCohesionLoss, viaLow.defenderCohesionLoss);
}

TEST(Combat, CohesionLossScalesWithStatsNotConstant)
{
    // Regression guard against the pre-Z6 bug where kConstantOrgFloor (100-150)
    // dominated org loss and made lightAttack/armoredAttack/piercing/shock
    // differences ~1% of total damage — functionally decorative. A "glass
    // cannon" attacker (high everything) must deal MEANINGFULLY more org damage
    // than a "weak" attacker (low everything) against the identical defender —
    // a flat-floor-dominated formula could never produce this large a ratio.
    DivisionCombatStats weakAttacker = BaselineCombatStats();
    weakAttacker.lightAttack = 2.0f; weakAttacker.armoredAttack = 2.0f;
    weakAttacker.piercing = 1.0f; weakAttacker.shock = 0.0f;
    DivisionCombatStats strongAttacker = BaselineCombatStats();
    strongAttacker.lightAttack = 30.0f; strongAttacker.armoredAttack = 30.0f;
    strongAttacker.piercing = 15.0f; strongAttacker.shock = 15.0f;
    DivisionCombatStats defender = BaselineCombatStats();

    DivisionDuelResult weak   = ResolveDivisionDuel(weakAttacker, defender, 1.0, 0, 0, 0);
    DivisionDuelResult strong = ResolveDivisionDuel(strongAttacker, defender, 1.0, 0, 0, 0);

    EXPECT_GT(strong.defenderCohesionLoss, weak.defenderCohesionLoss * 3.0f);
}

// ─── Supply-gated cohesion ceiling (Z1 + Z3) ─────────────────────────────────

TEST(Cohesion, UnsuppliedEffectiveMaxCohesionCollapses)
{
    SwordsmanDivision supplied;     // default ctor: full weapon/food
    SwordsmanDivision unsupplied;
    unsupplied.weaponSupply = 0;
    unsupplied.foodSupply = 0;

    float suppliedMax   = ResolveEffectiveDivisionMaxCohesion(supplied, nullptr);
    float unsuppliedMax = ResolveEffectiveDivisionMaxCohesion(unsupplied, nullptr);

    EXPECT_NEAR(suppliedMax, 40.0f, 0.01f);     // full max, untouched
    EXPECT_LT(unsuppliedMax, 40.0f * 0.15f);    // well under 15% of full — "no equipment = no cohesion"
}

TEST(Cohesion, RecoveryRateScalesWithMorale)
{
    SwordsmanDivision lowMorale;
    SwordsmanDivision highMorale;
    lowMorale.cohesion = 10.0f;
    highMorale.cohesion = 10.0f;
    lowMorale.morale = 10;
    highMorale.morale = 100;

    RegenerateDivisionCohesion(lowMorale, 5.0, /*inOwnTerritory=*/true, /*engaged=*/false, nullptr);
    RegenerateDivisionCohesion(highMorale, 5.0, /*inOwnTerritory=*/true, /*engaged=*/false, nullptr);

    EXPECT_GT(highMorale.cohesion, lowMorale.cohesion);
}

// ─── Money test: the reported bug, disproven end to end ──────────────────────
// User's exact complaint: "wróg bez zapasów wycofuje się, regeneruje kohezję
// (dzięki obronie/terytorium) i wygrywa kolejną turę". This drives a full,
// real GameWorld battle where the DEFENDER is permanently cut off (weapon and
// food capacity both 0 — no resupply is even possible), lets it retreat and
// sit at home (full territory + defensive posture bonuses active) for a long
// settle window, then launches a SECOND attack. The defender must lose again,
// not win the rematch.
TEST(CombatTrace, SuppliedBeatsUnsuppliedAcrossRetreatCycle)
{
    GameWorld world;
    auto attacker = std::make_unique<Player>(0, world.GetTileMapForTesting());
    Player* attackerPtr = attacker.get();
    world.GetPlayerHandlerForTesting().players[0] = std::move(attacker);
    auto defender = std::make_unique<Player>(1, world.GetTileMapForTesting());
    Player* defenderPtr = defender.get();
    world.GetPlayerHandlerForTesting().players[1] = std::move(defender);

    FillGrass(world.GetTileMapForTesting(), defenderPtr, 20, 20);
    auto* atkHq = PlaceHomeHq(world, attackerPtr, {0, 0}, 1);
    auto* defHq = PlaceHomeHq(world, defenderPtr, {16, 16}, 2);
    ASSERT_NE(atkHq, nullptr);
    ASSERT_NE(defHq, nullptr);

    attackerPtr->diplomatic.DeclareWar(0, 1);
    defenderPtr->diplomatic.DeclareWar(0, 1);

    // Defender: cut off from the start, permanently — pools EMPTY (not
    // capacity-zeroed: DivisionSupplyEfficiency treats capacity==0 as "this
    // division doesn't use the pool" and skips the penalty entirely, which is
    // the opposite of what "cut off" means here). No SupplyHub/road exists on
    // this tiny test map, so nothing can ever refill it. Identical unit type to
    // the attacker, so logistics is the ONLY difference between the two sides.
    auto defDiv = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 100);
    defDiv->occupiedTile = {4, 4};
    defDiv->sectorCell = SectorCellOf(defDiv->occupiedTile);
    defDiv->garrisonBuildingId = defHq->positionId;
    defDiv->weaponSupply = 0;
    defDiv->foodSupply = 0;
    defenderPtr->forces.push_back(std::move(defDiv));

    auto atkDiv = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 200);
    atkDiv->occupiedTile = {5, 4};
    atkDiv->sectorCell = SectorCellOf(atkDiv->occupiedTile);
    atkDiv->garrisonBuildingId = atkHq->positionId;
    atkDiv->currentOrder = MilitaryOrderType::Attack;
    atkDiv->orderTargetPositionId = world.GetTileMapForTesting().GetIdFromCoords({4, 4});
    attackerPtr->forces.push_back(std::move(atkDiv));

    CombatObserver observer(world);
    const double dt = 1.0 / 100.0;
    const int maxTicks = 6000;   // 60s ceiling per battle

    auto runUntilBattleResolves = [&]() -> int
    {
        bool started = false;
        int t = 0;
        for (; t < maxTicks; t++)
        {
            observer.Step(dt);
            const bool anyActive = !world.GetBattles().empty();
            if (anyActive) started = true;
            if (started && !anyActive) break;
        }
        return t;
    };

    int firstTicks = runUntilBattleResolves();
    ASSERT_TRUE(observer.SawBattle()) << "first attack never produced a Battle";
    ASSERT_LT(firstTicks, maxTicks) << "first battle never resolved";

    // Long settle: every chance to regenerate — home territory, defensive
    // posture, full morale. It must NOT meaningfully recover, since it has no
    // weapons/food to sustain organization above its collapsed ceiling.
    for (int s = 0; s < 6000; s++)
        observer.Step(dt);

    SoldierDivision* defAfterFirst = nullptr;
    for (auto& f : defenderPtr->forces) if (f != nullptr && f->id == 100) defAfterFirst = f.get();

    if (defAfterFirst != nullptr)
    {
        float effectiveMax = ResolveEffectiveDivisionMaxCohesion(*defAfterFirst, nullptr);
        EXPECT_LE(defAfterFirst->cohesion, effectiveMax + 1.0f)
            << "unsupplied defender regenerated cohesion beyond its supply-capped ceiling";
        EXPECT_LT(defAfterFirst->cohesion, 10.0f)
            << "unsupplied defender recovered far more cohesion than its logistics should allow";

        // Rematch: order the (fully-supplied) attacker onto the defender's new
        // position for a second engagement.
        SoldierDivision* atkMutable = nullptr;
        for (auto& f : attackerPtr->forces) if (f != nullptr && f->id == 200) atkMutable = f.get();
        ASSERT_NE(atkMutable, nullptr) << "attacker should have survived the first battle";
        atkMutable->regroupTimer = 0.0f;

        // Issue a REAL AttackTile command (not a direct field-poke) — the
        // defender retreated a whole sector away, so the attacker needs to
        // physically march there first; only the command layer
        // (MoveDivisionToAttackTile) knows how to queue that march before
        // flagging the Attack order. A bare `currentOrder=Attack` field-poke
        // (as used for the FIRST engagement, where the two divisions started
        // pre-adjacent) would silently never engage here.
        world.SubmitCommand(GameCommand::AttackTile(
            attackerPtr->id, atkHq->positionId, atkMutable->id,
            world.GetTileMapForTesting().GetIdFromCoords(defAfterFirst->occupiedTile)));

        const int marchAndFightTicks = 20000;   // 200s ceiling: march there, then fight
        bool secondStarted = false;
        int secondTicks = 0;
        for (; secondTicks < marchAndFightTicks; secondTicks++)
        {
            observer.Step(dt);
            const bool anyActive = !world.GetBattles().empty();
            if (anyActive) secondStarted = true;
            if (secondStarted && !anyActive) break;
        }
        EXPECT_TRUE(secondStarted) << "rematch attack order never produced a Battle";
        EXPECT_LT(secondTicks, marchAndFightTicks) << "rematch never resolved";

        SoldierDivision* defAfterRematch = nullptr;
        for (auto& f : defenderPtr->forces) if (f != nullptr && f->id == 100) defAfterRematch = f.get();

        // The whole point: the crippled defender must NOT win the rematch. It
        // either broke again (cohesion still capped near its collapsed
        // ceiling) or was destroyed outright — either is an acceptable outcome
        // proving the retreat-regen-win cycle is gone. It must never come back
        // with meaningfully-recovered cohesion able to hold the line.
        if (defAfterRematch != nullptr)
            EXPECT_LT(defAfterRematch->cohesion, 10.0f)
                << "unsupplied defender should have broken again in the rematch, not held";
    }
    // else: the division died outright from equipment/starvation attrition
    // during the settle window — also a fully acceptable outcome (Z4): a
    // permanently cut-off division is not supposed to survive indefinitely.
}

// ─── Encirclement ("kocioł") ──────────────────────────────────────────────────
// Regression for the "stuck division" bug: a defeated division that keeps
// losing battles but has nowhere left to retreat (RetreatTargetTile's neighbour
// search finding no candidate better than the tile it's already on — IsTileFree
// excludes the mover itself) used to loop forever, never actually leaving. Per
// user decision: a division is destroyed outright ONLY when enemy (at-war)
// divisions hold ALL FOUR cardinal-adjacent quadrant cells (N/S/E/W of its own
// cell) — diagonal neighbours don't count, and RetreatTargetTile's retreat step
// is cardinal-only so a unit can't dodge this by slipping diagonally past a
// wall that only covers the cardinal approaches.
TEST(CombatTrace, FullyEncircledDivisionIsDestroyedOutright)
{
    GameWorld world;
    auto attacker = std::make_unique<Player>(0, world.GetTileMapForTesting());
    Player* attackerPtr = attacker.get();
    world.GetPlayerHandlerForTesting().players[0] = std::move(attacker);
    auto defender = std::make_unique<Player>(1, world.GetTileMapForTesting());
    Player* defenderPtr = defender.get();
    world.GetPlayerHandlerForTesting().players[1] = std::move(defender);

    FillGrass(world.GetTileMapForTesting(), defenderPtr, 30, 30);
    auto* atkHq = PlaceHomeHq(world, attackerPtr, {0, 0}, 1);
    auto* defHq = PlaceHomeHq(world, defenderPtr, {20, 20}, 2);
    ASSERT_NE(atkHq, nullptr);
    ASSERT_NE(defHq, nullptr);

    attackerPtr->diplomatic.DeclareWar(0, 1);
    defenderPtr->diplomatic.DeclareWar(0, 1);

    // Defender's lone (weak) division sits at the centre cell.
    const Vec2i centerTile{14, 14};
    const Vec2i centerCell = SectorCellOf(centerTile);

    auto defDiv = CreateMilitaryDivision(MilitaryUnitType::Militia, 100);
    defDiv->occupiedTile = centerTile;
    defDiv->sectorCell = centerCell;
    defDiv->garrisonBuildingId = defHq->positionId;
    defenderPtr->forces.push_back(std::move(defDiv));

    // Four attacker divisions occupy the four CARDINAL neighbour cells (N/S/E/W)
    // of the defender's cell. The WEST one sits one tile off centerTile (still
    // inside its own, different cell) so it is close enough to actually engage;
    // the other three only need to exist in their cell for the encirclement
    // check — they never need to fight.
    auto* attackerLeadPtr = [&]() -> SoldierDivision*
    {
        auto div = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 200);
        div->occupiedTile = {centerTile.x - 1, centerTile.y};   // west cell, adjacent tile
        div->sectorCell = SectorCellOf(div->occupiedTile);
        div->garrisonBuildingId = atkHq->positionId;
        SoldierDivision* raw = div.get();
        attackerPtr->forces.push_back(std::move(div));
        return raw;
    }();

    const Vec2i otherCardinalCells[3] = {
        {centerCell.x + 1, centerCell.y},   // east
        {centerCell.x, centerCell.y + 1},   // south
        {centerCell.x, centerCell.y - 1},   // north
    };
    int nextId = 201;
    for (Vec2i cell : otherCardinalCells)
    {
        auto div = CreateMilitaryDivision(MilitaryUnitType::Swordsman, nextId++);
        div->occupiedTile = {cell.x * 2, cell.y * 2};
        div->sectorCell = cell;
        div->garrisonBuildingId = atkHq->positionId;
        attackerPtr->forces.push_back(std::move(div));
    }

    attackerLeadPtr->currentOrder = MilitaryOrderType::Attack;
    attackerLeadPtr->orderTargetPositionId = world.GetTileMapForTesting().GetIdFromCoords(centerTile);

    const double dt = 1.0 / 100.0;
    const int maxTicks = 6000;
    bool battleStarted = false;
    int ticks = 0;
    for (; ticks < maxTicks; ticks++)
    {
        world.UpdateSimulation(dt);
        const bool anyActive = !world.GetBattles().empty();
        if (anyActive) battleStarted = true;
        if (battleStarted && !anyActive) break;
    }
    ASSERT_TRUE(battleStarted) << "attack order never produced a Battle";
    ASSERT_LT(ticks, maxTicks) << "battle never resolved";

    // DisengageDivision sets strength=0 the tick the battle resolves; the actual
    // cull (erase from forces) happens on the FOLLOWING tick's
    // UpdateDeployedDivisions pass — give it a few more ticks to run.
    for (int s = 0; s < 10; s++)
        world.UpdateSimulation(dt);

    bool defenderSurvives = false;
    for (const auto& f : defenderPtr->forces)
        if (f != nullptr && f->id == 100) defenderSurvives = true;
    EXPECT_FALSE(defenderSurvives)
        << "fully-encircled division (enemy on all 4 cardinal quadrants) "
           "should have been destroyed outright, not left retreating in place";
}

// Control: only THREE of the four cardinal neighbours are enemy-held (north is
// open) — the division must survive and actually retreat, not be destroyed.
TEST(CombatTrace, PartiallySurroundedDivisionRetreatsInsteadOfDying)
{
    GameWorld world;
    auto attacker = std::make_unique<Player>(0, world.GetTileMapForTesting());
    Player* attackerPtr = attacker.get();
    world.GetPlayerHandlerForTesting().players[0] = std::move(attacker);
    auto defender = std::make_unique<Player>(1, world.GetTileMapForTesting());
    Player* defenderPtr = defender.get();
    world.GetPlayerHandlerForTesting().players[1] = std::move(defender);

    FillGrass(world.GetTileMapForTesting(), defenderPtr, 30, 30);
    auto* atkHq = PlaceHomeHq(world, attackerPtr, {0, 0}, 1);
    auto* defHq = PlaceHomeHq(world, defenderPtr, {20, 20}, 2);
    ASSERT_NE(atkHq, nullptr);
    ASSERT_NE(defHq, nullptr);

    attackerPtr->diplomatic.DeclareWar(0, 1);
    defenderPtr->diplomatic.DeclareWar(0, 1);

    const Vec2i centerTile{14, 14};
    const Vec2i centerCell = SectorCellOf(centerTile);

    auto defDiv = CreateMilitaryDivision(MilitaryUnitType::Militia, 100);
    defDiv->occupiedTile = centerTile;
    defDiv->sectorCell = centerCell;
    defDiv->garrisonBuildingId = defHq->positionId;
    defenderPtr->forces.push_back(std::move(defDiv));

    auto* attackerLeadPtr = [&]() -> SoldierDivision*
    {
        auto div = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 200);
        div->occupiedTile = {centerTile.x - 1, centerTile.y};   // west
        div->sectorCell = SectorCellOf(div->occupiedTile);
        div->garrisonBuildingId = atkHq->positionId;
        SoldierDivision* raw = div.get();
        attackerPtr->forces.push_back(std::move(div));
        return raw;
    }();

    // Only east and south — north is left open, so this is NOT encirclement.
    const Vec2i otherCardinalCells[2] = {
        {centerCell.x + 1, centerCell.y},   // east
        {centerCell.x, centerCell.y + 1},   // south
    };
    int nextId = 201;
    for (Vec2i cell : otherCardinalCells)
    {
        auto div = CreateMilitaryDivision(MilitaryUnitType::Swordsman, nextId++);
        div->occupiedTile = {cell.x * 2, cell.y * 2};
        div->sectorCell = cell;
        div->garrisonBuildingId = atkHq->positionId;
        attackerPtr->forces.push_back(std::move(div));
    }

    attackerLeadPtr->currentOrder = MilitaryOrderType::Attack;
    attackerLeadPtr->orderTargetPositionId = world.GetTileMapForTesting().GetIdFromCoords(centerTile);

    const double dt = 1.0 / 100.0;
    const int maxTicks = 6000;
    bool battleStarted = false;
    int ticks = 0;
    for (; ticks < maxTicks; ticks++)
    {
        world.UpdateSimulation(dt);
        const bool anyActive = !world.GetBattles().empty();
        if (anyActive) battleStarted = true;
        if (battleStarted && !anyActive) break;
    }
    ASSERT_TRUE(battleStarted) << "attack order never produced a Battle";
    ASSERT_LT(ticks, maxTicks) << "battle never resolved";

    const SoldierDivision* defSurvivor = nullptr;
    for (const auto& f : defenderPtr->forces)
        if (f != nullptr && f->id == 100) defSurvivor = f.get();
    ASSERT_NE(defSurvivor, nullptr)
        << "a division with an open (north) cardinal side should retreat, not be destroyed";
    EXPECT_TRUE(defSurvivor->retreating);
}
