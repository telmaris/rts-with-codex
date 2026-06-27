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
    // Initializes MilitaryOrderName.
    const char* MilitaryOrderName(MilitaryOrderType order)
    {
        switch (order)
        {
            case MilitaryOrderType::Attack: return "Attack";
            case MilitaryOrderType::Support: return "Support";
            case MilitaryOrderType::Defend: return "Defend";
            case MilitaryOrderType::None:
            default: return "None";
        }
    }

    // Initializes CountIncomingResources.
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

            for (auto* transportable : carrier->transportables)
            {
                auto* resource = dynamic_cast<Resource*>(transportable);
                if (resource != nullptr && resource->targetBuilding == target && resource->type == type)
                    incoming++;
            }
        }

        return incoming;
    }

    // Rebuilds legacy troop counters from the division list.
    void RecountDivisionTypes(MilitaryBuilding& building)
    {
        building.militia = 0;
        building.swordsmen = 0;
        building.archers = 0;
        for (const auto& division : building.divisions)
        {
            switch (division.type)
            {
                case MilitaryUnitType::Swordsman: building.swordsmen++; break;
                case MilitaryUnitType::Archer: building.archers++; break;
                case MilitaryUnitType::Militia:
                default: building.militia++; break;
            }
        }
        building.garrison = building.GetTotalTroops();
    }

    // Returns one division's attack damage with placeholder supply scaling.
    int DivisionAttackDamage(const MilitaryDivision& division)
    {
        float health = division.HealthRatio();
        float weaponSupply = division.weaponSupplyCapacity > 0
            ? std::clamp(division.weaponSupply / static_cast<float>(division.weaponSupplyCapacity), 0.25f, 1.0f)
            : 1.0f;
        return std::max(1, static_cast<int>(std::round(division.strength * health * weaponSupply)));
    }

    // Returns travel delay in seconds based on tile distance and division speed.
    double DivisionTravelDelaySeconds(const MilitaryBuilding& source, const MilitaryDivision& division, int targetPositionId)
    {
        if (source.owner == nullptr || targetPositionId < 0 || division.speedTilesPerMinute <= 0.0)
            return 0.0;

        Vec2i from = source.owner->tilemap.GetCoordsFromId(source.positionId);
        Vec2i to = source.owner->tilemap.GetCoordsFromId(targetPositionId);
        double dx = static_cast<double>(to.x - from.x);
        double dy = static_cast<double>(to.y - from.y);
        double distance = std::sqrt(dx * dx + dy * dy);
        return distance / division.speedTilesPerMinute * 60.0;
    }
}

// Returns lifetime efficiency as active time divided by total lifetime.
float Building::GetEfficiency() const
{
    if (lifetime <= 0.0)
        return 0.0f;

    return static_cast<float>(activeTime / lifetime);
}

// Returns construction progress normalized to the 0..1 range.
float Building::GetConstructionProgress() const
{
    double baseBuildTime = buildTime.GetBase();
    if (baseBuildTime <= 0.0)
        return 1.0f;

    return static_cast<float>(std::clamp((baseBuildTime - constructionRemaining) / baseBuildTime, 0.0, 1.0));
}

// Returns a readable UI/debug label for a military unit type.
const char* MilitaryUnitLabel(MilitaryUnitType type)
{
    switch (type)
    {
        case MilitaryUnitType::Swordsman: return "Swordsman";
        case MilitaryUnitType::Archer: return "Archer";
        case MilitaryUnitType::Militia:
        default: return "Militia";
    }
}

// Returns base recruitment duration before player modifiers.
double GetBaseRecruitmentTime(MilitaryUnitType type)
{
    switch (type)
    {
        case MilitaryUnitType::Swordsman: return 12.0;
        case MilitaryUnitType::Archer: return 10.0;
        case MilitaryUnitType::Militia:
        default: return 6.0;
    }
}

// Returns base manpower cost before player modifiers.
int GetBaseRecruitmentManpowerCost(MilitaryUnitType type)
{
    switch (type)
    {
        case MilitaryUnitType::Swordsman: return 10;
        case MilitaryUnitType::Archer: return 10;
        case MilitaryUnitType::Militia:
        default: return 8;
    }
}

// Returns placeholder division-level material recruitment costs.
std::vector<std::pair<ResourceType, int>> GetBaseRecruitmentResourceCosts(MilitaryUnitType type)
{
    switch (type)
    {
        case MilitaryUnitType::Swordsman:
            return {{ResourceType::FOOD_PROVISIONS, 6}, {ResourceType::WEAPON_SUPPLY, 6}, {ResourceType::IRON_SWORD, 10}};
        case MilitaryUnitType::Archer:
            return {{ResourceType::FOOD_PROVISIONS, 6}, {ResourceType::BOW, 10}, {ResourceType::ARROWS, 20}};
        case MilitaryUnitType::Militia:
        default:
            return {{ResourceType::FOOD_PROVISIONS, 4}, {ResourceType::WEAPON_SUPPLY, 5}};
    }
}

// Creates a placeholder division template for a trained unit type.
SoldierDivision CreateMilitaryDivision(MilitaryUnitType type, int id)
{
    SoldierDivision division;
    division.id = id;
    division.type = type;
    switch (type)
    {
        case MilitaryUnitType::Swordsman:
            division.manpowerScale = 10;
            division.maxHealth = 130;
            division.health = 130;
            division.endurance = 62;
            division.strength = 22;
            division.morale = 68;
            division.foodSupplyCapacity = 10;
            division.foodSupply = 10;
            division.weaponSupplyCapacity = 10;
            division.weaponSupply = 10;
            division.speedTilesPerMinute = 10.0;
            division.equipment.weapon = ResourceType::IRON_SWORD;
            division.equipment.armor = ResourceType::TOOLS;
            break;
        case MilitaryUnitType::Archer:
            division.manpowerScale = 10;
            division.maxHealth = 90;
            division.health = 90;
            division.endurance = 58;
            division.strength = 17;
            division.morale = 64;
            division.foodSupplyCapacity = 10;
            division.foodSupply = 10;
            division.weaponSupplyCapacity = 10;
            division.weaponSupply = 10;
            division.speedTilesPerMinute = 12.0;
            division.equipment.rangedWeapon = ResourceType::BOW;
            division.equipment.ammo = ResourceType::ARROWS;
            break;
        case MilitaryUnitType::Militia:
        default:
            division.manpowerScale = 8;
            division.maxHealth = 100;
            division.health = 100;
            division.endurance = 50;
            division.strength = 10;
            division.morale = 55;
            division.foodSupplyCapacity = 8;
            division.foodSupply = 8;
            division.weaponSupplyCapacity = 8;
            division.weaponSupply = 8;
            division.speedTilesPerMinute = 14.0;
            division.equipment.weapon = ResourceType::WEAPON_SUPPLY;
            break;
    }
    return division;
}

// Advances UpdateConstruction for one frame or simulation tick.
bool Building::UpdateConstruction(double dt)
{
    if (constructionRemaining <= 0.0)
        return false;

    constructionRemaining = std::max(0.0, constructionRemaining - dt);
    return constructionRemaining > 0.0;
}

// Advances construction first and returns the operational time left in this tick.
double Building::BeginOperationalUpdate(double dt)
{
    if (dt <= 0.0)
        return 0.0;

    if (constructionRemaining <= 0.0)
    {
        lifetime += dt;
        return dt;
    }

    double constructionBefore = constructionRemaining;
    constructionRemaining = std::max(0.0, constructionRemaining - dt);
    if (constructionRemaining > 0.0)
        return 0.0;

    double operationalDt = std::max(0.0, dt - constructionBefore);
    lifetime += operationalDt;
    return operationalDt;
}

// Returns transport time after road and owner modifiers are applied.
double Building::GetModifiedTransportTime() const
{
    double base = transportTime.GetBase();
    if (const auto* road = dynamic_cast<const Road*>(this))
        base = road->GetModifiedSpeedModifier() > 0.0 ? base / road->GetModifiedSpeedModifier() : base;

    return owner != nullptr
        ? owner->ModifyBalanceForBuilding(transportTime.GetStatId(), base, this)
        : base;
}

