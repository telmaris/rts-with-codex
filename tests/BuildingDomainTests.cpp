#include "../inc/MapGenerator.h"
#include "../inc/Player.h"
#include "../inc/ProductionBuildings.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace
{
    // Creates a small owned grass map for building-domain tests.
    void FillOwnedGrass(TileMap& map, Player* owner, int width = 8, int height = 8)
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

    // Paints one terrain resource area with matching richness.
    void Paint(TileMap& map, Vec2i anchor, Vec2i footprint, TileType type, int richness)
    {
        for (int y = 0; y < footprint.y; y++)
        {
            for (int x = 0; x < footprint.x; x++)
            {
                Tile& tile = map.tilemap[map.GetIdFromCoords({anchor.x + x, anchor.y + y})];
                tile.tileType = type;
                tile.resourceRichness = richness;
            }
        }
    }
}

TEST(BuildingDomainTests, BaseBuildingTracksEfficiencyConstructionAndProgress)
{
    Road building{1};
    building.lifetime = 20.0;
    building.activeTime = 5.0;
    EXPECT_FLOAT_EQ(building.GetEfficiency(), 0.25f);

    building.buildTime = 10.0;
    building.constructionRemaining = 6.0;
    EXPECT_FLOAT_EQ(building.GetConstructionProgress(), 0.4f);
    EXPECT_TRUE(building.UpdateConstruction(2.0));
    EXPECT_DOUBLE_EQ(building.constructionRemaining, 4.0);
    EXPECT_FALSE(building.UpdateConstruction(10.0));
    EXPECT_DOUBLE_EQ(building.constructionRemaining, 0.0);
}

TEST(BuildingDomainTests, ProductionBuildingReportsBuffersConnectionsAndStalledState)
{
    ProductionBuilding building{7};
    building.ingredients[ResourceType::WOOD] = 2;
    building.products[ResourceType::PLANKS] = 1;
    building.inputBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 4};
    building.outputBuffers[ResourceType::PLANKS] = ResourceBuffer{ResourceType::PLANKS, 2};
    building.workerCapacity = 4;
    building.assignedWorkers = 2;
    building.productionTime = 10.0;
    building.productionStarted = true;
    building.elapsedTime = 5.0;

    EXPECT_FLOAT_EQ(building.GetWorkerRatio(), 0.5f);
    EXPECT_FLOAT_EQ(building.GetProductionProgress(), 0.5f);
    EXPECT_TRUE(building.CanAcceptResource(ResourceType::WOOD));
    EXPECT_TRUE(building.CanReceiveResource(ResourceType::WOOD));
    EXPECT_FALSE(building.HasSupplier(ResourceType::WOOD));
    EXPECT_FALSE(building.HasReceiver(ResourceType::PLANKS));

    auto inputs = building.GetInputBufferViews();
    ASSERT_EQ(inputs.size(), 1u);
    EXPECT_EQ(inputs.front().recipeAmount, 2);

    auto outputs = building.GetOutputBufferViews();
    ASSERT_EQ(outputs.size(), 1u);
    EXPECT_EQ(outputs.front().recipeAmount, 1);

    EXPECT_TRUE(building.IsProductionStalled());
    building.SetProductionBlocked(true);
    EXPECT_FALSE(building.IsProductionStalled());
}

TEST(BuildingDomainTests, ProductionBuildingEffectiveCycleTimeUsesWorkerEfficiency)
{
    ProductionBuilding building{7};
    building.productionTime = 10.0;
    building.workerCapacity = 4;
    building.assignedWorkers = 2;

    EXPECT_DOUBLE_EQ(building.GetEffectiveProductionCycleTime(), 20.0);

    building.assignedWorkers = 0;
    EXPECT_TRUE(std::isinf(building.GetEffectiveProductionCycleTime()));
}

TEST(BuildingDomainTests, TerrainRichnessIsConsumedAndTurnsExhaustedTileToGrass)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);

    Vec2i anchor{2, 2};
    Vec2i footprint = GetBuildingDefinition(BuildingType::Woodcutter).footprint;
    Paint(map, anchor, footprint, TileType::WOOD, 1);

    auto* woodcutter = dynamic_cast<Woodcutter*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords(anchor), &player, std::make_unique<Woodcutter>(3)));
    ASSERT_NE(woodcutter, nullptr);

    EXPECT_TRUE(woodcutter->HasTerrainRichness());
    EXPECT_TRUE(woodcutter->ConsumeTerrainRichness());
    EXPECT_EQ(map.tilemap[map.GetIdFromCoords(anchor)].tileType, TileType::GRASS);
    EXPECT_TRUE(map.terrainDirty);
}

