#include "../inc/Resource.h"
#include "../inc/Building.h"

static ResourcePool resourcePool;

// Adds this object or value to local state.
void ResourceBuffer::AddResource(Resource* res)
{
    if(buffer.size() < bufferSize)
    {
        buffer.push_back(res);
    }
}

// Removes and returns one resource pointer when available.
std::pair<bool, Resource*> ResourceBuffer::GetResource()
{
    if(buffer.size() > 0)
    {
        auto res = buffer.back();
        buffer.pop_back();
        return {true, res};
    }
    return {false, nullptr};
}

// Initializes ResourceBuffer::GenerateResource.
void ResourceBuffer::GenerateResource(ResourceType type)
{
    auto res = resourcePool.GetResource(type);
    AddResource(res);
}

// Returns this resource to its pool or buffer.
void ResourceBuffer::FreeResource()
{
    auto res = buffer.back();
    resourcePool.FreeResource(res);
    buffer.pop_back();
}

// Clears this runtime state.
void ResourceBuffer::Clear()
{
    while (!buffer.empty())
        FreeResource();
}

// Updates the requested state value.
void ResourceBuffer::SetStoredAmount(int amount)
{
    Clear();
    for (int i = 0; i < amount && i < bufferSize; i++)
        GenerateResource(type);
}

// Returns one pooled resource instance of the requested type.
Resource* ResourcePool::GetResource(ResourceType type)
{
    auto res = addressPool[type].addresses.front();
    addressPool[type].addresses.pop_front();
    return res;
}

// Returns this resource to its pool or buffer.
void ResourcePool::FreeResource(Resource* res)
{
    auto type = res->type;
    addressPool[type].addresses.push_front(res);
}