// Advances UpdateTransportables for one frame or simulation tick.
void Building::UpdateTransportables(double dt)
{
    for (auto it = transportables.begin(); it != transportables.end();)
    {
        bool flag = (*it)->Update(dt);
        if (flag == true)
        {
            auto* resource = dynamic_cast<Resource*>(*it);
            std::string resourceName = resource != nullptr ? rt2s(resource->type) : "Transportable";
            Log::Msg(tag, "resource ", resourceName, " deleted from transportables vector! position: ", (*it)->map->GetCoordsFromId(positionId));
            transportables.erase(it);
            continue;
        }
        it++;
    }
}

// Receives a transportable arriving at this building or road.
void Building::ReceptTransport(Transportable* trans)
{
    trans->elapsedTime = 0.0;
    trans->transportTime = GetModifiedTransportTime();
    
    if(trans->sourceBuilding != this)
    {
        trans->currentPathStep++;
    }

    if(trans->targetBuilding == this)
    {
        auto ptr = dynamic_cast<Resource*>(trans);
        if(ptr != nullptr)
        {
            Log::Msg(tag, "Transport of ", rt2s(ptr->type)," finished, adding resource to the buffer! at ID:", positionId, " position: ", trans->map->GetCoordsFromId(positionId));
            AddResource(ptr);
        }
    }
    else 
    {
        transportables.push_back(trans);
        auto* resource = dynamic_cast<Resource*>(trans);
        std::string resourceName = resource != nullptr ? rt2s(resource->type) : "Transportable";
        Log::Msg(tag, resourceName, " pushed into transportables vector! at ID: ", positionId, " position: ", trans->map->GetCoordsFromId(positionId));
    }

}

// Initializes ProductionBuilding::ProductionBuilding.
ProductionBuilding::ProductionBuilding(int ajdi)
{
    id = ajdi;
    type = TileType::GRASS;
    name = "ProductionBuilding";
    tag = "[ProductionBuilding]";
    buildingType = BuildingType::ProductionBuilding;
}

// Advances this object's state for one frame.
void ProductionBuilding::Update(double dt)
{
    double operationalDt = BeginOperationalUpdate(dt);
    if (operationalDt <= 0.0)
        return;

    if (owner != nullptr)
        owner->AutoAssignWorkers(this);

    if (GetWorkerRatio() > 0.0f)
        MaintainInputRequests();
    Produce(operationalDt);
    HandleTransport();
    UpdateTransportables(operationalDt);
}

// Initializes ProductionBuilding::Produce.
void ProductionBuilding::Produce(double dt)
{
    if (productionBlocked)
        return;

    double workerEfficiency = owner != nullptr ? GetWorkerRatio() * owner->GetFoodProductivity() : GetWorkerRatio();
    if (workerEfficiency <= 0.0)
        return;

    bool terrainBasedProduction = ingredients.empty() && type != TileType::GRASS;

    double effectiveProductionTime = GetModifiedProductionTime();
    if (productionStarted)
    {
        if (elapsedTime >= effectiveProductionTime)
        {
            for (auto &[resource, amount] : products)
            {
                int modifiedAmount = GetModifiedOutputAmount(resource, amount);
                for (int i = 0; i < modifiedAmount; i++)
                {
                    if (terrainBasedProduction && ShouldConsumeTerrainRichness() && !ConsumeTerrainRichness())
                        break;

                    outputBuffers[resource].GenerateResource(resource);
                    totalProduced++;
                    Log::Msg(tag, "Created a resource: ", rt2s(resource));
                }
            }

            productionStarted = false;
            elapsedTime = 0.0;
        }
        else
        {
            elapsedTime += dt * workerEfficiency;
            activeTime += dt * workerEfficiency;
        }
    }
    else
    {
        for (auto &[resource, buffer] : outputBuffers)
        {
            if (buffer.buffer.size() >= buffer.bufferSize)
            {
                return;
            }
        }

        bool canStart = true;
        if (terrainBasedProduction && !HasTerrainRichness())
            canStart = false;

        for (auto &[resource, amount] : ingredients)
        {
            if (inputBuffers[resource].buffer.size() < amount)
            {
                canStart = false;
                int amountcalculus = inputBuffers[resource].bufferSize - inputBuffers[resource].buffer.size();
                RequestResource(resource, amountcalculus);
            }
        }   
                    
        if (canStart)
        {
            for (auto &[resource, amount] : ingredients)
            {
                for (int i = 0; i < amount; i++)
                {
                    inputBuffers[resource].FreeResource();
                }
                RequestResource(resource, amount);
            }
            for (auto &[resource, amount] : products)
            {
                Log::Msg(tag, "Production of ", rt2s(resource), " started");
            }
            
            productionStarted = true;
        }
    }
}

// Handles the requested event or transfer.
void ProductionBuilding::HandleTransport()
{
    for(auto& [resource, receiver] : receiversMap)
    {
        if (receiver == nullptr)
            continue;

        if(receiver->IsStorageLike())
        {
            while (receiver->CanReceiveResource(resource))
            {
                auto [isAvailable, res] = outputBuffers[resource].GetResource();
                if (!isAvailable)
                    break;

                Log::Msg(tag, "ID: ", id, " ", rt2s(res->type), " transport started to ", receiver->name, " with ID ", receiver->id);
                if (!owner->BeginTransport(this, receiver, res))
                {
                    outputBuffers[resource].AddResource(res);
                    break;
                }
            }
        }
    }
}

// Returns whether this condition is currently true.
bool ProductionBuilding::HasTerrainRichness() const
{
    if (owner == nullptr || positionId < 0 || type == TileType::GRASS)
        return false;

    Vec2i anchor = owner->tilemap.GetCoordsFromId(positionId);
    for (int y = 0; y < footprint.y; y++)
    {
        for (int x = 0; x < footprint.x; x++)
        {
            Vec2i pos{anchor.x + x, anchor.y + y};
            if (!owner->tilemap.IsInside(pos))
                continue;

            const Tile& tile = owner->tilemap.tilemap[owner->tilemap.GetIdFromCoords(pos)];
            if (tile.tileType == type && tile.resourceRichness > 0)
                return true;
        }
    }
    return false;
}

// Consumes available resources or map richness.
bool ProductionBuilding::ConsumeTerrainRichness()
{
    if (owner == nullptr || positionId < 0 || type == TileType::GRASS)
        return false;

    Vec2i anchor = owner->tilemap.GetCoordsFromId(positionId);
    for (int y = 0; y < footprint.y; y++)
    {
        for (int x = 0; x < footprint.x; x++)
        {
            Vec2i pos{anchor.x + x, anchor.y + y};
            if (!owner->tilemap.IsInside(pos))
                continue;

            Tile& tile = owner->tilemap.tilemap[owner->tilemap.GetIdFromCoords(pos)];
            if (tile.tileType != type || tile.resourceRichness <= 0)
                continue;

            tile.resourceRichness--;
            if (tile.resourceRichness <= 0)
            {
                tile.tileType = TileType::GRASS;
                std::mt19937 rng(static_cast<unsigned int>(tile.id + owner->tilemap.params.seed));
                tile.terrainTextureId = owner->tilemap.PickTerrainTexture(TileType::GRASS, rng);
                owner->tilemap.terrainDirty = true;
            }
            return true;
        }
    }

    return false;
}

// Initializes ProductionBuilding::MaintainInputRequests.
void ProductionBuilding::MaintainInputRequests()
{
    inputRequestBlocked = false;
    for (auto& [resource, buffer] : inputBuffers)
    {
        int stored = static_cast<int>(buffer.buffer.size());
        int pending = CountIncomingResources(this, resource);
        pendingInputRequests[resource] = pending;
        int missing = buffer.bufferSize - stored - pending;
        if (missing > 0)
            RequestResource(resource, missing);
    }
}

