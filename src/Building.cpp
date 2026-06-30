#include "../inc/Building.h"
#include "../inc/BuildingConfig.h"
#include "../inc/ProductionBuildings.h"
#include "../inc/Player.h"
#include "../inc/MapGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    int CountIncomingResources(Building* target, ResourceType type)
    {
        if (target == nullptr || target->owner == nullptr)
            return 0;

        int incoming = 0;
        for (auto& tile : target->owner->tilemap.tilemap)
        {
            Building* carrier = tile.building.get();
            if (carrier == nullptr)
                continue;

            for (auto* t : carrier->transportables)
            {
                auto* res = dynamic_cast<Resource*>(t);
                if (res != nullptr && res->targetBuilding == target && res->type == type)
                    incoming++;
            }
        }
        return incoming;
    }

    int GetReceiveCapacity(Building* target, ResourceType type)
    {
        if (target == nullptr || !target->CanReceiveResource(type))
            return 0;

        auto findCap = [type](const std::vector<ResourceBufferView>& views) -> int
        {
            for (const auto& v : views)
                if (v.type == type) return std::max(0, v.capacity - v.amount);
            return -1;
        };

        int free = findCap(target->GetInputBufferViews());
        if (free < 0) free = findCap(target->GetOutputBufferViews());
        if (free < 0) free = target->CanReceiveResource(type) ? 1 : 0;

        return std::max(0, free - CountIncomingResources(target, type));
    }
} // namespace

// ─── Building (base) ─────────────────────────────────────────────────────────

float Building::GetEfficiency() const
{
    if (lifetime <= 0.0) return 0.0f;
    return static_cast<float>(activeTime / lifetime);
}

float Building::GetConstructionProgress() const
{
    double base = buildTime.GetBase();
    if (base <= 0.0) return 1.0f;
    return static_cast<float>(std::clamp((base - constructionRemaining) / base, 0.0, 1.0));
}

void Building::Update(double dt)
{
    double opDt = BeginOperationalUpdate(dt);
    if (opDt <= 0.0) return;

    for (auto* component : m_components)
        component->Update(*this, opDt);

    UpdateTransportables(opDt);
}

void Building::InitBuilding(TileType t)
{
    if (auto* production = GetComponent<ProductionComponent>())
        production->terrainType = t;
}

// ─── Building resource-flow facade ────────────────────────────────────────────
// Every building routes resource flow and capability queries to whichever
// component owns that behaviour. Buildings without a matching component degrade
// to safe no-ops, so callers can treat any Building* uniformly.

void Building::AddResource(Resource* res)
{
    if (res == nullptr) return;

    if (auto* prod = GetComponent<ProductionComponent>())
    {
        auto it = prod->inputBuffers.find(res->type);
        if (it == prod->inputBuffers.end() ||
            static_cast<int>(it->second.buffer.size()) >= it->second.bufferSize)
        {
            if (res->sourceBuilding != nullptr)
                res->sourceBuilding->ReturnOutgoingResource(res);
            CancelRequestedResource(res->type);
            return;
        }

        Log::Msg(tag, "resource added!");
        it->second.AddResource(res);
        if (auto* log = GetComponent<LogisticsComponent>())
        {
            auto pending = log->pendingRequests.find(res->type);
            if (pending != log->pendingRequests.end() && pending->second > 0)
                pending->second--;
        }
        return;
    }

    if (auto* storage = GetComponent<StorageComponent>())
    {
        storage->AddResource(res, *this);
        return;
    }

    if (auto* supply = GetComponent<SupplyBufferComponent>())
    {
        if (res->type != ResourceType::FOOD_PROVISIONS || !CanReceiveResource(res->type))
        {
            if (res->sourceBuilding != nullptr)
                res->sourceBuilding->ReturnOutgoingResource(res);
            return;
        }
        supply->AddResource(res);
        return;
    }

    if (auto* pop = GetComponent<PopulationComponent>())
    {
        if (res->type != ResourceType::FOOD_PROVISIONS || !CanReceiveResource(res->type))
        {
            if (res->sourceBuilding != nullptr)
                res->sourceBuilding->ReturnOutgoingResource(res);
            return;
        }
        pop->foodBuffer.AddResource(res);
        pop->hasFood = true;
        return;
    }

    if (res->sourceBuilding != nullptr)
        res->sourceBuilding->ReturnOutgoingResource(res);
}

