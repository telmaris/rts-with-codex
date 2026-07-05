#include "warfare/Equipment.h"
#include "economy/SupplyPackage.h"

#include <algorithm>

namespace
{
    // Quality multiplier per material tier. Tuned so each step up the progression
    // is a meaningful but not overwhelming upgrade.
    float MaterialQuality(EquipmentMaterial material)
    {
        switch (material)
        {
            case EquipmentMaterial::Wood:    return 0.6f;
            case EquipmentMaterial::Stone:   return 0.7f;
            case EquipmentMaterial::Leather: return 0.8f;
            case EquipmentMaterial::Copper:  return 1.0f;
            case EquipmentMaterial::Bronze:  return 1.2f;
            case EquipmentMaterial::Iron:    return 1.5f;
            case EquipmentMaterial::Steel:   return 1.9f;
            case EquipmentMaterial::Blackpowder: return 2.4f;
            // BUG 5C: enchanted material sits above blackpowder.
            // Add concrete ResourceTypes + profile rows in BuildProfiles() when ready.
            case EquipmentMaterial::Enchanted:   return 3.0f;
            default:                             return 0.0f;
        }
    }

    // The equipment catalogue. One row per equipment ResourceType. Quality is
    // derived from the material tier so the table stays consistent and editable.
    //
    // HOW TO ADD A NEW PIECE OF EQUIPMENT (BUG 5C):
    //  1. Add a new value to ResourceType in Resource.h.
    //  2. Optionally add a new EquipmentCategory or EquipmentMaterial to Equipment.h,
    //     and handle the new enum value in MaterialQuality(), EquipmentCategoryLabel(),
    //     and EquipmentMaterialLabel() below.
    //  3. Add one row to the `rows[]` array in BuildProfiles() here.
    //  4. If the item costs manpower to recruit, add its category price to
    //     the relevant unit class constructor in Building.cpp / Player.cpp.
    //  5. Bump save version in GameWorld.Persistence.cpp if the new resource type
    //     appears inside saved division equipment slots.
    // Nothing else in the simulation needs to know: supply hubs pick the best item
    // per category generically, and DivisionEquipmentQuality uses quality scores.
    std::vector<EquipmentProfile> BuildProfiles()
    {
        struct Row { ResourceType resource; EquipmentCategory category; EquipmentMaterial material; };
        const Row rows[] = {
            // Swords (material progression)
            {ResourceType::COPPER_SWORD,  EquipmentCategory::Sword,    EquipmentMaterial::Copper},
            {ResourceType::BRONZE_SWORD,  EquipmentCategory::Sword,    EquipmentMaterial::Bronze},
            {ResourceType::IRON_SWORD,    EquipmentCategory::Sword,    EquipmentMaterial::Iron},
            {ResourceType::STEEL_SWORD,   EquipmentCategory::Sword,    EquipmentMaterial::Steel},
            // Spears
            {ResourceType::SPEAR,         EquipmentCategory::Spear,    EquipmentMaterial::Iron},
            // Ranged
            {ResourceType::BOW,           EquipmentCategory::Bow,      EquipmentMaterial::Wood},
            {ResourceType::CROSSBOW,      EquipmentCategory::Crossbow, EquipmentMaterial::Iron},
            {ResourceType::MUSKET,        EquipmentCategory::Firearm,  EquipmentMaterial::Blackpowder},
            // Ammo
            {ResourceType::ARROWS,        EquipmentCategory::Ammo,     EquipmentMaterial::Wood},
            {ResourceType::BOLTS,         EquipmentCategory::Ammo,     EquipmentMaterial::Iron},
            {ResourceType::CARTRIDGE,     EquipmentCategory::Ammo,     EquipmentMaterial::Blackpowder},
            // Shields
            {ResourceType::WOODEN_SHIELD, EquipmentCategory::Shield,   EquipmentMaterial::Wood},
            {ResourceType::IRON_SHIELD,   EquipmentCategory::Shield,   EquipmentMaterial::Iron},
            // Armor
            {ResourceType::LEATHER_ARMOR, EquipmentCategory::Armor,    EquipmentMaterial::Leather},
            {ResourceType::IRON_ARMOR,    EquipmentCategory::Armor,    EquipmentMaterial::Iron},
        };

        std::vector<EquipmentProfile> profiles;
        profiles.reserve(std::size(rows));
        for (const auto& row : rows)
            profiles.push_back({row.resource, row.category, row.material, MaterialQuality(row.material)});

        std::sort(profiles.begin(), profiles.end(), [](const EquipmentProfile& a, const EquipmentProfile& b)
        {
            if (a.category != b.category)
                return a.category < b.category;
            return a.quality < b.quality;
        });
        return profiles;
    }
}

