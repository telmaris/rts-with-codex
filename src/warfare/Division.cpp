#include "warfare/Division.h"
#include "warfare/Equipment.h"

#include <cmath>

const char* MilitaryUnitLabel(MilitaryUnitType type)
{
    switch (type)
    {
        case MilitaryUnitType::Swordsman: return "Swordsman";
        case MilitaryUnitType::Archer:    return "Archer";
        case MilitaryUnitType::Spearman:  return "Spearman";
        case MilitaryUnitType::Cavalry:   return "Cavalry";
        default:                          return "Militia";
    }
}

double GetBaseRecruitmentTime(MilitaryUnitType type)
{
    switch (type)
    {
        case MilitaryUnitType::Swordsman: return 12.0;
        case MilitaryUnitType::Archer:    return 10.0;
        case MilitaryUnitType::Spearman:  return 11.0;
        case MilitaryUnitType::Cavalry:   return 16.0;
        default:                          return 6.0;
    }
}

int GetBaseRecruitmentManpowerCost(MilitaryUnitType type)
{
    // Recruiting a full division costs its own establishment's worth of manpower.
    return static_cast<int>(std::lround(MakeDefaultUnitStats(type).maxStrength.GetBase()));
}

std::vector<std::pair<ResourceType, int>> GetBaseRecruitmentResourceCosts(MilitaryUnitType type)
{
    // Plain resources only — the weapon itself is charged by category (see
    // GetBaseRecruitmentEquipmentCosts) so any material/quality can arm the unit.
    switch (type)
    {
        case MilitaryUnitType::Swordsman:
            return {{ResourceType::FOOD_PROVISIONS, 20}, {ResourceType::WEAPON_SUPPLY, 20}};
        case MilitaryUnitType::Archer:
            return {{ResourceType::FOOD_PROVISIONS, 12}};
        case MilitaryUnitType::Spearman:
            return {{ResourceType::FOOD_PROVISIONS, 18}, {ResourceType::WEAPON_SUPPLY, 18}};
        case MilitaryUnitType::Cavalry:
            return {{ResourceType::FOOD_PROVISIONS, 8}, {ResourceType::WEAPON_SUPPLY, 8}};
        default:
            return {{ResourceType::FOOD_PROVISIONS, 10}, {ResourceType::WEAPON_SUPPLY, 40}};
    }
}

std::vector<std::pair<EquipmentCategory, int>> GetBaseRecruitmentEquipmentCosts(MilitaryUnitType type)
{
    switch (type)
    {
        case MilitaryUnitType::Swordsman:
            return {{EquipmentCategory::Sword, 40}};
        case MilitaryUnitType::Archer:
            return {{EquipmentCategory::Bow, 40}, {EquipmentCategory::Ammo, 80}};
        case MilitaryUnitType::Spearman:
            return {{EquipmentCategory::Spear, 40}};
        case MilitaryUnitType::Cavalry:
            return {{EquipmentCategory::Sword, 30}};
        default: // Militia arms itself from the generic WEAPON_SUPPLY pool.
            return {};
    }
}

// ─── Concrete unit-class constructors ─────────────────────────────────────────
// Each seeds the class's baseline gear and per-instance pools, then derives the
// combat stat block from BaseStats(). Balance numbers for the stat block itself
// live centrally in MakeDefaultUnitStats().

// Establishment (full equipment loadout) and initial pools are derived from the
// class's own UnitStats (maxStrength/maxCohesion), so scale changes only need to
// happen in MakeDefaultUnitStats(). See docs/war_system_phase2_design.md (Phase A).
MilitiaDivision::MilitiaDivision() : SoldierDivision(MilitaryUnitType::Militia)
{
    stats = BaseStats();
    manpowerScale = static_cast<int>(std::lround(stats.maxStrength.GetBase()));
    maxHealth = manpowerScale; health = manpowerScale;
    endurance = 50; strength = manpowerScale; morale = 55;
    cohesion = stats.maxCohesion.GetBase();
    foodSupplyCapacity = manpowerScale; foodSupply = manpowerScale;
    weaponSupplyCapacity = 40; weaponSupply = 40;
    materielSupplyCapacity = manpowerScale / 2; materielSupply = materielSupplyCapacity;
    speedTilesPerMinute = 14.0;
    equipment.weapon = ResourceType::WEAPON_SUPPLY;
}
UnitStats MilitiaDivision::BaseStats() const { return MakeDefaultUnitStats(MilitaryUnitType::Militia); }