Resource Building::GetResource(ResourceType type)
{
    if (auto* prod = GetComponent<ProductionComponent>())
    {
        auto [avail, res] = prod->inputBuffers[type].GetResource();
        return avail ? *res : Resource{};
    }
    if (auto* storage = GetComponent<StorageComponent>())
        return storage->GetResource(type);
    if (auto* supply = GetComponent<SupplyBufferComponent>())
        return type == ResourceType::FOOD_PROVISIONS ? supply->GetResource() : Resource{};
    if (auto* pop = GetComponent<PopulationComponent>())
    {
        if (type != ResourceType::FOOD_PROVISIONS) return Resource{};
        auto [avail, res] = pop->foodBuffer.GetResource();
        return avail ? *res : Resource{};
    }
    return Resource{};
}

void Building::ReturnOutgoingResource(Resource* res)
{
    if (res == nullptr) return;
    if (auto* prod = GetComponent<ProductionComponent>())
    {
        prod->outputBuffers[res->type].AddResource(res);
        return;
    }
    if (auto* storage = GetComponent<StorageComponent>())
    {
        storage->ReturnOutgoingResource(res);
        return;
    }
    if (res->type != ResourceType::FOOD_PROVISIONS)
        return;
    if (auto* supply = GetComponent<SupplyBufferComponent>())
        supply->ReturnOutgoingResource(res);
    else if (auto* pop = GetComponent<PopulationComponent>())
        pop->foodBuffer.AddResource(res);
}

void Building::CancelRequestedResource(ResourceType type)
{
    auto* log = GetComponent<LogisticsComponent>();
    if (log == nullptr) return;
    auto it = log->pendingRequests.find(type);
    if (it != log->pendingRequests.end() && it->second > 0)
        it->second--;
}

void Building::SetSupplier(ResourceType type, Building* supplier)
{
    if (auto* log = GetComponent<LogisticsComponent>())
        log->SetSupplier(type, supplier, *this);
}

void Building::SetReceiver(ResourceType type, Building* receiver)
{
    auto* log = GetComponent<LogisticsComponent>();
    auto* prod = GetComponent<ProductionComponent>();
    if (log != nullptr && prod != nullptr)
    {
        log->SetReceiver(type, receiver, *this, *prod);
        return;
    }
    // Storage hubs do not track receivers; they register themselves as the
    // receiver's supplier instead.
    if (IsStorageLike() && receiver != nullptr)
        receiver->SetSupplier(type, this);
}

void Building::SetAlternativeReceiver(ResourceType type, Building* receiver)
{
    if (auto* log = GetComponent<LogisticsComponent>())
        log->SetAltReceiver(type, receiver, *this);
}

void Building::RemoveSupplier(ResourceType type, Building* supplier)
{
    if (auto* log = GetComponent<LogisticsComponent>())
        log->RemoveSupplier(type, supplier);
}

void Building::RemoveReceiver(ResourceType type, Building* receiver)
{
    auto* log = GetComponent<LogisticsComponent>();
    auto* prod = GetComponent<ProductionComponent>();
    if (log != nullptr && prod != nullptr)
        log->RemoveReceiver(type, receiver, *this, *prod);
}

int Building::HandleTransport(ResourceType type, int amount, Building* receiver)
{
    if (GetComponent<ProductionComponent>() != nullptr)
    {
        auto* log = GetComponent<LogisticsComponent>();
        auto* prod = GetComponent<ProductionComponent>();
        return log != nullptr ? log->HandleTransportFrom(type, amount, receiver, *this, *prod) : 0;
    }
    if (auto* storage = GetComponent<StorageComponent>())
        return storage->HandleTransport(type, amount, receiver, *this);
    if (auto* supply = GetComponent<SupplyBufferComponent>())
        return type == ResourceType::FOOD_PROVISIONS
            ? supply->HandleTransport(amount, receiver, *this) : 0;
    return 0;
}