const std::vector<EquipmentProfile>& GetEquipmentProfiles()
{
    static const std::vector<EquipmentProfile> profiles = BuildProfiles();
    return profiles;
}

const EquipmentProfile* FindEquipmentProfile(ResourceType type)
{
    for (const auto& profile : GetEquipmentProfiles())
        if (profile.resource == type)
            return &profile;
    return nullptr;
}

bool IsEquipment(ResourceType type)
{
    return FindEquipmentProfile(type) != nullptr;
}

float GetMaterialQuality(EquipmentMaterial material)
{
    return MaterialQuality(material);
}

EquipmentSlot SlotForCategory(EquipmentCategory category)
{
    switch (category)
    {
        case EquipmentCategory::Sword:
        case EquipmentCategory::Spear:
            return EquipmentSlot::Melee;
        case EquipmentCategory::Bow:
        case EquipmentCategory::Crossbow:
        case EquipmentCategory::Firearm:
            return EquipmentSlot::Ranged;
        case EquipmentCategory::Ammo:
            return EquipmentSlot::Ammo;
        case EquipmentCategory::Shield:
        case EquipmentCategory::Armor:
            return EquipmentSlot::Armor;
        case EquipmentCategory::Siege:
            return EquipmentSlot::Siege;
        default:
            return EquipmentSlot::None;
    }
}

const char* EquipmentCategoryLabel(EquipmentCategory category)
{
    switch (category)
    {
        case EquipmentCategory::Sword:    return "Sword";
        case EquipmentCategory::Spear:    return "Spear";
        case EquipmentCategory::Bow:      return "Bow";
        case EquipmentCategory::Crossbow: return "Crossbow";
        case EquipmentCategory::Firearm:  return "Firearm";
        case EquipmentCategory::Shield:   return "Shield";
        case EquipmentCategory::Armor:    return "Armor";
        case EquipmentCategory::Ammo:     return "Ammo";
        case EquipmentCategory::Siege:    return "Siege";
        default:                          return "None";
    }
}

const char* EquipmentMaterialLabel(EquipmentMaterial material)
{
    switch (material)
    {
        case EquipmentMaterial::Wood:    return "Wood";
        case EquipmentMaterial::Stone:   return "Stone";
        case EquipmentMaterial::Leather: return "Leather";
        case EquipmentMaterial::Copper:  return "Copper";
        case EquipmentMaterial::Bronze:  return "Bronze";
        case EquipmentMaterial::Iron:    return "Iron";
        case EquipmentMaterial::Steel:   return "Steel";
        case EquipmentMaterial::Blackpowder: return "Blackpowder";
        case EquipmentMaterial::Enchanted:   return "Enchanted";
        default:                             return "None";
    }
}

// ─── SupplyPackage ────────────────────────────────────────────────────────────

void SupplyPackage::Add(ResourceType type, int amount)
{
    if (amount <= 0 || type == ResourceType::Null)
        return;

    for (auto& item : items)
    {
        if (item.type == type)
        {
            item.amount += amount;
            return;
        }
    }
    items.push_back({type, amount});
}

int SupplyPackage::CountCategory(EquipmentCategory category) const
{
    int total = 0;
    for (const auto& item : items)
    {
        const EquipmentProfile* profile = FindEquipmentProfile(item.type);
        if (profile != nullptr && profile->category == category)
            total += item.amount;
    }
    return total;
}

ResourceType SupplyPackage::BestOfCategory(EquipmentCategory category) const
{
    ResourceType best = ResourceType::Null;
    float bestQuality = -1.0f;
    for (const auto& item : items)
    {
        if (item.amount <= 0)
            continue;
        const EquipmentProfile* profile = FindEquipmentProfile(item.type);
        if (profile == nullptr || profile->category != category)
            continue;
        if (profile->quality > bestQuality)
        {
            bestQuality = profile->quality;
            best = item.type;
        }
    }
    return best;
}

int SupplyPackage::TotalItems() const
{
    int total = 0;
    for (const auto& item : items)
        total += item.amount;
    return total;
}