// Handles the requested event or transfer.
int ProductionBuilding::HandleTransport(ResourceType resource, int amount, Building* receiver)
{
    int sent = 0;
    for(int i = 0; i<amount; i++)
    {
        auto [isAvailable, res] = outputBuffers[resource].GetResource();
        if(isAvailable)
        {
            if (receiver == nullptr || !receiver->CanReceiveResource(resource))
            {
                outputBuffers[resource].AddResource(res);
                break;
            }

            Log::Msg(tag, "ID: ", id, " ", rt2s(res->type), " transport started to ", receiver->name, " with ID ", receiver->id);
            if (owner->BeginTransport(this, receiver, res))
                sent++;
            else
                outputBuffers[resource].AddResource(res);
        }
    }
    return sent;
}

// Initializes ProductionBuilding::RequestResource.
int ProductionBuilding::RequestResource(ResourceType ResType, int amount)
{
    if(amount <= 0)
        return 0;

    if (!suppliersMap.contains(ResType))
    {
        inputRequestBlocked = true;
        return 0;
    }

    int sent = 0;
    auto& suppliers = suppliersMap[ResType];
    for (auto* supplier : suppliers)
    {
        if (supplier == nullptr)
            continue;

        int missing = amount - sent;
        if (missing <= 0)
            break;

        sent += supplier->HandleTransport(ResType, missing, this);
    }

    if (sent < amount)
        inputRequestBlocked = true;

    pendingInputRequests[ResType] += sent;
    return sent;
}

// Adds this object or value to local state.
void ProductionBuilding::AddResource(Resource* res)
{
    if (res == nullptr)
        return;

    auto bufferIt = inputBuffers.find(res->type);
    if (bufferIt == inputBuffers.end() || static_cast<int>(bufferIt->second.buffer.size()) >= bufferIt->second.bufferSize)
    {
        if (res->sourceBuilding != nullptr)
            res->sourceBuilding->ReturnOutgoingResource(res);
        CancelRequestedResource(res->type);
        return;
    }

    Log::Msg(tag, "resource added!");
    bufferIt->second.AddResource(res);
    auto pending = pendingInputRequests.find(res->type);
    if (pending != pendingInputRequests.end() && pending->second > 0)
        pending->second--;
}

// Initializes ProductionBuilding::ReturnOutgoingResource.
void ProductionBuilding::ReturnOutgoingResource(Resource* res)
{
    if (res == nullptr)
        return;

    outputBuffers[res->type].AddResource(res);
}

// Returns whether this condition is currently true.
void ProductionBuilding::CancelRequestedResource(ResourceType type)
{
    auto pending = pendingInputRequests.find(type);
    if (pending != pendingInputRequests.end() && pending->second > 0)
        pending->second--;
}

// Removes and returns one resource from the requested input buffer.
Resource ProductionBuilding::GetResource(ResourceType type)
{
    auto [isAvailable, resource] = inputBuffers[type].GetResource();
    return isAvailable ? *resource : Resource{};
}

// Returns UI snapshots for production input buffers.
std::vector<ResourceBufferView> ProductionBuilding::GetInputBufferViews() const
{
    std::vector<ResourceBufferView> result;
    for (const auto &[resource, buffer] : inputBuffers)
    {
        int recipeAmount = 0;
        auto recipeIt = ingredients.find(resource);
        if (recipeIt != ingredients.end())
            recipeAmount = recipeIt->second;

        result.push_back({resource, static_cast<int>(buffer.buffer.size()), buffer.bufferSize, recipeAmount});
    }
    return result;
}

// Returns UI snapshots for production output buffers.
std::vector<ResourceBufferView> ProductionBuilding::GetOutputBufferViews() const
{
    std::vector<ResourceBufferView> result;
    for (const auto &[resource, buffer] : outputBuffers)
    {
        int recipeAmount = 0;
        auto recipeIt = products.find(resource);
        if (recipeIt != products.end())
            recipeAmount = GetModifiedOutputAmount(resource, recipeIt->second);

        result.push_back({resource, static_cast<int>(buffer.buffer.size()), buffer.bufferSize, recipeAmount});
    }
    return result;
}

// Returns UI snapshots for current suppliers.
std::vector<BuildingConnectionView> ProductionBuilding::GetSupplierViews() const
{
    std::vector<BuildingConnectionView> result;
    for (const auto &[resource, amount] : ingredients)
    {
        auto it = suppliersMap.find(resource);
        if (it == suppliersMap.end() || it->second.empty())
        {
            result.push_back({resource, nullptr});
            continue;
        }

        for (auto* supplier : it->second)
            result.push_back({resource, supplier});
    }
    return result;
}

// Returns UI snapshots for current receivers.
std::vector<BuildingConnectionView> ProductionBuilding::GetReceiverViews() const
{
    std::vector<BuildingConnectionView> result;
    for (const auto &[resource, amount] : products)
    {
        auto it = receiversMap.find(resource);
        result.push_back({resource, it != receiversMap.end() ? it->second : nullptr});
    }
    return result;
}

// Returns whether this condition is currently true.
bool ProductionBuilding::HasSupplier(ResourceType type) const
{
    auto it = suppliersMap.find(type);
    if (it == suppliersMap.end())
        return false;

    for (auto* supplier : it->second)
    {
        if (supplier != nullptr)
            return true;
    }

    return false;
}

// Returns whether this condition is currently true.
bool ProductionBuilding::HasReceiver(ResourceType type) const
{
    return receiversMap.contains(type) && receiversMap.at(type) != nullptr;
}

// Returns whether this condition is currently true.
bool ProductionBuilding::CanAcceptResource(ResourceType type) const
{
    return inputBuffers.contains(type);
}

// Returns whether this condition is currently true.
bool ProductionBuilding::CanReceiveResource(ResourceType type) const
{
    auto it = inputBuffers.find(type);
    return it != inputBuffers.end() && static_cast<int>(it->second.buffer.size()) < it->second.bufferSize;
}

std::string ProductionBuilding::GetActiveRecipeName() const
{
    if (activeRecipeIndex < 0 || activeRecipeIndex >= static_cast<int>(recipes.size()))
        return "Default";
    return recipes[activeRecipeIndex].name.empty() ? "Default" : recipes[activeRecipeIndex].name;
}

void ProductionBuilding::SetRecipes(std::vector<ProductionRecipeRuntime> newRecipes)
{
    recipes = std::move(newRecipes);
    activeRecipeIndex = 0;
    if (!recipes.empty())
        SetActiveRecipe(0);
}

bool ProductionBuilding::SetActiveRecipe(int index)
{
    if (index < 0 || index >= static_cast<int>(recipes.size()))
        return false;

    for (auto& [resource, buffer] : inputBuffers)
        buffer.Clear();
    for (auto& [resource, buffer] : outputBuffers)
        buffer.Clear();

    activeRecipeIndex = index;
    const auto& recipe = recipes[activeRecipeIndex];
    productionTime = recipe.cycleTime;
    ingredients = recipe.inputs;
    products = recipe.outputs;
    inputBuffers.clear();
    outputBuffers.clear();
    suppliersMap.clear();
    receiversMap.clear();
    pendingInputRequests.clear();
    inputRequestBlocked = false;
    elapsedTime = 0.0;
    productionStarted = false;

    for (const auto& [resource, capacity] : recipe.inputBufferCapacities)
        inputBuffers[resource] = ResourceBuffer{resource, capacity};
    for (const auto& [resource, capacity] : recipe.outputBufferCapacities)
        outputBuffers[resource] = ResourceBuffer{resource, capacity};

    workerCapacity = std::max(0, recipe.workerCapacity);
    assignedWorkers = std::min(assignedWorkers, workerCapacity.GetBase());
    return true;
}

bool ProductionBuilding::CycleRecipe()
{
    if (recipes.size() <= 1)
        return false;
    int next = (activeRecipeIndex + 1) % static_cast<int>(recipes.size());
    return SetActiveRecipe(next);
}

bool ProductionBuilding::StartTechnologyResearch(const std::string& id, double totalTime)
{
    if (!activeTechnologyId.empty())
        return false;

    activeTechnologyId = id;
    activeTechnologyTotal = std::max(0.0, totalTime);
    activeTechnologyRemaining = activeTechnologyTotal;
    return true;
}

