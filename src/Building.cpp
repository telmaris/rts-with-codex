#include "../inc/Building.h"
#include "../inc/ProductionBuildings.h"
#include "../inc/Player.h"
#include "../inc/MapGenerator.h"

float Building::GetEfficiency() const
{
    if (lifetime <= 0.0)
        return 0.0f;

    return static_cast<float>(activeTime / lifetime);
}

void Building::UpdateTransportables(double dt)
{
    for (auto it = transportables.begin(); it != transportables.end();)
    {
        bool flag = (*it)->Update(dt);
        if (flag == true)
        {
            // usunięcie resource z vectora, dodanie go do celu
            Log::Msg(tag, "resource ", rt2s(((Resource*)(*it))->type)," deleted from transportables vector! position: ", (*it)->map->GetCoordsFromId(positionId));
            transportables.erase(it);
            continue;
        }
        it++;
    }
}

void Building::ReceptTransport(Transportable* trans)
{
    trans->elapsedTime = 0.0;
    trans->transportTime = transportTime;
    
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
        Log::Msg(tag, rt2s(((Resource*)trans)->type)," pushed into transportables vector! at ID: ", positionId, " position: ", trans->map->GetCoordsFromId(positionId));
    }

}

ProductionBuilding::ProductionBuilding(int ajdi)
{
    id = ajdi;
    type = TileType::GRASS;
    name = "ProductionBuilding";
    tag = "[ProductionBuilding]";
    buildingType = BuildingType::ProductionBuilding;
}

void ProductionBuilding::Update(double dt)
{
    lifetime += dt;
    if (productionStarted && !productionBlocked)
        activeTime += dt;

    Produce(dt);
    HandleTransport();
    UpdateTransportables(dt);
}

