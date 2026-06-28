#include "../inc/MapGenerator.h"
#include "../inc/Player.h"
#include "../inc/ProductionBuildings.h"
#include "../inc/RoadNetwork.h"

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

    // Places a loaded building and registers its footprint in the road network.
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

TEST(BuildingDomainTests, BuildingCapabilitiesExposeAttachedComponents)
{
    Road road{1};
    EXPECT_TRUE(road.HasComponent<RoadComponent>());
    EXPECT_EQ(road.GetComponent<StorageComponent>(), nullptr);

    Woodcutter production{2};
    EXPECT_TRUE(production.HasComponent<ProductionComponent>());
    EXPECT_TRUE(production.HasComponent<LogisticsComponent>());
    EXPECT_TRUE(production.HasComponent<WorkerComponent>());
    EXPECT_TRUE(production.HasComponent<RecipeComponent>());
    EXPECT_FALSE(production.HasComponent<ResearchComponent>()); // only University researches
    EXPECT_EQ(production.GetComponent<ProductionComponent>(), &production.production);
    EXPECT_EQ(production.GetComponent<WorkerComponent>(), &production.workers);
    EXPECT_EQ(production.GetComponent<RecipeComponent>(), &production.recipes);

    University university{5};
    EXPECT_TRUE(university.HasComponent<ResearchComponent>());
    EXPECT_EQ(university.GetComponent<ResearchComponent>(), &university.research);

    Headquarters headquarters{3};
    EXPECT_TRUE(headquarters.HasComponent<StorageComponent>());
    EXPECT_TRUE(headquarters.HasComponent<TerritoryComponent>());
    EXPECT_EQ(headquarters.GetComponent<StorageComponent>(), &headquarters.storage);
    EXPECT_EQ(headquarters.GetComponent<TerritoryComponent>(), &headquarters.territory);

    Barracks barracks{4};
    EXPECT_TRUE(barracks.HasComponent<GarrisonComponent>());
    EXPECT_TRUE(barracks.HasComponent<SupplyBufferComponent>());
    EXPECT_TRUE(barracks.HasComponent<RecruitmentComponent>());
    EXPECT_EQ(barracks.GetComponent<RecruitmentComponent>(), &barracks.recruitment);
}

TEST(BuildingDomainTests, ProductionBuildingReportsBuffersConnectionsAndStalledState)
{
    Woodcutter building{7};
    building.production.ingredients.clear();
    building.production.products.clear();
    building.production.inputBuffers.clear();
    building.production.outputBuffers.clear();
    building.production.ingredients[ResourceType::WOOD] = 2;
    building.production.products[ResourceType::PLANKS] = 1;
    building.production.inputBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 4};
    building.production.outputBuffers[ResourceType::PLANKS] = ResourceBuffer{ResourceType::PLANKS, 2};
    building.workers.capacity = 4;
    building.workers.assigned = 2;
    building.production.cycleTime = 10.0;
    building.production.started = true;
    building.production.elapsed = 5.0;

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
    Woodcutter building{7};
    building.production.cycleTime = 10.0;
    building.workers.capacity = 4;
    building.workers.assigned = 2;

    EXPECT_DOUBLE_EQ(building.production.GetEffectiveCycleTime(building), 20.0);

    building.workers.assigned = 0;
    EXPECT_TRUE(std::isinf(building.production.GetEffectiveCycleTime(building)));
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

    EXPECT_TRUE(woodcutter->production.HasTerrainRichness(*woodcutter));
    EXPECT_TRUE(woodcutter->production.ConsumeTerrainRichness(*woodcutter));
    EXPECT_EQ(map.tilemap[map.GetIdFromCoords(anchor)].tileType, TileType::GRASS);
    EXPECT_TRUE(map.terrainDirty);
}