bool Building::CanAcceptResource(ResourceType type) const
{
    if (auto* prod = GetComponent<ProductionComponent>())
        return prod->inputBuffers.contains(type);
    if (auto* storage = GetComponent<StorageComponent>())
        return storage->CanAccept(type);
    if (GetComponent<SupplyBufferComponent>() != nullptr ||
        GetComponent<PopulationComponent>() != nullptr)
        return type == ResourceType::FOOD_PROVISIONS;
    return false;
}

bool Building::CanReceiveResource(ResourceType type) const
{
    if (auto* prod = GetComponent<ProductionComponent>())
    {
        auto it = prod->inputBuffers.find(type);
        return it != prod->inputBuffers.end() &&
               static_cast<int>(it->second.buffer.size()) < it->second.bufferSize;
    }
    if (auto* storage = GetComponent<StorageComponent>())
        return storage->CanReceive(type);
    if (auto* supply = GetComponent<SupplyBufferComponent>())
        return type == ResourceType::FOOD_PROVISIONS && supply->CanReceive();
    if (auto* pop = GetComponent<PopulationComponent>())
        return type == ResourceType::FOOD_PROVISIONS &&
               static_cast<int>(pop->foodBuffer.buffer.size()) < pop->foodBuffer.bufferSize;
    return false;
}

std::vector<ResourceBufferView> Building::GetInputBufferViews() const
{
    if (auto* prod = GetComponent<ProductionComponent>())
        return prod->GetInputBufferViews(prod->ingredients);
    if (auto* pop = GetComponent<PopulationComponent>())
        return {{ResourceType::FOOD_PROVISIONS,
                 static_cast<int>(pop->foodBuffer.buffer.size()),
                 pop->foodBuffer.bufferSize,
                 static_cast<int>(std::ceil(pop->foodPackageUpkeep))}};
    return {};
}

std::vector<ResourceBufferView> Building::GetOutputBufferViews() const
{
    if (auto* prod = GetComponent<ProductionComponent>())
        return prod->GetOutputBufferViews(*this);
    if (auto* storage = GetComponent<StorageComponent>())
        return storage->GetBufferViews();
    return {};
}

std::vector<BuildingConnectionView> Building::GetSupplierViews() const
{
    auto* log = GetComponent<LogisticsComponent>();
    auto* prod = GetComponent<ProductionComponent>();
    return (log != nullptr && prod != nullptr) ? log->GetSupplierViews(*prod)
                                               : std::vector<BuildingConnectionView>{};
}

std::vector<BuildingConnectionView> Building::GetReceiverViews() const
{
    auto* log = GetComponent<LogisticsComponent>();
    auto* prod = GetComponent<ProductionComponent>();
    return (log != nullptr && prod != nullptr) ? log->GetReceiverViews(*prod)
                                               : std::vector<BuildingConnectionView>{};
}

bool Building::HasSupplier(ResourceType type) const
{
    auto* log = GetComponent<LogisticsComponent>();
    return log != nullptr && log->HasSupplier(type);
}

bool Building::HasReceiver(ResourceType type) const
{
    auto* log = GetComponent<LogisticsComponent>();
    return log != nullptr && log->HasReceiver(type);
}

bool Building::IsStorageLike() const
{
    return HasComponent<StorageComponent>();
}

float Building::GetProductionProgress() const
{
    auto* prod = GetComponent<ProductionComponent>();
    return prod != nullptr ? prod->GetProgress() : 0.0f;
}

float Building::GetWorkerRatio() const
{
    auto* workers = GetComponent<WorkerComponent>();
    return workers != nullptr ? workers->GetRatio() : 0.0f;
}

int Building::GetAssignedWorkers() const
{
    auto* workers = GetComponent<WorkerComponent>();
    return workers != nullptr ? workers->assigned : 0;
}

int Building::GetWorkerCapacity() const
{
    auto* workers = GetComponent<WorkerComponent>();
    return workers != nullptr ? workers->GetModifiedCapacity(*this) : 0;
}

bool Building::CanBlockProduction() const
{
    return HasComponent<ProductionComponent>();
}

int Building::GetHitPoints() const
{
    auto* territory = GetComponent<TerritoryComponent>();
    return territory != nullptr ? territory->hp : 0;
}

