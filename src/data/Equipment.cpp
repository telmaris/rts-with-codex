#include "data/Equipment.h"

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
            default:                             return 0.0f;
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
            {ResourceType::HEAVY_BOW,     EquipmentCategory::Bow,      EquipmentMaterial::Wood},
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
            {ResourceType::HEAVY_ARMOR,   EquipmentCategory::Armor,    EquipmentMaterial::Iron},
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
