#include "../inc/Equipment.h"
#include "../inc/SupplyPackage.h"

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
            default:                         return 0.0f;
        }
    }

    // The equipment catalogue. One row per equipment ResourceType. Quality is
    // derived from the material tier so the table stays consistent and editable.
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
            // Ammo
            {ResourceType::ARROWS,        EquipmentCategory::Ammo,     EquipmentMaterial::Wood},
            {ResourceType::BOLTS,         EquipmentCategory::Ammo,     EquipmentMaterial::Iron},
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

const char* EquipmentCategoryLabel(EquipmentCategory category)
{
    switch (category)
    {
        case EquipmentCategory::Sword:    return "Sword";
        case EquipmentCategory::Spear:    return "Spear";
        case EquipmentCategory::Bow:      return "Bow";
        case EquipmentCategory::Crossbow: return "Crossbow";
        case EquipmentCategory::Shield:   return "Shield";
        case EquipmentCategory::Armor:    return "Armor";
        case EquipmentCategory::Ammo:     return "Ammo";
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
        default:                         return "None";
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
               category == EquipmentCategory::Bow   || category == EquipmentCategory::Crossbow;
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
