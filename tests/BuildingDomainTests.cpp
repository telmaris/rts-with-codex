#include "simulation/MapGenerator.h"
#include "economy/Player.h"
#include "economy/ProductionBuildings.h"
#include "simulation/RoadNetwork.h"

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
    EXPECT_EQ(headquarters.GetComponent<StorageComponent>(), &headquarters.storage);
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

// Regression test for the "production stalls for a moment right at 100%"
// report: GetProductionProgress() used to divide by the unmodified
// cycleTime.GetBase(), while Produce() actually completes the cycle when
// elapsed reaches GetModifiedCycleTime() (tech/focus/state-development
// adjusted) — any active modifier on ProductionCycleTime desynced the two,
// most commonly a StateDevelopment level bonus, which applies automatically
// as the player's civilization progresses (not a chosen tech), so this bit
// nearly every real playthrough once past the starting tier.
TEST(BuildingDomainTests, ProductionProgressMatchesModifiedCycleTimeNotBaseline)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);

    auto* building = dynamic_cast<Woodcutter*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({2, 2}), &player, std::make_unique<Woodcutter>(9)));
    ASSERT_NE(building, nullptr);
    building->production.cycleTime = 10.0;
    building->production.started = true;

    // An extra 50% slowdown (e.g. a StateDevelopment / focus penalty) on top
    // of whatever a fresh player already has (a default StateDevelopment tier
    // alone gives a non-1.0 ProductionCycleTime multiplier — this bug bit
    // essentially every real playthrough, not just an edge case). Read back
    // the real effective threshold rather than hardcoding it: the point is
    // that GetProductionProgress must track WHATEVER GetModifiedCycleTime
    // returns, not the raw base.
    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::ProductionCycleTime, 0.0, 1.5, BalanceModifierScope::Global(),
        std::nullopt, std::nullopt, "test:slow_production"});
    double effective = building->production.GetModifiedCycleTime(*building);
    ASSERT_GT(effective, 10.0) << "test modifier should make the cycle slower than the 10.0 base";

    // At elapsed == base (10.0), the cycle is nowhere near actually done
    // (needs `effective`) — progress must reflect that, not report 100%.
    building->production.elapsed = 10.0;
    EXPECT_FLOAT_EQ(building->GetProductionProgress(), static_cast<float>(10.0 / effective));

    // At elapsed == the real modified threshold, progress is exactly 100% —
    // matching the point where Produce() actually completes the cycle.
    building->production.elapsed = effective;
    EXPECT_FLOAT_EQ(building->GetProductionProgress(), 1.0f);
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
    Barracks barracks{19};
    EXPECT_EQ(hq.buildingType, BuildingType::Headquarters);
    EXPECT_FALSE(hq.CanBeManuallyDestroyed());
    EXPECT_EQ(village.buildingType, BuildingType::Village);
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
    EXPECT_DOUBLE_EQ(player.strategicResources.Get(StrategicResourceType::Manpower), 1.12);
    EXPECT_DOUBLE_EQ(village->activeTime, 1.0);

    village->Update(1.0);
    EXPECT_TRUE(village->population.hasFood);
    EXPECT_NEAR(village->GetFoodSupplyRatio(), 0.67, 0.0001);
    EXPECT_NEAR(village->GetWorkerProductivity(), 0.769, 0.0001);
    EXPECT_NEAR(player.strategicResources.Get(StrategicResourceType::Manpower), 1.8704, 0.0001);
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

