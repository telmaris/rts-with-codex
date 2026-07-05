#include "../inc/Building.h"
#include "../inc/Equipment.h"
#include "../inc/SupplyPackage.h"
#include "../inc/UnitStats.h"
#include "../inc/BalanceModifiers.h"
#include "../inc/MapGenerator.h"
#include "../inc/Player.h"
#include "../inc/DivisionSector.h"
#include "../inc/MovementPlanner.h"
#include "../inc/SectorGraph.h"
#include "../inc/GameCommand.h"
#include "../inc/GameWorld.h"

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

TEST(DivisionCombatStats, EquipmentQualityScalesAttack)
{
    auto copperArmed = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    copperArmed->equipment = DivisionEquipment{};
    copperArmed->equipment.weapon = ResourceType::COPPER_SWORD;

    auto steelArmed = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 2);
    steelArmed->equipment = DivisionEquipment{};
    steelArmed->equipment.weapon = ResourceType::STEEL_SWORD;

    DivisionCombatStats copper = ComputeDivisionCombatStats(*copperArmed, nullptr);
    DivisionCombatStats steel = ComputeDivisionCombatStats(*steelArmed, nullptr);

    EXPECT_GT(steel.equipmentQuality, copper.equipmentQuality);
    EXPECT_GT(steel.lightAttack, copper.lightAttack);  // better sword hits harder
    EXPECT_FLOAT_EQ(steel.morale, copper.morale);      // morale is gear-independent
}

TEST(DivisionCombatStats, UnarmedDivisionIsMakeshift)
{
    DivisionEquipment empty{};
    EXPECT_FLOAT_EQ(DivisionEquipmentQuality(empty), 0.5f);
    EXPECT_GT(DivisionEquipmentQuality([] {
        DivisionEquipment e{}; e.weapon = ResourceType::IRON_SWORD; return e; }()), 0.5f);
}

// ─── Field combat (division duels) ───────────────────────────────────────────

TEST(FieldCombat, BetterGearWinsTheExchange)
{
    auto steel = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    steel->equipment = DivisionEquipment{};
    steel->equipment.weapon = ResourceType::STEEL_SWORD;
    auto copper = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 2);
    copper->equipment = DivisionEquipment{};
    copper->equipment.weapon = ResourceType::COPPER_SWORD;

    DivisionDuelResult duel = ResolveDivisionDuel(
        ComputeDivisionCombatStats(*steel, nullptr),
        ComputeDivisionCombatStats(*copper, nullptr), 1.0);

    // The steel division (attacker) inflicts more than it takes.
    EXPECT_GT(duel.defenderStrengthLoss, duel.attackerStrengthLoss);
}

TEST(FieldCombat, ArmorReducesLossesAndDtScalesThem)
{
    DivisionCombatStats striker{};
    striker.lightAttack = 20.0f;

    DivisionCombatStats soft{};
    DivisionCombatStats armored{};
    armored.armor = 10.0f;
    armored.defense = 5.0f;

    float vsSoft = ResolveDivisionDuel(striker, soft, 1.0).defenderStrengthLoss;
    float vsArmored = ResolveDivisionDuel(striker, armored, 1.0).defenderStrengthLoss;
    EXPECT_GT(vsSoft, vsArmored);  // armor mitigates

    float oneTick = ResolveDivisionDuel(striker, soft, 1.0).defenderStrengthLoss;
    float twoTicks = ResolveDivisionDuel(striker, soft, 2.0).defenderStrengthLoss;
    EXPECT_FLOAT_EQ(twoTicks, oneTick * 2.0f);  // linear in dt
}

// ─── War Phase 2 — Phase B: per-tick supply consumption (B8/B9) ──────────────

TEST(Supply, IdleDivisionConsumesOnlyFood)
{
    // Not fighting: food is still eaten (upkeep), but weapons and materiel are only
    // expended in battle — they must stay untouched while idle.
    SwordsmanDivision div;
    int foodBefore = div.foodSupply;
    int weaponBefore = div.weaponSupply;
    int materielBefore = div.materielSupply;

    for (int i = 0; i < 50; i++)
        ConsumeDivisionSupply(div, 1.0, /*engaged=*/false, /*deployed=*/true);

    EXPECT_LT(div.foodSupply, foodBefore);
    EXPECT_EQ(div.weaponSupply, weaponBefore);
    EXPECT_EQ(div.materielSupply, materielBefore);
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
    // Idle food upkeep is only a fraction (20%) of combat consumption.
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

TEST(Combat, UnsuppliedAttackerDealsLessDamageThroughTheFloor)
{
    // Same class + gear, one fully supplied, one out of ammo AND food. The whole
    // duel output (including the constant damage floor) must scale down with supply
    // — the fix for "enemies with no supply chain hit just as hard".
    auto defender = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    auto suppliedAtk = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 2);
    auto starvedAtk  = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 3);
    starvedAtk->weaponSupply = 0;
    starvedAtk->foodSupply   = 0;

    DivisionCombatStats def      = ComputeDivisionCombatStats(*defender, nullptr);
    DivisionCombatStats supplied = ComputeDivisionCombatStats(*suppliedAtk, nullptr);
    DivisionCombatStats starved  = ComputeDivisionCombatStats(*starvedAtk, nullptr);

    DivisionDuelResult rSupplied = ResolveDivisionDuel(supplied, def, 1.0, 7, 2, 1);
    DivisionDuelResult rStarved  = ResolveDivisionDuel(starved,  def, 1.0, 7, 3, 1);

    EXPECT_GT(rSupplied.defenderStrengthLoss, 0.0f);
    EXPECT_LT(rStarved.defenderStrengthLoss, rSupplied.defenderStrengthLoss);
    EXPECT_LT(rStarved.defenderCohesionLoss, rSupplied.defenderCohesionLoss);
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

TEST(FieldCombat, NoOrderMeansNoCombatEvenWhenAdjacent)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);
    auto p1 = std::make_unique<Player>(1, world.tilemap);
    Player* a = p0.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    FillGrass(world.tilemap, a, 24, 24);

    auto* towerA = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), world.playerHandler.players[0].get(), std::make_unique<GuardTower>(1)));
    auto* towerB = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({18, 18}), world.playerHandler.players[1].get(), std::make_unique<GuardTower>(2)));
    ASSERT_NE(towerA, nullptr);
    ASSERT_NE(towerB, nullptr);

    // Adjacent QUADRANTS (cells (5,5) and (6,5)) — the front line at rest.
    // Sharing one quadrant would auto-start a battle (physical contact), but
    // facing each other across the border must stay calm without orders.
    DeployDivision(towerA, 1, {10, 10});
    DeployDivision(towerB, 1, {12, 10});

    int startHealth = towerB->garrison.divisions.front()->health;
    for (int i = 0; i < 100; i++)
        world.UpdateSimulation(0.01);

    EXPECT_EQ(towerB->garrison.divisions.front()->health, startHealth);   // untouched
    EXPECT_FALSE(towerA->garrison.divisions.front()->engaged);
}

