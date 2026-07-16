#include "core/GameWorld.h"
#include "core/GameCommand.h"
#include "core/GameSession.h"
#include "warfare/TowerAttackSystem.h"
#include "warfare/UnitMarchSystem.h"
#include "economy/ProductionBuildings.h"
#include "simulation/MapGenerator.h"
#include "simulation/RoadNetwork.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <memory>

// TD(etap-7) — tower targeting/range, ammo consumption, firing without ammo,
// homing-projectile hits on a moving unit via CombatResolver, Tower*
// modifiers, and the ammo-request integration with the resource road network.

namespace
{
    MapParameters MakeSmallRingParams(unsigned int seed)
    {
        MapParameters params;
        params.sizeX = 81;
        params.sizeY = 81;
        params.aiOpponentCount = 1;
        params.seed = seed;
        return params;
    }

    int AddUnitToRoster(Player& player, const std::string& unitDefId)
    {
        int instanceId = player.id * 100000 + player.nextUnitInstanceId++;
        BattleUnit unit(instanceId, player.id, unitDefId);
        unit.currentHp = unit.GetEffectiveMaxHp(player);
        player.roster.AddUnit(std::move(unit));
        return instanceId;
    }

    // Places a DefenseTower whose footprint CENTER lands `offsetTiles` away
    // from a specific point on the route, offset PERPENDICULAR to the
    // route's local direction there (derived from the neighboring route
    // tiles) rather than a fixed axis. Bug found 2026-07-14: a fixed
    // along-x offset only reliably lands "beside the path" by coincidence —
    // the route can run in any local direction (SerpentineBias wiggle, B1/B2
    // n-gon HQ placement), and a since-fixed off-by-one in world-gen retry
    // validation (docs/work_plan_2026-07-13.md B5) had been masking this by
    // always exhausting every retry and silently using a different perturbed
    // seed than the one requested — once that bug was fixed, these named
    // seeds started producing their ACTUAL (unperturbed) route geometry, and
    // the fixed-axis offset stopped reliably landing in range. Bypasses the
    // normal build-cost/placement validation (PlaceLoadedBuilding) so range
    // tests get a precisely controlled distance instead of depending on
    // where world-gen happened to put things.
    Building* PlaceTowerNearRouteTile(GameWorld& world, Player* owner, const std::vector<int>& route,
                                       size_t routeIndex, int offsetTiles, int id)
    {
        TileMap& map = world.GetTileMap();
        // Sample the tangent over a WIDE span (not the immediate +/-1
        // neighbor) — the route is a 4-directional grid path, so even along
        // an overall-straight or gently curving trend it "staircases" at the
        // single-tile level (alternating individual N/E/S/W steps), which
        // makes a narrow sample noisy/unreliable. A wide span averages that
        // out and tracks the actual long-wavelength SerpentineBias trend.
        size_t span = std::min<size_t>(10, std::min(routeIndex, route.size() - 1 - routeIndex));
        span = std::max<size_t>(span, 1);
        size_t prevIndex = routeIndex - span;
        size_t nextIndex = routeIndex + span;
        Vec2i prevPos = map.GetCoordsFromId(route[prevIndex]);
        Vec2i nextPos = map.GetCoordsFromId(route[nextIndex]);

        float tangentX = static_cast<float>(nextPos.x - prevPos.x);
        float tangentY = static_cast<float>(nextPos.y - prevPos.y);
        float length = std::sqrt(tangentX * tangentX + tangentY * tangentY);
        float perpX = length > 0.0f ? -tangentY / length : 1.0f;
        float perpY = length > 0.0f ? tangentX / length : 0.0f;

        Vec2i tileAnchor = map.GetCoordsFromId(route[routeIndex]);
        Vec2i towerAnchor{
            tileAnchor.x + static_cast<int>(std::lround(perpX * offsetTiles)),
            tileAnchor.y + static_cast<int>(std::lround(perpY * offsetTiles))};
        return map.PlaceLoadedBuilding(map.GetIdFromCoords(towerAnchor), owner, std::make_unique<DefenseTower>(id));
    }

    // Same helper pattern as BuildingDomainTests.cpp.
    void FillOwnedGrass(TileMap& map, Player* owner, int width, int height)
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