namespace
{
    bool IsPrimaryWeapon(EquipmentCategory category)
    {
        return category == EquipmentCategory::Sword || category == EquipmentCategory::Spear ||
               category == EquipmentCategory::Bow   || category == EquipmentCategory::Crossbow ||
               category == EquipmentCategory::Firearm;
    }

    // Highest-quality available resource of a category, or Null.
    ResourceType BestAvailableOfCategory(const std::map<ResourceType, int>& available,
                                         EquipmentCategory category)
    {
        ResourceType best = ResourceType::Null;
        float bestQuality = -1.0f;
        for (const auto& [type, amount] : available)
        {
            if (amount <= 0)
                continue;
            const EquipmentProfile* profile = FindEquipmentProfile(type);
            if (profile == nullptr || profile->category != category)
                continue;
            if (profile->quality > bestQuality)
            {
                bestQuality = profile->quality;
                best = type;
            }
        }
        return best;
    }
}

// ─── SupplyDemand ─────────────────────────────────────────────────────────────

int SupplyDemand::WeaponTotal() const
{
    int total = 0;
    for (const auto& [cat, amount] : weapons)
        total += amount;
    return total;
}

void SupplyDemand::AddWeapon(EquipmentCategory category, int amount)
{
    if (amount <= 0)
        return;
    weapons[category] += amount;
}

void SupplyDemand::Merge(const SupplyDemand& other)
{
    food += other.food;
    materiel += other.materiel;
    for (const auto& [cat, amount] : other.weapons)
        weapons[cat] += amount;
}

bool SupplyDemand::Wants(SupplyCategory category) const
{
    switch (category)
    {
        case SupplyCategory::Food:     return food > 0;
        case SupplyCategory::Materiel: return materiel > 0;
        case SupplyCategory::Weapons:  return !weapons.empty();
    }
    return false;
}

// Packs the best available item for each DEMANDED weapon/ammo category, sized to
// the smaller of demand and `cap`. Requires at least one primary weapon to land.
static bool PlanDemandWeapons(const std::map<ResourceType, int>& available,
                              const SupplyDemand& demand, int cap, SupplyPackage& out)
{
    SupplyPackage planned;
    planned.category = SupplyCategory::Weapons;
    planned.soldierCapacity = cap;

    bool hasWeapon = false;
    for (const auto& [category, wanted] : demand.weapons)
    {
        if (wanted <= 0)
            continue;
        ResourceType chosen = BestAvailableOfCategory(available, category);
        if (chosen == ResourceType::Null)
            continue;

        auto countIt = available.find(chosen);
        int count = countIt != available.end() ? countIt->second : 0;
        int take = std::min({count, wanted, cap});
        if (take <= 0)
            continue;

        planned.Add(chosen, take);
        if (IsPrimaryWeapon(category))
            hasWeapon = true;
    }

    if (!hasWeapon)
        return false;

    out = std::move(planned);
    return true;
}

bool PlanDemandPackage(const std::map<ResourceType, int>& available,
                       const SupplyDemand& demand, SupplyCategory category,
                       int cap, SupplyPackage& out)
{
    if (cap <= 0 || !demand.Wants(category))
        return false;

    switch (category)
    {
        case SupplyCategory::Food:
        {
            auto it = available.find(ResourceType::FOOD_PROVISIONS);
            int have = it != available.end() ? it->second : 0;
            int take = std::min({have, demand.food, cap});
            if (take <= 0)
                return false;

            SupplyPackage planned;
            planned.category = SupplyCategory::Food;
            planned.rations = take;
            planned.soldierCapacity = cap;
            out = std::move(planned);
            return true;
        }
        case SupplyCategory::Materiel:
        {
            SupplyPackage planned;
            planned.category = SupplyCategory::Materiel;
            planned.soldierCapacity = cap;
            int remaining = std::min(demand.materiel, cap);
            for (ResourceType type : {ResourceType::WOOD, ResourceType::PLANKS, ResourceType::STONE, ResourceType::TOOLS})
            {
                if (remaining <= 0)
                    break;
                auto it = available.find(type);
                int have = it != available.end() ? it->second : 0;
                int take = std::min(have, remaining);
                if (take > 0)
                {
                    planned.Add(type, take);
                    remaining -= take;
                }
            }
            if (planned.items.empty())
                return false;
            out = std::move(planned);
            return true;
        }
        case SupplyCategory::Weapons:
        default:
            return PlanDemandWeapons(available, demand, cap, out);
    }
}