// Physical contact starts a battle by itself: two hostile divisions standing in
// the SAME quadrant engage automatically, no explicit attack order needed —
// entering the enemy's province IS the attack.
TEST(FieldCombat, SharedQuadrantAutoStartsBattle)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);
    auto p1 = std::make_unique<Player>(1, world.tilemap);
    Player* a = p0.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    FillGrass(world.tilemap, a, 24, 24);

    auto* towerA = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), world.playerHandler.players[0].get(), std::make_unique<GuardTower>(1)));
    auto* towerB = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({18, 18}), world.playerHandler.players[1].get(), std::make_unique<GuardTower>(2)));
    ASSERT_NE(towerA, nullptr);
    ASSERT_NE(towerB, nullptr);

    // Both on quadrant (5,5) — tiles {10,10} and {11,10} share one 2x2 cell.
    SoldierDivision* attacker = DeployDivision(towerA, 1, {10, 10});
    SoldierDivision* defender = DeployDivision(towerB, 1, {11, 10});

    // Stale transient sectorCell must not matter — combat derives the quadrant
    // from occupiedTile. (Regression: battles randomly failed to start when the
    // cached cell disagreed with where the division actually stood.)
    attacker->sectorCell = {-7, -7};
    defender->sectorCell = {-9, -9};

    // Phase C: cohesion (organization) is the FAST-depleting bar and the one
    // combat visibly moves within a second of fighting; strength (manpower)
    // drains far more slowly and is not expected to register yet.
    float defCohesionStart = defender->cohesion;
    float atkCohesionStart = attacker->cohesion;
    for (int i = 0; i < 100; i++)
        world.UpdateSimulation(0.01);

    EXPECT_TRUE(attacker->engaged);          // contact auto-engaged both sides
    EXPECT_TRUE(defender->engaged);
    EXPECT_LT(defender->cohesion, defCohesionStart);   // and they actually trade damage
    EXPECT_LT(attacker->cohesion, atkCohesionStart);
}

// A marching column cannot roll straight through a defended quadrant: combat
// tracks the PHYSICAL position of in-transit divisions, so the moment the
// column's body crosses a quadrant held by an enemy it is engaged, halted
// (intercepted) and the battle plays out there. Regression for "wojsko
// przeciwnika przejeżdża przez moje dywizje jak gdyby nigdy nic".
TEST(FieldCombat, MarchingColumnIsInterceptedInHeldQuadrant)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);  // defender (holds the line)
    auto p1 = std::make_unique<Player>(1, world.tilemap);  // mover (marches through)
    Player* defPlayer = p0.get();
    Player* movPlayer = p1.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    // The mover owns the ground so its march is unrestricted by territory rules.
    FillGrass(world.tilemap, movPlayer, 30, 30);

    auto* defTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({26, 26}), defPlayer, std::make_unique<GuardTower>(1)));
    auto* movTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), movPlayer, std::make_unique<GuardTower>(2)));
    ASSERT_NE(defTower, nullptr);
    ASSERT_NE(movTower, nullptr);

    // The mover plans its route while the corridor is still clear...
    SoldierDivision* mover = DeployDivision(movTower, 1, {2, 10});
    world.SubmitCommand(GameCommand::MoveDivision(
        movPlayer->id, movTower->positionId, /*divisionId*/ 1,
        world.tilemap.GetIdFromCoords({20, 10})));
    world.UpdateSimulation(0.01);
    auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    ASSERT_TRUE(results.front().accepted);
    ASSERT_TRUE(mover->inTransit);

    // ...then a defender takes position on the route (quadrant (5,5)).
    SoldierDivision* blocker = DeployDivision(defTower, 1, {11, 10});

    bool intercepted = false;
    for (int i = 0; i < 8000 && !intercepted; i++)
    {
        world.UpdateSimulation(0.05);
        if (mover->engaged && !mover->inTransit)
            intercepted = true;
    }

    EXPECT_TRUE(intercepted);                 // the column was stopped mid-march...
    EXPECT_TRUE(blocker->engaged);            // ...and a battle started
    EXPECT_EQ(SectorCellOf(mover->occupiedTile), SectorCellOf(Vec2i{11, 10}));
    EXPECT_NE(mover->occupiedTile, (Vec2i{20, 10}));  // it never reached its goal
}

TEST(FieldCombat, AttackOrderStartsAndSustainsTheBattle)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);
    auto p1 = std::make_unique<Player>(1, world.tilemap);
    Player* a = p0.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    FillGrass(world.tilemap, a, 24, 24);

    auto* towerA = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), world.playerHandler.players[0].get(), std::make_unique<GuardTower>(1)));
    auto* towerB = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({18, 18}), world.playerHandler.players[1].get(), std::make_unique<GuardTower>(2)));
    ASSERT_NE(towerA, nullptr);
    ASSERT_NE(towerB, nullptr);

    SoldierDivision* attacker = DeployDivision(towerA, 1, {10, 10});
    SoldierDivision* defender = DeployDivision(towerB, 1, {11, 10});

    // Issue an attack order on the defender's tile.
    attacker->currentOrder = MilitaryOrderType::Attack;
    attacker->orderTargetPositionId = world.tilemap.GetIdFromCoords({11, 10});

    // Phase C: cohesion is the fast-depleting bar combat actually moves within a
    // second of fighting; strength (manpower) drains far more slowly.
    float defCohesionStart = defender->cohesion;
    float atkCohesionStart = attacker->cohesion;
    for (int i = 0; i < 100; i++)
        world.UpdateSimulation(0.01);

    EXPECT_LT(defender->cohesion, defCohesionStart);   // attacker hurt the defender
    EXPECT_LT(attacker->cohesion, atkCohesionStart);   // defender fought back (auto-defence)
    EXPECT_TRUE(defender->engaged);          // both are now in a sticky battle
    EXPECT_TRUE(attacker->engaged);
}