bool ProductionBuilding::UpdateTechnologyResearch(double dt)
{
    if (activeTechnologyId.empty())
        return false;

    activeTechnologyRemaining = std::max(0.0, activeTechnologyRemaining - std::max(0.0, dt));
    if (activeTechnologyRemaining > 0.0)
        return false;

    activeTechnologyRemaining = 0.0;
    return true;
}

double ProductionBuilding::GetActiveTechnologyProgress() const
{
    if (activeTechnologyId.empty() || activeTechnologyTotal <= 0.0)
        return 0.0;
    return std::clamp(1.0 - activeTechnologyRemaining / activeTechnologyTotal, 0.0, 1.0);
}

// Returns assigned worker coverage normalized to the 0..1 range.
float ProductionBuilding::GetWorkerRatio() const
{
    int capacity = GetWorkerCapacity();
    if (capacity <= 0)
        return 1.0f;

    return std::clamp(assignedWorkers / static_cast<float>(capacity), 0.0f, 1.0f);
}

// Returns current production cycle progress normalized to the 0..1 range.
float ProductionBuilding::GetProductionProgress() const
{
    double effectiveProductionTime = GetModifiedProductionTime();
    if (!productionStarted || effectiveProductionTime <= 0.0)
        return 0.0f;

    return std::clamp(static_cast<float>(elapsedTime / effectiveProductionTime), 0.0f, 1.0f);
}

// Returns whether this condition is currently true.
bool ProductionBuilding::IsProductionStalled() const
{
    if (IsUnderConstruction() || productionBlocked)
        return false;

    for (const auto& [resource, buffer] : outputBuffers)
    {
        if (buffer.bufferSize > 0 && static_cast<int>(buffer.buffer.size()) >= buffer.bufferSize)
            return true;
    }

    if (inputRequestBlocked)
        return true;

    if (GetWorkerCapacity() > 0 && assignedWorkers <= 0)
        return true;

    for (const auto& [resource, amount] : ingredients)
    {
        auto bufferIt = inputBuffers.find(resource);
        int stored = bufferIt != inputBuffers.end() ? static_cast<int>(bufferIt->second.buffer.size()) : 0;
        if (stored < amount && !HasSupplier(resource))
            return true;
    }

    return false;
}

// Returns worker capacity after owner modifiers are applied.
int ProductionBuilding::GetWorkerCapacity() const
{
    return owner != nullptr
        ? owner->ResolveStat(workerCapacity, this, ResourceType::Null, std::nullopt, 0)
        : workerCapacity.GetBase();
}

// Returns production cycle time after owner modifiers are applied.
double ProductionBuilding::GetModifiedProductionTime() const
{
    return owner != nullptr
        ? owner->ResolveStat(productionTime, this)
        : productionTime.GetBase();
}

// Returns real-time seconds needed to finish one cycle after worker and food efficiency.
double ProductionBuilding::GetEffectiveProductionCycleTime() const
{
    double modifiedTime = GetModifiedProductionTime();
    if (modifiedTime <= 0.0)
        return 0.0;

    double workerEfficiency = GetWorkerRatio();
    if (owner != nullptr)
        workerEfficiency *= owner->GetFoodProductivity();

    if (workerEfficiency <= 0.0)
        return std::numeric_limits<double>::infinity();

    return modifiedTime / workerEfficiency;
}

// Returns produced amount after owner modifiers are applied.
int ProductionBuilding::GetModifiedOutputAmount(ResourceType type, int baseAmount) const
{
    return owner != nullptr
        ? owner->ModifyBalanceIntForBuilding(BalanceStat::ProductionOutputAmount, baseAmount, this, type, std::nullopt, 0)
        : baseAmount;
}

// Updates the requested state value.
void ProductionBuilding::SetSupplier(ResourceType type, Building* supplier)
{
    if (supplier == nullptr)
        return;

    auto& suppliers = suppliersMap[type];
    bool changedSuppliers = false;
    if (!supplier->IsStorageLike())
    {
        size_t before = suppliers.size();
        suppliers.erase(
            std::remove_if(suppliers.begin(), suppliers.end(), [](Building* existing)
            {
                return existing != nullptr && existing->IsStorageLike();
            }),
            suppliers.end());
        changedSuppliers = suppliers.size() != before;
    }

    if (std::find(suppliers.begin(), suppliers.end(), supplier) == suppliers.end())
    {
        suppliers.push_back(supplier);
        changedSuppliers = true;
    }

    if (changedSuppliers)
        pendingInputRequests[type] = CountIncomingResources(this, type);
}

// Updates the requested state value.
void ProductionBuilding::SetReceiver(ResourceType type, Building* receiver)
{
    auto it = receiversMap.find(type);
    Building* previousReceiver = (it != receiversMap.end()) ? it->second : nullptr;
    if (previousReceiver != nullptr && previousReceiver != receiver)
    {
        previousReceiver->RemoveSupplier(type, this);
        if (previousReceiver->owner != nullptr && previousReceiver->CanAcceptResource(type) && !previousReceiver->HasSupplier(type))
        {
            Building* storage = previousReceiver->owner->tilemap.FindNearestStorage(previousReceiver, previousReceiver->owner);
            if (storage != nullptr && storage != previousReceiver)
                previousReceiver->SetSupplier(type, storage);
        }
    }

    receiversMap[type] = receiver;
    if (receiver != nullptr)
        receiver->SetSupplier(type, this);
}

// Removes this relationship or runtime state.
void ProductionBuilding::RemoveSupplier(ResourceType type, Building* supplier)
{
    auto it = suppliersMap.find(type);
    if (it == suppliersMap.end())
        return;

    auto& suppliers = it->second;
    suppliers.erase(std::remove(suppliers.begin(), suppliers.end(), supplier), suppliers.end());
    if (suppliers.empty())
        suppliersMap.erase(it);

    pendingInputRequests.erase(type);
}

// Removes this relationship or runtime state.
void ProductionBuilding::RemoveReceiver(ResourceType type, Building* receiver)
{
    auto it = receiversMap.find(type);
    if (it == receiversMap.end() || it->second != receiver)
        return;

    receiversMap.erase(it);
}

// Receives a transportable arriving at this building or road.
void ProductionBuilding::ReceptTransport(Transportable* trans)
{
}

// Initializes Woodcutter::Woodcutter.
Woodcutter::Woodcutter(int i)
{
    id = i;
    const auto& definition = GetBuildingDefinition(BuildingType::Woodcutter);
    ApplyBuildingDefinition(*this, definition);
    ApplyProductionDefinition(*this, definition.production);
    ApplyProductionRecipes(*this, definition);
}

// Initializes HuntersHut::HuntersHut.
HuntersHut::HuntersHut(int i)
{
    id = i;
    const auto& definition = GetBuildingDefinition(BuildingType::HuntersHut);
    ApplyBuildingDefinition(*this, definition);
}

// Initializes runtime state for this object.
void HuntersHut::InitBuilding(TileType tile)
{
    type = tile;
    if (const auto* terrainProduction = FindTerrainProductionDefinition(BuildingType::HuntersHut, type))
        ApplyProductionDefinition(*this, terrainProduction->production);
}

// Initializes LumberMill::LumberMill.
LumberMill::LumberMill(int i)
{
    id = i;
    const auto& definition = GetBuildingDefinition(BuildingType::LumberMill);
    ApplyBuildingDefinition(*this, definition);
    ApplyProductionDefinition(*this, definition.production);
    ApplyProductionRecipes(*this, definition);
}

// Initializes Mine::Mine.
Mine::Mine(int i)
{
    id = i;
    const auto& definition = GetBuildingDefinition(BuildingType::Mine);
    ApplyBuildingDefinition(*this, definition);
}

// Initializes runtime state for this object.
void Mine::InitBuilding(TileType tile)
{
    type = tile;
    if (const auto* terrainProduction = FindTerrainProductionDefinition(BuildingType::Mine, type))
        ApplyProductionDefinition(*this, terrainProduction->production);
}

// Initializes Foundry::Foundry.
Foundry::Foundry(int ajdi)
{
    id = ajdi;
    const auto& definition = GetBuildingDefinition(BuildingType::Foundry);
    ApplyBuildingDefinition(*this, definition);
    ApplyProductionDefinition(*this, definition.production);
    ApplyProductionRecipes(*this, definition);
}

