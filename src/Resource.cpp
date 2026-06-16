#include "../inc/Resource.h"
#include "../inc/Building.h"

static ResourcePool resourcePool;

void ResourceBuffer::AddResource(Resource* res)
{
    if(buffer.size() < bufferSize)
    {
        buffer.push_back(res);
    }
}

//todo: usunąć std::pair
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

void ResourceBuffer::GenerateResource(ResourceType type)
{
    auto res = resourcePool.GetResource(type);
    AddResource(res);
}

void ResourceBuffer::FreeResource()
{
    auto res = buffer.back();
    resourcePool.FreeResource(res);
    buffer.pop_back();
}

Resource* ResourcePool::GetResource(ResourceType type)
{
    auto res = addressPool[type].addresses.front();
    addressPool[type].addresses.pop_front();
    return res;
}

void ResourcePool::FreeResource(Resource* res)
{
    auto type = res->type;
    addressPool[type].addresses.push_front(res);
}