TEST(BuildingDomainTests, StorageBuffersExposeCapacityAndReceiveRules)
{
    StorageBuilding storage{5};
    storage.resourceBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 1};

    EXPECT_TRUE(storage.IsStorageLike());
    EXPECT_TRUE(storage.CanAcceptResource(ResourceType::WOOD));
    EXPECT_TRUE(storage.CanReceiveResource(ResourceType::WOOD));

    storage.resourceBuffers[ResourceType::WOOD].GenerateResource(ResourceType::WOOD);
    EXPECT_FALSE(storage.CanReceiveResource(ResourceType::WOOD));

    auto views = storage.GetOutputBufferViews();
    auto woodView = std::find_if(views.begin(), views.end(), [](const ResourceBufferView& view)
    {
        return view.type == ResourceType::WOOD;
    });
    ASSERT_NE(woodView, views.end());
    EXPECT_EQ(woodView->amount, 1);
    EXPECT_EQ(woodView->capacity, 1);
    storage.resourceBuffers[ResourceType::WOOD].Clear();
}

TEST(BuildingDomainTests, StorageAddsGetsAndRejectsResourcesPrecisely)
{
    StorageBuilding storage{6};
    storage.resourceBuffers.clear();
    storage.resourceBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 1};

    Resource wood{ResourceType::WOOD};
    Resource stone{ResourceType::STONE};
    storage.AddResource(&wood);
    storage.AddResource(&stone);

    EXPECT_EQ(storage.resourceBuffers[ResourceType::WOOD].buffer.size(), 1u);
    EXPECT_FALSE(storage.resourceBuffers.contains(ResourceType::STONE));

    Resource fetched = storage.GetResource(ResourceType::WOOD);
    EXPECT_EQ(fetched.type, ResourceType::WOOD);
    EXPECT_TRUE(storage.resourceBuffers[ResourceType::WOOD].buffer.empty());
}

TEST(BuildingDomainTests, ProductionBuildingAcceptsAndReturnsResources)
{
    ProductionBuilding building{8};
    building.inputBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 1};
    building.outputBuffers[ResourceType::PLANKS] = ResourceBuffer{ResourceType::PLANKS, 2};
    building.pendingInputRequests[ResourceType::WOOD] = 1;

    Resource wood{ResourceType::WOOD};
    building.AddResource(&wood);
    EXPECT_EQ(building.inputBuffers[ResourceType::WOOD].buffer.size(), 1u);
    EXPECT_EQ(building.pendingInputRequests[ResourceType::WOOD], 0);

    Resource fetched = building.GetResource(ResourceType::WOOD);
    EXPECT_EQ(fetched.type, ResourceType::WOOD);

    Resource plank{ResourceType::PLANKS};
    building.ReturnOutgoingResource(&plank);
    EXPECT_EQ(building.outputBuffers[ResourceType::PLANKS].buffer.size(), 1u);
}

TEST(BuildingDomainTests, ProductionBuildingRequestsFromMultipleSuppliers)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);
    RoadNetwork network{map};
    player.roadNetwork = std::make_unique<RoadNetwork>(map);

    ProductionBuilding building{8};
    building.owner = &player;
    building.inputBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 4};

    StorageBuilding supplierA{1};
    supplierA.owner = &player;
    supplierA.resourceBuffers.clear();
    supplierA.resourceBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};
    supplierA.resourceBuffers[ResourceType::WOOD].SetStoredAmount(1);

    StorageBuilding supplierB{2};
    supplierB.owner = &player;
    supplierB.resourceBuffers.clear();
    supplierB.resourceBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};
    supplierB.resourceBuffers[ResourceType::WOOD].SetStoredAmount(1);

    building.SetSupplier(ResourceType::WOOD, &supplierA);
    building.SetSupplier(ResourceType::WOOD, &supplierB);

    EXPECT_TRUE(building.HasSupplier(ResourceType::WOOD));
    EXPECT_EQ(building.RequestResource(ResourceType::WOOD, 2), 0);
    EXPECT_TRUE(building.inputRequestBlocked);
    supplierA.resourceBuffers[ResourceType::WOOD].Clear();
    supplierB.resourceBuffers[ResourceType::WOOD].Clear();
}

