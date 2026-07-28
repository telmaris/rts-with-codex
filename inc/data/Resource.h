#ifndef RESOURCE_H
#define RESOURCE_H

#include "core/Types.h"
#include "simulation/Transport.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

// raylib exposes GOLD as a color macro; resources need the plain enum name.
#ifdef GOLD
#undef GOLD
#endif

// ─── Resource Taxonomy ─────────────────────────────────────────────────────
// Resources fall into two classes, each with different transport/storage rules:
//
// 1. CONCRETE — individual items produced by buildings and transported over roads
//    as Transportable resource objects. Stored in StorageComponent buffers.
//    Types: WOOD, PLANKS, BREAD, MEAT, TOOLS, all ore/metal, armor, weapons (SWORD, BOW, etc),
//    and FOOD_PROVISIONS (consumed by Village -> manpower, see PopulationComponent).
//    Transport: BeginTransport(source, receiver, resource)
//
// 2. STRATEGIC — global aggregates tracked in StrategicResourcePool on Player
//    (no per-building instance, never transported). Types: Manpower, Workers.

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

    COPPER_SWORD = 24,
    IRON_SWORD = 25,
    STEEL_SWORD = 26,
    BOW = 27,
    ARROWS = 28,
    HORSE = 29,

    // Equipment categories for the supply/division system (material progression:
    // stone → copper → bronze → iron → steel). See Equipment.h for the taxonomy.
    BRONZE_SWORD = 30,
    SPEAR = 31,
    CROSSBOW = 32,
    BOLTS = 33,
    WOODEN_SHIELD = 34,
    IRON_SHIELD = 35,
    LEATHER_ARMOR = 36,
    IRON_ARMOR = 37,

    // Resource & world expansion (see docs/resource_world_design.md).
    // Raw deposits:
    TIN_ORE = 38,
    SAND = 39,
    SULFUR = 40,
    SALTPETER = 41,
    // Smelted / processed:
    TIN = 42,
    BRONZE = 43,
    COKE = 44,
    STEEL = 45,
    GLASS = 46,
    GUNPOWDER = 47,

    // Firearms (Phase 3 — steampunk chemistry consumer):
    MUSKET = 48,
    CARTRIDGE = 49,

    // Active medieval economy expansion. Values are appended deliberately:
    // saves serialize ResourceType numerically, so retired prototype resources
    // above must keep their historical ids even when no longer generated.
    CLAY = 50,
    CATTLE = 51,
    RAW_HIDE = 52,
    TALLOW = 53,
    CLOTHES = 54,
    POTTERY = 55,
    HOUSEHOLD_GOODS = 56,
    SOAP = 57,
    INK = 58,
    BOOKS = 59,
    COPPERWARE = 60,
    URBAN_GOODS = 61,
    HEMP = 62,
    FIBRE = 63,
    ROPE = 64,
    COPPER_VESSEL = 65,
    COPPER_PIPE = 66,
    MECHANICAL_PARTS = 67,
    HEAVY_BOW = 68,
    LIGHT_WEAPON = 69,
    HEAVY_ARMOR = 70,
    BRICKS = 71,
    CLOTH = 72,
    BALLISTA = 73,
    BATTERING_RAM = 74,
    CATAPULT = 75

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
    ResourceType::COPPER_SWORD,
    ResourceType::IRON_SWORD,
    ResourceType::STEEL_SWORD,
    ResourceType::BOW,
    ResourceType::ARROWS,
    ResourceType::HORSE,
    ResourceType::BRONZE_SWORD,
    ResourceType::SPEAR,
    ResourceType::CROSSBOW,
    ResourceType::BOLTS,
    ResourceType::WOODEN_SHIELD,
    ResourceType::IRON_SHIELD,
    ResourceType::LEATHER_ARMOR,
    ResourceType::IRON_ARMOR,
    ResourceType::COPPER_ORE,
    ResourceType::COPPER,
    ResourceType::IRON_ORE,
    ResourceType::IRON,
    ResourceType::SILVER_ORE,
    ResourceType::SILVER,
    ResourceType::GOLD_ORE,
    ResourceType::GOLD,
    ResourceType::TIN_ORE,
    ResourceType::SAND,
    ResourceType::SULFUR,
    ResourceType::SALTPETER,
    ResourceType::TIN,
    ResourceType::BRONZE,
    ResourceType::COKE,
    ResourceType::STEEL,
    ResourceType::GLASS,
    ResourceType::GUNPOWDER,
    ResourceType::MUSKET,
    ResourceType::CARTRIDGE,
    ResourceType::CLAY,
    ResourceType::CATTLE,
    ResourceType::RAW_HIDE,
    ResourceType::TALLOW,
    ResourceType::CLOTHES,
    ResourceType::POTTERY,
    ResourceType::HOUSEHOLD_GOODS,
    ResourceType::SOAP,
    ResourceType::INK,
    ResourceType::BOOKS,
    ResourceType::COPPERWARE,
    ResourceType::URBAN_GOODS,
    ResourceType::HEMP,
    ResourceType::FIBRE,
    ResourceType::ROPE,
    ResourceType::COPPER_VESSEL,
    ResourceType::COPPER_PIPE,
    ResourceType::MECHANICAL_PARTS,
    ResourceType::HEAVY_BOW,
    ResourceType::LIGHT_WEAPON,
    ResourceType::HEAVY_ARMOR,
    ResourceType::BRICKS,
    ResourceType::CLOTH,
    ResourceType::BALLISTA,
    ResourceType::BATTERING_RAM,
    ResourceType::CATAPULT
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
        case ResourceType::PAPER: return "PAPER";
        case ResourceType::TOOLS: return "TOOLS";
        case ResourceType::COPPER_SWORD: return "COPPER_SWORD";
        case ResourceType::IRON_SWORD: return "IRON_SWORD";
        case ResourceType::STEEL_SWORD: return "STEEL_SWORD";
        case ResourceType::BOW: return "BOW";
        case ResourceType::ARROWS: return "ARROWS";
        case ResourceType::HORSE: return "HORSE";
        case ResourceType::BRONZE_SWORD: return "BRONZE_SWORD";
        case ResourceType::SPEAR: return "SPEAR";
        case ResourceType::CROSSBOW: return "CROSSBOW";
        case ResourceType::BOLTS: return "BOLTS";
        case ResourceType::WOODEN_SHIELD: return "WOODEN_SHIELD";
        case ResourceType::IRON_SHIELD: return "IRON_SHIELD";
        case ResourceType::LEATHER_ARMOR: return "LEATHER_ARMOR";
        case ResourceType::IRON_ARMOR: return "IRON_ARMOR";
        case ResourceType::TIN_ORE: return "TIN_ORE";
        case ResourceType::SAND: return "SAND";
        case ResourceType::SULFUR: return "SULFUR";
        case ResourceType::SALTPETER: return "SALTPETER";
        case ResourceType::TIN: return "TIN";
        case ResourceType::BRONZE: return "BRONZE";
        case ResourceType::COKE: return "COKE";
        case ResourceType::STEEL: return "STEEL";
        case ResourceType::GLASS: return "GLASS";
        case ResourceType::GUNPOWDER: return "GUNPOWDER";
        case ResourceType::MUSKET: return "MUSKET";
        case ResourceType::CARTRIDGE: return "CARTRIDGE";
        case ResourceType::CLAY: return "CLAY";
        case ResourceType::CATTLE: return "CATTLE";
        case ResourceType::RAW_HIDE: return "RAW_HIDE";
        case ResourceType::TALLOW: return "TALLOW";
        case ResourceType::CLOTHES: return "CLOTHES";
        case ResourceType::POTTERY: return "POTTERY";
        case ResourceType::HOUSEHOLD_GOODS: return "HOUSEHOLD_GOODS";
        case ResourceType::SOAP: return "SOAP";
        case ResourceType::INK: return "INK";
        case ResourceType::BOOKS: return "BOOKS";
        case ResourceType::COPPERWARE: return "COPPERWARE";
        case ResourceType::URBAN_GOODS: return "URBAN_GOODS";
        case ResourceType::HEMP: return "HEMP";
        case ResourceType::FIBRE: return "FIBRE";
        case ResourceType::ROPE: return "ROPE";
        case ResourceType::COPPER_VESSEL: return "COPPER_VESSEL";
        case ResourceType::COPPER_PIPE: return "COPPER_PIPE";
        case ResourceType::MECHANICAL_PARTS: return "MECHANICAL_PARTS";
        case ResourceType::HEAVY_BOW: return "HEAVY_BOW";
        case ResourceType::LIGHT_WEAPON: return "LIGHT_WEAPON";
        case ResourceType::HEAVY_ARMOR: return "HEAVY_ARMOR";
        case ResourceType::BRICKS: return "BRICKS";
        case ResourceType::CLOTH: return "CLOTH";
        case ResourceType::BALLISTA: return "BALLISTA";
        case ResourceType::BATTERING_RAM: return "BATTERING_RAM";
        case ResourceType::CATAPULT: return "CATAPULT";

        default: return "Unknown";
    }
}

