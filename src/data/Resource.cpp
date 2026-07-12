#include "data/Resource.h"
#include "economy/Building.h"
#include "data/Equipment.h"

static ResourcePool resourcePool;

// Maps an equipment role (Equipment.h) onto its economic tag. Keeping the weapon
// categories derived from the equipment table means the two taxonomies can never
// disagree — add one profile row and both layers see the new gear.
static ResourceCategory CategoryOfEquipment(EquipmentCategory category)
{
    switch (category)
    {
        case EquipmentCategory::Sword:    return ResourceCategory::Sword;
        case EquipmentCategory::Spear:    return ResourceCategory::Spear;
        case EquipmentCategory::Bow:      return ResourceCategory::Bow;
        case EquipmentCategory::Crossbow: return ResourceCategory::Crossbow;
        case EquipmentCategory::Firearm:  return ResourceCategory::Firearm;
        case EquipmentCategory::Ammo:     return ResourceCategory::Ammunition;
        case EquipmentCategory::Shield:   return ResourceCategory::Shield;
        case EquipmentCategory::Armor:    return ResourceCategory::Armor;
        default:                          return ResourceCategory::None;
    }
}

// Authoritative resource -> category mapping. Equipment defers to the profile
// table; everything else is classified here by production chain.
ResourceCategory ResourceCategoryOf(ResourceType type)
{
    if (const EquipmentProfile* profile = FindEquipmentProfile(type))
        return CategoryOfEquipment(profile->category);

    switch (type)
    {
        // Raw ore deposits
        case ResourceType::COPPER_ORE:
        case ResourceType::IRON_ORE:
        case ResourceType::SILVER_ORE:
        case ResourceType::GOLD_ORE:
        case ResourceType::TIN_ORE:
            return ResourceCategory::Ore;

        // Quarried / mined non-metal solids
        case ResourceType::COAL:
        case ResourceType::STONE:
        case ResourceType::SAND:
        case ResourceType::SULFUR:
        case ResourceType::SALTPETER:
            return ResourceCategory::Mineral;

        // Refined metals
        case ResourceType::COPPER:
        case ResourceType::IRON:
        case ResourceType::SILVER:
        case ResourceType::GOLD:
        case ResourceType::TIN:
        case ResourceType::BRONZE:
        case ResourceType::STEEL:
            return ResourceCategory::Metal;

        case ResourceType::WOOD:
        case ResourceType::PLANKS:
            return ResourceCategory::Timber;

        case ResourceType::LEATHER:
            return ResourceCategory::Textile;

        case ResourceType::WHEAT:
        case ResourceType::FLOUR:
        case ResourceType::BREAD:
        case ResourceType::MEAT:
        case ResourceType::WATER:
        case ResourceType::BEER:
            return ResourceCategory::Foodstuff;

        // Processed industrial goods (COKE is a fuel but is a processed chemical good)
        case ResourceType::GLASS:
        case ResourceType::GUNPOWDER:
        case ResourceType::COKE:
            return ResourceCategory::Chemical;

        case ResourceType::TOOLS:
            return ResourceCategory::Tool;

        case ResourceType::PAPER:
            return ResourceCategory::Paper;

        case ResourceType::COINS:
            return ResourceCategory::Currency;

        case ResourceType::HORSE:
            return ResourceCategory::Mount;

        case ResourceType::FOOD_PROVISIONS:
            return ResourceCategory::MilitarySupply;

        default:
            return ResourceCategory::None;
    }
}

const char* ResourceCategoryLabel(ResourceCategory category)
{
    switch (category)
    {
        case ResourceCategory::Ore:            return "Ore";
        case ResourceCategory::Mineral:        return "Mineral";
        case ResourceCategory::Metal:          return "Metal";
        case ResourceCategory::Timber:         return "Timber";
        case ResourceCategory::Textile:        return "Textile";
        case ResourceCategory::Foodstuff:      return "Foodstuff";
        case ResourceCategory::Chemical:       return "Chemical";
        case ResourceCategory::Tool:           return "Tool";
        case ResourceCategory::Paper:          return "Paper";
        case ResourceCategory::Currency:       return "Currency";
        case ResourceCategory::Mount:          return "Mount";
        case ResourceCategory::MilitarySupply: return "MilitarySupply";
        case ResourceCategory::Sword:          return "Sword";
        case ResourceCategory::Spear:          return "Spear";
        case ResourceCategory::Bow:            return "Bow";
        case ResourceCategory::Crossbow:       return "Crossbow";
        case ResourceCategory::Firearm:        return "Firearm";
        case ResourceCategory::Ammunition:     return "Ammunition";
        case ResourceCategory::Shield:         return "Shield";
        case ResourceCategory::Armor:          return "Armor";
        default:                               return "None";
    }
}

bool IsEquipmentCategory(ResourceCategory category)
{
    switch (category)
    {
        case ResourceCategory::Sword:
        case ResourceCategory::Spear:
        case ResourceCategory::Bow:
        case ResourceCategory::Crossbow:
        case ResourceCategory::Firearm:
        case ResourceCategory::Ammunition:
        case ResourceCategory::Shield:
        case ResourceCategory::Armor:
            return true;
        default:
            return false;
    }
}

bool IsWeaponCategory(ResourceCategory category)
{
    switch (category)
    {
        case ResourceCategory::Sword:
        case ResourceCategory::Spear:
        case ResourceCategory::Bow:
        case ResourceCategory::Crossbow:
        case ResourceCategory::Firearm:
            return true;
        default:
            return false;
    }
}

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
    if (res != nullptr)
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

// Returns one pooled resource instance of the requested type, or nullptr if
// this type's fixed-size pool (see `pool` — std::array<Resource, 10000> per
// type) is currently fully checked out. Exhaustion is expected, not
// exceptional (e.g. many short-lived test worlds sharing this process-wide
// pool without ever returning every instance) — callers already treat "no
// resource available" as routine, matching the ResourceType::Null sentinel
// convention used throughout this codebase.
Resource* ResourcePool::GetResource(ResourceType type)
{
    auto& addresses = addressPool[type].addresses;
    if (addresses.empty())
        return nullptr;

    auto res = addresses.front();
    addresses.pop_front();
    return res;
}

// Returns this resource to its pool or buffer.
void ResourcePool::FreeResource(Resource* res)
{
    auto type = res->type;
    addressPool[type].addresses.push_front(res);
}