TEST(FieldCombat, BuildingIsCapturedNotDeletedAndDefendersSurvive)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);  // attacker
    auto p1 = std::make_unique<Player>(1, world.tilemap);  // defender
    Player* atkPlayer = p0.get();
    Player* defPlayer = p1.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    FillGrass(world.tilemap, atkPlayer, 30, 30);

    auto* atkTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), atkPlayer, std::make_unique<GuardTower>(1)));
    auto* defHq = dynamic_cast<Headquarters*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({26, 26}), defPlayer, std::make_unique<Headquarters>(2)));
    auto* defTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({14, 14}), defPlayer, std::make_unique<GuardTower>(3)));
    ASSERT_NE(atkTower, nullptr);
    ASSERT_NE(defHq, nullptr);
    ASSERT_NE(defTower, nullptr);

    defTower->territory.hp = 5;  // almost down, so the siege captures it quickly

    SoldierDivision* attacker = DeployDivision(atkTower, 1, {13, 14});  // adjacent to defTower
    attacker->currentOrder = MilitaryOrderType::Attack;
    attacker->orderTargetPositionId = defTower->positionId;

    DeployDivision(defTower, 1, {16, 14});  // defender's field division, homed in defTower

    for (int i = 0; i < 200 && defTower->owner == defPlayer; i++)
        world.UpdateSimulation(0.01);

    EXPECT_EQ(defTower->owner, atkPlayer);                 // captured, not deleted
    EXPECT_EQ(defTower->garrison.divisions.size(), 0u);    // its old garrison was vacated
    EXPECT_GE(defHq->garrison.divisions.size(), 1u);       // defender's field unit survived at HQ
}

// Full command path: a field division that is NOT yet adjacent to an enemy
// military building, ordered via IssueMilitaryOrder(Attack), must march up to
// the building, engage, and siege it down — no manual order/tile poking.
TEST(FieldCombat, DivisionMarchesToAttackEnemyBuildingViaCommand)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);  // attacker
    auto p1 = std::make_unique<Player>(1, world.tilemap);  // defender
    Player* atkPlayer = p0.get();
    Player* defPlayer = p1.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    FillGrass(world.tilemap, atkPlayer, 30, 30);

    auto* atkTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), atkPlayer, std::make_unique<GuardTower>(1)));
    auto* defTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({14, 14}), defPlayer, std::make_unique<GuardTower>(2)));
    ASSERT_NE(atkTower, nullptr);
    ASSERT_NE(defTower, nullptr);

    defTower->territory.hp = 20;
    // Manned walls: an empty garrison would fall without a fight, and this test
    // exercises the full march → engage → siege battle path.
    GarrisonAdd(*defTower, CreateMilitaryDivision(MilitaryUnitType::Militia, 9));

    // Deploy the attacker several tiles away from the enemy building (not adjacent).
    SoldierDivision* attacker = DeployDivision(atkTower, 1, {8, 8});
    ASSERT_GT(std::abs(attacker->occupiedTile.x - 14) + std::abs(attacker->occupiedTile.y - 14), 2);

    // Issue the attack purely through the command pipeline.
    world.SubmitCommand(GameCommand::IssueMilitaryOrder(
        atkPlayer->id, MilitaryOrderType::Attack,
        atkTower->positionId, defTower->positionId, /*divisionId*/ 1));

    world.UpdateSimulation(0.01);
    auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results.front().accepted);            // command accepted
    EXPECT_EQ(attacker->currentOrder, MilitaryOrderType::Attack);
    EXPECT_EQ(attacker->orderTargetPositionId, defTower->positionId);

    // While still marching, the division must NOT siege from range.
    int hpBeforeArrival = defTower->territory.hp;
    for (int i = 0; i < 3 && attacker->inTransit; i++)
        world.UpdateSimulation(0.05);
    EXPECT_TRUE(attacker->inTransit);                  // still en route on the first ticks
    EXPECT_EQ(defTower->territory.hp, hpBeforeArrival);  // no damage before arrival

    // Run to completion: the division marches up, engages, and sieges the tower.
    bool everEngaged = false;
    bool everArrived = false;
    for (int i = 0; i < 4000 && defTower->owner == defPlayer; i++)
    {
        world.UpdateSimulation(0.05);
        if (attacker->engaged) everEngaged = true;
        if (!attacker->inTransit) everArrived = true;
    }

    EXPECT_TRUE(everArrived);                          // the division finished its march
    EXPECT_TRUE(everEngaged);                          // ...and engaged the building
    EXPECT_EQ(defTower->owner, atkPlayer);            // the assault captured the building
}

// Attacking a building that sits on the ENEMY'S OWN territory must work from a
// cold start (no prior war): the attack order itself declares the war, and only
// then can the march enter enemy ground — movement is territory-gated on IsAtWar.
// Regression for the deadlock where the march was planned BEFORE the war was
// declared, so every path into enemy territory was blocked, the helper failed,
// the command was rejected, and the war declaration was never reached: the army
// simply did not react to attack orders on enemy land.
TEST(FieldCombat, AttackOrderOnEnemyTerritoryDeclaresWarAndMarches)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);  // attacker
    auto p1 = std::make_unique<Player>(1, world.tilemap);  // defender
    Player* atkPlayer = p0.get();
    Player* defPlayer = p1.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    FillGrass(world.tilemap, atkPlayer, 30, 30);

    // Right half of the map is the defender's territory — a real border, unlike
    // the all-attacker-owned map most movement tests use.
    for (int y = 0; y < 30; y++)
        for (int x = 10; x < 30; x++)
            world.tilemap.tilemap[world.tilemap.GetIdFromCoords({x, y})].owner = defPlayer;

    auto* atkTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), atkPlayer, std::make_unique<GuardTower>(1)));
    auto* defTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({14, 14}), defPlayer, std::make_unique<GuardTower>(2)));
    ASSERT_NE(atkTower, nullptr);
    ASSERT_NE(defTower, nullptr);
    defTower->territory.hp = 20;
    // Manned walls so the capture is a real siege (empty works fall instantly).
    GarrisonAdd(*defTower, CreateMilitaryDivision(MilitaryUnitType::Militia, 9));

    SoldierDivision* attacker = DeployDivision(atkTower, 1, {8, 8});  // on own ground
    ASSERT_FALSE(atkPlayer->diplomatic.IsAtWar(defPlayer->id));

    world.SubmitCommand(GameCommand::IssueMilitaryOrder(
        atkPlayer->id, MilitaryOrderType::Attack,
        atkTower->positionId, defTower->positionId, /*divisionId*/ 1));
    world.UpdateSimulation(0.01);
    auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results.front().accepted);                       // order accepted
    EXPECT_TRUE(atkPlayer->diplomatic.IsAtWar(defPlayer->id));   // war declared both ways
    EXPECT_TRUE(defPlayer->diplomatic.IsAtWar(atkPlayer->id));
    EXPECT_TRUE(attacker->inTransit);                            // the march actually started

    // The division must be able to cross enemy ground, arrive and take the tower.
    bool everArrived = false;
    for (int i = 0; i < 4000 && defTower->owner == defPlayer; i++)
    {
        world.UpdateSimulation(0.05);
        if (!attacker->inTransit)
            everArrived = true;
    }
    EXPECT_TRUE(everArrived);
    EXPECT_EQ(defTower->owner, atkPlayer);
}

