#ifndef RESOURCE_H
#define RESOURCE_H

#include "Utils.h"
#include "Transport.h"

// raylib exposes GOLD as a color macro; resources need the plain enum name.
#ifdef GOLD
#undef GOLD
#endif

enum class ResourceType : uint8_t
{
    Null = 255,

    COPPER_ORE = 0,
    COPPER = 1,
    IRON_ORE = 2,
    IRON = 3,
    SILVER_ORE = 4,
    SILVER = 5,
    GOLD_ORE = 6,
    GOLD = 7,

    WOOD = 8,
    PLANKS = 9,

    LEATHER = 10,
    COAL = 11,
    STONE = 12,

    WHEAT = 13,
    FLOUR = 14,
    BREAD = 15,
    MEAT = 16,
    WATER = 17,
    BEER = 18,
    
    COINS = 19,
    PAPER = 20,
    
    TOOLS = 21,
    FOOD_PROVISIONS = 22,
    WEAPON_SUPPLY = 23,

    COPPER_SWORD = 24,
    IRON_SWORD = 25,
    STEEL_SWORD = 26,
    BOW = 27,
    ARROWS = 28,
    HORSE = 29,
    // STONE_HAMMER = 34,
    // ARMOR = 38

};

// Resource types currently allocated by the fixed resource pool.
constexpr ResourceType resourceTypes[] = 
{
    ResourceType::WOOD,
    ResourceType::PLANKS,
    ResourceType::LEATHER,
    ResourceType::COAL,
    ResourceType::STONE,
    ResourceType::WHEAT,
    ResourceType::FLOUR,
    ResourceType::BREAD,
    ResourceType::MEAT,
    ResourceType::WATER,
    ResourceType::BEER,
    ResourceType::COINS,
    ResourceType::PAPER,
    ResourceType::TOOLS,
    ResourceType::FOOD_PROVISIONS,
    ResourceType::WEAPON_SUPPLY,
    ResourceType::COPPER_SWORD,
    ResourceType::IRON_SWORD,
    ResourceType::STEEL_SWORD,
    ResourceType::BOW,
    ResourceType::ARROWS,
    ResourceType::HORSE,
    ResourceType::COPPER_ORE,
    ResourceType::COPPER,
    ResourceType::IRON_ORE,
    ResourceType::IRON,
    ResourceType::SILVER_ORE,
    ResourceType::SILVER,
    ResourceType::GOLD_ORE,
    ResourceType::GOLD
};

// Converts resource type to a readable debug label.
inline std::string rt2s(ResourceType s)
{
    switch (s)
    {
        case ResourceType::Null: return "NULL";
        case ResourceType::COPPER_ORE: return "COPPER_ORE";
        case ResourceType::COPPER: return "COPPER";
        case ResourceType::WOOD:  return "WOOD";
        case ResourceType::IRON_ORE: return "IRON_ORE";
        case ResourceType::SILVER_ORE: return "SILVER_ORE";
        case ResourceType::SILVER: return "SILVER";
        case ResourceType::GOLD_ORE: return "GOLD_ORE";
        case ResourceType::GOLD: return "GOLD";
        case ResourceType::COAL: return "COAL";
        case ResourceType::STONE: return "STONE";
        case ResourceType::IRON: return "IRON";
        case ResourceType::PLANKS: return "PLANKS";
        case ResourceType::LEATHER: return "LEATHER";
        case ResourceType::MEAT: return "MEAT";
        case ResourceType::WHEAT: return "WHEAT";
        case ResourceType::BREAD: return "BREAD";
        case ResourceType::FLOUR: return "FLOUR";
        case ResourceType::WATER: return "WATER";
        case ResourceType::BEER: return "BEER";
        case ResourceType::COINS: return "COINS";
        case ResourceType::FOOD_PROVISIONS: return "FOOD_PROVISIONS";
        case ResourceType::WEAPON_SUPPLY: return "WEAPON_SUPPLY";
        case ResourceType::PAPER: return "PAPER";
        case ResourceType::TOOLS: return "TOOLS";
        case ResourceType::COPPER_SWORD: return "COPPER_SWORD";
        case ResourceType::IRON_SWORD: return "IRON_SWORD";
        case ResourceType::STEEL_SWORD: return "STEEL_SWORD";
        case ResourceType::BOW: return "BOW";
        case ResourceType::ARROWS: return "ARROWS";
        case ResourceType::HORSE: return "HORSE";

        default: return "Unknown";
    }
}

// Transportable resource instance owned by the global resource pool.
struct Resource : Transportable
{
    Resource() = default;
    Resource(ResourceType rtype) : type(rtype) {}
    ~Resource() = default;
    std::string tag{"[Resource]"};
    ResourceType type{ResourceType::Null};
};

// Single-resource-type FIFO/LIFO buffer used by buildings.
class ResourceBuffer
{
    public:
        ResourceBuffer(ResourceType t, int size) : type(t), bufferSize(size) {}
        ResourceBuffer() = default;

        int bufferSize{0};
        ResourceType type{ResourceType::Null};

        // Adds a resource pointer when there is free capacity.
        void AddResource(Resource* res);
        // Removes and returns one resource pointer when available.
        std::pair<bool, Resource*> GetResource();
        
        // Pulls one resource instance from the pool and stores it in this buffer.
        void GenerateResource(ResourceType type);
        // Returns one stored resource instance to the pool.
        void FreeResource();
        // Returns all stored resources to the pool.
        void Clear();
        // Replaces stored amount with freshly generated pooled resources.
        void SetStoredAmount(int amount);

        std::vector<Resource*> buffer;
};

// Free-list of available resource instances for one resource type.
struct AddressPool
{
    std::deque<Resource*> addresses;
};

// Fixed-size resource allocator used to avoid per-resource heap allocations.
class ResourcePool
{
public:

    ResourcePool()
    {
        for(auto& resType : resourceTypes)
        {
            auto& arr = pool[resType];
            for (auto& x : arr)
            {
                x = Resource(resType);
            }
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

    // Returns an available resource instance of the requested type.
    Resource* GetResource(ResourceType);
    // Returns a resource instance to its type-specific free-list.
    void FreeResource(Resource*);

    std::map<ResourceType, std::array<Resource, 10000>> pool;
    std::map<ResourceType, AddressPool> addressPool;
};

#endif