// Updates the requested state value.
void Foundry::SetSupplier(ResourceType type, Building* supplier)
{
    ProductionBuilding::SetSupplier(type, supplier);
}

// Updates the requested state value.
void Foundry::SetReceiver(ResourceType type, Building* receiver)
{
    auto it = receiversMap.find(type);
    if (it != receiversMap.end() && it->second != nullptr && it->second != receiver)
        it->second->RemoveSupplier(type, this);

    receiversMap[type] = receiver;
    if (receiver != nullptr)
        receiver->SetSupplier(type, this);
}

// Initializes Well::Well.
Well::Well(int i)
{
    id = i;
    const auto& definition = GetBuildingDefinition(BuildingType::Well);
    ApplyBuildingDefinition(*this, definition);
    ApplyProductionDefinition(*this, definition.production);
    ApplyProductionRecipes(*this, definition);
}

// Initializes WheatFarm::WheatFarm.
WheatFarm::WheatFarm(int i)
{
    id = i;
    const auto& definition = GetBuildingDefinition(BuildingType::WheatFarm);
    ApplyBuildingDefinition(*this, definition);
    ApplyProductionDefinition(*this, definition.production);
    ApplyProductionRecipes(*this, definition);
}

// Initializes Windmill::Windmill.
Windmill::Windmill(int i)
{
    id = i;
    const auto& definition = GetBuildingDefinition(BuildingType::Windmill);
    ApplyBuildingDefinition(*this, definition);
    ApplyProductionDefinition(*this, definition.production);
    ApplyProductionRecipes(*this, definition);
}

// Initializes Bakery::Bakery.
Bakery::Bakery(int i)
{
    id = i;
    const auto& definition = GetBuildingDefinition(BuildingType::Bakery);
    ApplyBuildingDefinition(*this, definition);
    ApplyProductionDefinition(*this, definition.production);
    ApplyProductionRecipes(*this, definition);
}

// Initializes Inn::Inn.
Inn::Inn(int i)
{
    id = i;
    const auto& definition = GetBuildingDefinition(BuildingType::Inn);
    ApplyBuildingDefinition(*this, definition);
    ApplyProductionDefinition(*this, definition.production);
    ApplyProductionRecipes(*this, definition);
}

// Initializes Paperworks::Paperworks.
Paperworks::Paperworks(int i)
{
    id = i;
    const auto& definition = GetBuildingDefinition(BuildingType::Paperworks);
    ApplyBuildingDefinition(*this, definition);
    ApplyProductionDefinition(*this, definition.production);
    ApplyProductionRecipes(*this, definition);
}

// Initializes Smith::Smith.
Smith::Smith(int i)
{
    id = i;
    const auto& definition = GetBuildingDefinition(BuildingType::Smith);
    ApplyBuildingDefinition(*this, definition);
    ApplyProductionDefinition(*this, definition.production);
    ApplyProductionRecipes(*this, definition);
}

// Initializes University::University.
University::University(int i)
{
    id = i;
    const auto& definition = GetBuildingDefinition(BuildingType::University);
    ApplyBuildingDefinition(*this, definition);
    ApplyProductionDefinition(*this, definition.production);
    ApplyProductionRecipes(*this, definition);
}

// Initializes StorageBuilding::StorageBuilding.
StorageBuilding::StorageBuilding(int actualId)
{
    id = actualId;
    const auto& definition = GetBuildingDefinition(BuildingType::StorageBuilding);
    ApplyBuildingDefinition(*this, definition);
    ApplyStorageDefinition(*this, definition);
}

// Initializes Headquarters::Headquarters.
Headquarters::Headquarters(int actualId)
    // Initializes StorageBuilding.
    : StorageBuilding(actualId)
{
    const auto& definition = GetBuildingDefinition(BuildingType::Headquarters);
    ApplyBuildingDefinition(*this, definition);
    ApplyStorageDefinition(*this, definition);
    territoryRadius = definition.military.territoryRadius;
    hitPoints = definition.military.hitPoints;
    maxHitPoints = definition.military.hitPoints;
}

// Initializes Headquarters::ReceiveDamage.
void Headquarters::ReceiveDamage(int damage)
{
    hitPoints = std::max(0, hitPoints - std::max(0, damage));
}

// Returns territory radius after owner modifiers are applied.
int Headquarters::GetTerritoryRadius() const
{
    return owner != nullptr
        ? owner->ResolveStat(territoryRadius, this, ResourceType::Null, std::nullopt, 0)
        : territoryRadius.GetBase();
}

// Returns maximum hit points after owner modifiers are applied.
int Headquarters::GetMaxHitPoints() const
{
    return owner != nullptr
        ? owner->ResolveStat(maxHitPoints, this, ResourceType::Null, std::nullopt, 1)
        : maxHitPoints.GetBase();
}

// Initializes Village::Village.
Village::Village(int actualId)
{
    id = actualId;
    const auto& definition = GetBuildingDefinition(BuildingType::Village);
    ApplyBuildingDefinition(*this, definition);
    manpowerRate = definition.village.manpowerRate;
    populationCap = definition.village.populationCap;
    upkeepInterval = definition.village.upkeepInterval;
    foodPackageUpkeep = definition.village.foodPackageUpkeep;
}

// Advances this object's state for one frame.
void Village::Update(double dt)
{
    double operationalDt = BeginOperationalUpdate(dt);
    if (operationalDt <= 0.0)
        return;

    if (owner == nullptr)
        return;

    int rejectedFoodRequests = RequestFoodSupply();
    bool hasBufferedFood = !foodSupplyBuffer.buffer.empty();
    bool hasIncomingFood = CountIncomingResources(this, ResourceType::FOOD_PROVISIONS) > 0;
    if (rejectedFoodRequests > 0 && !hasBufferedFood && !hasIncomingFood)
    {
        double requestPressure = std::clamp(static_cast<double>(rejectedFoodRequests) / std::max(1, foodSupplyBuffer.bufferSize), 0.0, 1.0);
        double starvationDropRate = (0.025 + 0.055 * requestPressure) / std::max(0.45, foodSupplyLevel);
        foodSupplyLevel = std::max(0.0, foodSupplyLevel - starvationDropRate * operationalDt);
    }

    upkeepTimer += operationalDt;
    if (upkeepTimer >= upkeepInterval)
    {
        upkeepTimer = 0.0;
        int packagesNeeded = std::max(1, static_cast<int>(std::ceil(foodPackageUpkeep)));
        bool consumed = static_cast<int>(foodSupplyBuffer.buffer.size()) >= packagesNeeded;
        if (consumed)
        {
            for (int i = 0; i < packagesNeeded; i++)
                foodSupplyBuffer.FreeResource();
            foodSupplyLevel = std::min(1.0, foodSupplyLevel + 0.45);
        }
        else
        {
            foodSupplyLevel = std::max(0.0, foodSupplyLevel - foodSupplyDropPerMissedUpkeep);
        }
        hasFood = foodSupplyLevel > 0.0;
    }

    double efficiency = GetManpowerProductivity();
    double modifiedRate = owner->ResolveStat(manpowerRate, this);
    owner->AddManpower(modifiedRate * efficiency * operationalDt);
    activeTime += operationalDt * efficiency;
}

// Adds delivered food provisions to the village supply buffer.
void Village::AddResource(Resource* res)
{
    if (res == nullptr)
        return;

    if (res->type != ResourceType::FOOD_PROVISIONS || !CanReceiveResource(res->type))
    {
        if (res->sourceBuilding != nullptr)
            res->sourceBuilding->ReturnOutgoingResource(res);
        return;
    }

    foodSupplyBuffer.AddResource(res);
    hasFood = true;
}

// Returns one stored food package when requested by logistics.
Resource Village::GetResource(ResourceType type)
{
    if (type != ResourceType::FOOD_PROVISIONS)
        return Resource{};

    auto [available, resource] = foodSupplyBuffer.GetResource();
    return available ? *resource : Resource{};
}