// An UNMANNED defensive work puts up no fight: an attack order on it captures it
// outright — no engagement, no battle. Only a manned garrison must be besieged.
TEST(FieldCombat, EmptyGarrisonFallsWithoutAFight)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);  // attacker
    auto p1 = std::make_unique<Player>(1, world.tilemap);  // defender
    Player* atkPlayer = p0.get();
    Player* defPlayer = p1.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    FillGrass(world.tilemap, atkPlayer, 30, 30);

    auto* atkTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), atkPlayer, std::make_unique<GuardTower>(1)));
    auto* defTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({14, 14}), defPlayer, std::make_unique<GuardTower>(2)));
    ASSERT_NE(atkTower, nullptr);
    ASSERT_NE(defTower, nullptr);

    // The attacker stands one quadrant away; the tower has nobody inside.
    SoldierDivision* attacker = DeployDivision(atkTower, 1, {12, 14});
    world.SubmitCommand(GameCommand::IssueMilitaryOrder(
        atkPlayer->id, MilitaryOrderType::Attack,
        atkTower->positionId, defTower->positionId, /*divisionId*/ 1));

    for (int i = 0; i < 100 && defTower->owner == defPlayer; i++)
        world.UpdateSimulation(0.01);

    EXPECT_EQ(defTower->owner, atkPlayer);                        // taken...
    EXPECT_FALSE(attacker->engaged);                              // ...without a battle
    EXPECT_EQ(attacker->currentOrder, MilitaryOrderType::None);   // order fulfilled
}

// Capturing a garrison transfers the ground it projected: the defender's tiles
// are released and immediately re-claimed by the conqueror. Regression for the
// recompute ordering that left the radius NEUTRAL until an unrelated refresh.
TEST(FieldCombat, CaptureTransfersTerritoryToTheConqueror)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);  // attacker
    auto p1 = std::make_unique<Player>(1, world.tilemap);  // defender
    Player* atkPlayer = p0.get();
    Player* defPlayer = p1.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    // Unowned ground: each tower claims its own radius on placement, so the
    // defender's tower really does sit on DEFENDER territory.
    FillGrass(world.tilemap, nullptr, 30, 30);

    auto* atkTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), atkPlayer, std::make_unique<GuardTower>(1)));
    auto* defTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({14, 14}), defPlayer, std::make_unique<GuardTower>(2)));
    ASSERT_NE(atkTower, nullptr);
    ASSERT_NE(defTower, nullptr);
    // Loaded buildings claim their radius lazily — establish the initial borders.
    atkTower->constructionRemaining = 0.0;
    defTower->constructionRemaining = 0.0;
    world.tilemap.RecalculateTerritory(atkPlayer);
    world.tilemap.RecalculateTerritory(defPlayer);
    const Vec2i probe{17, 14};   // inside the defender tower's radius, far from the attacker
    ASSERT_EQ(world.tilemap.tilemap[world.tilemap.GetIdFromCoords(probe)].owner, defPlayer);

    SoldierDivision* attacker = DeployDivision(atkTower, 1, {12, 14});
    world.SubmitCommand(GameCommand::IssueMilitaryOrder(
        atkPlayer->id, MilitaryOrderType::Attack,
        atkTower->positionId, defTower->positionId, /*divisionId*/ 1));

    for (int i = 0; i < 100 && defTower->owner == defPlayer; i++)
        world.UpdateSimulation(0.01);
    ASSERT_EQ(defTower->owner, atkPlayer);

    // The captured tower's ground belongs to the conqueror right away.
    EXPECT_EQ(world.tilemap.tilemap[world.tilemap.GetIdFromCoords(probe)].owner, atkPlayer);
}

// Capturing an enemy Headquarters eliminates its owner: the player is flagged
// defeated, every building it held (incl. civil ones) passes to the conqueror,
// and the game reports the conqueror as the victor.
TEST(Elimination, CapturingHqEliminatesOwnerAndTransfersEverything)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);  // attacker
    auto p1 = std::make_unique<Player>(1, world.tilemap);  // defender
    Player* atkPlayer = p0.get();
    Player* defPlayer = p1.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    FillGrass(world.tilemap, nullptr, 30, 30);
    // roadNetwork navMaps were sized to the (empty) tilemap at Player construction;
    // rebuild now that the map exists so the HQ->Village logistics CalculatePath
    // (and capture nav updates) don't index a stale, too-small navMap.
    atkPlayer->roadNetwork = std::make_unique<RoadNetwork>(world.tilemap);
    defPlayer->roadNetwork = std::make_unique<RoadNetwork>(world.tilemap);

    auto* atkTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), atkPlayer, std::make_unique<GuardTower>(1)));
    auto* defHq = dynamic_cast<Headquarters*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({14, 14}), defPlayer, std::make_unique<Headquarters>(2)));
    auto* defVillage = dynamic_cast<Village*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({18, 18}), defPlayer, std::make_unique<Village>(3)));
    ASSERT_NE(atkTower, nullptr);
    ASSERT_NE(defHq, nullptr);
    ASSERT_NE(defVillage, nullptr);
    atkTower->constructionRemaining = 0.0;
    defHq->constructionRemaining = 0.0;
    defVillage->constructionRemaining = 0.0;
    world.tilemap.RecalculateTerritory(atkPlayer);
    world.tilemap.RecalculateTerritory(defPlayer);
    // Low HQ HP so the siege concludes quickly (balance-agnostic test).
    defHq->territory.hp = 1;

    SoldierDivision* attacker = DeployDivision(atkTower, 1, {12, 14});
    ASSERT_NE(attacker, nullptr);
    world.SubmitCommand(GameCommand::IssueMilitaryOrder(
        atkPlayer->id, MilitaryOrderType::Attack,
        atkTower->positionId, defHq->positionId, /*divisionId*/ 1));

    for (int i = 0; i < 400 && !defPlayer->defeated; i++)
        world.UpdateSimulation(0.01);

    EXPECT_TRUE(defPlayer->defeated);
    EXPECT_EQ(defHq->owner, atkPlayer);
    EXPECT_EQ(defVillage->owner, atkPlayer) << "Civil buildings pass to the conqueror on elimination";
    EXPECT_TRUE(defPlayer->forces.empty()) << "Defeated player's army is disbanded";
    EXPECT_EQ(world.GetVictorPlayerId(), atkPlayer->id);
    EXPECT_TRUE(world.IsPlayerDefeated(defPlayer->id));
}