int Building::GetTerritoryRadius() const
{
    auto* territory = GetComponent<TerritoryComponent>();
    return territory != nullptr ? territory->GetRadius(*this) : 0;
}

bool Building::IsProductionStalled() const
{
    auto* prod = GetComponent<ProductionComponent>();
    if (prod == nullptr || IsUnderConstruction() || productionBlocked)
        return false;

    for (const auto& [res, buf] : prod->outputBuffers)
        if (buf.bufferSize > 0 && static_cast<int>(buf.buffer.size()) >= buf.bufferSize)
            return true;

    auto* log = GetComponent<LogisticsComponent>();
    if (log != nullptr && log->requestBlocked) return true;

    auto* workers = GetComponent<WorkerComponent>();
    int cap = workers != nullptr ? workers->GetModifiedCapacity(*this) : 0;
    if (cap > 0 && (workers == nullptr || workers->assigned <= 0)) return true;

    for (const auto& [res, amount] : prod->ingredients)
    {
        auto it = prod->inputBuffers.find(res);
        int stored = it != prod->inputBuffers.end()
            ? static_cast<int>(it->second.buffer.size()) : 0;
        if (stored < amount && (log == nullptr || !log->HasSupplier(res)))
            return true;
    }
    return false;
}

const char* MilitaryUnitLabel(MilitaryUnitType type)
{
    switch (type)
    {
        case MilitaryUnitType::Swordsman: return "Swordsman";
        case MilitaryUnitType::Archer:    return "Archer";
        default:                          return "Militia";
    }
}

double GetBaseRecruitmentTime(MilitaryUnitType type)
{
    switch (type)
    {
        case MilitaryUnitType::Swordsman: return 12.0;
        case MilitaryUnitType::Archer:    return 10.0;
        default:                          return 6.0;
    }
}

int GetBaseRecruitmentManpowerCost(MilitaryUnitType type)
{
    switch (type)
    {
        case MilitaryUnitType::Swordsman: return 10;
        case MilitaryUnitType::Archer:    return 10;
        default:                          return 8;
    }
}

std::vector<std::pair<ResourceType, int>> GetBaseRecruitmentResourceCosts(MilitaryUnitType type)
{
    switch (type)
    {
        case MilitaryUnitType::Swordsman:
            return {{ResourceType::FOOD_PROVISIONS, 6}, {ResourceType::WEAPON_SUPPLY, 6}, {ResourceType::IRON_SWORD, 10}};
        case MilitaryUnitType::Archer:
            return {{ResourceType::FOOD_PROVISIONS, 6}, {ResourceType::BOW, 10}, {ResourceType::ARROWS, 20}};
        default:
            return {{ResourceType::FOOD_PROVISIONS, 4}, {ResourceType::WEAPON_SUPPLY, 5}};
    }
}

SoldierDivision CreateMilitaryDivision(MilitaryUnitType type, int id)
{
    SoldierDivision div;
    div.id   = id;
    div.type = type;
    switch (type)
    {
        case MilitaryUnitType::Swordsman:
            div.manpowerScale = 10; div.maxHealth = 130; div.health = 130;
            div.endurance = 62; div.strength = 22; div.morale = 68;
            div.foodSupplyCapacity = 10; div.foodSupply = 10;
            div.weaponSupplyCapacity = 10; div.weaponSupply = 10;
            div.speedTilesPerMinute = 10.0;
            div.equipment.weapon = ResourceType::IRON_SWORD;
            div.equipment.armor  = ResourceType::TOOLS;
            break;
        case MilitaryUnitType::Archer:
            div.manpowerScale = 10; div.maxHealth = 90; div.health = 90;
            div.endurance = 58; div.strength = 17; div.morale = 64;
            div.foodSupplyCapacity = 10; div.foodSupply = 10;
            div.weaponSupplyCapacity = 10; div.weaponSupply = 10;
            div.speedTilesPerMinute = 12.0;
            div.equipment.rangedWeapon = ResourceType::BOW;
            div.equipment.ammo         = ResourceType::ARROWS;
            break;
        default: // Militia
            div.manpowerScale = 8;  div.maxHealth = 100; div.health = 100;
            div.endurance = 50; div.strength = 10; div.morale = 55;
            div.foodSupplyCapacity = 8; div.foodSupply = 8;
            div.weaponSupplyCapacity = 8; div.weaponSupply = 8;
            div.speedTilesPerMinute = 14.0;
            div.equipment.weapon = ResourceType::WEAPON_SUPPLY;
            break;
    }
    div.stats = MakeDefaultUnitStats(type);
    return div;
}