// Returns an outgoing food provision to the village supply buffer.
void Village::ReturnOutgoingResource(Resource* res)
{
    if (res == nullptr)
        return;

    if (res->type == ResourceType::FOOD_PROVISIONS)
        foodSupplyBuffer.AddResource(res);
}

// Returns whether villages accept the requested resource type.
bool Village::CanAcceptResource(ResourceType type) const
{
    return type == ResourceType::FOOD_PROVISIONS;
}

// Returns whether the village has room for more food provisions.
bool Village::CanReceiveResource(ResourceType type) const
{
    return type == ResourceType::FOOD_PROVISIONS &&
           static_cast<int>(foodSupplyBuffer.buffer.size()) < foodSupplyBuffer.bufferSize;
}

// Returns UI snapshots for village food supply.
std::vector<ResourceBufferView> Village::GetInputBufferViews() const
{
    return {{ResourceType::FOOD_PROVISIONS,
             static_cast<int>(foodSupplyBuffer.buffer.size()),
             foodSupplyBuffer.bufferSize,
             static_cast<int>(std::ceil(foodPackageUpkeep))}};
}

// Returns the normalized stored supply level.
double Village::GetFoodSupplyRatio() const
{
    return std::clamp(foodSupplyLevel, 0.0, 1.0);
}

// Returns the manpower growth multiplier from food supply.
double Village::GetManpowerProductivity() const
{
    return GetFoodSupplyRatio();
}

// Returns the worker productivity multiplier from food supply.
double Village::GetWorkerProductivity() const
{
    return 0.3 + 0.7 * GetFoodSupplyRatio();
}

// Requests missing food provisions from owned storage-like buildings.
int Village::RequestFoodSupply()
{
    if (owner == nullptr || !CanReceiveResource(ResourceType::FOOD_PROVISIONS))
        return 0;

    int stored = static_cast<int>(foodSupplyBuffer.buffer.size());
    int incoming = CountIncomingResources(this, ResourceType::FOOD_PROVISIONS);
    int missing = foodSupplyBuffer.bufferSize - stored - incoming;
    if (missing <= 0)
        return 0;

    for (auto& tile : owner->tilemap.tilemap)
    {
        auto* storage = dynamic_cast<StorageBuilding*>(tile.building.get());
        if (storage == nullptr || storage->owner != owner)
            continue;

        missing -= storage->HandleTransport(ResourceType::FOOD_PROVISIONS, missing, this);
        if (missing <= 0)
            break;
    }
    return std::max(0, missing);
}

// Initializes MilitaryBuilding::MilitaryBuilding.
MilitaryBuilding::MilitaryBuilding(int actualId)
{
    id = actualId;
    const auto& definition = GetBuildingDefinition(BuildingType::MilitaryBuilding);
    ApplyBuildingDefinition(*this, definition);
    ApplyMilitaryDefinition(*this, definition);
}

// Advances this object's state for one frame.
void MilitaryBuilding::Update(double dt)
{
    garrison = GetTotalTroops();
    supplyBuffer.bufferSize = GetSupplyCapacity();
    supply = static_cast<int>(supplyBuffer.buffer.size());
    double operationalDt = BeginOperationalUpdate(dt);
    if (operationalDt <= 0.0)
        return;

    activeTime += operationalDt;
    for (size_t i = 0; i < divisions.size();)
    {
        auto& division = divisions[i];
        if (division.currentOrder == MilitaryOrderType::None || division.orderTargetPositionId < 0)
        {
            i++;
            continue;
        }

        division.orderCooldown = std::max(0.0, division.orderCooldown - operationalDt);
        if (division.orderCooldown > 0.0)
        {
            i++;
            continue;
        }

        Building* target = owner != nullptr ? owner->tilemap.GetBuilding(division.orderTargetPositionId) : nullptr;
        if (target == nullptr || target == this || target->IsUnderConstruction())
        {
            division.currentOrder = MilitaryOrderType::None;
            division.orderTargetPositionId = -1;
            division.orderCooldown = 0.0;
            i++;
            continue;
        }

        if (division.currentOrder == MilitaryOrderType::Attack)
        {
            auto* defender = dynamic_cast<IMilitaryBuilding*>(target);
            if (target->owner == owner || defender == nullptr || defender->GetHitPoints() <= 0)
            {
                division.currentOrder = MilitaryOrderType::None;
                division.orderTargetPositionId = -1;
                division.orderCooldown = 0.0;
                i++;
                continue;
            }

            int damage = DivisionAttackDamage(division);
            defender->ReceiveDamage(damage);
            Log::Msg("[Combat]", name, " division #", division.id, " attacks ", target->name,
                     " for ", damage, " damage (", defender->GetHitPoints(), "/", defender->GetMaxHitPoints(), ")");
            division.orderCooldown = 3.0;
            i++;
            continue;
        }

        if (division.currentOrder == MilitaryOrderType::Support)
        {
            auto* friendly = dynamic_cast<MilitaryBuilding*>(target);
            if (friendly == nullptr || target->owner != owner)
            {
                division.currentOrder = MilitaryOrderType::None;
                division.orderTargetPositionId = -1;
                division.orderCooldown = 0.0;
                i++;
                continue;
            }

            if (friendly->GetFreeDivisionSpace() > 0)
            {
                MilitaryDivision moved = division;
                moved.currentOrder = MilitaryOrderType::None;
                moved.orderTargetPositionId = -1;
                moved.orderCooldown = 0.0;
                friendly->divisions.push_back(moved);
                divisions.erase(divisions.begin() + static_cast<std::ptrdiff_t>(i));
                RecountDivisionTypes(*this);
                RecountDivisionTypes(*friendly);
                continue;
            }

            division.orderCooldown = 3.0;
            i++;
            continue;
        }

        if (division.currentOrder == MilitaryOrderType::Defend)
            division.orderCooldown = 3.0;
        i++;
    }

    if (buildingType == BuildingType::Barracks)
    {
        ClearOrder();
        return;
    }

    if (currentOrder == MilitaryOrderType::None || owner == nullptr || orderTargetPositionId < 0)
        return;

    orderCooldown = std::max(0.0, orderCooldown - operationalDt);
    if (orderCooldown > 0.0)
        return;

    Building* target = owner->tilemap.GetBuilding(orderTargetPositionId);
    if (target == nullptr || target == this || target->IsUnderConstruction())
    {
        ClearOrder();
        return;
    }

    if (currentOrder == MilitaryOrderType::Attack)
    {
        auto* defender = dynamic_cast<IMilitaryBuilding*>(target);
        if (target->owner == owner || defender == nullptr || defender->GetHitPoints() <= 0)
        {
            ClearOrder();
            return;
        }

        int damage = GetModifiedAttackDamage();
        defender->ReceiveDamage(damage);
        Log::Msg("[Combat]", name, " executes ", MilitaryOrderName(currentOrder), " on ", target->name,
                 " for ", damage, " damage (", defender->GetHitPoints(), "/", defender->GetMaxHitPoints(), ")");
        orderCooldown = 3.0;
        return;
    }

    auto* friendly = dynamic_cast<MilitaryBuilding*>(target);
    if (friendly == nullptr || target->owner != owner)
    {
        ClearOrder();
        return;
    }

    if (currentOrder == MilitaryOrderType::Support)
    {
        bool transferred = false;
        if (friendly->GetFreeDivisionSpace() > 0 && !divisions.empty())
        {
            friendly->divisions.push_back(divisions.front());
            divisions.erase(divisions.begin());
            RecountDivisionTypes(*this);
            RecountDivisionTypes(*friendly);
            transferred = true;
        }
        else if (friendly->GetFreeGarrisonSpace() > 0 && GetTotalTroops() > 0)
        {
            if (militia > 0)
            {
                militia--;
                friendly->militia++;
            }
            else if (swordsmen > 0)
            {
                swordsmen--;
                friendly->swordsmen++;
            }
            else if (archers > 0)
            {
                archers--;
                friendly->archers++;
            }
            garrison = GetTotalTroops();
            friendly->garrison = friendly->GetTotalTroops();
            transferred = true;
        }

        if (supplyBuffer.buffer.size() > 0 && friendly->CanReceiveResource(ResourceType::FOOD_PROVISIONS))
        {
            auto [available, resource] = supplyBuffer.GetResource();
            if (available)
            {
                if (owner->BeginTransport(this, friendly, resource))
                {
                    supply = static_cast<int>(supplyBuffer.buffer.size());
                    transferred = true;
                }
                else
                {
                    supplyBuffer.AddResource(resource);
                }
            }
        }

        orderCooldown = transferred ? 1.0 : 3.0;
        return;
    }

    if (currentOrder == MilitaryOrderType::Defend)
        orderCooldown = 3.0;
}