TEST(BuildingDomainTests, MilitaryBuildingTracksTroopsDamageAndOrders)
{
    GuardTower tower{11};
    tower.militia = 2;
    tower.swordsmen = 1;
    tower.archers = 1;
    tower.hitPoints = 20;
    tower.garrisonCapacity = 8;
    tower.supplyCapacity = 5;

    EXPECT_EQ(tower.GetTotalTroops(), 4);
    EXPECT_EQ(tower.GetFreeGarrisonSpace(), 4);
    EXPECT_GT(tower.GetEffectiveStrength(), 0);
    EXPECT_EQ(tower.GetSupplyCapacity(), 5);

    tower.ReceiveDamage(7);
    EXPECT_EQ(tower.GetHitPoints(), 13);
    tower.ReceiveDamage(999);
    EXPECT_EQ(tower.GetHitPoints(), 0);

    tower.IssueOrder(MilitaryOrderType::Defend, 123);
    EXPECT_EQ(tower.currentOrder, MilitaryOrderType::Defend);
    EXPECT_EQ(tower.orderTargetPositionId, 123);
    tower.ClearOrder();
    EXPECT_EQ(tower.currentOrder, MilitaryOrderType::None);
}

TEST(BuildingDomainTests, BarracksConsumesManpowerAndCompletesRecruitment)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);

    Barracks barracks{30};
    barracks.owner = &player;
    barracks.garrisonCapacity = 5;
    barracks.constructionRemaining = 0.0;
    player.strategicResources.Set(StrategicResourceType::Manpower, 10);

    ASSERT_TRUE(barracks.QueueRecruitment(MilitaryUnitType::Militia));
    ASSERT_TRUE(barracks.QueueRecruitment(MilitaryUnitType::Swordsman));
    ASSERT_TRUE(barracks.QueueRecruitment(MilitaryUnitType::Archer));
    EXPECT_DOUBLE_EQ(player.strategicResources.Get(StrategicResourceType::Manpower), 5.0);

    barracks.Update(100.0);
    barracks.Update(100.0);
    barracks.Update(100.0);

    EXPECT_EQ(barracks.militia, 1);
    EXPECT_EQ(barracks.swordsmen, 1);
    EXPECT_EQ(barracks.archers, 1);
    EXPECT_TRUE(barracks.recruitmentQueue.empty());
}

TEST(BuildingDomainTests, RoadStatsUseConfiguredBaseValues)
{
    Road road{20};
    road.maxCapacity = 9;
    road.speedModifier = 1.75;
    road.transportTime = 7.0;

    EXPECT_EQ(road.GetModifiedMaxCapacity(), 9);
    EXPECT_DOUBLE_EQ(road.GetModifiedSpeedModifier(), 1.75);
    EXPECT_DOUBLE_EQ(road.GetModifiedTransportTime(), 4.0);
}