TEST(BuildingDomainTests, ConstructionQueueLimitsActiveBuildersAndTracksPositions)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 12, 8);

    auto place = [&](int id, Vec2i at) -> Road*
    {
        auto* road = dynamic_cast<Road*>(
            map.PlaceLoadedBuilding(map.GetIdFromCoords(at), &player, std::make_unique<Road>(id)));
        if (road != nullptr)
        {
            road->buildTime = 10.0;
            road->constructionRemaining = 10.0;
        }
        return road;
    };

    // Placed out of id order to prove the queue orders by id (== placement order).
    Road* second = place(20, {2, 1});
    Road* first = place(10, {4, 1});
    Road* third = place(30, {6, 1});
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(third, nullptr);

    // One builder: only the earliest (lowest-id) building actually progresses.
    player.construction.builders = 1;
    player.construction.Refresh(player);
    EXPECT_EQ(player.construction.QueueLength(), 3);
    EXPECT_EQ(player.construction.QueuePosition(first->id), 1);
    EXPECT_EQ(player.construction.QueuePosition(second->id), 2);
    EXPECT_EQ(player.construction.QueuePosition(third->id), 3);
    EXPECT_TRUE(player.construction.IsActive(first->id));
    EXPECT_FALSE(player.construction.IsActive(second->id));
    EXPECT_TRUE(first->constructionActive);
    EXPECT_FALSE(second->constructionActive);

    // Only the assigned builder advances construction; queued buildings wait.
    first->Update(1.0);
    second->Update(1.0);
    EXPECT_DOUBLE_EQ(first->constructionRemaining, 9.0);
    EXPECT_DOUBLE_EQ(second->constructionRemaining, 10.0);

    // A BuilderAmount buff (tech / focus / national) widens the active window.
    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::BuilderAmount, 1.0, 1.0, BalanceModifierScope::Global(),
        std::nullopt, std::nullopt, "tech:masons"});
    EXPECT_EQ(player.construction.EffectiveBuilders(player), 2);
    player.construction.Refresh(player);
    EXPECT_TRUE(player.construction.IsActive(second->id));
    EXPECT_FALSE(player.construction.IsActive(third->id));

    // Removing the front building (cancel) renumbers the rest of the queue.
    player.UnregisterBuilding(first);
    player.construction.Refresh(player);
    EXPECT_EQ(player.construction.QueuePosition(first->id), 0);
    EXPECT_EQ(player.construction.QueuePosition(second->id), 1);
    EXPECT_EQ(player.construction.QueuePosition(third->id), 2);
}

TEST(BuildingDomainTests, RefundBuildCostReturnsResourcesToStorageAndDropsOverflow)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);

    auto* storage = dynamic_cast<StorageBuilding*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({2, 2}), &player, std::make_unique<StorageBuilding>(1)));
    ASSERT_NE(storage, nullptr);
    storage->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 10};
    storage->storage.buffers[ResourceType::WOOD].SetStoredAmount(2);

    // Cancelling a build hands the paid resources back into owned storage.
    player.RefundBuildCost({{ResourceType::WOOD, 3}});
    EXPECT_EQ(storage->storage.buffers[ResourceType::WOOD].buffer.size(), 5u);

    // Anything past the buffer capacity spills rather than piling up.
    player.RefundBuildCost({{ResourceType::WOOD, 100}});
    EXPECT_EQ(storage->storage.buffers[ResourceType::WOOD].buffer.size(), 10u);

    storage->storage.buffers[ResourceType::WOOD].Clear();
}