bool Building::UpdateConstruction(double dt)
{
    if (constructionRemaining <= 0.0) return false;
    constructionRemaining = std::max(0.0, constructionRemaining - dt);
    return constructionRemaining > 0.0;
}

double Building::BeginOperationalUpdate(double dt)
{
    if (dt <= 0.0)          return 0.0;
    if (constructionRemaining <= 0.0) { lifetime += dt; return dt; }

    double before = constructionRemaining;
    constructionRemaining = std::max(0.0, constructionRemaining - dt);
    if (constructionRemaining > 0.0) return 0.0;

    double opDt = std::max(0.0, dt - before);
    lifetime += opDt;
    return opDt;
}

double Building::GetModifiedTransportTime() const
{
    double base = transportTime.GetBase();
    if (const auto* road = GetComponent<RoadComponent>())
        base = road->GetModifiedSpeedModifier(*this) > 0.0 ? base / road->GetModifiedSpeedModifier(*this) : base;

    return owner != nullptr
        ? owner->ModifyBalanceForBuilding(transportTime.GetStatId(), base, this)
        : base;
}

void Building::UpdateTransportables(double dt)
{
    for (auto it = transportables.begin(); it != transportables.end();)
    {
        bool done = (*it)->Update(dt);
        if (done)
        {
            auto* res = dynamic_cast<Resource*>(*it);
            std::string resName = res != nullptr ? rt2s(res->type) : "Transportable";
            Log::Msg(tag, "resource ", resName, " deleted from transportables; pos: ",
                     (*it)->map->GetCoordsFromId(positionId));
            it = transportables.erase(it);
            continue;
        }
        ++it;
    }
}

void Building::ReceptTransport(Transportable* trans)
{
    trans->elapsedTime   = 0.0;
    trans->transportTime = GetModifiedTransportTime();

    if (trans->sourceBuilding != this)
        trans->currentPathStep++;

    if (trans->targetBuilding == this)
    {
        auto* ptr = dynamic_cast<Resource*>(trans);
        if (ptr != nullptr)
        {
            Log::Msg(tag, "Transport of ", rt2s(ptr->type), " finished, adding resource; ID:",
                     positionId, " pos: ", trans->map->GetCoordsFromId(positionId));
            AddResource(ptr);
        }
    }
    else
    {
        transportables.push_back(trans);
        auto* res = dynamic_cast<Resource*>(trans);
        std::string resName = res != nullptr ? rt2s(res->type) : "Transportable";
        Log::Msg(tag, resName, " pushed into transportables; ID: ", positionId, " pos: ",
                 trans->map->GetCoordsFromId(positionId));
    }
}

// ─── Road ────────────────────────────────────────────────────────────────────

Road::Road(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Road);
    ApplyBuildingDefinition(*this, def);
    road.upgradeLevel  = def.road.upgradeLevel;
    road.maxCapacity   = def.road.maxCapacity;
    road.speedModifier = def.road.speedModifier;
    RegisterComponent(&road);
}

int Road::GetModifiedMaxCapacity() const
{
    return road.GetModifiedMaxCapacity(*this);
}

double Road::GetModifiedSpeedModifier() const
{
    return road.GetModifiedSpeedModifier(*this);
}

// ─── StorageBuilding ─────────────────────────────────────────────────────────

StorageBuilding::StorageBuilding(int actualId)
{
    id = actualId;
    RegisterComponent(&storage);
    const auto& def = GetBuildingDefinition(BuildingType::StorageBuilding);
    ApplyBuildingDefinition(*this, def);
    ApplyStorageDefinition(*this, def);
}

// ─── Headquarters ────────────────────────────────────────────────────────────

Headquarters::Headquarters(int actualId)
{
    id = actualId;
    RegisterComponent(&storage);
    RegisterComponent(&territory);
    RegisterComponent(&garrison);
    const auto& def = GetBuildingDefinition(BuildingType::Headquarters);
    ApplyBuildingDefinition(*this, def);
    ApplyStorageDefinition(*this, def);
    territory.radius = def.military.territoryRadius;
    territory.hp     = def.military.hitPoints;
    territory.maxHp  = def.military.hitPoints;
}