    template <typename T>
    T* PlaceAndRegister(TileMap& map, RoadNetwork& network, Player* owner, Vec2i anchor, int id)
    {
        auto* placed = dynamic_cast<T*>(
            map.PlaceLoadedBuilding(map.GetIdFromCoords(anchor), owner, std::make_unique<T>(id)));
        if (placed == nullptr)
            return nullptr;
        for (int occupiedTileId : map.GetBuildingTileIds(placed))
            network.UpdateNavMap(occupiedTileId, placed);
        return placed;
    }
}

TEST(TowerAttackSystemTests, TowerHitsMovingEnemyWithinRangeAndConsumesAmmo)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(301));
    Player* human = world.GetPlayerHandler().players.at(0).get();
    Player* ai = world.GetPlayerHandler().players.at(1).get();

    int unitId = AddUnitToRoster(*ai, "militia");
    world.SubmitCommand(GameCommand::DeployUnits(1, 0, {unitId}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    std::vector<int> route = world.GetMilitaryRoads().GetDirectedTiles(1, 0);
    ASSERT_GT(route.size(), 20u);
    int midIndex = static_cast<int>(route.size()) / 2;

    Building* tower = PlaceTowerNearRouteTile(world, human, route, midIndex, /*offsetTiles*/ 2, 9001);
    ASSERT_NE(tower, nullptr);
    auto* combat = tower->GetComponent<TowerCombatComponent>();
    auto* storage = tower->GetComponent<StorageComponent>();
    ASSERT_NE(combat, nullptr);
    ASSERT_NE(storage, nullptr);
    storage->buffers[ResourceType::ARROWS].SetStoredAmount(10);
    int ammoBefore = static_cast<int>(storage->buffers[ResourceType::ARROWS].buffer.size());
    double unitHpBefore = world.GetDeployedUnits().at(unitId).currentHp;

    // Real root cause found 2026-07-14 via direct instrumentation (tower
    // position/range/closest-approach dump) after two blind geometry fixes
    // didn't help: the tower placement math was fine all along (route[mid]
    // sat ~2.2 tiles from the tower, well within its 6-tile range) — the
    // unit simply never got there. This test's tick budget was tuned against
    // the PRE-B5-fix world (world-gen retry validation had an off-by-one
    // that made it silently exhaust all 8 retries and use a different
    // perturbed seed than the one requested, every time — see
    // docs/work_plan_2026-07-13.md B5). Once that bug was fixed, seed=301
    // started producing its real, unperturbed ring, which measured 119 tiles
    // (vs. whatever much shorter route the perturbed fallback happened to
    // produce) — reaching the midpoint alone needs ~5900+ ticks at
    // moveSpeed 1, so the old 5000-tick budget fell short by design, not by
    // bug. Bumped with generous headroom rather than tuned to one route length.
    bool damaged = false;
    for (int i = 0; i < 15000 && world.GetDeployedUnits().count(unitId) != 0 && !damaged; i++)
    {
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
        auto it = world.GetDeployedUnits().find(unitId);
        if (it != world.GetDeployedUnits().end() && it->second.currentHp < unitHpBefore)
            damaged = true;
    }

    EXPECT_TRUE(damaged) << "the tower should have hit the marching unit as it passed within range";
    int ammoAfter = static_cast<int>(storage->buffers[ResourceType::ARROWS].buffer.size());
    EXPECT_LT(ammoAfter, ammoBefore) << "firing should have consumed ammo";
}

TEST(TowerAttackSystemTests, TowerDoesNotFireAtEnemyBeyondRange)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(302));
    Player* human = world.GetPlayerHandler().players.at(0).get();
    Player* ai = world.GetPlayerHandler().players.at(1).get();

    int unitId = AddUnitToRoster(*ai, "militia");
    world.SubmitCommand(GameCommand::DeployUnits(1, 0, {unitId}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    std::vector<int> route = world.GetMilitaryRoads().GetDirectedTiles(1, 0);
    ASSERT_GT(route.size(), 20u);
    int midIndex = static_cast<int>(route.size()) / 2;

    // Default tower range is 6 tiles (buildings.rtsdata) — 40 tiles away is
    // comfortably beyond it for the unit's entire march past this point.
    Building* tower = PlaceTowerNearRouteTile(world, human, route, midIndex, /*offsetTiles*/ 40, 9002);
    ASSERT_NE(tower, nullptr);
    auto* storage = tower->GetComponent<StorageComponent>();
    ASSERT_NE(storage, nullptr);
    storage->buffers[ResourceType::ARROWS].SetStoredAmount(10);

    for (int i = 0; i < 3000 && world.GetDeployedUnits().count(unitId) != 0; i++)
        world.UpdateSimulation(FixedSimulationClock::FixedDt);

    EXPECT_EQ(storage->buffers[ResourceType::ARROWS].buffer.size(), 10u)
        << "a tower with nothing ever in range should never fire";
    EXPECT_TRUE(world.GetProjectiles().empty());
}

TEST(TowerAttackSystemTests, TowerStopsFiringWithoutAmmo)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(303));
    Player* human = world.GetPlayerHandler().players.at(0).get();
    Player* ai = world.GetPlayerHandler().players.at(1).get();

    int unitId = AddUnitToRoster(*ai, "militia");
    world.SubmitCommand(GameCommand::DeployUnits(1, 0, {unitId}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    std::vector<int> route = world.GetMilitaryRoads().GetDirectedTiles(1, 0);
    ASSERT_GT(route.size(), 20u);
    int midIndex = static_cast<int>(route.size()) / 2;

    Building* tower = PlaceTowerNearRouteTile(world, human, route, midIndex, /*offsetTiles*/ 2, 9003);
    ASSERT_NE(tower, nullptr);
    auto* storage = tower->GetComponent<StorageComponent>();
    ASSERT_NE(storage, nullptr);
    storage->buffers[ResourceType::ARROWS].SetStoredAmount(0); // no ammo at all

    double unitHpBefore = world.GetDeployedUnits().at(unitId).currentHp;
    // Stop once the unit reaches the HQ door — HqCombatSystem's thorns would
    // otherwise (correctly) damage it there too, unrelated to this tower.
    // Budget bumped (see TowerHitsMovingEnemyWithinRangeAndConsumesAmmo above
    // for why): reaching the door needs the FULL route length, which can run
    // well past the old 5000-tick budget on this map size.
    for (int i = 0; i < 15000; i++)
    {
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
        auto it = world.GetDeployedUnits().find(unitId);
        if (it == world.GetDeployedUnits().end() || it->second.state == BattleUnitState::AttackingHq)
            break;
    }

    ASSERT_EQ(world.GetDeployedUnits().count(unitId), 1u) << "unopposed, the unit should survive its march up to the door";
    EXPECT_DOUBLE_EQ(world.GetDeployedUnits().at(unitId).currentHp, unitHpBefore);
    EXPECT_TRUE(world.GetProjectiles().empty());
}

TEST(TowerAttackSystemTests, TowerBalanceModifiersApply)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(304));
    Player* human = world.GetPlayerHandler().players.at(0).get();

    Building* tower = world.GetTileMap().PlaceLoadedBuilding(
        world.GetTileMap().GetIdFromCoords({5, 5}), human, std::make_unique<DefenseTower>(9004));
    ASSERT_NE(tower, nullptr);
    auto* combat = tower->GetComponent<TowerCombatComponent>();
    ASSERT_NE(combat, nullptr);

    double baseDamage = combat->GetModifiedDamage(*tower);
    double baseRange = combat->GetModifiedRange(*tower);
    double baseAttackSpeed = combat->GetModifiedAttackSpeed(*tower);

    human->balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::TowerDamage, 0.0, 1.5, BalanceModifierScope::Global(), std::nullopt, std::nullopt, "test:tower"});
    human->balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::TowerRange, 2.0, 1.0, BalanceModifierScope::Global(), std::nullopt, std::nullopt, "test:tower"});
    human->balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::TowerAttackSpeed, 0.0, 2.0, BalanceModifierScope::Global(), std::nullopt, std::nullopt, "test:tower"});

    EXPECT_DOUBLE_EQ(combat->GetModifiedDamage(*tower), baseDamage * 1.5);
    EXPECT_DOUBLE_EQ(combat->GetModifiedRange(*tower), baseRange + 2.0);
    EXPECT_DOUBLE_EQ(combat->GetModifiedAttackSpeed(*tower), baseAttackSpeed * 2.0);
}

