#ifndef SUPPLY_PACKAGE_H
#define SUPPLY_PACKAGE_H

#include "Equipment.h"
#include "Resource.h"

#include <map>
#include <vector>

// ─── SupplyPackage ────────────────────────────────────────────────────────────
// A bundle of equipment + rations assembled by a SupplyHub and shipped to the
// front. Unlike a plain pooled Resource, a package *tracks exactly what it
// holds*, so when it reaches a division the contents can be distributed across
// individual soldiers — meaning two soldiers in the same division may end up
// with slightly different gear (and therefore slightly different stats).
//
// This is the seed of the equipment system; combat does not consume it yet.

// Which of a division's three supply pools a package replenishes. See
// docs/war_system_phase2_design.md (Phase B) — Food/Materiel/Weapons travel as
// independent package streams rather than one bundle.
enum class SupplyCategory : uint8_t { Food, Materiel, Weapons };

// Classifies a resource type into the supply category it belongs to when
// carried in a package. FOOD_PROVISIONS -> Food; WOOD/PLANKS/TOOLS -> Materiel;
// anything with an equipment profile -> Weapons. Anything else defaults to
// Weapons (never referenced in practice — callers only classify resources they
// already know belong to one of the three streams).
SupplyCategory CategoryOfResource(ResourceType type);

struct SupplyLineItem
{
    ResourceType type{ResourceType::Null};
    int          amount{0};
};

struct SupplyPackage
{
    SupplyCategory category{SupplyCategory::Weapons};
    std::vector<SupplyLineItem> items;   // equipment/materiel carried by the package
    int  rations{0};                     // FOOD_PROVISIONS units bundled in (Food packages)
    int  soldierCapacity{0};             // how many soldiers this package can equip

    bool IsEmpty() const { return items.empty() && rations == 0; }

    // Total count of items belonging to one equipment category.
    int CountCategory(EquipmentCategory category) const;

    // Total count of all equipment items (ammo included).
    int TotalItems() const;

    // Manpower-weighted average quality of the carried equipment (0 when empty).
    float AverageQuality() const;

    // Highest-quality item of one category currently in the package, or Null.
    ResourceType BestOfCategory(EquipmentCategory category) const;

    // Adds equipment, merging with an existing line of the same type.
    void Add(ResourceType type, int amount);
};

// Plans (but does not consume) one package from the gear `available` in the
// network: picks the best item per category, requires at least one primary
// weapon and `rationsPerPackage` rations. Returns false when a package cannot be
// formed. Pure — the assembled item amounts double as the amounts to consume.
bool PlanSupplyPackage(const std::map<ResourceType, int>& available,
                       const std::vector<EquipmentCategory>& categories,
                       int soldiersPerPackage, int rationsPerPackage,
                       SupplyPackage& out);

// Plans one package for a single supply category from the gear/goods `available`
// in the network (does not consume). Food takes up to `soldiers` FOOD_PROVISIONS
// as rations; Materiel takes up to `soldiers` combined WOOD/PLANKS/TOOLS; Weapons
// delegates to PlanSupplyPackage (best-per-category, no rations bundled — food
// travels as its own package now). Returns false when nothing could be planned.
bool PlanCategoryPackage(const std::map<ResourceType, int>& available,
                         SupplyCategory category, int soldiers, SupplyPackage& out);

#endif