// Regression test, updated 2026-07-15 (docs/work_plan_2026-07-13.md, A5 +
// correction): the T3 background-pull mechanism this test originally
// verified (LogisticsComponent::MaintainStorageRequests continuously topping
// up Barracks' own buffer regardless of demand) was removed by design.
// QueueRecruitment now queues the order immediately (tagged "waiting" — see
// RecruitmentQueueEntry::resourcesReady) and requests the shortfall from its
// wired supplier; RecruitmentComponent::Update keeps retrying each tick until
// it physically arrives through the real road network, at which point the
// entry's build timer starts.
TEST(BuildingDomainTests, BarracksRequestsAndReceivesUnitCostsOnDemandThroughRoadNetwork)
{
    TileMap map;
    map.params.sizeX = 10;
    map.params.sizeY = 6;
    map.tilemap.clear();
    map.tilemap.reserve(map.params.sizeX * map.params.sizeY);
    for (int i = 0; i < map.params.sizeX * map.params.sizeY; i++)
        map.tilemap.emplace_back(i);

    Player player{0, map};
    player.strategicResources.Set(StrategicResourceType::Manpower, 100);

    auto* warehouse = dynamic_cast<StorageBuilding*>(player.Build<StorageBuilding>(Vec2i{0, 0}, false));
    auto* barracks = dynamic_cast<Barracks*>(player.Build<Barracks>(Vec2i{5, 0}, false));
    auto* roadA = player.Build<Road>(Vec2i{3, 1}, false);
    auto* roadB = player.Build<Road>(Vec2i{4, 1}, false);
    ASSERT_NE(warehouse, nullptr);
    ASSERT_NE(barracks, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);

    warehouse->storage.buffers[ResourceType::IRON_SWORD].GenerateResource(ResourceType::IRON_SWORD);
    for (int i = 0; i < 3; i++)
        warehouse->storage.buffers[ResourceType::FOOD_PROVISIONS].GenerateResource(ResourceType::FOOD_PROVISIONS);

    // No ticks have run yet: nothing has moved toward the Barracks on its
    // own, unlike the removed background poll.
    EXPECT_EQ(barracks->storage.buffers[ResourceType::IRON_SWORD].buffer.size(), 0u);

    ASSERT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "swordsman"))
        << "manpower alone should be enough to queue the order";
    ASSERT_FALSE(barracks->recruitment.queue.empty());
    EXPECT_FALSE(barracks->recruitment.queue.front().resourcesReady)
        << "nothing is local yet, so the order should start out waiting";

    bool resourcesArrived = false;
    for (int tick = 0; tick < 20 && !resourcesArrived; tick++)
    {
        warehouse->Update(1.0);
        roadA->Update(1.0);
        roadB->Update(1.0);
        barracks->Update(1.0);
        resourcesArrived = !barracks->recruitment.queue.empty() && barracks->recruitment.queue.front().resourcesReady;
    }

    EXPECT_TRUE(resourcesArrived) << "swordsman costs (IRON_SWORD 1, FOOD_PROVISIONS 3) never arrived at Barracks";
}

// Regression test: user report — only militia/swordsman were recruitable, not
// knight or ram. Root cause was in the DATA, not the code: buildings.rtsdata's
// Barracks block only declared storage buffers for FOOD_PROVISIONS and
// IRON_SWORD (swordsman's cost) — knight needs STEEL_SWORD and ram needs
// PLANKS+IRON, neither of which had a buffer at all, so
// RecruitmentComponent::QueueRecruitment's `storage->buffers.find(cost.type)`
// always came up empty and recruitment silently failed regardless of how much
// of those resources the player had. Fixed by adding the missing `storage`
// lines to buildings.rtsdata's Barracks block.
TEST(BuildingDomainTests, BarracksCanRecruitKnightAndRamNotJustSwordsman)
{
    TileMap map;
    Player player{0, map};
    player.strategicResources.Set(StrategicResourceType::Manpower, 100);

    Barracks barracks{1};
    barracks.owner = &player;
    // Recruitment now pulls from the player's GLOBAL storage network
    // (docs/work_plan_2026-07-13.md, 2026-07-14), which is indexed via
    // RegisterBuilding — Player::Build<T> does this implicitly, but a
    // manually-constructed Barracks needs it done explicitly here.
    player.RegisterBuilding(&barracks);
    EXPECT_TRUE(barracks.storage.buffers.contains(ResourceType::STEEL_SWORD))
        << "knight costs STEEL_SWORD but Barracks has no buffer for it";
    EXPECT_TRUE(barracks.storage.buffers.contains(ResourceType::PLANKS))
        << "ram costs PLANKS but Barracks has no buffer for it";
    EXPECT_TRUE(barracks.storage.buffers.contains(ResourceType::IRON))
        << "ram costs IRON but Barracks has no buffer for it";

    barracks.storage.buffers[ResourceType::STEEL_SWORD].GenerateResource(ResourceType::STEEL_SWORD);
    barracks.storage.buffers[ResourceType::FOOD_PROVISIONS].GenerateResource(ResourceType::FOOD_PROVISIONS);
    for (int i = 0; i < 4; i++)
        barracks.storage.buffers[ResourceType::FOOD_PROVISIONS].GenerateResource(ResourceType::FOOD_PROVISIONS);
    EXPECT_TRUE(barracks.recruitment.QueueRecruitment(barracks, "knight"));

    barracks.storage.buffers[ResourceType::PLANKS].SetStoredAmount(20);
    barracks.storage.buffers[ResourceType::IRON].SetStoredAmount(10);
    for (int i = 0; i < 4; i++)
        barracks.storage.buffers[ResourceType::FOOD_PROVISIONS].GenerateResource(ResourceType::FOOD_PROVISIONS);
    EXPECT_TRUE(barracks.recruitment.QueueRecruitment(barracks, "ram"));
}