// ─── Village ─────────────────────────────────────────────────────────────────

Village::Village(int actualId)
{
    id = actualId;
    const auto& def = GetBuildingDefinition(BuildingType::Village);
    ApplyBuildingDefinition(*this, def);
    population.manpowerRate        = def.village.manpowerRate;
    population.populationCap       = def.village.populationCap;
    population.upkeepInterval      = def.village.upkeepInterval;
    population.foodPackageUpkeep   = def.village.foodPackageUpkeep;

    RegisterComponent(&population);
}

// ─── GuardTower / Fortress / Castle ──────────────────────────────────────────

GuardTower::GuardTower(int actualId)
{
    id = actualId;
    RegisterComponent(&territory);
    RegisterComponent(&garrison);
    RegisterComponent(&supplyBuffer);
    const auto& def = GetBuildingDefinition(BuildingType::GuardTower);
    ApplyBuildingDefinition(*this, def);
    ApplyMilitaryDefinition(*this, def);
}

Fortress::Fortress(int actualId)
{
    id = actualId;
    RegisterComponent(&territory);
    RegisterComponent(&garrison);
    RegisterComponent(&supplyBuffer);
    const auto& def = GetBuildingDefinition(BuildingType::Fortress);
    ApplyBuildingDefinition(*this, def);
    ApplyMilitaryDefinition(*this, def);
}

Castle::Castle(int actualId)
{
    id = actualId;
    RegisterComponent(&territory);
    RegisterComponent(&garrison);
    RegisterComponent(&supplyBuffer);
    const auto& def = GetBuildingDefinition(BuildingType::Castle);
    ApplyBuildingDefinition(*this, def);
    ApplyMilitaryDefinition(*this, def);
}

// ─── Barracks ────────────────────────────────────────────────────────────────

Barracks::Barracks(int actualId)
{
    id = actualId;
    RegisterComponent(&territory);
    RegisterComponent(&garrison);
    RegisterComponent(&supplyBuffer);
    RegisterComponent(&recruitment);
    const auto& def = GetBuildingDefinition(BuildingType::Barracks);
    ApplyBuildingDefinition(*this, def);
    ApplyMilitaryDefinition(*this, def);
}

// ─── SupplyHub ───────────────────────────────────────────────────────────────

SupplyHub::SupplyHub(int actualId)
{
    id = actualId;
    RegisterComponent(&packaging);
    const auto& def = GetBuildingDefinition(BuildingType::SupplyHub);
    ApplyBuildingDefinition(*this, def);
}

// ─── Concrete ProductionBuilding subclasses ───────────────────────────────────

Woodcutter::Woodcutter(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Woodcutter);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

HuntersHut::HuntersHut(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::HuntersHut);
    ApplyBuildingDefinition(*this, def);
    production.consumesTerrain = false;
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
}

void HuntersHut::InitBuilding(TileType tile)
{
    production.terrainType = tile;
    if (const auto* tp = FindTerrainProductionDefinition(BuildingType::HuntersHut, tile))
        ApplyProductionDefinition(*this, tp->production);
}

LumberMill::LumberMill(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::LumberMill);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Mine::Mine(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Mine);
    ApplyBuildingDefinition(*this, def);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
}

void Mine::InitBuilding(TileType tile)
{
    production.terrainType = tile;
    if (const auto* tp = FindTerrainProductionDefinition(BuildingType::Mine, tile))
        ApplyProductionDefinition(*this, tp->production);
}

Foundry::Foundry(int ajdi)
{
    id = ajdi;
    const auto& def = GetBuildingDefinition(BuildingType::Foundry);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Well::Well(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Well);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

WheatFarm::WheatFarm(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::WheatFarm);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Windmill::Windmill(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Windmill);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Bakery::Bakery(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Bakery);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Inn::Inn(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Inn);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Paperworks::Paperworks(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Paperworks);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Smith::Smith(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Smith);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

University::University(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::University);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    RegisterComponent(&research);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}