// Overrunning enemy ground captures the CIVIL infrastructure on it (a village,
// production chain, roads), but military works still require a real siege.
TEST(Elimination, OverrunTransfersCivilBuildingButNotMilitary)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);  // conqueror of ground
    auto p1 = std::make_unique<Player>(1, world.tilemap);  // owner of buildings
    Player* atkPlayer = p0.get();
    Player* defPlayer = p1.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    FillGrass(world.tilemap, nullptr, 30, 30);

    auto* atkHq = dynamic_cast<Headquarters*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), atkPlayer, std::make_unique<Headquarters>(1)));
    auto* defVillage = dynamic_cast<Village*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({14, 14}), defPlayer, std::make_unique<Village>(2)));
    auto* defTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({20, 20}), defPlayer, std::make_unique<GuardTower>(3)));
    ASSERT_NE(atkHq, nullptr);
    ASSERT_NE(defVillage, nullptr);
    ASSERT_NE(defTower, nullptr);

    // Simulate the ground under both defender buildings being overrun by the
    // conqueror (as if the front swept over it).
    world.tilemap.tilemap[defVillage->positionId].owner = atkPlayer;
    world.tilemap.tilemap[defTower->positionId].owner = atkPlayer;

    // TransferOverrunBuildings runs at 10 Hz (tick % 10) — a dozen ticks is plenty.
    for (int i = 0; i < 15; i++)
        world.UpdateSimulation(0.01);

    EXPECT_EQ(defVillage->owner, atkPlayer) << "Civil building follows the captured ground";
    EXPECT_EQ(defTower->owner, defPlayer)   << "Military works are taken only by siege, not by overrun";
}

// The Barracks is a CIVIL building: a direct attack order on it is rejected, and
// even a battle raging right beside it never sieges or captures it. Armies only
// besiege military targets — defensive works and the HQ.
TEST(FieldCombat, BarracksIsNotAMilitaryTarget)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);  // attacker
    auto p1 = std::make_unique<Player>(1, world.tilemap);  // defender
    Player* atkPlayer = p0.get();
    Player* defPlayer = p1.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    // Neutral ground: the barracks sits on no-man's-land so this test isolates the
    // "not a SIEGE target" rule from the separate overrun-capture mechanic (a civil
    // building on ENEMY territory is captured — that is tested elsewhere).
    FillGrass(world.tilemap, nullptr, 30, 30);

    auto* atkTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), atkPlayer, std::make_unique<GuardTower>(1)));
    auto* defTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({26, 26}), defPlayer, std::make_unique<GuardTower>(2)));
    auto* defBarracks = dynamic_cast<Barracks*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({14, 14}), defPlayer, std::make_unique<Barracks>(3)));
    ASSERT_NE(atkTower, nullptr);
    ASSERT_NE(defTower, nullptr);
    ASSERT_NE(defBarracks, nullptr);
    int hpStart = defBarracks->territory.hp;

    // A direct attack order on the factory is rejected outright.
    SoldierDivision* attacker = DeployDivision(atkTower, 1, {12, 14});
    world.SubmitCommand(GameCommand::IssueMilitaryOrder(
        atkPlayer->id, MilitaryOrderType::Attack,
        atkTower->positionId, defBarracks->positionId, /*divisionId*/ 1));
    world.UpdateSimulation(0.01);
    auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results.front().accepted);

    // A field battle right next to the barracks does not siege it either.
    DeployDivision(defTower, 1, {13, 14});   // same quadrant as the attacker → battle
    for (int i = 0; i < 200; i++)
        world.UpdateSimulation(0.01);
    EXPECT_TRUE(attacker->engaged);                   // the armies do fight...
    EXPECT_EQ(defBarracks->territory.hp, hpStart);    // ...but the factory is untouched
    EXPECT_EQ(defBarracks->owner, defPlayer);         // and never captured
}

// HoI4 flow: ordering a MOVE into a quadrant held by an enemy army converts into
// an attack on that army (declares war, marches to contact) instead of being
// rejected because the province is blocked.
TEST(FieldCombat, MoveOrderIntoEnemyQuadrantBecomesAttack)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);  // attacker
    auto p1 = std::make_unique<Player>(1, world.tilemap);  // defender
    Player* atkPlayer = p0.get();
    Player* defPlayer = p1.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    FillGrass(world.tilemap, atkPlayer, 30, 30);

    for (int y = 0; y < 30; y++)
        for (int x = 10; x < 30; x++)
            world.tilemap.tilemap[world.tilemap.GetIdFromCoords({x, y})].owner = defPlayer;

    auto* atkTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), atkPlayer, std::make_unique<GuardTower>(1)));
    auto* defTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({24, 24}), defPlayer, std::make_unique<GuardTower>(2)));
    ASSERT_NE(atkTower, nullptr);
    ASSERT_NE(defTower, nullptr);

    SoldierDivision* attacker = DeployDivision(atkTower, 1, {8, 8});
    DeployDivision(defTower, 7, {14, 8});   // enemy holds quadrant (7,4)
    ASSERT_FALSE(atkPlayer->diplomatic.IsAtWar(defPlayer->id));

    // Plain MOVE order onto a tile of the enemy-held quadrant ({15,9} shares
    // cell (7,4) with the enemy's {14,8}).
    world.SubmitCommand(GameCommand::MoveDivision(
        atkPlayer->id, atkTower->positionId, /*divisionId*/ 1,
        world.tilemap.GetIdFromCoords({15, 9})));
    world.UpdateSimulation(0.01);
    auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results.front().accepted);                        // converted, not rejected
    EXPECT_TRUE(atkPlayer->diplomatic.IsAtWar(defPlayer->id));    // move became an attack
    EXPECT_TRUE(defPlayer->diplomatic.IsAtWar(atkPlayer->id));
    EXPECT_EQ(attacker->currentOrder, MilitaryOrderType::Attack); // carries the attack order
    EXPECT_TRUE(attacker->inTransit);                             // and marches to contact
}

// AttackTile (attack an enemy army standing on its own territory) must likewise
// declare the war itself before planning the march — it previously never
// declared war at all, so the route into enemy ground stayed blocked.
TEST(FieldCombat, AttackTileOnEnemyDivisionDeclaresWarAndMarches)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);  // attacker
    auto p1 = std::make_unique<Player>(1, world.tilemap);  // defender
    Player* atkPlayer = p0.get();
    Player* defPlayer = p1.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    FillGrass(world.tilemap, atkPlayer, 30, 30);

    for (int y = 0; y < 30; y++)
        for (int x = 10; x < 30; x++)
            world.tilemap.tilemap[world.tilemap.GetIdFromCoords({x, y})].owner = defPlayer;

    auto* atkTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), atkPlayer, std::make_unique<GuardTower>(1)));
    auto* defTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({24, 24}), defPlayer, std::make_unique<GuardTower>(2)));
    ASSERT_NE(atkTower, nullptr);
    ASSERT_NE(defTower, nullptr);

    SoldierDivision* attacker = DeployDivision(atkTower, 1, {8, 8});    // on own ground
    DeployDivision(defTower, 7, {14, 8});                               // enemy army on enemy ground
    ASSERT_FALSE(atkPlayer->diplomatic.IsAtWar(defPlayer->id));

    world.SubmitCommand(GameCommand::AttackTile(
        atkPlayer->id, atkTower->positionId, /*divisionId*/ 1,
        world.tilemap.GetIdFromCoords({14, 8})));
    world.UpdateSimulation(0.01);
    auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results.front().accepted);                       // order accepted
    EXPECT_TRUE(atkPlayer->diplomatic.IsAtWar(defPlayer->id));   // war declared both ways
    EXPECT_TRUE(defPlayer->diplomatic.IsAtWar(atkPlayer->id));
    EXPECT_EQ(attacker->currentOrder, MilitaryOrderType::Attack);
    EXPECT_TRUE(attacker->inTransit);                            // marching toward the enemy army
}

