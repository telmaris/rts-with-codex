#include "economy/Building.h"
#include "economy/Player.h"
#include "simulation/MapGenerator.h"
#include "warfare/DivisionSector.h"
#include "warfare/MovementPlanner.h"
#include "simulation/SectorGraph.h"
#include "warfare/Equipment.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

// ─── RecruitmentComponent ────────────────────────────────────────────────────

namespace
{
    // Deploys a freshly trained division onto the first free walkable tile around
    // the building footprint, growing the search ring when the inner one is full.
    // Row-major scan order → deterministic for lockstep. Returns false when every
    // nearby tile is taken (the division then stays garrisoned inside as fallback).
    bool DeployRecruitNextToBuilding(Building& self, SoldierDivision& division)
    {
        if (self.owner == nullptr || self.positionId < 0)
            return false;

        TileMap& map = self.owner->tilemap;
        if (map.tilemap.empty())
            return false;

        Vec2i anchor = map.GetCoordsFromId(self.positionId);
        Vec2i footprint = self.GetFootprint();

        for (int radius = 1; radius <= 3; radius++)
        {
            for (int y = anchor.y - radius; y < anchor.y + footprint.y + radius; y++)
            {
                for (int x = anchor.x - radius; x < anchor.x + footprint.x + radius; x++)
                {
                    bool insideFootprint = x >= anchor.x && x < anchor.x + footprint.x &&
                                           y >= anchor.y && y < anchor.y + footprint.y;
                    if (insideFootprint)
                        continue;
                    Vec2i tile{x, y};
                    if (!map.IsInside(tile) || !IsTileWalkableForDivision(map, tile))
                        continue;
                    if (DivisionOnTile(*self.owner, tile, division.id) >= 0)
                        continue;

                    division.occupiedTile = tile;
                    division.sectorCell = SectorCellOf(tile);
                    division.worldPos = {(tile.x + 0.5f) * TILE_SIZE, (tile.y + 0.5f) * TILE_SIZE};
                    division.inTransit = false;
                    return true;
                }
            }
        }
        return false;
    }
}

RecruitmentComponent::Job::Job()
    : type(MilitaryUnitType::Militia)
{}

RecruitmentComponent::Job::Job(MilitaryUnitType t, double r)
    : type(t), remaining(r)
{}

void RecruitmentComponent::Update(Building& self, double dt)
{
    if (queue.empty())
        return;

    auto* garrisonPtr = self.GetComponent<GarrisonComponent>();
    if (garrisonPtr == nullptr)
        return;
    auto& garrison = *garrisonPtr;

    auto& job = queue.front();
    job.remaining = std::max(0.0, job.remaining - dt);
    if (job.remaining > 0.0)
        return;

    if (garrison.GetFreeDivisionSpace(self) <= 0)
        return;

    // The division is owned by the player, homed at this building. AddForce updates
    // the home building's view when it is registered in the tilemap; push to this
    // garrison's view directly too (guarded) so recruitment works for buildings not
    // in the tilemap (some unit tests) and stays correct before the next rebuild.
    if (self.owner != nullptr)
    {
        SoldierDivision* d = self.owner->AddForce(
            CreateMilitaryDivision(job.type, self.id * 10000 + garrison.nextDivisionId++), self.positionId);
        if (d != nullptr && std::find(garrison.divisions.begin(), garrison.divisions.end(), d) == garrison.divisions.end())
            garrison.divisions.push_back(d);
        // Stamp the gear actually purchased (any material of the right category), so
        // the unit fights with the quality it was armed with rather than a fixed type.
        if (d != nullptr)
        {
            if (job.weapon != ResourceType::Null)       d->equipment.weapon = job.weapon;
            if (job.rangedWeapon != ResourceType::Null)  d->equipment.rangedWeapon = job.rangedWeapon;
            if (job.ammo != ResourceType::Null)          d->equipment.ammo = job.ammo;
            if (job.armor != ResourceType::Null)          d->equipment.armor = job.armor;
        }
        // HoI4-style factory: the freshly trained division deploys straight onto
        // a free tile beside the building. The Barracks hands units to the
        // player's field army instead of garrisoning them inside.
        if (d != nullptr)
            DeployRecruitNextToBuilding(self, *d);
    }
    garrison.Recount();
    queue.pop_front();
}

bool RecruitmentComponent::QueueUnit(MilitaryUnitType type, Building& self,
                                      GarrisonComponent& garrison)
{
    if (self.owner == nullptr || self.IsUnderConstruction() ||
        garrison.GetFreeDivisionSpace(self) <= static_cast<int>(queue.size()))
        return false;

    int manpowerCost = self.owner->ModifyBalanceIntForBuilding(
        BalanceStat::RecruitmentManpowerCost,
        GetBaseRecruitmentManpowerCost(type), &self, ResourceType::Null, type, 0);
    if (!self.owner->strategicResources.Consume(StrategicResourceType::Manpower, manpowerCost))
        return false;

    std::vector<ResourceAmountDefinition> materialCosts;
    for (const auto& [res, amount] : GetBaseRecruitmentResourceCosts(type))
        materialCosts.push_back({res, amount});
    auto equipmentCosts = GetBaseRecruitmentEquipmentCosts(type);

    // Atomic affordability check across plain resources + every equipment category
    // BEFORE consuming anything, so a partial payment can never strand manpower.
    bool affordable = self.owner->HasBuildResources(materialCosts);
    for (const auto& [cat, amount] : equipmentCosts)
        affordable = affordable && self.owner->CountEquipmentCategory(cat) >= amount;
    if (!affordable)
    {
        self.owner->strategicResources.Add(StrategicResourceType::Manpower, manpowerCost);
        return false;
    }

    self.owner->TryPayBuildCost(materialCosts);   // guaranteed to succeed by the check

    // Charge each equipment category (any material satisfies it) and remember the
    // representative piece so the trained division carries the quality paid for.
    // Dispatch by body-slot (SlotForCategory), not by hand-checking category
    // values here — that previously dumped Shield/Armor into the weapon slot
    // whenever a unit costed them, silently overwriting the melee weapon.
    ResourceType paidWeapon = ResourceType::Null;
    ResourceType paidRanged = ResourceType::Null;
    ResourceType paidAmmo   = ResourceType::Null;
    ResourceType paidArmor  = ResourceType::Null;
    for (const auto& [cat, amount] : equipmentCosts)
    {
        ResourceType rep = ResourceType::Null;
        self.owner->TryPayEquipmentCategory(cat, amount, &rep);
        switch (SlotForCategory(cat))
        {
            case EquipmentSlot::Ammo:   paidAmmo = rep;   break;
            case EquipmentSlot::Ranged: paidRanged = rep; break;
            case EquipmentSlot::Armor:  paidArmor = rep;  break;
            case EquipmentSlot::Melee:  paidWeapon = rep; break;
            default: break;
        }
    }

    double time = self.owner->debugMode
        ? 1.0
        : self.owner->ModifyBalanceForBuilding(
              BalanceStat::RecruitmentTime,
              GetBaseRecruitmentTime(type), &self, ResourceType::Null, type);
    queue.push_back({type, time});
    queue.back().weapon = paidWeapon;
    queue.back().rangedWeapon = paidRanged;
    queue.back().ammo = paidAmmo;
    queue.back().armor = paidArmor;
    return true;
}

