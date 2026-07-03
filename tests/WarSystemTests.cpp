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
#include <map>
#include <memory>
#include <set>
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
    pkg.Add(ResourceType::IRON_SWORD, 10);
    pkg.Add(ResourceType::IRON_ARMOR, 10);
    pkg.rations = 10;
    pkg.soldierCapacity = 10;

    ASSERT_TRUE(ApplyPackageToMilitary(pkg, tower));

    const SoldierDivision& equipped = *tower.garrison.divisions.front();
    EXPECT_EQ(equipped.equipment.weapon, ResourceType::IRON_SWORD);  // upgraded
    EXPECT_EQ(equipped.equipment.armor, ResourceType::IRON_ARMOR);
    EXPECT_EQ(equipped.weaponSupply, equipped.weaponSupplyCapacity);
    EXPECT_EQ(equipped.foodSupply, equipped.foodSupplyCapacity);
    EXPECT_LT(pkg.rations, 10);  // rations consumed into the food buffer
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
    ASSERT_NE(depot, nullptr);
    ASSERT_NE(hub, nullptr);

    depot->storage.buffers[ResourceType::COPPER_SWORD].SetStoredAmount(30);  // worse
    depot->storage.buffers[ResourceType::STEEL_SWORD].SetStoredAmount(10);   // best
    depot->storage.buffers[ResourceType::ARROWS].SetStoredAmount(30);
    depot->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(100);

    ASSERT_TRUE(hub->packaging.AssemblePackage(*hub));

    const SupplyPackage& pkg = hub->packaging.readyPackages.front();
    EXPECT_EQ(pkg.BestOfCategory(EquipmentCategory::Sword), ResourceType::STEEL_SWORD);
    // Best sword drained from the network, the worse one left in the depot.
    EXPECT_TRUE(depot->storage.buffers[ResourceType::STEEL_SWORD].buffer.empty());
    EXPECT_EQ(depot->storage.buffers[ResourceType::COPPER_SWORD].buffer.size(), 30u);
}

TEST(SupplyHub, ShipsWeaponSupplyToNeedyArmyAtTheFront)
{
    TileMap map;
    Player player{0, map};
    FillGrass(map, &player, 20, 20);

    auto* depot = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Headquarters>(1)));
    auto* hub = dynamic_cast<SupplyHub*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({8, 8}), &player, std::make_unique<SupplyHub>(2)));
    auto* tower = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({14, 14}), &player, std::make_unique<GuardTower>(3)));
    ASSERT_NE(depot, nullptr);
    ASSERT_NE(hub, nullptr);
    ASSERT_NE(tower, nullptr);

    depot->storage.buffers[ResourceType::IRON_SWORD].SetStoredAmount(30);
    depot->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(100);

    auto div = CreateMilitaryDivision(MilitaryUnitType::Swordsman, 1);
    div->equipment.weapon = ResourceType::COPPER_SWORD;  // outdated
    div->weaponSupply = 0;
    GarrisonAdd(*tower, std::move(div));

    hub->packaging.Update(*hub, hub->packaging.assembleInterval + 0.1);

    EXPECT_GT(hub->packaging.totalPackagesAssembled, 0);
    EXPECT_GT(hub->packaging.totalPackagesDelivered, 0);

    const SoldierDivision& equipped = *tower->garrison.divisions.front();
    EXPECT_EQ(equipped.equipment.weapon, ResourceType::IRON_SWORD);  // resupplied from the hub
    EXPECT_EQ(equipped.weaponSupply, equipped.weaponSupplyCapacity);
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

    int defStart = defender->health;
    int atkStart = attacker->health;
    for (int i = 0; i < 100; i++)
        world.UpdateSimulation(0.01);

    EXPECT_TRUE(attacker->engaged);          // contact auto-engaged both sides
    EXPECT_TRUE(defender->engaged);
    EXPECT_LT(defender->health, defStart);   // and they actually trade damage
    EXPECT_LT(attacker->health, atkStart);
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

    int defStart = defender->health;
    int atkStart = attacker->health;
    for (int i = 0; i < 100; i++)
        world.UpdateSimulation(0.01);

    EXPECT_LT(defender->health, defStart);   // attacker hurt the defender
    EXPECT_LT(attacker->health, atkStart);   // defender fought back (auto-defence)
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
    FillGrass(world.tilemap, atkPlayer, 30, 30);

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
    playerPtr->strategicResources.Set(StrategicResourceType::Manpower, 30);

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