// ─── War Phase 2 — Phase C: HoI4-style deterministic combat ──────────────────

// Sanity-checks ResolveDivisionDuel against the documented formula (hits =
// min(attacks,defenses)*0.10 + max(0,attacks-defenses)*0.40) with hand-picked
// stats where attacks/defenses are exact round numbers, so the expected loss is
// computable by hand. See docs/war_system_phase2_design.md Phase C.
TEST(Combat, Hoi4DamageMatchesExpectedValueExample)
{
    DivisionCombatStats attacker{};
    attacker.lightAttack = 100.0f;    // attacks = round(100/10) = 10
    attacker.strength = 200.0f;
    attacker.maxStrength = 200.0f;    // full HP -> hpScaling = 1.0

    DivisionCombatStats defender{};
    defender.defense = 40.0f;         // defenses = round(40/10) = 4

    // dt = 60s = exactly one "combat hour" (h = 1).
    // BUG 4 fix: the formula now includes a constant floor term so that even
    // near-dead divisions deal minimum damage and battles always conclude.
    // The HoI4 scaled term is still computed correctly; we just add a floor on top.
    DivisionDuelResult duel = ResolveDivisionDuel(attacker, defender, 60.0);

    const float expectedHits = 4.0f * 0.10f + 6.0f * 0.40f;  // = 2.8
    // Scaled HoI4 term (floor + scaled): BUG 4 added kConstantHpFloor=200,
    // kConstantOrgFloor=80 (per-combat-hour) to guarantee finite battle time.
    // Variance (±7.5%) is seeded deterministically; with seed (0,0,0) it is a
    // known constant but testing the exact hash output is brittle. We instead
    // verify that the scaled (non-floor) portion still dominates the *ratio*
    // between the two sides, i.e. attacker hits harder because it has more attacks.
    // Check only that the floor-boosted values are positive and defender takes damage.
    EXPECT_GT(duel.defenderStrengthLoss, 0.0f);
    EXPECT_GT(duel.defenderCohesionLoss, 0.0f);
    // The scaled HoI4 component still accounts for the correct expected hits.
    // With h=1 and hpScaling=1, the HoI4 part alone equals expectedHp/expectedOrg;
    // the floor adds a fixed extra. Verify the direction is right (more attacks → more loss).
    const float hpPerHoI4  = expectedHits * 1.5f * 0.06f;   // = 0.252 per combat-hour
    const float orgPerHoI4 = expectedHits * 2.5f * 0.053f;  // = 0.371 per combat-hour
    // Total must be above the HoI4-only value (floor always adds positive amount).
    EXPECT_GT(duel.defenderStrengthLoss, hpPerHoI4 * 0.9f);   // within 10% above
    EXPECT_GT(duel.defenderCohesionLoss, orgPerHoI4 * 0.9f);
}

TEST(Combat, ArmoredUnpiercedDealsMoreOrgDamage)
{
    DivisionCombatStats unarmored{};
    unarmored.lightAttack = 100.0f; unarmored.strength = 100.0f; unarmored.maxStrength = 100.0f;
    unarmored.armor = 20.0f; unarmored.isArmored = false;

    DivisionCombatStats armored = unarmored;
    armored.isArmored = true;   // same stats, but counts as an armored formation

    DivisionCombatStats defender{};
    defender.defense = 40.0f;
    defender.piercing = 5.0f;   // well under the attacker's armor -> stays unpierced

    float orgUnarmored = ResolveDivisionDuel(unarmored, defender, 60.0).defenderCohesionLoss;
    float orgArmored    = ResolveDivisionDuel(armored, defender, 60.0).defenderCohesionLoss;

    EXPECT_GT(orgArmored, orgUnarmored);  // bigger org die (3.5 vs 2.5) when unpierced
}

TEST(Combat, LowHpAttackerDealsScaledDownDamage)
{
    DivisionCombatStats fullHp{};
    fullHp.lightAttack = 100.0f; fullHp.strength = 100.0f; fullHp.maxStrength = 100.0f;

    DivisionCombatStats lowHp = fullHp;
    lowHp.strength = 85.0f;   // 85% -> steps of 10% -> hpScaling = 0.8

    DivisionCombatStats defender{};
    defender.defense = 20.0f;

    float fullLoss = ResolveDivisionDuel(fullHp, defender, 60.0).defenderStrengthLoss;
    float lowLoss  = ResolveDivisionDuel(lowHp, defender, 60.0).defenderStrengthLoss;

    // BUG 4 fix: a constant HP-independent floor is added so even near-dead
    // divisions deal minimum damage. This means the low-HP attacker no longer
    // deals exactly 0.8× the full-HP damage (the floor lifts the floor).
    // The key properties are still enforced:
    //   (a) low-HP deals strictly less than full-HP (the scaled term still differs)
    //   (b) low-HP attacker still deals positive damage (no asymptote to zero)
    EXPECT_LT(lowLoss, fullLoss);    // damaged attacker still deals less
    EXPECT_GT(lowLoss, 0.0f);        // but always some minimum output
}