SwordsmanDivision::SwordsmanDivision() : SoldierDivision(MilitaryUnitType::Swordsman)
{
    stats = BaseStats();
    manpowerScale = static_cast<int>(std::lround(stats.maxStrength.GetBase()));
    maxHealth = manpowerScale; health = manpowerScale;
    endurance = 62; strength = manpowerScale; morale = 68;
    cohesion = stats.maxCohesion.GetBase();
    foodSupplyCapacity = manpowerScale; foodSupply = manpowerScale;
    weaponSupplyCapacity = 40; weaponSupply = 40;
    materielSupplyCapacity = manpowerScale / 2; materielSupply = materielSupplyCapacity;
    speedTilesPerMinute = 10.0;
    equipment.weapon = ResourceType::IRON_SWORD;
    equipment.armor  = ResourceType::TOOLS;
}
UnitStats SwordsmanDivision::BaseStats() const { return MakeDefaultUnitStats(MilitaryUnitType::Swordsman); }

ArcherDivision::ArcherDivision() : SoldierDivision(MilitaryUnitType::Archer)
{
    stats = BaseStats();
    manpowerScale = static_cast<int>(std::lround(stats.maxStrength.GetBase()));
    maxHealth = manpowerScale; health = manpowerScale;
    endurance = 58; strength = manpowerScale; morale = 64;
    cohesion = stats.maxCohesion.GetBase();
    foodSupplyCapacity = manpowerScale; foodSupply = manpowerScale;
    weaponSupplyCapacity = 40; weaponSupply = 40;
    materielSupplyCapacity = manpowerScale / 2; materielSupply = materielSupplyCapacity;
    speedTilesPerMinute = 12.0;
    equipment.rangedWeapon = ResourceType::BOW;
    equipment.ammo         = ResourceType::ARROWS;
}
UnitStats ArcherDivision::BaseStats() const { return MakeDefaultUnitStats(MilitaryUnitType::Archer); }

SpearmanDivision::SpearmanDivision() : SoldierDivision(MilitaryUnitType::Spearman)
{
    stats = BaseStats();
    manpowerScale = static_cast<int>(std::lround(stats.maxStrength.GetBase()));
    maxHealth = manpowerScale; health = manpowerScale;
    endurance = 60; strength = manpowerScale; morale = 66;
    cohesion = stats.maxCohesion.GetBase();
    foodSupplyCapacity = manpowerScale; foodSupply = manpowerScale;
    weaponSupplyCapacity = 40; weaponSupply = 40;
    materielSupplyCapacity = manpowerScale / 2; materielSupply = materielSupplyCapacity;
    speedTilesPerMinute = 11.0;
    equipment.weapon = ResourceType::COPPER_SWORD;
    equipment.armor  = ResourceType::TOOLS;
}
UnitStats SpearmanDivision::BaseStats() const { return MakeDefaultUnitStats(MilitaryUnitType::Spearman); }

CavalryDivision::CavalryDivision() : SoldierDivision(MilitaryUnitType::Cavalry)
{
    stats = BaseStats();
    manpowerScale = static_cast<int>(std::lround(stats.maxStrength.GetBase()));
    maxHealth = manpowerScale; health = manpowerScale;
    endurance = 65; strength = manpowerScale; morale = 70;
    cohesion = stats.maxCohesion.GetBase();
    foodSupplyCapacity = manpowerScale; foodSupply = manpowerScale;
    weaponSupplyCapacity = 30; weaponSupply = 30;
    materielSupplyCapacity = manpowerScale / 2; materielSupply = materielSupplyCapacity;
    speedTilesPerMinute = 18.0;
    equipment.weapon = ResourceType::IRON_SWORD;
    equipment.armor  = ResourceType::TOOLS;
}
UnitStats CavalryDivision::BaseStats() const { return MakeDefaultUnitStats(MilitaryUnitType::Cavalry); }

std::unique_ptr<SoldierDivision> CreateMilitaryDivision(MilitaryUnitType type, int id)
{
    std::unique_ptr<SoldierDivision> div;
    switch (type)
    {
        case MilitaryUnitType::Swordsman: div = std::make_unique<SwordsmanDivision>(); break;
        case MilitaryUnitType::Archer:    div = std::make_unique<ArcherDivision>();    break;
        case MilitaryUnitType::Spearman:  div = std::make_unique<SpearmanDivision>();  break;
        case MilitaryUnitType::Cavalry:   div = std::make_unique<CavalryDivision>();   break;
        default:                          div = std::make_unique<MilitiaDivision>();   break;
    }
    div->id = id;
    return div;
}
