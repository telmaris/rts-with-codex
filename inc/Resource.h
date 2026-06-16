#ifndef RESOURCE_H
#define RESOURCE_H

#include "Utils.h"
#include "Transport.h"

enum class ResourceType : uint8_t
{
    Null = 0,
    WOOD = 1,
    IRON_ORE = 2,
    COAL = 3,
    IRON = 4,
    PLANKS = 5
};

constexpr ResourceType resourceTypes[] = 
{
    ResourceType::WOOD,
    ResourceType::IRON_ORE,
    ResourceType::COAL,
    ResourceType::IRON,
    ResourceType::PLANKS
};

inline std::string rt2s(ResourceType s)
{
    switch (s)
    {
        case ResourceType::Null: return "NULL";
        case ResourceType::WOOD:  return "WOOD";
        case ResourceType::IRON_ORE: return "IRON_ORE";
        case ResourceType::COAL: return "COAL";
        case ResourceType::IRON: return "IRON";
        case ResourceType::PLANKS: return "PLANKS";

        default: return "Unknown";
    }
}

struct Resource : Transportable
{
    Resource() = default;
    Resource(ResourceType rtype) : type(rtype) {}
    ~Resource() = default;
    std::string tag{"[Resource]"};
    ResourceType type{ResourceType::Null};
};

class ResourceBuffer
{
    public:
        ResourceBuffer(ResourceType t, int size) : type(t), bufferSize(size) {}
        ResourceBuffer() = default;

        int bufferSize;
        ResourceType type;  // buffer can allocate 1 type of resources

        void AddResource(Resource* res);
        std::pair<bool, Resource*> GetResource();    // returns bool - is resource available, Resource - obtained resource
        
        void GenerateResource(ResourceType type);
        void FreeResource();

        std::vector<Resource*> buffer;
};

// =========== RESOURCE POOL ================

struct AddressPool
{
    std::deque<Resource*> addresses;
};

class ResourcePool
{
public:

    ResourcePool()
    {
        for(auto& resType : resourceTypes)
        {
            std::array<Resource, 10000> arr;
            for (auto& x : arr)
            {
                x = Resource(resType);
            }
            pool.insert({resType, arr});
        }

        for(auto& [type, arr] : pool)
        {
            auto& addresses = addressPool[type];
            for(int i = 0; i < arr.size(); i++)
            {
                addresses.addresses.push_back(&arr[i]);
            }
        }
    }

    Resource* GetResource(ResourceType);
    void FreeResource(Resource*);

    std::map<ResourceType, std::array<Resource, 10000>> pool;
    std::map<ResourceType, AddressPool> addressPool;
};

#endif