TEST(BuildingDomainTests, StorageBuffersExposeCapacityAndReceiveRules)
{
    StorageBuilding storage{5};
    storage.storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 1};

    EXPECT_TRUE(storage.IsStorageLike());
    EXPECT_TRUE(storage.CanAcceptResource(ResourceType::WOOD));
    EXPECT_TRUE(storage.CanReceiveResource(ResourceType::WOOD));

    storage.storage.buffers[ResourceType::WOOD].GenerateResource(ResourceType::WOOD);
    EXPECT_FALSE(storage.CanReceiveResource(ResourceType::WOOD));

    auto views = storage.GetOutputBufferViews();
    auto woodView = std::find_if(views.begin(), views.end(), [](const ResourceBufferView& view)
    {
        return view.type == ResourceType::WOOD;
    });
    ASSERT_NE(woodView, views.end());
    EXPECT_EQ(woodView->amount, 1);
    EXPECT_EQ(woodView->capacity, 1);
    storage.storage.buffers[ResourceType::WOOD].Clear();
}

TEST(BuildingDomainTests, StorageAddsGetsAndRejectsResourcesPrecisely)
{
    StorageBuilding storage{6};
    storage.storage.buffers.clear();
    storage.storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 1};

    Resource wood{ResourceType::WOOD};
    Resource stone{ResourceType::STONE};
    storage.AddResource(&wood);
    storage.AddResource(&stone);

    EXPECT_EQ(storage.storage.buffers[ResourceType::WOOD].buffer.size(), 1u);
    EXPECT_FALSE(storage.storage.buffers.contains(ResourceType::STONE));

    Resource fetched = storage.GetResource(ResourceType::WOOD);
    EXPECT_EQ(fetched.type, ResourceType::WOOD);
    EXPECT_TRUE(storage.storage.buffers[ResourceType::WOOD].buffer.empty());
}

TEST(BuildingDomainTests, ProductionBuildingAcceptsAndReturnsResources)
{
    Woodcutter building{8};
    building.production.inputBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 1};
    building.production.outputBuffers[ResourceType::PLANKS] = ResourceBuffer{ResourceType::PLANKS, 2};
    building.logistics.pendingRequests[ResourceType::WOOD] = 1;

    Resource wood{ResourceType::WOOD};
    building.AddResource(&wood);
    EXPECT_EQ(building.production.inputBuffers[ResourceType::WOOD].buffer.size(), 1u);
    EXPECT_EQ(building.logistics.pendingRequests[ResourceType::WOOD], 0);

    Resource fetched = building.GetResource(ResourceType::WOOD);
    EXPECT_EQ(fetched.type, ResourceType::WOOD);

    Resource plank{ResourceType::PLANKS};
    building.ReturnOutgoingResource(&plank);
    EXPECT_EQ(building.production.outputBuffers[ResourceType::PLANKS].buffer.size(), 1u);
}

TEST(BuildingDomainTests, ProductionBuildingRequestsFromMultipleSuppliers)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);
    RoadNetwork network{map};
    player.roadNetwork = std::make_unique<RoadNetwork>(map);

    Woodcutter building{8};
    building.owner = &player;
    building.production.products.clear();
    building.production.outputBuffers.clear();
    building.production.inputBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 4};

    StorageBuilding supplierA{1};
    supplierA.owner = &player;
    supplierA.storage.buffers.clear();
    supplierA.storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};
    supplierA.storage.buffers[ResourceType::WOOD].SetStoredAmount(1);

    StorageBuilding supplierB{2};
    supplierB.owner = &player;
    supplierB.storage.buffers.clear();
    supplierB.storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};
    supplierB.storage.buffers[ResourceType::WOOD].SetStoredAmount(1);

    building.SetSupplier(ResourceType::WOOD, &supplierA);
    building.SetSupplier(ResourceType::WOOD, &supplierB);

    EXPECT_TRUE(building.HasSupplier(ResourceType::WOOD));
    // Suppliers together hold only 2 WOOD, so a request for 4 cannot be filled
    // and must flag the producer as blocked.
    EXPECT_LT(building.logistics.RequestResource(ResourceType::WOOD, 4, building), 4);
    EXPECT_TRUE(building.logistics.requestBlocked);
    supplierA.storage.buffers[ResourceType::WOOD].Clear();
    supplierB.storage.buffers[ResourceType::WOOD].Clear();
}