// Player-facing resource name. Keep rt2s() as the stable, all-caps debug and
// serialization label; UI should use this helper instead.
inline std::string ResourceDisplayName(ResourceType type)
{
    std::string name = rt2s(type);
    bool capitalizeNext = true;
    for (char& character : name)
    {
        if (character == '_')
        {
            character = ' ';
            capitalizeNext = true;
            continue;
        }

        unsigned char value = static_cast<unsigned char>(character);
        character = static_cast<char>(capitalizeNext ? std::toupper(value) : std::tolower(value));
        capitalizeNext = false;
    }
    return name;
}

// ─── Resource categories / tags ───────────────────────────────────────────────
// Every ResourceType belongs to exactly one broad category. Categories let
// buildings and bonuses reason about *classes* of goods instead of hard-coding
// individual resource ids: a "+10% Metal production" bonus lifts every metal, a
// "+5% Sword power" bonus lifts every sword tier, a supply hub can pack "the best
// available Sword" without naming COPPER_SWORD/IRON_SWORD/… one by one.
//
// This is the authoritative economic tag layer. The finer combat role of a
// weapon (slot, quality) still lives in Equipment.h's EquipmentCategory; the two
// are kept consistent by ResourceCategoryOf() deriving weapon categories from the
// equipment profile (see Resource.cpp).
enum class ResourceCategory : uint8_t
{
    None = 0,