TEST(TowerAttackSystemTests, TowerRequestsAmmoOverRoadNetworkFromSupplier)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 10, 4);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.roadNetwork = std::move(network);

    auto* warehouse = PlaceAndRegister<StorageBuilding>(map, *networkPtr, &player, {0, 0}, 1);
    auto* tower = PlaceAndRegister<DefenseTower>(map, *networkPtr, &player, {5, 0}, 2);
    ASSERT_NE(warehouse, nullptr);
    ASSERT_NE(tower, nullptr);

    for (int x = 3; x <= 4; x++)
    {
        auto* road = PlaceAndRegister<Road>(map, *networkPtr, &player, {x, 1}, 100 + x);
        ASSERT_NE(road, nullptr);
        road->road.maxCapacity.SetBase(16);
    }

    warehouse->storage.buffers[ResourceType::ARROWS].SetStoredAmount(50);
    tower->SetSupplier(ResourceType::ARROWS, warehouse);

    // TowerCombatComponent::Update is what actually issues the request
    // (mirroring ProductionComponent::MaintainRequests, minus the
    // ProductionComponent coupling that method requires).
    tower->combat.Update(*tower, 0.01);

    EXPECT_GT(warehouse->transportables.size(), 0u)
        << "the tower's ammo request should have started a real road-network transport";
}