// Updated 2026-07-15 (docs/work_plan_2026-07-13.md, A5 correction): Diagnose
// and QueueRecruitment are now DELIBERATELY decoupled — Diagnose stays a
// global feasibility scan (see BuildingComponents.h for why), while
// QueueRecruitment only hard-fails on manpower and otherwise queues the
// order as "waiting" until resources arrive locally. They no longer agree
// on "empty iff recruitable" by design.
TEST(BuildingDomainTests, DiagnoseRecruitmentBlockReportsMissingResourceAndManpower)
{
    TileMap map;
    Player player{0, map};

    Barracks barracks{1};
    barracks.owner = &player;
    // See BarracksCanRecruitKnightAndRamNotJustSwordsman above: global-storage
    // recruitment needs the building registered to be visible to the check.
    player.RegisterBuilding(&barracks);

    // No manpower, no resources yet: Diagnose reports both, and
    // QueueRecruitment fails outright — manpower is the one hard gate.
    std::string reason = barracks.recruitment.DiagnoseRecruitmentBlock(barracks, "swordsman");
    EXPECT_FALSE(reason.empty());
    EXPECT_FALSE(barracks.recruitment.QueueRecruitment(barracks, "swordsman"));
    EXPECT_TRUE(barracks.recruitment.queue.empty());

    // Give manpower but still no unit-cost resources anywhere: Diagnose still
    // reports the missing resource, but QueueRecruitment now succeeds anyway
    // — the order joins the queue tagged "waiting for resources" instead of
    // being rejected.
    player.strategicResources.Set(StrategicResourceType::Manpower, 100);
    reason = barracks.recruitment.DiagnoseRecruitmentBlock(barracks, "swordsman");
    EXPECT_FALSE(reason.empty());
    ASSERT_TRUE(barracks.recruitment.QueueRecruitment(barracks, "swordsman"));
    ASSERT_FALSE(barracks.recruitment.queue.empty());
    EXPECT_FALSE(barracks.recruitment.queue.back().resourcesReady);

    // Stock it directly: Diagnose returns empty. The order queued above is
    // still waiting, and strict FIFO (TODO #1, 2026-07-16) means a fresh
    // order must NOT grab the buffer past it — Update()'s readiness pass
    // serves the oldest waiting entry first. Swordsman costs IRON_SWORD 1 +
    // FOOD_PROVISIONS 3 (assets/data/units.rtsdata).
    barracks.storage.buffers[ResourceType::IRON_SWORD].GenerateResource(ResourceType::IRON_SWORD);
    for (int i = 0; i < 3; i++)
        barracks.storage.buffers[ResourceType::FOOD_PROVISIONS].GenerateResource(ResourceType::FOOD_PROVISIONS);
    EXPECT_TRUE(barracks.recruitment.DiagnoseRecruitmentBlock(barracks, "swordsman").empty());
    ASSERT_TRUE(barracks.recruitment.QueueRecruitment(barracks, "swordsman"));
    EXPECT_FALSE(barracks.recruitment.queue.back().resourcesReady)
        << "strict FIFO: a fresh order waits behind the earlier waiting entry";
    barracks.recruitment.Update(barracks, 0.01);
    EXPECT_TRUE(barracks.recruitment.queue.front().resourcesReady)
        << "the stocked cost must go to the FIRST waiting order";

    // With no earlier waiting entries, a fresh order whose cost is already
    // buffered consumes immediately and starts counting down right away.
    barracks.recruitment.queue.clear();
    barracks.storage.buffers[ResourceType::IRON_SWORD].GenerateResource(ResourceType::IRON_SWORD);
    for (int i = 0; i < 3; i++)
        barracks.storage.buffers[ResourceType::FOOD_PROVISIONS].GenerateResource(ResourceType::FOOD_PROVISIONS);
    ASSERT_TRUE(barracks.recruitment.QueueRecruitment(barracks, "swordsman"));
    EXPECT_TRUE(barracks.recruitment.queue.back().resourcesReady);
}

