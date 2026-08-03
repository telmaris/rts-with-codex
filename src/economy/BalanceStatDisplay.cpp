#include "economy/BalanceStatDisplay.h"

#include <cmath>

const char* BalanceStatLabel(BalanceStat stat)
{
    switch (stat)
    {
        case BalanceStat::BuildTime: return "Build time";
        case BalanceStat::BuildCost: return "Build cost";
        case BalanceStat::ProductionCycleTime: return "Production cycle time";
        case BalanceStat::ProductionOutputAmount: return "Production output";
        case BalanceStat::WorkerCapacity: return "Worker capacity";
        case BalanceStat::TransportTime: return "Transport time";
        case BalanceStat::RoadCapacity: return "Road capacity";
        case BalanceStat::RoadSpeed: return "Road speed";
        case BalanceStat::ManpowerRate: return "Manpower growth";
        case BalanceStat::PopulationCap: return "Population cap";
        case BalanceStat::BuilderAmount: return "Builders";
        // T5 (docs/post_pivot_audit_2026-07-12.md): the rest of the enum used to
        // fall through to "Effect" — every modifier touching a unit/HQ/tower
        // stat showed no real label.
        case BalanceStat::UnitHp: return "Unit HP";
        case BalanceStat::UnitRoadAttack: return "Unit road attack";
        case BalanceStat::UnitSiegeAttack: return "Unit siege attack";
        case BalanceStat::UnitArmor: return "Unit armor";
        case BalanceStat::UnitMoveSpeed: return "Unit move speed";
        case BalanceStat::UnitAttackSpeed: return "Unit attack speed";
        case BalanceStat::UnitRecruitTime: return "Unit recruit time";
        case BalanceStat::UnitRecruitManpowerCost: return "Unit manpower cost";
        case BalanceStat::HqMaxHp: return "HQ max HP";
        case BalanceStat::HqDefense: return "HQ hard defense";
        case BalanceStat::HqThorns: return "HQ thorns damage";
        case BalanceStat::ConquestSpoilsFraction: return "Conquest spoils";
        case BalanceStat::TowerDamage: return "Tower damage";
        case BalanceStat::TowerRange: return "Tower range";
        case BalanceStat::TowerAttackSpeed: return "Tower attack speed";
        case BalanceStat::TowerAmmoEfficiency: return "Tower ammo per shot";
        default: return "Effect";
    }
}

bool LowerValueIsBetter(BalanceStat stat)
{
    switch (stat)
    {
        case BalanceStat::BuildTime:
        case BalanceStat::BuildCost:
        case BalanceStat::ProductionCycleTime:
        case BalanceStat::TransportTime:
        // Staffing a building is a cost, not a reward: the design goal is as few
        // people tied up in buildings as possible, so MORE worker capacity is a
        // nerf and has to render as one.
        case BalanceStat::WorkerCapacity:
        // T5: "less" is the improvement for these three, same reasoning as the
        // build/production timers above — a lower recruit time, lower manpower
        // cost, or fewer arrows burned per shot is the bonus direction.
        case BalanceStat::UnitRecruitTime:
        case BalanceStat::UnitRecruitManpowerCost:
        case BalanceStat::TowerAmmoEfficiency:
            return true;
        default:
            return false;
    }
}

const char* ImprovedRateLabel(BalanceStat stat)
{
    switch (stat)
    {
        case BalanceStat::BuildTime: return "Build speed";
        case BalanceStat::ProductionCycleTime: return "Production speed";
        case BalanceStat::TransportTime: return "Transport speed";
        default: return BalanceStatLabel(stat);
    }
}

bool IsPositiveModifier(const BalanceModifier& modifier)
{
    bool lowerIsBetter = LowerValueIsBetter(modifier.stat);
    if (std::abs(modifier.additive) > 0.001)
        return lowerIsBetter ? modifier.additive < 0.0 : modifier.additive > 0.0;
    if (std::abs(modifier.multiplier - 1.0) > 0.001)
        return lowerIsBetter ? modifier.multiplier < 1.0 : modifier.multiplier > 1.0;
    return true;
}

const char* BalanceBuildingLabel(BuildingType type)
{
    switch (type)
    {
        case BuildingType::Headquarters: return "Headquarters";
        case BuildingType::Village: return "Village";
        case BuildingType::StorageBuilding: return "Storage";
        case BuildingType::Woodcutter: return "Woodcutter";
        case BuildingType::HuntersHut: return "Hunters Hut";
        case BuildingType::LumberMill: return "Lumber Mill";
        case BuildingType::Mine: return "Mine";
        case BuildingType::Foundry: return "Foundry";
        case BuildingType::Well: return "Well";
        case BuildingType::WheatFarm: return "Wheat Farm";
        case BuildingType::Windmill: return "Windmill";
        case BuildingType::Bakery: return "Bakery";
        case BuildingType::Inn: return "Inn";
        case BuildingType::Paperworks: return "Paperworks";
        case BuildingType::Smith: return "Smith";
        case BuildingType::Mint: return "Mint";
        case BuildingType::Glassworks: return "Glassworks";
        case BuildingType::Powderworks: return "Powderworks";
        case BuildingType::University: return "University";
        case BuildingType::Barracks: return "Barracks";
        case BuildingType::DefenseTower: return "Defense Tower";
        case BuildingType::Road: return "Road";
        case BuildingType::Bridge: return "Bridge";
        case BuildingType::AnimalFarm: return "Animal Farm";
        case BuildingType::Butcher: return "Butcher";
        case BuildingType::Tannery: return "Tannery";
        case BuildingType::Tailor: return "Tailor";
        case BuildingType::Armorer: return "Armorer";
        case BuildingType::HorseStable: return "Horse Stable";
        case BuildingType::Kiln: return "Kiln";
        case BuildingType::HouseholdWorkshop: return "Household Workshop";
        case BuildingType::Soapworks: return "Soapworks";
        case BuildingType::Inkworks: return "Inkworks";
        case BuildingType::Scriptorium: return "Scriptorium";
        case BuildingType::Copperworks: return "Copperworks";
        case BuildingType::UrbanWorkshop: return "Urban Workshop";
        case BuildingType::HempFarm: return "Hemp Farm";
        case BuildingType::Ropery: return "Ropery";
        case BuildingType::Weaver: return "Weaver";
        case BuildingType::Bowyer: return "Bowyer";
        case BuildingType::SpearWorkshop: return "Spear Workshop";
        case BuildingType::SiegeWorkshop: return "Siege Workshop";
        default: return "Building";
    }
}