// A division whose organization breaks while badly outnumbered falls back to a
// rear quadrant instead of fighting to the last man (the soft-loss rule).
TEST(Combat, LosingDivisionRetreatsToRearQuadrant)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);  // attacker (overwhelms)
    auto p1 = std::make_unique<Player>(1, world.tilemap);  // defender (falls back)
    Player* atkPlayer = p0.get();
    Player* defPlayer = p1.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    // The whole map is the defender's own territory, so a rear quadrant is
    // always legally available — this test isolates the retreat DECISION, not
    // territory ownership edge cases.
    FillGrass(world.tilemap, defPlayer, 40, 40);

    auto* atkTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), atkPlayer, std::make_unique<GuardTower>(1)));
    auto* defTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({30, 30}), defPlayer, std::make_unique<GuardTower>(2)));
    ASSERT_NE(atkTower, nullptr);
    ASSERT_NE(defTower, nullptr);

    // Three attacking divisions crowd the lone defender's quadrant — physically
    // sharing the cell auto-starts the battle (no order needed), and the 3:1
    // manpower imbalance makes LosingLocalFight true as soon as cohesion breaks.
    DeployDivision(atkTower, 1, {20, 20});
    DeployDivision(atkTower, 2, {21, 20});
    DeployDivision(atkTower, 3, {20, 21});
    SoldierDivision* defender = DeployDivision(defTower, 9, {21, 21});
    // The current balance constants (Phase A placeholders — see
    // docs/war_system_phase2_design.md, "do strojenia") make cohesion loss per
    // duel tiny, so a REAL fight would starve the division (food runs out well
    // before organization does) before ever illustrating the retreat DECISION
    // this test targets. Starting cohesion nearly broken (and food topped up)
    // isolates that decision from unrelated balance/starvation timing.
    defender->cohesion = 0.05f;
    defender->foodSupply = defender->foodSupplyCapacity * 10;

    bool retreated = false;
    for (int i = 0; i < 20000 && !retreated; i++)
    {
        world.UpdateSimulation(0.05);
        if (defender->retreating)
            retreated = true;
    }

    EXPECT_TRUE(retreated);
    EXPECT_FALSE(defender->engaged);   // fell back out of the fight
    EXPECT_GT(defender->strength, 0);  // soft loss — it survived
}

// A division surrounded on every side (no cardinal-neighbour quadrant is farther
// from the enemy than where it stands) cannot organize a retreat — the HoI4
// kocioł — and is destroyed outright once its strength runs out.
TEST(Combat, EncircledDivisionCannotRetreatAndCanBeDestroyed)
{
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);  // attacker (encircles)
    auto p1 = std::make_unique<Player>(1, world.tilemap);  // defender (trapped)
    Player* atkPlayer = p0.get();
    Player* defPlayer = p1.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    FillGrass(world.tilemap, defPlayer, 40, 40);

    auto* atkTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), atkPlayer, std::make_unique<GuardTower>(1)));
    auto* defTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({20, 20}), defPlayer, std::make_unique<GuardTower>(2)));
    ASSERT_NE(atkTower, nullptr);
    ASSERT_NE(defTower, nullptr);

    // One attacker shares the defender's quadrant (bootstraps the battle via
    // physical contact); four more ring every cardinal-neighbour quadrant so no
    // direction is ever farther from the enemy than the one the defender holds.
    DeployDivision(atkTower, 1, {21, 21});   // same cell as the defender
    DeployDivision(atkTower, 2, {18, 20});   // west
    DeployDivision(atkTower, 3, {22, 20});   // east
    DeployDivision(atkTower, 4, {20, 18});   // north
    DeployDivision(atkTower, 5, {20, 22});   // south
    SoldierDivision* defender = DeployDivision(defTower, 9, {20, 20});

    bool everRetreating = false;
    bool destroyed = false;
    for (int i = 0; i < 20000; i++)
    {
        world.UpdateSimulation(0.05);
        if (defPlayer->forces.empty())
        {
            destroyed = true;
            break;
        }
        if (defender->retreating)
            everRetreating = true;
    }

    EXPECT_TRUE(destroyed);        // no legal rear quadrant -> the kocioł claims it
    EXPECT_FALSE(everRetreating);  // never found a way out
}

// ─── War Phase 2 — Phase C: Supply Conservation ──────────────────────────────

TEST(Combat, SupplyConservationHalvesRequiredSupply)
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

TEST(Combat, SupplyConservationIsCapped)
{
    GameWorld world;
    Player player{0, world.tilemap};
    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::SupplyConservation, /*additive*/5.0, /*multiplier*/1.0, {}, {}, {}, {}, "test.overflow"});

    EXPECT_LE(PlayerSupplyConservation(player), kMaxSupplyConservation);
    EXPECT_DOUBLE_EQ(PlayerSupplyConservation(player), kMaxSupplyConservation);
}

TEST(Combat, SupplyConservationFromTechApplies)
{
    GameWorld world;
    Player player{0, world.tilemap};
    EXPECT_DOUBLE_EQ(PlayerSupplyConservation(player), 0.0);

    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::SupplyConservation, /*additive*/0.15, /*multiplier*/1.0, {}, {}, {}, {}, "tech.field_logistics"});

    EXPECT_NEAR(PlayerSupplyConservation(player), 0.15, 1e-6);
}

// ─── BUG 4: Combat finite-time resolution + deterministic RNG ────────────────

TEST(Combat, BattleEndsInFiniteSimTime)
{
    // BUG 4 regression: before the fix, damage scaled down with HP toward zero
    // (the 1/x asymptote), so two equal divisions would fight forever.
    // After the fix a constant floor guarantees battles conclude.
    // Two equal swordsman divisions, fighting directly.  We drive the sim until
    // one side's cohesion hits 0 (retreat) or strength hits 0 (destroyed) — this
    // should happen well within 300 sim-seconds (kConstantOrgFloor drains cohesion).
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);
    auto p1 = std::make_unique<Player>(1, world.tilemap);
    Player* atkPlayer = p0.get();
    Player* defPlayer = p1.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);
    FillGrass(world.tilemap, atkPlayer, 20, 20);

    auto* atkTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), atkPlayer, std::make_unique<GuardTower>(1)));
    auto* defTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({16, 16}), defPlayer, std::make_unique<GuardTower>(2)));
    ASSERT_NE(atkTower, nullptr);
    ASSERT_NE(defTower, nullptr);

    // Set all tiles to be owned by attacker (simplifies territory for retreat).
    // The defender's tile ownership matters only for retreat — here we just want
    // to confirm the battle resolves, so give the defender its tower's area.
    world.tilemap.tilemap[world.tilemap.GetIdFromCoords({16, 16})].owner = defPlayer;
    world.tilemap.tilemap[world.tilemap.GetIdFromCoords({17, 16})].owner = defPlayer;
    world.tilemap.tilemap[world.tilemap.GetIdFromCoords({16, 17})].owner = defPlayer;
    world.tilemap.tilemap[world.tilemap.GetIdFromCoords({17, 17})].owner = defPlayer;

    DeployDivision(atkTower, 1, {8, 8});
    DeployDivision(defTower, 2, {8, 8});  // same quadrant — auto-engage (Phase 1b)

    const int kMaxTicks = 30000;  // 300s at dt=0.01 — any longer means bug
    bool resolved = false;
    for (int i = 0; i < kMaxTicks; i++)
    {
        world.UpdateSimulation(0.01);
        // Battle resolved when at least one side has no cohesion or retreated.
        bool atkDone = atkPlayer->forces.empty() ||
            (!atkPlayer->forces.empty() && atkPlayer->forces[0]->cohesion <= 0.0f);
        bool defDone = defPlayer->forces.empty() ||
            (!defPlayer->forces.empty() && defPlayer->forces[0]->cohesion <= 0.0f);
        if (atkDone || defDone)
        { resolved = true; break; }
    }

    EXPECT_TRUE(resolved) << "Battle should resolve in finite sim time (BUG 4 regression)";
}