TEST(BuildingDomainTests, ConfiguredBuildingConstructorsApplyRuntimeDefinitions)
{
    Woodcutter woodcutter{1};
    EXPECT_EQ(woodcutter.buildingType, BuildingType::Woodcutter);
    EXPECT_FALSE(woodcutter.products.empty());

    HuntersHut hunter{2};
    hunter.InitBuilding(TileType::WOOD);
    EXPECT_EQ(hunter.buildingType, BuildingType::HuntersHut);
    EXPECT_FALSE(hunter.ShouldConsumeTerrainRichness());
    EXPECT_FALSE(hunter.products.empty());

    Mine mine{3};
    mine.InitBuilding(TileType::STONE);
    EXPECT_EQ(mine.buildingType, BuildingType::Mine);
    EXPECT_FALSE(mine.products.empty());

    LumberMill lumberMill{4};
    Foundry foundry{5};
    Well well{6};
    WheatFarm wheatFarm{7};
    Windmill windmill{8};
    Bakery bakery{9};
    Inn inn{10};
    Paperworks paperworks{11};
    Smith smith{12};
    University university{13};
    EXPECT_EQ(lumberMill.buildingType, BuildingType::LumberMill);
    EXPECT_EQ(foundry.buildingType, BuildingType::Foundry);
    EXPECT_EQ(well.buildingType, BuildingType::Well);
    EXPECT_EQ(wheatFarm.buildingType, BuildingType::WheatFarm);
    EXPECT_EQ(windmill.buildingType, BuildingType::Windmill);
    EXPECT_EQ(bakery.buildingType, BuildingType::Bakery);
    EXPECT_EQ(inn.buildingType, BuildingType::Inn);
    EXPECT_EQ(paperworks.buildingType, BuildingType::Paperworks);
    EXPECT_EQ(smith.buildingType, BuildingType::Smith);
    EXPECT_EQ(university.buildingType, BuildingType::University);

    Headquarters hq{14};
    Village village{15};
    GuardTower tower{16};
    Fortress fortress{17};
    Castle castle{18};
    Barracks barracks{19};
    EXPECT_EQ(hq.buildingType, BuildingType::Headquarters);
    EXPECT_FALSE(hq.CanBeManuallyDestroyed());
    EXPECT_EQ(village.buildingType, BuildingType::Village);
    EXPECT_EQ(tower.buildingType, BuildingType::GuardTower);
    EXPECT_EQ(fortress.buildingType, BuildingType::Fortress);
    EXPECT_EQ(castle.buildingType, BuildingType::Castle);
    EXPECT_EQ(barracks.buildingType, BuildingType::Barracks);
}

TEST(BuildingDomainTests, VillageGeneratesManpowerAndFoodShortageReducesProductivity)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);

    auto* village = dynamic_cast<Village*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Village>(40)));
    ASSERT_NE(village, nullptr);
    village->owner = &player;
    village->constructionRemaining = 0.0;
    village->manpowerRate = 1.0;
    village->populationCap = 10;
    village->upkeepInterval = 1.0;
    village->foodPackageUpkeep = 1.0;
    village->foodSupplyBuffer.Clear();
    village->foodSupplyBuffer = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 3};
    village->foodSupplyBuffer.SetStoredAmount(1);
    village->foodSupplyLevel = 1.0;

    village->Update(1.0);
    EXPECT_TRUE(village->hasFood);
    EXPECT_DOUBLE_EQ(player.strategicResources.Get(StrategicResourceType::Manpower), 1.0);
    EXPECT_DOUBLE_EQ(village->activeTime, 1.0);

    village->Update(1.0);
    EXPECT_TRUE(village->hasFood);
    EXPECT_NEAR(village->GetFoodSupplyRatio(), 0.75, 0.0001);
    EXPECT_NEAR(village->GetWorkerProductivity(), 0.825, 0.0001);
    EXPECT_NEAR(player.strategicResources.Get(StrategicResourceType::Manpower), 1.75, 0.0001);
    EXPECT_NEAR(village->activeTime, 1.75, 0.0001);
    village->foodSupplyBuffer.Clear();
}

TEST(BuildingDomainTests, VillageRequestsFoodProvisionsFromOwnedStorage)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 12, 8);
    player.roadNetwork = std::make_unique<RoadNetwork>(map);

    auto* storage = dynamic_cast<StorageBuilding*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({0, 1}), &player, std::make_unique<StorageBuilding>(1)));
    auto* village = dynamic_cast<Village*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({6, 1}), &player, std::make_unique<Village>(2)));
    ASSERT_NE(storage, nullptr);
    ASSERT_NE(village, nullptr);
    for (int tileId : map.GetBuildingTileIds(storage))
        player.roadNetwork->UpdateNavMap(tileId, storage);
    for (int tileId : map.GetBuildingTileIds(village))
        player.roadNetwork->UpdateNavMap(tileId, village);

    auto* roadA = dynamic_cast<Road*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({3, 2}), &player, std::make_unique<Road>(3)));
    auto* roadB = dynamic_cast<Road*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({4, 2}), &player, std::make_unique<Road>(4)));
    auto* roadC = dynamic_cast<Road*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({5, 2}), &player, std::make_unique<Road>(5)));
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);
    ASSERT_NE(roadC, nullptr);
    player.roadNetwork->UpdateNavMap(roadA->positionId, roadA);
    player.roadNetwork->UpdateNavMap(roadB->positionId, roadB);
    player.roadNetwork->UpdateNavMap(roadC->positionId, roadC);

    storage->resourceBuffers.clear();
    storage->resourceBuffers[ResourceType::FOOD_PROVISIONS] = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 3};
    storage->resourceBuffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(1);
    village->foodSupplyBuffer.Clear();
    village->foodSupplyBuffer = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 3};

    village->RequestFoodSupply();

    ASSERT_EQ(storage->transportables.size(), 1u);
    auto* resource = dynamic_cast<Resource*>(storage->transportables.front());
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(resource->type, ResourceType::FOOD_PROVISIONS);
    EXPECT_EQ(resource->targetBuilding, village);
    storage->resourceBuffers[ResourceType::FOOD_PROVISIONS].Clear();
}

