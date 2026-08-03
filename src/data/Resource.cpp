#include "data/Resource.h"
#include "economy/Building.h"
#include "data/Equipment.h"

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
        case ResourceType::CLAY:
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
        case ResourceType::RAW_HIDE:
        case ResourceType::HEMP:
        case ResourceType::FIBRE:
        case ResourceType::ROPE:
        case ResourceType::CLOTH:
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
        case ResourceType::TALLOW:
        case ResourceType::SOAP:
        case ResourceType::INK:
            return ResourceCategory::Chemical;

        case ResourceType::TOOLS:
            return ResourceCategory::Tool;

        case ResourceType::PAPER:
            return ResourceCategory::Paper;

        case ResourceType::COINS:
            return ResourceCategory::Currency;

        case ResourceType::HORSE:
            return ResourceCategory::Mount;

        case ResourceType::CATTLE:
            return ResourceCategory::Livestock;

        case ResourceType::HOUSEHOLD_GOODS:
        case ResourceType::URBAN_GOODS:
            return ResourceCategory::SettlementSupply;

        case ResourceType::CLOTHES:
        case ResourceType::POTTERY:
        case ResourceType::BOOKS:
        case ResourceType::COPPERWARE:
        case ResourceType::COPPER_VESSEL:
        case ResourceType::COPPER_PIPE:
        case ResourceType::MECHANICAL_PARTS:
        case ResourceType::BRICKS:
        case ResourceType::BALLISTA:
        case ResourceType::BATTERING_RAM:
        case ResourceType::CATAPULT:
            return ResourceCategory::CraftedGood;

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
        case ResourceCategory::Livestock:      return "Livestock";
        case ResourceCategory::CraftedGood:    return "CraftedGood";
        case ResourceCategory::SettlementSupply:return "SettlementSupply";
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

Resource* Resource::CreateOwned(ResourceType type)
{
    Resource* resource = new Resource(type);
    resource->ownedAllocation = true;
    return resource;
}

void Resource::DestroyOwned(Resource* resource)
{
    if (resource == nullptr || !resource->ownedAllocation)
        return;
    resource->ownedAllocation = false;
    delete resource;
}

ResourceBuffer::~ResourceBuffer()
{
    Clear();
}

ResourceBuffer::ResourceBuffer(const ResourceBuffer& other)
    : bufferSize(other.bufferSize), type(other.type)
{
    for (std::size_t i = 0; i < other.buffer.size(); ++i)
        GenerateResource(type);
}

ResourceBuffer& ResourceBuffer::operator=(const ResourceBuffer& other)
{
    if (this == &other)
        return *this;
    Clear();
    bufferSize = other.bufferSize;
    type = other.type;
    for (std::size_t i = 0; i < other.buffer.size(); ++i)
        GenerateResource(type);
    return *this;
}

ResourceBuffer::ResourceBuffer(ResourceBuffer&& other) noexcept
    : bufferSize(other.bufferSize), type(other.type), buffer(std::move(other.buffer))
{
    other.bufferSize = 0;
    other.type = ResourceType::Null;
}

ResourceBuffer& ResourceBuffer::operator=(ResourceBuffer&& other) noexcept
{
    if (this == &other)
        return *this;
    Clear();
    bufferSize = other.bufferSize;
    type = other.type;
    buffer = std::move(other.buffer);
    other.bufferSize = 0;
    other.type = ResourceType::Null;
    return *this;
}

// Adds this object or value to local state.
void ResourceBuffer::AddResource(Resource* res)
{
    if (res == nullptr)
        return;
    if (buffer.size() < static_cast<std::size_t>(std::max(0, bufferSize)))
        buffer.push_back(res);
    else
        Resource::DestroyOwned(res);
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
    AddResource(Resource::CreateOwned(type));
}

// Releases one stored owned resource instance.
void ResourceBuffer::FreeResource()
{
    if (buffer.empty())
        return;
    auto res = buffer.back();
    buffer.pop_back();
    Resource::DestroyOwned(res);
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

// Compatibility facade for callers that still request an owned resource
// directly. New gameplay code uses ResourceBuffer::GenerateResource.
Resource* ResourcePool::GetResource(ResourceType type)
{
    return Resource::CreateOwned(type);
}

// Releases an owned resource, ignoring external stack-backed values.
void ResourcePool::FreeResource(Resource* res)
{
    Resource::DestroyOwned(res);
}