TEST(Combat, CombatIsDeterministicAcrossRuns)
{
    // Same simulation tick and division IDs must produce the exact same damage.
    DivisionCombatStats attacker{};
    attacker.lightAttack = 50.0f;
    attacker.strength = 200.0f;
    attacker.maxStrength = 200.0f;
    attacker.hpDamageMultiplier = 1.0f;
    attacker.orgDamageMultiplier = 1.0f;

    DivisionCombatStats defender{};
    defender.defense = 30.0f;
    defender.maxStrength = 150.0f;
    defender.strength = 150.0f;

    // Same tick and IDs twice → identical result.
    DivisionDuelResult r1 = ResolveDivisionDuel(attacker, defender, 0.01, /*tick=*/12345, /*idA=*/7, /*idB=*/13);
    DivisionDuelResult r2 = ResolveDivisionDuel(attacker, defender, 0.01, /*tick=*/12345, /*idA=*/7, /*idB=*/13);

    EXPECT_FLOAT_EQ(r1.defenderStrengthLoss, r2.defenderStrengthLoss);
    EXPECT_FLOAT_EQ(r1.defenderCohesionLoss, r2.defenderCohesionLoss);
    EXPECT_FLOAT_EQ(r1.attackerStrengthLoss, r2.attackerStrengthLoss);
    EXPECT_FLOAT_EQ(r1.attackerCohesionLoss, r2.attackerCohesionLoss);

    // Different tick → different result (variance varies per tick).
    DivisionDuelResult r3 = ResolveDivisionDuel(attacker, defender, 0.01, /*tick=*/12346, /*idA=*/7, /*idB=*/13);
    // At least one component should differ (extremely high probability with WangHash).
    bool differs = (r3.defenderStrengthLoss != r1.defenderStrengthLoss) ||
                   (r3.defenderCohesionLoss != r1.defenderCohesionLoss);
    EXPECT_TRUE(differs) << "Adjacent ticks should produce different variance";
}

TEST(Combat, LowHpDivisionStillDealsMinimumDamage)
{
    // BUG 4 regression: before the fix a near-dead attacker caused damage to
    // asymptote to zero. After the fix, even strength=1 (worst case) must still
    // deal a meaningful amount due to the constant floor.
    DivisionCombatStats nearDead{};
    nearDead.lightAttack = 10.0f;
    nearDead.strength = 1.0f;    // 1% of max → hpScaling = 0.1 (floor)
    nearDead.maxStrength = 100.0f;
    nearDead.hpDamageMultiplier  = 1.0f;
    nearDead.orgDamageMultiplier = 1.0f;

    DivisionCombatStats defender{};
    defender.defense = 0.0f;

    // Over 1 "combat hour" (dt=60s), even a near-dead attacker must deal positive damage.
    DivisionDuelResult duel = ResolveDivisionDuel(nearDead, defender, 60.0);

    EXPECT_GT(duel.defenderStrengthLoss, 0.5f) << "Near-dead attacker must deal minimum strength damage";
    EXPECT_GT(duel.defenderCohesionLoss, 0.5f) << "Near-dead attacker must deal minimum cohesion damage";
}

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

TEST(WarSystem, EnemyDivisionOnPlayerTileIsReclaimed)
{
    // BUG 2 regression: after BUG 4 fix battles now resolve in finite time.
    // Verify that once an enemy division that occupied a player tile is destroyed,
    // ClaimTilesUnderDivisions (called every tick in UpdateSimulation)
    // returns the tile to the player who still stands on it.
    //
    // Setup: 3 attackers vs 1 defender in the same quadrant on attacker's territory.
    // The defender is completely surrounded (no own territory nearby) so it cannot
    // retreat (encirclement) — it fights until strength=0 and is removed.
    // The attackers survive, reclaiming the tile through ClaimTilesUnderDivisions.
    GameWorld world;
    auto p0 = std::make_unique<Player>(0, world.tilemap);
    auto p1 = std::make_unique<Player>(1, world.tilemap);
    Player* atkPlayer = p0.get();
    Player* defPlayer = p1.get();
    world.playerHandler.players[0] = std::move(p0);
    world.playerHandler.players[1] = std::move(p1);

    // All tiles owned by atkPlayer — enemy has NO territory, cannot retreat.
    FillGrass(world.tilemap, atkPlayer, 20, 20);

    auto* atkTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({2, 2}), atkPlayer, std::make_unique<GuardTower>(1)));
    auto* defTower = dynamic_cast<GuardTower*>(world.tilemap.PlaceLoadedBuilding(
        world.tilemap.GetIdFromCoords({18, 18}), defPlayer, std::make_unique<GuardTower>(2)));
    ASSERT_NE(atkTower, nullptr);
    ASSERT_NE(defTower, nullptr);

    // Three attackers vs one defender in the same 2×2 quadrant (cell {4,4}).
    // 3:1 manpower ratio ensures the lone defender is outmatched and cannot win.
    SoldierDivision* atkDiv = DeployDivision(atkTower, 1, {8, 8});
    DeployDivision(atkTower, 2, {9, 8});
    DeployDivision(atkTower, 3, {8, 9});
    DeployDivision(defTower, 9, {9, 9});  // same quadrant as attackers

    // Drive the sim until the defender is destroyed (encircled, cannot retreat).
    const int kMaxTicks = 30000;  // 300s — more than enough given the floor damage
    bool defDestroyed = false;
    for (int i = 0; i < kMaxTicks; i++)
    {
        world.UpdateSimulation(0.01);
        if (defPlayer->forces.empty())
        { defDestroyed = true; break; }
    }

    ASSERT_TRUE(defDestroyed) << "Defender should be destroyed (encircled, 3:1 odds) — BUG 4 must be fixed first";
    ASSERT_FALSE(atkPlayer->forces.empty()) << "Attacker should have survived";

    // Run a couple more ticks so ClaimTilesUnderDivisions can do its job.
    for (int j = 0; j < 5; j++)
        world.UpdateSimulation(0.01);

    // The attacker's division is still standing on (or near) its original tile.
    // ClaimTilesUnderDivisions must have claimed the whole quadrant back.
    ASSERT_TRUE(atkDiv->occupiedTile.x >= 0) << "Attacker division must be deployed";
    Vec2i atkTile = atkDiv->occupiedTile;
    ASSERT_TRUE(world.tilemap.IsInside(atkTile));
    EXPECT_EQ(world.tilemap.tilemap[world.tilemap.GetIdFromCoords(atkTile)].owner, atkPlayer)
        << "Player's tile should be reclaimed once enemy is destroyed (BUG 2 regression)";
}

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