// Initializes MilitaryBuilding::ReceiveDamage.
void MilitaryBuilding::ReceiveDamage(int damage)
{
    hitPoints = std::max(0, hitPoints - std::max(0, damage));
}

// Returns territory radius after owner modifiers are applied.
int MilitaryBuilding::GetTerritoryRadius() const
{
    return owner != nullptr
        ? owner->ResolveStat(territoryRadius, this, ResourceType::Null, std::nullopt, 0)
        : territoryRadius.GetBase();
}

// Returns maximum hit points after owner modifiers are applied.
int MilitaryBuilding::GetMaxHitPoints() const
{
    return owner != nullptr
        ? owner->ResolveStat(maxHitPoints, this, ResourceType::Null, std::nullopt, 1)
        : maxHitPoints.GetBase();
}

// Adds this object or value to local state.
void MilitaryBuilding::AddResource(Resource* res)
{
    if (res == nullptr)
        return;

    if (res->type != ResourceType::FOOD_PROVISIONS || !CanReceiveResource(res->type))
    {
        if (res->sourceBuilding != nullptr)
            res->sourceBuilding->ReturnOutgoingResource(res);
        return;
    }

    supplyBuffer.AddResource(res);
    supply = static_cast<int>(supplyBuffer.buffer.size());
}

// Initializes MilitaryBuilding::ReturnOutgoingResource.
void MilitaryBuilding::ReturnOutgoingResource(Resource* res)
{
    if (res == nullptr)
        return;

    if (res->type == ResourceType::FOOD_PROVISIONS)
    {
        supplyBuffer.AddResource(res);
        supply = static_cast<int>(supplyBuffer.buffer.size());
    }
}

// Removes and returns one supply resource from this military building.
Resource MilitaryBuilding::GetResource(ResourceType type)
{
    if (type != ResourceType::FOOD_PROVISIONS)
        return Resource{};

    auto [available, resource] = supplyBuffer.GetResource();
    supply = static_cast<int>(supplyBuffer.buffer.size());
    return available ? *resource : Resource{};
}

// Handles the requested event or transfer.
int MilitaryBuilding::HandleTransport(ResourceType type, int amount, Building* receiver)
{
    if (type != ResourceType::FOOD_PROVISIONS || receiver == nullptr || amount <= 0)
        return 0;

    int sent = 0;
    for (int i = 0; i < amount; i++)
    {
        if (!receiver->CanReceiveResource(type))
            break;

        auto [available, resource] = supplyBuffer.GetResource();
        if (!available)
            break;

        if (owner->BeginTransport(this, receiver, resource))
            sent++;
        else
        {
            supplyBuffer.AddResource(resource);
            break;
        }
    }

    supply = static_cast<int>(supplyBuffer.buffer.size());
    return sent;
}

// Returns whether this condition is currently true.
bool MilitaryBuilding::CanAcceptResource(ResourceType type) const
{
    return type == ResourceType::FOOD_PROVISIONS;
}

// Returns whether this condition is currently true.
bool MilitaryBuilding::CanReceiveResource(ResourceType type) const
{
    return type == ResourceType::FOOD_PROVISIONS &&
           static_cast<int>(supplyBuffer.buffer.size()) < supplyBuffer.bufferSize;
}

// Returns combat strength including garrison and owner modifiers.
int MilitaryBuilding::GetEffectiveStrength() const
{
    int base = owner != nullptr
        ? owner->ResolveStat(combatStrength, this, ResourceType::Null, std::nullopt, 0)
        : combatStrength.GetBase();
    if (!divisions.empty())
    {
        int divisionStrength = 0;
        for (const auto& division : divisions)
            divisionStrength += division.strength * division.health / std::max(1, division.maxHealth);
        return base + divisionStrength;
    }
    return base + militia + swordsmen * 4 + archers * 3;
}

// Returns whether this condition is currently true.
void MilitaryBuilding::IssueOrder(MilitaryOrderType order, int targetPosition)
{
    currentOrder = order;
    orderTargetPositionId = targetPosition;
    orderCooldown = 0.0;
}

// Assigns an order to one stationed division.
bool MilitaryBuilding::IssueDivisionOrder(int divisionId, MilitaryOrderType order, int targetPosition)
{
    for (auto& division : divisions)
    {
        if (division.id != divisionId)
            continue;

        division.currentOrder = order;
        division.orderTargetPositionId = targetPosition;
        division.orderCooldown = DivisionTravelDelaySeconds(*this, division, targetPosition);
        return true;
    }

    return false;
}

// Clears this runtime state.
void MilitaryBuilding::ClearOrder()
{
    currentOrder = MilitaryOrderType::None;
    orderTargetPositionId = -1;
    orderCooldown = 0.0;
}

// Returns true when at least one division has an active order.
bool MilitaryBuilding::HasActiveDivisionOrders() const
{
    for (const auto& division : divisions)
        if (division.currentOrder != MilitaryOrderType::None)
            return true;
    return false;
}

// Returns total troops currently stationed in this building.
int MilitaryBuilding::GetTotalTroops() const
{
    if (!divisions.empty())
        return static_cast<int>(divisions.size());
    return militia + swordsmen + archers;
}

// Returns remaining garrison capacity.
int MilitaryBuilding::GetFreeGarrisonSpace() const
{
    int capacity = owner != nullptr
        ? owner->ResolveStat(garrisonCapacity, this, ResourceType::Null, std::nullopt, 0)
        : garrisonCapacity.GetBase();
    return std::max(0, capacity - GetTotalTroops());
}

// Returns division slots available in this building.
int MilitaryBuilding::GetDivisionCapacity() const
{
    int capacity = owner != nullptr
        ? owner->ResolveStat(garrisonCapacity, this, ResourceType::Null, std::nullopt, 0)
        : garrisonCapacity.GetBase();
    return std::max(0, capacity / 10);
}

// Returns free division slots in this building.
int MilitaryBuilding::GetFreeDivisionSpace() const
{
    return std::max(0, GetDivisionCapacity() - static_cast<int>(divisions.size()));
}

// Returns average morale of stationed divisions.
int MilitaryBuilding::GetAverageDivisionMorale() const
{
    if (divisions.empty())
        return 0;
    int total = 0;
    for (const auto& division : divisions)
        total += division.morale;
    return total / static_cast<int>(divisions.size());
}

// Returns average experience of stationed divisions.
int MilitaryBuilding::GetAverageDivisionExperience() const
{
    if (divisions.empty())
        return 0;
    int total = 0;
    for (const auto& division : divisions)
        total += division.experience;
    return total / static_cast<int>(divisions.size());
}

// Returns supply capacity after owner modifiers are applied.
int MilitaryBuilding::GetSupplyCapacity() const
{
    return owner != nullptr
        ? owner->ResolveStat(supplyCapacity, this, ResourceType::Null, std::nullopt, 0)
        : supplyCapacity.GetBase();
}

// Returns supply consumption after owner modifiers are applied.
int MilitaryBuilding::GetSupplyConsumption() const
{
    if (!divisions.empty())
    {
        int manpower = 0;
        for (const auto& division : divisions)
            manpower += division.manpowerScale;
        return owner != nullptr
            ? owner->ModifyBalanceIntForBuilding(BalanceStat::SupplyConsumption, manpower, this, ResourceType::Null, std::nullopt, 0)
            : manpower;
    }
    return owner != nullptr
        ? owner->ModifyBalanceIntForBuilding(BalanceStat::SupplyConsumption, GetTotalTroops(), this, ResourceType::Null, std::nullopt, 0)
        : GetTotalTroops();
}