TEST(BuildingDomainTests, MultipleProducersPushOutputToSameConsumerUntilInputIsReserved)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 12, 9);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.roadNetwork = std::move(network);

    auto* woodcutterA = PlaceAndRegister<Woodcutter>(map, *networkPtr, &player, {0, 0}, 1);
    auto* woodcutterB = PlaceAndRegister<Woodcutter>(map, *networkPtr, &player, {0, 3}, 2);
    auto* woodcutterC = PlaceAndRegister<Woodcutter>(map, *networkPtr, &player, {0, 6}, 3);
    auto* lumberMill = PlaceAndRegister<LumberMill>(map, *networkPtr, &player, {8, 3}, 4);
    ASSERT_NE(woodcutterA, nullptr);
    ASSERT_NE(woodcutterB, nullptr);
    ASSERT_NE(woodcutterC, nullptr);
    ASSERT_NE(lumberMill, nullptr);

    for (int y = 1; y <= 7; y++)
    {
        auto* road = PlaceAndRegister<Road>(map, *networkPtr, &player, {2, y}, 100 + y);
        ASSERT_NE(road, nullptr);
        road->road.maxCapacity.SetBase(16);
    }
    for (int x = 3; x <= 7; x++)
    {
        auto* road = PlaceAndRegister<Road>(map, *networkPtr, &player, {x, 4}, 200 + x);
        ASSERT_NE(road, nullptr);
        road->road.maxCapacity.SetBase(16);
    }

    auto fillWoodOutput = [](Woodcutter* woodcutter)
    {
        woodcutter->production.outputBuffers[ResourceType::WOOD].SetStoredAmount(3);
        woodcutter->SetReceiver(ResourceType::WOOD, nullptr);
    };
    fillWoodOutput(woodcutterA);
    fillWoodOutput(woodcutterB);
    fillWoodOutput(woodcutterC);

    lumberMill->production.inputBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 8};
    woodcutterA->SetReceiver(ResourceType::WOOD, lumberMill);
    woodcutterB->SetReceiver(ResourceType::WOOD, lumberMill);
    woodcutterC->SetReceiver(ResourceType::WOOD, lumberMill);

    woodcutterA->logistics.DispatchOutputs(*woodcutterA, woodcutterA->production);
    woodcutterB->logistics.DispatchOutputs(*woodcutterB, woodcutterB->production);
    woodcutterC->logistics.DispatchOutputs(*woodcutterC, woodcutterC->production);

    EXPECT_EQ(woodcutterA->transportables.size(), 3u);
    EXPECT_EQ(woodcutterB->transportables.size(), 3u);
    EXPECT_EQ(woodcutterC->transportables.size(), 2u);
    EXPECT_EQ(lumberMill->production.inputBuffers[ResourceType::WOOD].buffer.size(), 0u);
}

TEST(BuildingDomainTests, ProducerWithNoReceiverPushesFullOutputToNearestHeadquarters)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 10, 6);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.roadNetwork = std::move(network);

    Vec2i woodAnchor{0, 1};
    Paint(map, woodAnchor, GetBuildingDefinition(BuildingType::Woodcutter).footprint, TileType::WOOD, 10);
    auto* woodcutter = PlaceAndRegister<Woodcutter>(map, *networkPtr, &player, woodAnchor, 1);
    auto* headquarters = PlaceAndRegister<Headquarters>(map, *networkPtr, &player, {5, 1}, 2);
    ASSERT_NE(woodcutter, nullptr);
    ASSERT_NE(headquarters, nullptr);

    ASSERT_NE(PlaceAndRegister<Road>(map, *networkPtr, &player, {2, 2}, 10), nullptr);
    ASSERT_NE(PlaceAndRegister<Road>(map, *networkPtr, &player, {3, 2}, 11), nullptr);
    ASSERT_NE(PlaceAndRegister<Road>(map, *networkPtr, &player, {4, 2}, 12), nullptr);

    woodcutter->SetReceiver(ResourceType::WOOD, nullptr);
    woodcutter->production.outputBuffers[ResourceType::WOOD].SetStoredAmount(3);
    const size_t initialHeadquartersWood = headquarters->storage.buffers[ResourceType::WOOD].buffer.size();

    woodcutter->logistics.DispatchOutputs(*woodcutter, woodcutter->production);

    EXPECT_EQ(woodcutter->transportables.size(), 3u);
    EXPECT_TRUE(woodcutter->HasReceiver(ResourceType::WOOD));

    for (int i = 0; i < 8; i++)
        map.UpdateBuildings(1.1);

    EXPECT_TRUE(woodcutter->transportables.empty());
    EXPECT_EQ(headquarters->storage.buffers[ResourceType::WOOD].buffer.size(), initialHeadquartersWood + 3u);
}