SupplyCategory CategoryOfResource(ResourceType type)
{
    if (type == ResourceType::FOOD_PROVISIONS)
        return SupplyCategory::Food;
    if (type == ResourceType::WOOD || type == ResourceType::PLANKS ||
        type == ResourceType::STONE || type == ResourceType::TOOLS)
        return SupplyCategory::Materiel;
    return SupplyCategory::Weapons;
}

bool PlanSupplyPackage(const std::map<ResourceType, int>& available,
                       const std::vector<EquipmentCategory>& categories,
                       int soldiersPerPackage, int rationsPerPackage,
                       SupplyPackage& out)
{
    auto rationsIt = available.find(ResourceType::FOOD_PROVISIONS);
    int rationsAvailable = rationsIt != available.end() ? rationsIt->second : 0;
    if (rationsAvailable < rationsPerPackage)
        return false;

    SupplyPackage planned;
    planned.soldierCapacity = soldiersPerPackage;
    planned.rations = rationsPerPackage;

    bool hasWeapon = false;
    for (EquipmentCategory category : categories)
    {
        ResourceType chosen = BestAvailableOfCategory(available, category);
        if (chosen == ResourceType::Null)
            continue;

        auto countIt = available.find(chosen);
        int count = countIt != available.end() ? countIt->second : 0;
        int take = std::min(count, soldiersPerPackage);
        if (take <= 0)
            continue;

        planned.Add(chosen, take);
        if (IsPrimaryWeapon(category))
            hasWeapon = true;
    }

    if (!hasWeapon)
        return false;

    out = std::move(planned);
    return true;
}

float SupplyPackage::AverageQuality() const
{
    float weighted = 0.0f;
    int count = 0;
    for (const auto& item : items)
    {
        const EquipmentProfile* profile = FindEquipmentProfile(item.type);
        if (profile == nullptr)
            continue;
        weighted += profile->quality * static_cast<float>(item.amount);
        count += item.amount;
    }
    return count > 0 ? weighted / static_cast<float>(count) : 0.0f;
}

bool PlanCategoryPackage(const std::map<ResourceType, int>& available,
                         SupplyCategory category, int soldiers, SupplyPackage& out)
{
    if (soldiers <= 0)
        return false;

    switch (category)
    {
        case SupplyCategory::Food:
        {
            auto it = available.find(ResourceType::FOOD_PROVISIONS);
            int have = it != available.end() ? it->second : 0;
            int take = std::min(have, soldiers);
            if (take <= 0)
                return false;

            SupplyPackage planned;
            planned.category = SupplyCategory::Food;
            planned.rations = take;
            planned.soldierCapacity = soldiers;
            out = std::move(planned);
            return true;
        }
        case SupplyCategory::Materiel:
        {
            SupplyPackage planned;
            planned.category = SupplyCategory::Materiel;
            planned.soldierCapacity = soldiers;
            int remaining = soldiers;
            for (ResourceType type : {ResourceType::WOOD, ResourceType::PLANKS, ResourceType::STONE, ResourceType::TOOLS})
            {
                if (remaining <= 0)
                    break;
                auto it = available.find(type);
                int have = it != available.end() ? it->second : 0;
                int take = std::min(have, remaining);
                if (take > 0)
                {
                    planned.Add(type, take);
                    remaining -= take;
                }
            }
            if (planned.items.empty())
                return false;
            out = std::move(planned);
            return true;
        }
        case SupplyCategory::Weapons:
        default:
        {
            static const std::vector<EquipmentCategory> kWeaponCategories{
                EquipmentCategory::Sword, EquipmentCategory::Spear, EquipmentCategory::Bow,
                EquipmentCategory::Crossbow, EquipmentCategory::Firearm, EquipmentCategory::Shield,
                EquipmentCategory::Armor, EquipmentCategory::Ammo};

            SupplyPackage planned;
            // Rations are their own package category now, so pass 0 — the ration
            // check inside PlanSupplyPackage is trivially satisfied.
            if (!PlanSupplyPackage(available, kWeaponCategories, soldiers, 0, planned))
                return false;
            planned.category = SupplyCategory::Weapons;
            planned.rations = 0;
            out = std::move(planned);
            return true;
        }
    }
}
