#ifndef EQUIPMENT_H
#define EQUIPMENT_H

#include "Resource.h"

#include <cstdint>
#include <vector>

// ─── Equipment taxonomy ───────────────────────────────────────────────────────
// Equipment resources (swords, shields, …) are ordinary ResourceTypes produced
// by the Smith and stored in warehouses. This taxonomy describes *what role* an
// equipment resource fills and *how good* it is, so the supply system can build
// packages generically — "give every swordsman the best available Sword + Armor
// + Shield + rations" — without hard-coding individual resource ids.
//
// Adding a new piece of equipment = add a ResourceType + one row in the profile
// table (Equipment.cpp). Nothing else needs to know about it.

enum class EquipmentCategory : uint8_t
{
    None = 0,
    Sword,      // primary melee
    Spear,      // primary melee, anti-cavalry flavour later
    Bow,        // primary ranged
    Crossbow,   // primary ranged, higher piercing
    Firearm,    // primary ranged, gunpowder — highest tier (Phase 3)
    Shield,     // defensive
    Armor,      // defensive
    Ammo,       // consumed by ranged weapons
    // BUG 5C — future categories; no gameplay code uses them yet, but they are
    // declared here so new equipment can be added purely by extending the profile
    // table in Equipment.cpp and adding the ResourceType, without touching headers.
    Siege,      // catapults, ballistas, artillery (Phase 4+)
    Count
};

// Material progression: stone → copper → bronze → iron → steel (plus wood/leather
// for shields/armor). Higher tiers yield higher quality.
enum class EquipmentMaterial : uint8_t
{
    None = 0,
    Wood,
    Stone,
    Leather,
    Copper,
    Bronze,
    Iron,
    Steel,
    Blackpowder,   // gunpowder-era firearms, above steel (Phase 3)
    // BUG 5C — future materials; extend MaterialQuality() in Equipment.cpp when used.
    Enchanted,     // magic / enchanted items; quality above steel (Phase 4+)
    Count
};

// Describes one equipment resource: its role, material and relative quality.
struct EquipmentProfile
{
    ResourceType     resource{ResourceType::Null};
    EquipmentCategory category{EquipmentCategory::None};
    EquipmentMaterial material{EquipmentMaterial::None};
    float             quality{1.0f};   // effectiveness multiplier vs a baseline tier-1 item
};

// Which DivisionEquipment body-slot a category is stamped into. Multiple
// categories can share a slot (e.g. Sword/Spear are both "Melee"); this is the
// single place that mapping lives, so recruitment/supply code dispatches by
// tag instead of hand-rolling a chain of `cat == EquipmentCategory::X` checks
// that silently mis-assigns whenever a new category is added.
enum class EquipmentSlot : uint8_t
{
    None = 0,
    Melee,      // Sword, Spear -> DivisionEquipment::weapon
    Ranged,     // Bow, Crossbow, Firearm -> DivisionEquipment::rangedWeapon
    Ammo,       // Ammo -> DivisionEquipment::ammo
    Armor,      // Shield, Armor -> DivisionEquipment::armor
    Siege,      // no slot yet (Phase 4+)
};

// Body-slot a given equipment category is stamped into.
EquipmentSlot SlotForCategory(EquipmentCategory category);

// All known equipment profiles, ordered ascending by quality within a category.
const std::vector<EquipmentProfile>& GetEquipmentProfiles();

// Returns the profile for a resource, or nullptr when it is not equipment.
const EquipmentProfile* FindEquipmentProfile(ResourceType type);

// True when the resource carries an equipment profile.
bool IsEquipment(ResourceType type);

// Relative quality of a material tier (1.0 = baseline tier-1).
float GetMaterialQuality(EquipmentMaterial material);

// Human-readable labels (debug / UI).
const char* EquipmentCategoryLabel(EquipmentCategory category);
const char* EquipmentMaterialLabel(EquipmentMaterial material);

#endif