// Returns attack damage derived from strength and owner modifiers.
int MilitaryBuilding::GetModifiedAttackDamage() const
{
    int base = std::max(4, GetEffectiveStrength() / 4);
    return owner != nullptr
        ? owner->ModifyBalanceIntForBuilding(BalanceStat::AttackDamage, base, this, ResourceType::Null, std::nullopt, 1)
        : base;
}

// Initializes GuardTower::GuardTower.
GuardTower::GuardTower(int actualId)
{
    id = actualId;
    const auto& definition = GetBuildingDefinition(BuildingType::GuardTower);
    ApplyBuildingDefinition(*this, definition);
    ApplyMilitaryDefinition(*this, definition);
}

// Initializes Fortress::Fortress.
Fortress::Fortress(int actualId)
{
    id = actualId;
    const auto& definition = GetBuildingDefinition(BuildingType::Fortress);
    ApplyBuildingDefinition(*this, definition);
    ApplyMilitaryDefinition(*this, definition);
}

// Initializes Castle::Castle.
Castle::Castle(int actualId)
{
    id = actualId;
    const auto& definition = GetBuildingDefinition(BuildingType::Castle);
    ApplyBuildingDefinition(*this, definition);
    ApplyMilitaryDefinition(*this, definition);
}

// Initializes Barracks::Barracks.
Barracks::Barracks(int actualId)
{
    id = actualId;
    const auto& definition = GetBuildingDefinition(BuildingType::Barracks);
    ApplyBuildingDefinition(*this, definition);
    ApplyMilitaryDefinition(*this, definition);
}

// Advances this object's state for one frame.
void Barracks::Update(double dt)
{
    bool wasUnderConstruction = IsUnderConstruction();
    double constructionBefore = constructionRemaining;
    MilitaryBuilding::Update(dt);
    if (IsUnderConstruction() || owner == nullptr || recruitmentQueue.empty())
        return;

    if (wasUnderConstruction)
    {
        dt = std::max(0.0, dt - constructionBefore);
        if (dt <= 0.0)
            return;
    }

    auto& job = recruitmentQueue.front();
    job.remaining = std::max(0.0, job.remaining - dt);
    if (job.remaining > 0.0)
        return;

    if (GetFreeDivisionSpace() <= 0)
        return;

    SoldierDivision division = CreateMilitaryDivision(job.type, id * 10000 + nextDivisionId++);
    divisions.push_back(division);
    RecountDivisionTypes(*this);

    recruitmentQueue.pop_front();
}

// Queues this work for later processing.
bool Barracks::QueueRecruitment(MilitaryUnitType type)
{
    if (owner == nullptr || IsUnderConstruction() || GetFreeDivisionSpace() <= static_cast<int>(recruitmentQueue.size()))
        return false;

    int manpowerCost = owner->ModifyBalanceIntForBuilding(BalanceStat::RecruitmentManpowerCost, GetBaseRecruitmentManpowerCost(type), this, ResourceType::Null, type, 0);
    if (!owner->strategicResources.Consume(StrategicResourceType::Manpower, manpowerCost))
        return false;

    std::vector<ResourceAmountDefinition> materialCosts;
    for (const auto& [resource, amount] : GetBaseRecruitmentResourceCosts(type))
        materialCosts.push_back({resource, amount});
    if (!owner->TryPayBuildCost(materialCosts))
    {
        owner->strategicResources.Add(StrategicResourceType::Manpower, manpowerCost);
        return false;
    }

    double recruitmentTime = owner->ModifyBalanceForBuilding(BalanceStat::RecruitmentTime, GetBaseRecruitmentTime(type), this, ResourceType::Null, type);
    recruitmentQueue.push_back({type, recruitmentTime});
    return true;
}

// Adds this object or value to local state.
void StorageBuilding::AddResource(Resource* res)
{
    if (res == nullptr)
        return;

    auto bufferIt = resourceBuffers.find(res->type);
    if (bufferIt == resourceBuffers.end() || static_cast<int>(bufferIt->second.buffer.size()) >= bufferIt->second.bufferSize)
    {
        if (res->sourceBuilding != nullptr)
            res->sourceBuilding->ReturnOutgoingResource(res);
        return;
    }

    Log::Msg(tag, "resource added!");
    bufferIt->second.AddResource(res);
}

// Initializes StorageBuilding::ReturnOutgoingResource.
void StorageBuilding::ReturnOutgoingResource(Resource* res)
{
    if (res == nullptr)
        return;

    resourceBuffers[res->type].AddResource(res);
}

// Removes and returns one stored resource of the requested type.
Resource StorageBuilding::GetResource(ResourceType type)
{
    auto [isAvailable, resource] = resourceBuffers[type].GetResource();
    return isAvailable ? *resource : Resource{};
}

// Handles the requested event or transfer.
int StorageBuilding::HandleTransport(ResourceType resource, int amount, Building* receiver)
{
    int sent = 0;
    for(int i = 0; i<amount; i++)
    {
        auto [isAvailable, res] = resourceBuffers[resource].GetResource();
        if(isAvailable)
        {
            if (receiver == nullptr || !receiver->CanReceiveResource(resource))
            {
                resourceBuffers[resource].AddResource(res);
                break;
            }

            Log::Msg(tag, "ID: ", id, " ", rt2s(res->type), " transport started to ", receiver->name, " with ID ", receiver->id);
            if (owner->BeginTransport(this, receiver, res))
                sent++;
            else
                resourceBuffers[resource].AddResource(res);
        }
    }
    return sent;
}
// Advances this object's state for one frame.
void StorageBuilding::Update(double dt)
{
    double operationalDt = BeginOperationalUpdate(dt);
    if (operationalDt <= 0.0)
        return;

    UpdateTransportables(operationalDt);
}
// Updates the requested state value.
void StorageBuilding::SetReceiver(ResourceType type, Building* receiver)
{
    receiver->SetSupplier(type, this);
}
// Updates the requested state value.
void StorageBuilding::SetSupplier(ResourceType type, Building* receiver)
{
    
}
// Initializes runtime state for this object.
void StorageBuilding::InitBuilding(TileType tajl)
{
    
}

// Returns UI snapshots for storage buffers.
std::vector<ResourceBufferView> StorageBuilding::GetOutputBufferViews() const
{
    std::vector<ResourceBufferView> result;
    for (const auto &[resource, buffer] : resourceBuffers)
    {
        result.push_back({resource, static_cast<int>(buffer.buffer.size()), buffer.bufferSize, 0});
    }
    return result;
}

// Returns whether this condition is currently true.
bool StorageBuilding::CanAcceptResource(ResourceType type) const
{
    return resourceBuffers.contains(type);
}

// Returns whether this condition is currently true.
bool StorageBuilding::CanReceiveResource(ResourceType type) const
{
    auto it = resourceBuffers.find(type);
    return it != resourceBuffers.end() && static_cast<int>(it->second.buffer.size()) < it->second.bufferSize;
}

// Receives a transportable arriving at this building or road.
void StorageBuilding::ReceptTransport(Transportable* trans)
{
}

// Initializes Road::Road.
Road::Road(int i)
{
    id = i;
    const auto& definition = GetBuildingDefinition(BuildingType::Road);
    ApplyBuildingDefinition(*this, definition);
    upgradeLevel = definition.road.upgradeLevel;
    maxCapacity = definition.road.maxCapacity;
    speedModifier = definition.road.speedModifier;
}

// Advances this object's state for one frame.
void Road::Update(double dt)
{
    double operationalDt = BeginOperationalUpdate(dt);
    if (operationalDt <= 0.0)
        return;

    UpdateTransportables(operationalDt);
}

// Returns road capacity after owner modifiers are applied.
int Road::GetModifiedMaxCapacity() const
{
    return owner != nullptr
        ? owner->ResolveStat(maxCapacity, this, ResourceType::Null, std::nullopt, 0)
        : maxCapacity.GetBase();
}

// Returns road speed multiplier after owner modifiers are applied.
double Road::GetModifiedSpeedModifier() const
{
    return owner != nullptr
        ? owner->ResolveStat(speedModifier, this)
        : speedModifier.GetBase();
}