TEST(BuildingDomainTests, ProducerPushesResourceImmediatelyWhenProductionCompletes)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 10, 6);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.roadNetwork = std::move(network);

    Vec2i woodAnchor{0, 1};
    Paint(map, woodAnchor, GetBuildingDefinition(BuildingType::Woodcutter).footprint, TileType::WOOD, 10);
    auto* woodcutter = PlaceAndRegister<Woodcutter>(map, *networkPtr, &player, woodAnchor, 1);
    auto* headquarters = PlaceAndRegister<Headquarters>(map, *networkPtr, &player, {5, 1}, 2);
    ASSERT_NE(woodcutter, nullptr);
    ASSERT_NE(headquarters, nullptr);

    ASSERT_NE(PlaceAndRegister<Road>(map, *networkPtr, &player, {2, 2}, 10), nullptr);
    ASSERT_NE(PlaceAndRegister<Road>(map, *networkPtr, &player, {3, 2}, 11), nullptr);
    ASSERT_NE(PlaceAndRegister<Road>(map, *networkPtr, &player, {4, 2}, 12), nullptr);

    woodcutter->SetReceiver(ResourceType::WOOD, nullptr);
    woodcutter->workers.assigned = woodcutter->workers.capacity.GetBase();
    woodcutter->production.started = true;
    woodcutter->production.elapsed = woodcutter->production.GetModifiedCycleTime(*woodcutter);

    woodcutter->production.Produce(*woodcutter, 0.01);

    const int baseWoodOutput = woodcutter->production.products[ResourceType::WOOD];
    const int expectedTransported = woodcutter->production.GetModifiedOutputAmount(*woodcutter, ResourceType::WOOD, baseWoodOutput);
    EXPECT_EQ(woodcutter->transportables.size(), static_cast<size_t>(expectedTransported));
    EXPECT_TRUE(woodcutter->production.outputBuffers[ResourceType::WOOD].buffer.empty());
    EXPECT_TRUE(woodcutter->HasReceiver(ResourceType::WOOD));
}

TEST(BuildingDomainTests, MilitaryBuildingTracksTroopsDamageAndOrders)
{
    GuardTower tower{11};
    tower.garrison.militia = 2;
    tower.garrison.swordsmen = 1;
    tower.garrison.archers = 1;
    tower.territory.hp = 20;
    tower.garrison.cap = 8;
    tower.supplyBuffer.capacity = 5;

    EXPECT_EQ(tower.garrison.GetTotalTroops(), 4);
    EXPECT_EQ(tower.garrison.GetFreeGarrisonSpace(tower), 4);
    EXPECT_GT(tower.garrison.GetEffectiveStrength(tower), 0);
    EXPECT_EQ(tower.supplyBuffer.GetModifiedCapacity(tower), 5);

    tower.territory.ReceiveDamage(7);
    EXPECT_EQ(tower.GetHitPoints(), 13);
    tower.territory.ReceiveDamage(999);
    EXPECT_EQ(tower.GetHitPoints(), 0);

    tower.garrison.IssueOrder(MilitaryOrderType::Defend, 123);
    EXPECT_EQ(tower.garrison.currentOrder, MilitaryOrderType::Defend);
    EXPECT_EQ(tower.garrison.orderTargetId, 123);
    tower.garrison.ClearOrder();
    EXPECT_EQ(tower.garrison.currentOrder, MilitaryOrderType::None);
}