void ProductionBuilding::Produce(double dt)
{
    if (productionBlocked)
        return;

    if (productionStarted)
    {
        // handle ongoing production
        if (elapsedTime >= productionTime)
        {
            // handle finished production
            for (auto &[resource, amount] : products)
            {
                for (int i = 0; i < amount; i++)
                {
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
            // handle ongoing production
            elapsedTime += dt;
        }
    }
    else
    {
        // check if production start is possible
        // assumption: ingredients vector is set, and inputBuffers is also properly configured

        // first, check if output buffers have space
        for (auto &[resource, buffer] : outputBuffers)
        {
            // todo: check for difference between available capacity and production volume
            if (buffer.buffer.size() >= buffer.bufferSize)
            {
                // Log::Msg("[production building]", "Cannot start due to the full output buffer!");
                return;
            }
        }

        bool canStart = true;

        // check if all resources are available
        for (auto &[resource, amount] : ingredients)
        {
            if (inputBuffers[resource].buffer.size() < amount)
            {
                canStart = false;
                int amountcalculus = inputBuffers[resource].bufferSize - inputBuffers[resource].buffer.size();
                RequestResource(resource, amountcalculus);
                //return;
            }
        }   
                    
        // if yes, delete resources in buffers and start production
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

void ProductionBuilding::HandleTransport()
{
    for(auto& [resource, receiver] : receiversMap)
    {
        // check if given resource has been storaged in output buffer
        if(receiver->buildingType == BuildingType::StorageBuilding)
        {
            //inside
            auto [isAvailable, res] = outputBuffers[resource].GetResource();
            if(isAvailable)
            {
                Log::Msg(tag, "ID: ", id, " ", rt2s(res->type), " transport started to ", receiver->name, " with ID ", receiver->id);
                owner->BeginTransport(this, receiver, res);
            }
        }
    }
}

void ProductionBuilding::HandleTransport(ResourceType resource, int amount, Building* receiver)
{
    for(int i = 0; i<amount; i++)
    {
        // check if given resource has been storaged in output buffer
        auto [isAvailable, res] = outputBuffers[resource].GetResource();
        if(isAvailable)
        {
            Log::Msg(tag, "ID: ", id, " ", rt2s(res->type), " transport started to ", receiver->name, " with ID ", receiver->id);
            owner->BeginTransport(this, receiver, res);
        }
    }
}

void ProductionBuilding::RequestResource(ResourceType ResType, int amount)
{
    // Log::Msg(tag, "Requesting resoure: ", rt2s(ResType), " amount: ", amount);
    if(suppliersMap.contains(ResType))
    {
        suppliersMap[ResType]->HandleTransport(ResType,amount,this);
    }
}

void ProductionBuilding::AddResource(Resource* res)
{
    Log::Msg(tag, "resource added!");
    inputBuffers[res->type].AddResource(res);
}

Resource ProductionBuilding::GetResource(ResourceType type)
{
    auto [isAvailable, resource] = inputBuffers[type].GetResource();
    return isAvailable ? *resource : Resource{};
}

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

std::vector<ResourceBufferView> ProductionBuilding::GetOutputBufferViews() const
{
    std::vector<ResourceBufferView> result;
    for (const auto &[resource, buffer] : outputBuffers)
    {
        int recipeAmount = 0;
        auto recipeIt = products.find(resource);
        if (recipeIt != products.end())
            recipeAmount = recipeIt->second;

        result.push_back({resource, static_cast<int>(buffer.buffer.size()), buffer.bufferSize, recipeAmount});
    }
    return result;
}

std::vector<BuildingConnectionView> ProductionBuilding::GetSupplierViews() const
{
    std::vector<BuildingConnectionView> result;
    for (const auto &[resource, amount] : ingredients)
    {
        auto it = suppliersMap.find(resource);
        result.push_back({resource, it != suppliersMap.end() ? it->second : nullptr});
    }
    return result;
}

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

bool ProductionBuilding::HasSupplier(ResourceType type) const
{
    return suppliersMap.contains(type) && suppliersMap.at(type) != nullptr;
}

bool ProductionBuilding::HasReceiver(ResourceType type) const
{
    return receiversMap.contains(type) && receiversMap.at(type) != nullptr;
}

float ProductionBuilding::GetProductionProgress() const
{
    if (!productionStarted || productionTime <= 0.0)
        return 0.0f;

    return std::clamp(static_cast<float>(elapsedTime / productionTime), 0.0f, 1.0f);
}

void ProductionBuilding::SetSupplier(ResourceType type, Building* supplier)
{
    auto [it, ok] = suppliersMap.insert({type, supplier});
    if(!ok)
    {
        suppliersMap[type] = supplier;
    }
}

void ProductionBuilding::SetReceiver(ResourceType type, Building* receiver)
{
    auto [it, ok] = receiversMap.insert({type, receiver});
    if(!ok)
    {
        receiversMap[type] = receiver;
    }
    receiver->SetSupplier(type, this);
}

void ProductionBuilding::ReceptTransport(Transportable* trans)
{
    // transportables.push_back(trans);
}

// ===== BUILDINGS =====

Woodcutter::Woodcutter(int i)
{
    id = i;
    name = "Woodcutter";
    tag = "[Woodcutter]";
    // type = Tile::WOOD;
    buildingType = BuildingType::Woodcutter;
    textureId = 0;
    footprint = {2,2};
    products.insert({ResourceType::WOOD, 1});
    productionTime = 5;
    ResourceBuffer output{ResourceType::WOOD, 3};
    outputBuffers.insert({ResourceType::WOOD, output}); // to jest stworzenie pojedynczego outputu
}
LumberMill::LumberMill(int i)
{
    id = i;
    name = "Lumber Mill";
    tag = "[Lumber Mill]";
    buildingType = BuildingType::LumberMill;
    textureId = 1;
    footprint = {2,2};
    // type = ResourceType::PLANKS;
    products.insert({ResourceType::PLANKS, 2});
    productionTime = 10;

    ingredients.insert({ResourceType::WOOD, 1});

    ResourceBuffer output{ResourceType::PLANKS, 16};
    ResourceBuffer input{ResourceType::WOOD, 8};

    inputBuffers.insert({ResourceType::WOOD, input});
    outputBuffers.insert({ResourceType::PLANKS, output});
}

Mine::Mine(int i)
{
    // type = ResourceType::Null;
    id = i;
    name = "Mine";
    tag = "[Mine]";
    buildingType = BuildingType::Mine;
    textureId = 2;
    footprint = {2,2};
}

void Mine::InitBuilding(TileType tile)
{
    type = tile;
    if (type == TileType::IRON_ORE)
    {
        products.insert({ResourceType::IRON_ORE, 2});
        productionTime = 2;
        outputBuffers.insert({ResourceType::IRON_ORE, ResourceBuffer{ResourceType::IRON_ORE, 10}});
    }
    if (type == TileType::COAL)
    {
        products.insert({ResourceType::COAL, 2});
        productionTime = 2;
        outputBuffers.insert({ResourceType::COAL, ResourceBuffer{ResourceType::COAL, 10}});
    }
}

Foundry::Foundry(int ajdi)
{
    id = ajdi;
    // type = ResourceType::Null;
    name = "Foundry";
    tag = "[Foundry]";
    buildingType = BuildingType::Foundry;
    textureId = 3;
    footprint = {2,2};
}

void Foundry::SetSupplier(ResourceType type, Building* supplier)
{
    // 1) foundry nic nie produkuje - czyli jest absolutnie nowy supplier
    // czyli: ustawiamy suppliera, ustawiamy produkcje danego surowca
    // 2) foundry coś produkuje i zmienia się supplier (tego samego surowca) ✅
    // czyli: musimy po prostu zmienić pointer na dostawce ✅
    // 3) foundry coś produkuje i zmienia się supplier i typ surowca
    // czyli: musimy wszystko wyczyścić (mapy ingredients, products, suppliers, receivers itd) i ustawić na nowo
    if(type != type && type != ResourceType::COAL) 
    {
        suppliersMap.clear();
        ingredients.clear();
        inputBuffers.clear();
        outputBuffers.clear();
        products.clear();
        receiversMap.clear();
    }
    auto [it, ok] = suppliersMap.insert({type, supplier});
    if(!ok)
    {
        suppliersMap[type] = supplier;
        return;
    }
    switch (type)
    {
    case ResourceType::IRON_ORE:
    {
    type = ResourceType::IRON;
    products.insert({ResourceType::IRON, 2});
    productionTime = 2;

    ingredients.insert({ResourceType::IRON_ORE, 1});
    ingredients.insert({ResourceType::COAL, 1});

    ResourceBuffer output{ResourceType::IRON, 16};
    ResourceBuffer input{ResourceType::IRON_ORE, 8};
    ResourceBuffer input2{ResourceType::COAL, 8};


    inputBuffers.insert({ResourceType::IRON_ORE, input});
    inputBuffers.insert({ResourceType::COAL, input2});
    outputBuffers.insert({ResourceType::IRON, output});
    break;
    }
    
    default:
        break;
    }    

}
    void Foundry::SetReceiver(ResourceType, Building*)
    {

    }

    
StorageBuilding::StorageBuilding(int actualId)
{
    id = actualId;
    name = "Storage Building";
    tag = "[StorageBuilding]";
    resourceBuffers.insert({ResourceType::IRON, ResourceBuffer {ResourceType::IRON, 16}});
    resourceBuffers.insert({ResourceType::IRON_ORE, ResourceBuffer {ResourceType::IRON_ORE, 16}});
    resourceBuffers.insert({ResourceType::WOOD, ResourceBuffer {ResourceType::WOOD, 16}});
    resourceBuffers.insert({ResourceType::PLANKS, ResourceBuffer {ResourceType::PLANKS, 16}});
    resourceBuffers.insert({ResourceType::COAL, ResourceBuffer {ResourceType::COAL, 16}});
    buildingType = BuildingType::StorageBuilding;
    textureId = 4;
    footprint = {2,2};
}
void StorageBuilding::AddResource(Resource* res)
{
    Log::Msg(tag, "resource added!");
    resourceBuffers[res->type].AddResource(res);
}

Resource StorageBuilding::GetResource(ResourceType type)
{
    auto [isAvailable, resource] = resourceBuffers[type].GetResource();
    return isAvailable ? *resource : Resource{};
}

void StorageBuilding::HandleTransport(ResourceType resource, int amount, Building* receiver)
{
    for(int i = 0; i<amount; i++)
    {
        // check if given resource has been storaged in output buffer
        auto [isAvailable, res] = resourceBuffers[resource].GetResource();
        if(isAvailable)
        {
            Log::Msg(tag, "ID: ", id, " ", rt2s(res->type), " transport started to ", receiver->name, " with ID ", receiver->id);
            owner->BeginTransport(this, receiver, res);
        }
    }
}
void StorageBuilding::Update(double dt)
{
    lifetime += dt;
    UpdateTransportables(dt);
}
void StorageBuilding::SetReceiver(ResourceType type, Building* receiver)
{
    receiver->SetSupplier(type, this);
}
void StorageBuilding::SetSupplier(ResourceType type, Building* receiver)
{
    
}
void StorageBuilding::InitBuilding(TileType tajl)
{
    
}

std::vector<ResourceBufferView> StorageBuilding::GetOutputBufferViews() const
{
    std::vector<ResourceBufferView> result;
    for (const auto &[resource, buffer] : resourceBuffers)
    {
        result.push_back({resource, static_cast<int>(buffer.buffer.size()), buffer.bufferSize, 0});
    }
    return result;
}

void StorageBuilding::ReceptTransport(Transportable* trans)
{
    // transportables.push_back(trans);
}

Road::Road(int i)
{
    id = i;
    name = "Road";
    tag = "[Road]";
    buildingType = BuildingType::Road;
    textureId = 5;
    transportTime = 1.0;
}

void Road::Update(double dt)
{
    lifetime += dt;
    UpdateTransportables(dt);
}