    // Economy / production chains
    Ore,             // raw mined ore deposits (COPPER_ORE, IRON_ORE, TIN_ORE, …)
    Mineral,         // quarried/mined non-metal solids (COAL, STONE, SAND, SULFUR, …)
    Metal,           // refined metals (COPPER, IRON, BRONZE, STEEL, …)
    Timber,          // WOOD, PLANKS
    Textile,         // LEATHER
    Foodstuff,       // WHEAT, FLOUR, BREAD, MEAT, WATER, BEER
    Chemical,        // processed industrial goods (GLASS, GUNPOWDER, COKE)
    Tool,            // TOOLS
    Paper,           // PAPER
    Currency,        // COINS
    Mount,           // HORSE
    Livestock,       // CATTLE
    CraftedGood,     // pottery, clothes, books, copper goods, mechanisms
    SettlementSupply,// HOUSEHOLD_GOODS, URBAN_GOODS

    // Military logistics (abstract package units carried to the front)
    MilitarySupply,  // FOOD_PROVISIONS

    // Equipment (mirrors Equipment.h EquipmentCategory so gear is tagged too)
    Sword,
    Spear,
    Bow,
    Crossbow,
    Firearm,
    Ammunition,      // ARROWS, BOLTS, CARTRIDGE
    Shield,
    Armor,

    Count
};

// Authoritative category of a resource type. Weapon/armor categories are derived
// from the equipment profile so the two taxonomies never drift. Defined in
// Resource.cpp.
ResourceCategory ResourceCategoryOf(ResourceType type);

// Human-readable label for a category (debug / UI / data files).
const char* ResourceCategoryLabel(ResourceCategory category);

// True when the category is a weapon/armor/ammo class (i.e. an equipment tag).
bool IsEquipmentCategory(ResourceCategory category);

// True when the category is a primary weapon (Sword/Spear/Bow/Crossbow/Firearm).
bool IsWeaponCategory(ResourceCategory category);

// Transportable resource instance owned by the global resource pool.
struct Resource : Transportable
{
    Resource() = default;
    Resource(ResourceType rtype) : type(rtype), category(ResourceCategoryOf(rtype)) {}
    ~Resource() = default;
    std::string tag{"[Resource]"};
    ResourceType type{ResourceType::Null};
    // Broad economic/combat tag of this resource, derived from `type`.
    ResourceCategory category{ResourceCategory::None};
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
    // Rebuilds every type's free-list to full capacity, discarding whatever
    // is currently checked out. This pool is a single process-wide static
    // (see Resource.cpp) shared by every GameWorld in the process — real
    // gameplay never needs a mid-run reset, but a test binary that
    // constructs many short-lived GameWorlds in one process can leave a
    // type's pool partially (or, given enough tests, mostly) checked out by
    // the time a later test runs. A test that then builds two structurally
    // identical worlds and expects identical starting stock breaks: whichever
    // world's GenerateResource() calls run first gets first pick of what's
    // left, so the second gets a silently truncated grant. See
    // tests/TestResourcePoolIsolation.cpp, which resets between test cases.
    void Reset();

    std::map<ResourceType, std::array<Resource, 50000>> pool;
    std::map<ResourceType, AddressPool> addressPool;
};

// Resets the process-wide resource pool (see ResourcePool::Reset). Exposed
// as a free function because `resourcePool` itself has internal linkage.
void ResetResourcePool();

#endif