TEST(BuildingDomainTests, BarracksConsumesManpowerAndCompletesRecruitment)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);

    auto* hq = dynamic_cast<Headquarters*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({5, 5}), &player, std::make_unique<Headquarters>(200)));
    ASSERT_NE(hq, nullptr);
    hq->constructionRemaining = 0.0;
    hq->storage.buffers[ResourceType::FOOD_PROVISIONS] = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 50};
    hq->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(50);
    hq->storage.buffers[ResourceType::WEAPON_SUPPLY] = ResourceBuffer{ResourceType::WEAPON_SUPPLY, 50};
    hq->storage.buffers[ResourceType::WEAPON_SUPPLY].SetStoredAmount(50);
    hq->storage.buffers[ResourceType::IRON_SWORD] = ResourceBuffer{ResourceType::IRON_SWORD, 20};
    hq->storage.buffers[ResourceType::IRON_SWORD].SetStoredAmount(20);
    hq->storage.buffers[ResourceType::BOW] = ResourceBuffer{ResourceType::BOW, 20};
    hq->storage.buffers[ResourceType::BOW].SetStoredAmount(20);
    hq->storage.buffers[ResourceType::ARROWS] = ResourceBuffer{ResourceType::ARROWS, 30};
    hq->storage.buffers[ResourceType::ARROWS].SetStoredAmount(30);

    Barracks barracks{30};
    barracks.owner = &player;
    barracks.garrison.cap = 50;
    barracks.constructionRemaining = 0.0;
    player.strategicResources.Set(StrategicResourceType::Manpower, 30);

    ASSERT_TRUE(barracks.QueueRecruitment(MilitaryUnitType::Militia));
    ASSERT_TRUE(barracks.QueueRecruitment(MilitaryUnitType::Swordsman));
    ASSERT_TRUE(barracks.QueueRecruitment(MilitaryUnitType::Archer));
    EXPECT_DOUBLE_EQ(player.strategicResources.Get(StrategicResourceType::Manpower), 2.0);

    barracks.Update(100.0);
    barracks.Update(100.0);
    barracks.Update(100.0);

    EXPECT_EQ(barracks.garrison.militia, 1);
    EXPECT_EQ(barracks.garrison.swordsmen, 1);
    EXPECT_EQ(barracks.garrison.archers, 1);
    EXPECT_TRUE(barracks.recruitment.queue.empty());
}

TEST(BuildingDomainTests, RoadStatsUseConfiguredBaseValues)
{
    Road road{20};
    road.road.maxCapacity = 9;
    road.road.speedModifier = 1.75;
    road.transportTime = 7.0;

    EXPECT_EQ(road.GetModifiedMaxCapacity(), 9);
    EXPECT_DOUBLE_EQ(road.GetModifiedSpeedModifier(), 1.75);
    EXPECT_DOUBLE_EQ(road.GetModifiedTransportTime(), 4.0);
}