TEST(BuildingDomainTests, MilitaryOrdersAttackSupportAndDefendUpdateCombatState)
{
    TileMap map;
    Player player{0, map};
    Player enemy{1, map};
    FillOwnedGrass(map, &player, 20, 20);
    player.roadNetwork = std::make_unique<RoadNetwork>(map);
    enemy.roadNetwork = std::make_unique<RoadNetwork>(map);

    auto* attacker = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<GuardTower>(1)));
    auto* defender = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({10, 1}), &enemy, std::make_unique<GuardTower>(2)));
    ASSERT_NE(attacker, nullptr);
    ASSERT_NE(defender, nullptr);
    attacker->constructionRemaining = 0.0;
    defender->constructionRemaining = 0.0;
    const int beforeHitPoints = defender->GetHitPoints();

    attacker->IssueOrder(MilitaryOrderType::Attack, defender->positionId);
    attacker->Update(1.0);
    EXPECT_LT(defender->GetHitPoints(), beforeHitPoints);
    EXPECT_EQ(attacker->currentOrder, MilitaryOrderType::Attack);
    EXPECT_GT(attacker->orderCooldown, 0.0);

    auto* source = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 10}), &player, std::make_unique<GuardTower>(3)));
    auto* target = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({10, 10}), &player, std::make_unique<GuardTower>(4)));
    ASSERT_NE(source, nullptr);
    ASSERT_NE(target, nullptr);
    source->constructionRemaining = 0.0;
    target->constructionRemaining = 0.0;
    source->militia = 1;
    source->swordsmen = 1;
    target->garrisonCapacity = 5;

    source->IssueOrder(MilitaryOrderType::Support, target->positionId);
    source->Update(1.0);
    EXPECT_EQ(source->GetTotalTroops(), 1);
    EXPECT_EQ(target->militia, 1);
    EXPECT_GT(source->orderCooldown, 0.0);

    source->IssueOrder(MilitaryOrderType::Defend, target->positionId);
    source->Update(1.0);
    EXPECT_EQ(source->currentOrder, MilitaryOrderType::Defend);
    EXPECT_DOUBLE_EQ(source->orderCooldown, 3.0);
}

TEST(BuildingDomainTests, MilitarySupplyBufferAcceptsReturnsAndRejectsResources)
{
    GuardTower tower{50};
    tower.supplyCapacity = 2;
    tower.supplyBuffer = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 2};

    Resource food{ResourceType::FOOD_PROVISIONS};
    tower.AddResource(&food);
    EXPECT_EQ(tower.supply, 1);
    EXPECT_TRUE(tower.CanAcceptResource(ResourceType::FOOD_PROVISIONS));
    EXPECT_TRUE(tower.CanReceiveResource(ResourceType::FOOD_PROVISIONS));

    Resource returned{ResourceType::FOOD_PROVISIONS};
    tower.ReturnOutgoingResource(&returned);
    EXPECT_EQ(tower.supply, 2);
    EXPECT_FALSE(tower.CanReceiveResource(ResourceType::FOOD_PROVISIONS));

    Resource fetched = tower.GetResource(ResourceType::FOOD_PROVISIONS);
    EXPECT_EQ(fetched.type, ResourceType::FOOD_PROVISIONS);
    EXPECT_EQ(tower.supply, 1);

    Resource wrong{ResourceType::WOOD};
    tower.AddResource(&wrong);
    EXPECT_EQ(tower.supply, 1);
    EXPECT_EQ(tower.GetResource(ResourceType::WOOD).type, ResourceType::Null);
}