// Regression test for T3 (docs/post_pivot_audit_2026-07-12.md): before this
// fix, Building::GetInputBufferViews() never exposed a tower's ammo buffer as
// an input, so AutoConnectBuilding had no input view to wire a supplier from
// — a tower only ever got ammo if something manually called SetSupplier (as
// the test above still does). Builds everything through the real production
// placement path (Player::Build<T>, no manual SetSupplier, no Tile::owner)
// and expects the ammo request to fire from auto-connect alone.
TEST(TowerAttackSystemTests, TowerAmmoAutoConnectsWithoutManualSupplierWiring)
{
    TileMap map;
    map.params.sizeX = 10;
    map.params.sizeY = 4;
    map.tilemap.clear();
    map.tilemap.reserve(map.params.sizeX * map.params.sizeY);
    for (int i = 0; i < map.params.sizeX * map.params.sizeY; i++)
        map.tilemap.emplace_back(i);

    Player player{0, map};

    auto* warehouse = dynamic_cast<StorageBuilding*>(player.Build<StorageBuilding>(Vec2i{0, 0}, false));
    auto* tower = dynamic_cast<DefenseTower*>(player.Build<DefenseTower>(Vec2i{5, 0}, false));
    auto* roadA = player.Build<Road>(Vec2i{3, 1}, false);
    auto* roadB = player.Build<Road>(Vec2i{4, 1}, false);
    ASSERT_NE(warehouse, nullptr);
    ASSERT_NE(tower, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);

    warehouse->storage.buffers[ResourceType::ARROWS].SetStoredAmount(50);

    tower->combat.Update(*tower, 0.01);

    EXPECT_GT(warehouse->transportables.size(), 0u)
        << "auto-connect should have wired the tower as a supplied receiver for ARROWS";
}

TEST(TowerAttackSystemTests, SaveAndLoadPreservesTowerAmmoAndAttackTimer)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(305));
    Player* human = world.GetPlayerHandler().players.at(0).get();

    Building* tower = world.GetTileMap().PlaceLoadedBuilding(
        world.GetTileMap().GetIdFromCoords({5, 5}), human, std::make_unique<DefenseTower>(9005));
    ASSERT_NE(tower, nullptr);
    auto* combat = tower->GetComponent<TowerCombatComponent>();
    auto* storage = tower->GetComponent<StorageComponent>();
    ASSERT_NE(combat, nullptr);
    ASSERT_NE(storage, nullptr);
    combat->attackTimer = 0.42;
    storage->buffers[ResourceType::ARROWS].SetStoredAmount(7);

    const auto path = (std::filesystem::temp_directory_path() / "rts_tower_test.save").string();
    ASSERT_TRUE(world.SaveToFile(path));

    GameWorld loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path, nullptr, nullptr));

    Building* loadedTower = loaded.GetTileMap().GetBuilding(tower->positionId);
    ASSERT_NE(loadedTower, nullptr);
    const auto* loadedCombat = loadedTower->GetComponent<TowerCombatComponent>();
    const auto* loadedStorage = loadedTower->GetComponent<StorageComponent>();
    ASSERT_NE(loadedCombat, nullptr);
    ASSERT_NE(loadedStorage, nullptr);
    EXPECT_DOUBLE_EQ(loadedCombat->attackTimer, 0.42);
    EXPECT_EQ(loadedStorage->buffers.at(ResourceType::ARROWS).buffer.size(), 7u);

    std::filesystem::remove(path);
}