TEST(BuildingDomainTests, ConfiguredBuildingConstructorsApplyRuntimeDefinitions)
{
    Woodcutter woodcutter{1};
    EXPECT_EQ(woodcutter.buildingType, BuildingType::Woodcutter);
    EXPECT_FALSE(woodcutter.production.products.empty());

    HuntersHut hunter{2};
    hunter.InitBuilding(TileType::WOOD);
    EXPECT_EQ(hunter.buildingType, BuildingType::HuntersHut);
    EXPECT_FALSE(hunter.production.consumesTerrain);
    EXPECT_FALSE(hunter.production.products.empty());

    Mine mine{3};
    mine.InitBuilding(TileType::STONE);
    EXPECT_EQ(mine.buildingType, BuildingType::Mine);
    EXPECT_FALSE(mine.production.products.empty());

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
    village->population.manpowerRate = 1.0;
    village->population.populationCap = 10;
    village->population.upkeepInterval = 1.0;
    village->population.foodPackageUpkeep = 1.0;
    village->population.foodBuffer.Clear();
    village->population.foodBuffer = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 3};
    village->population.foodBuffer.SetStoredAmount(1);
    village->population.foodSupplyLevel = 1.0;

    village->Update(1.0);
    EXPECT_TRUE(village->population.hasFood);
    EXPECT_DOUBLE_EQ(player.strategicResources.Get(StrategicResourceType::Manpower), 1.0);
    EXPECT_DOUBLE_EQ(village->activeTime, 1.0);

    village->Update(1.0);
    EXPECT_TRUE(village->population.hasFood);
    EXPECT_NEAR(village->GetFoodSupplyRatio(), 0.67, 0.0001);
    EXPECT_NEAR(village->GetWorkerProductivity(), 0.769, 0.0001);
    EXPECT_NEAR(player.strategicResources.Get(StrategicResourceType::Manpower), 1.67, 0.0001);
    EXPECT_NEAR(village->activeTime, 1.67, 0.0001);
    village->population.foodBuffer.Clear();
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

    storage->storage.buffers.clear();
    storage->storage.buffers[ResourceType::FOOD_PROVISIONS] = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 3};
    storage->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(1);
    village->population.foodBuffer.Clear();
    village->population.foodBuffer = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 3};

    village->RequestFoodSupply();

    ASSERT_EQ(storage->transportables.size(), 1u);
    auto* resource = dynamic_cast<Resource*>(storage->transportables.front());
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(resource->type, ResourceType::FOOD_PROVISIONS);
    EXPECT_EQ(resource->targetBuilding, village);
    storage->storage.buffers[ResourceType::FOOD_PROVISIONS].Clear();
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

    attacker->garrison.IssueOrder(MilitaryOrderType::Attack, defender->positionId);
    attacker->Update(1.0);
    // Buildings no longer deal direct damage — only stationed divisions do.
    EXPECT_EQ(defender->GetHitPoints(), beforeHitPoints);
    EXPECT_EQ(attacker->garrison.currentOrder, MilitaryOrderType::Attack);
    EXPECT_EQ(attacker->garrison.orderCooldown, 0.0);

    auto* source = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 10}), &player, std::make_unique<GuardTower>(3)));
    auto* target = dynamic_cast<GuardTower*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({10, 10}), &player, std::make_unique<GuardTower>(4)));
    ASSERT_NE(source, nullptr);
    ASSERT_NE(target, nullptr);
    source->constructionRemaining = 0.0;
    target->constructionRemaining = 0.0;
    source->garrison.militia = 1;
    source->garrison.swordsmen = 1;
    target->garrison.cap = 5;

    source->garrison.IssueOrder(MilitaryOrderType::Support, target->positionId);
    source->Update(1.0);
    EXPECT_EQ(source->garrison.GetTotalTroops(), 1);
    EXPECT_EQ(target->garrison.militia, 1);
    EXPECT_GT(source->garrison.orderCooldown, 0.0);

    source->garrison.IssueOrder(MilitaryOrderType::Defend, target->positionId);
    source->Update(1.0);
    EXPECT_EQ(source->garrison.currentOrder, MilitaryOrderType::Defend);
    EXPECT_DOUBLE_EQ(source->garrison.orderCooldown, 3.0);
}

TEST(BuildingDomainTests, MilitarySupplyBufferAcceptsReturnsAndRejectsResources)
{
    GuardTower tower{50};
    tower.supplyBuffer.capacity = 2;
    tower.supplyBuffer.buffer = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 2};

    Resource food{ResourceType::FOOD_PROVISIONS};
    tower.AddResource(&food);
    EXPECT_EQ(tower.supplyBuffer.stored, 1);
    EXPECT_TRUE(tower.CanAcceptResource(ResourceType::FOOD_PROVISIONS));
    EXPECT_TRUE(tower.CanReceiveResource(ResourceType::FOOD_PROVISIONS));

    Resource returned{ResourceType::FOOD_PROVISIONS};
    tower.ReturnOutgoingResource(&returned);
    EXPECT_EQ(tower.supplyBuffer.stored, 2);
    EXPECT_FALSE(tower.CanReceiveResource(ResourceType::FOOD_PROVISIONS));

    Resource fetched = tower.GetResource(ResourceType::FOOD_PROVISIONS);
    EXPECT_EQ(fetched.type, ResourceType::FOOD_PROVISIONS);
    EXPECT_EQ(tower.supplyBuffer.stored, 1);

    Resource wrong{ResourceType::WOOD};
    tower.AddResource(&wrong);
    EXPECT_EQ(tower.supplyBuffer.stored, 1);
    EXPECT_EQ(tower.GetResource(ResourceType::WOOD).type, ResourceType::Null);
}
