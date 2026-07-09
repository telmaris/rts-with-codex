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

// ─── SupplyPackageComponent ──────────────────────────────────────────────────

namespace
{
    // Equipment categories that draw on a division's weaponSupply pool (primary
    // weapons + their ammo). Shields/Armor are separate and not counted here.
    bool IsWeaponOrAmmoCategory(EquipmentCategory category)
    {
        switch (category)
        {
            case EquipmentCategory::Sword:
            case EquipmentCategory::Spear:
            case EquipmentCategory::Bow:
            case EquipmentCategory::Crossbow:
            case EquipmentCategory::Firearm:
            case EquipmentCategory::Ammo:
                return true;
            default:
                return false;
        }
    }
}

SupplyDemand ComputeMilitaryDemand(Building& target)
{
    SupplyDemand demand;
    auto* garrison = target.GetComponent<GarrisonComponent>();
    if (garrison == nullptr)
        return demand;

    for (const auto* division : garrison->divisions)
    {
        if (division == nullptr)
            continue;

        demand.food     += std::max(0, division->foodSupplyCapacity - division->foodSupply);
        demand.materiel += std::max(0, division->materielSupplyCapacity - division->materielSupply);

        int weaponNeed = std::max(0, division->weaponSupplyCapacity - division->weaponSupply);
        if (weaponNeed <= 0)
            continue;

        // Ask for exactly the weapon/ammo classes this unit uses so the hub packs
        // Bows for archers and Swords for swordsmen — never the wrong gear.
        bool tagged = false;
        for (const auto& [category, cost] : GetBaseRecruitmentEquipmentCosts(division->type))
        {
            if (!IsWeaponOrAmmoCategory(category))
                continue;
            demand.AddWeapon(category, weaponNeed);
            tagged = true;
        }
        // Fallback for classes with no listed equipment cost: melee by default,
        // ranged units ask for a bow so they are not left unarmed.
        if (!tagged)
            demand.AddWeapon(division->IsRanged() ? EquipmentCategory::Bow : EquipmentCategory::Sword,
                             weaponNeed);
    }

    // Building-level food buffer room (deployed subscribers draw from here too).
    if (auto* supply = target.GetComponent<SupplyBufferComponent>())
        demand.food += std::max(0, supply->buffer.bufferSize - static_cast<int>(supply->buffer.buffer.size()));

    return demand;
}

SupplyDemand AggregatePlayerDemand(Building& hub)
{
    SupplyDemand total;
    if (hub.owner == nullptr)
        return total;

    for (auto& tile : hub.owner->tilemap.tilemap)
    {
        Building* b = tile.building.get();
        if (b == nullptr || b == &hub || b->owner != hub.owner)
            continue;
        if (b->positionId != tile.id)          // visit each building once
            continue;
        if (auto* garrison = b->GetComponent<GarrisonComponent>())
        {
            total.Merge(ComputeMilitaryDemand(*b));
        }
        if (auto* pop = b->GetComponent<PopulationComponent>())
        {
            total.food += pop->GetFoodDemand();
        }
    }
    return total;
}

std::map<ResourceType, int> SurveyNetworkSupplies(Building& hub)
{
    std::map<ResourceType, int> available;
    if (hub.owner == nullptr)
        return available;

    for (auto& tile : hub.owner->tilemap.tilemap)
    {
        Building* source = tile.building.get();
        if (source == nullptr || source->owner != hub.owner)
            continue;
        if (source->positionId != tile.id)      // visit each building once
            continue;

        auto* storage = source->GetComponent<StorageComponent>();
        if (storage == nullptr)
            continue;

        for (const auto& [type, buffer] : storage->buffers)
        {
            if (type == ResourceType::FOOD_PROVISIONS || IsEquipment(type) ||
                type == ResourceType::WOOD || type == ResourceType::PLANKS ||
                type == ResourceType::STONE || type == ResourceType::TOOLS)
                available[type] += static_cast<int>(buffer.buffer.size());
        }
    }
    return available;
}

int TakeFromNetwork(Building& hub, ResourceType type, int amount)
{
    if (hub.owner == nullptr || amount <= 0)
        return 0;

    int taken = 0;
    for (auto& tile : hub.owner->tilemap.tilemap)
    {
        if (taken >= amount)
            break;

        Building* source = tile.building.get();
        if (source == nullptr || source->owner != hub.owner)
            continue;
        if (source->positionId != tile.id)
            continue;

        auto* storage = source->GetComponent<StorageComponent>();
        if (storage == nullptr)
            continue;

        auto bufferIt = storage->buffers.find(type);
        if (bufferIt == storage->buffers.end())
            continue;

        while (taken < amount && !bufferIt->second.buffer.empty())
        {
            bufferIt->second.FreeResource();
            hub.owner->economyTelemetry.RecordConsumption(type);
            taken++;
        }
    }
    return taken;
}

namespace
{
    // Total shortfall of one supply category at a military building — divisions'
    // pools plus (for Food) the building's own ration buffer. Drives DeliverPackages'
    // neediest-first routing.
    int CategorySupplyDeficit(SupplyCategory category, Building& target)
    {
        // ETAP 9: Food can be demanded by both military (GarrisonComponent)
        // and civilian (PopulationComponent) buildings.
        if (category == SupplyCategory::Food)
        {
            int deficit = 0;
            if (auto* garrison = target.GetComponent<GarrisonComponent>())
            {
                for (const auto* division : garrison->divisions)
                    deficit += std::max(0, division->foodSupplyCapacity - division->foodSupply);
                if (auto* supply = target.GetComponent<SupplyBufferComponent>())
                    deficit += std::max(0, supply->buffer.bufferSize - static_cast<int>(supply->buffer.buffer.size()));
            }
            if (auto* pop = target.GetComponent<PopulationComponent>())
            {
                deficit += pop->GetFoodDemand();
            }
            return deficit;
        }

        auto* garrison = target.GetComponent<GarrisonComponent>();
        if (garrison == nullptr)
            return 0;

        switch (category)
        {
            case SupplyCategory::Materiel:
            {
                int deficit = 0;
                for (const auto* division : garrison->divisions)
                    deficit += std::max(0, division->materielSupplyCapacity - division->materielSupply);
                return deficit;
            }
            case SupplyCategory::Weapons:
            default:
                return MilitaryWeaponDeficit(target);
        }
    }
}

bool SupplyPackageComponent::AssemblePackage(Building& self)
{
    // Demand-driven: the hub packs only what the front actually asks for. An
    // idle army with full supply produces no packages; an archer-only front pulls
    // Bows + Ammo, never Swords. This is the core of the supply rework — the
    // SupplyBufferComponents report their needs (AggregatePlayerDemand) and the
    // packer matches them instead of guessing "best available".
    SupplyDemand demand = AggregatePlayerDemand(self);
    if (demand.Empty())
        return false;

    bool assembledAny = false;
    for (int c = 0; c < 3; c++)
    {
        auto category = static_cast<SupplyCategory>(c);
        if (!servedCategories[c])              // this hub does not pack this stream
            continue;
        auto& queue = readyPackages[c];
        if (static_cast<int>(queue.size()) >= maxReadyPackages)
            continue;
        if (!demand.Wants(category))           // no outstanding need for it
            continue;

        // Plan against what the network currently holds (no consumption yet),
        // sized to the reported demand.
        std::map<ResourceType, int> available = SurveyNetworkSupplies(self);
        SupplyPackage package;
        if (!PlanDemandPackage(available, demand, category, soldiersPerPackage, package))
            continue;

        // Draw exactly what the plan calls for out of the warehouses.
        if (category == SupplyCategory::Food)
            package.rations = TakeFromNetwork(self, ResourceType::FOOD_PROVISIONS, package.rations);
        else
            for (auto& item : package.items)
                item.amount = TakeFromNetwork(self, item.type, item.amount);

        queue.push_back(std::move(package));
        totalPackagesAssembled++;
        assembledAny = true;
    }
    return assembledAny;
}

void SupplyPackageComponent::Update(Building& self, double dt)
{
    timer += dt;
    if (timer < assembleInterval)
        return;
    timer = 0.0;

    // Assemble up to the ready-queue cap (per category), then ship to needy
    // armies. Both are gated by the interval so the network scans run once per
    // cycle, not every simulation tick.
    while (AssemblePackage(self))
        ;
    DeliverPackages(self);
}

void SupplyPackageComponent::DeliverPackages(Building& self)
{
    // Reap in-flight packages that finished this cycle: delivered ones are done
    // (ApplyPackageToMilitary already ran in Building::ReceptTransport);
    // cancelled ones (owner change, missing building, capacity) are requeued so
    // the goods are not lost. See docs/war_system_phase2_design.md task B5.
    for (auto it = inFlight.begin(); it != inFlight.end();)
    {
        SupplyPackageTransportable* pkg = it->get();
        if (pkg->delivered)
        {
            totalPackagesDelivered++;
            it = inFlight.erase(it);
        }
        else if (pkg->cancelled)
        {
            readyPackages[static_cast<size_t>(pkg->payload.category)].push_back(std::move(pkg->payload));
            it = inFlight.erase(it);
        }
        else
            ++it;
    }

    if (self.owner == nullptr || self.owner->roadNetwork == nullptr)
        return;

    for (int c = 0; c < 3; c++)
    {
        auto category = static_cast<SupplyCategory>(c);
        auto& queue = readyPackages[c];
        if (queue.empty())
            continue;

        // Gather every friendly building that is short on this category,
        // worst-off first (deterministic tie-break by positionId).
        // ETAP 9: Food can go to military (GarrisonComponent) or civilian (PopulationComponent).
        std::vector<Building*> targets;
        for (auto& tile : self.owner->tilemap.tilemap)
        {
            Building* target = tile.building.get();
            if (target == nullptr || target == &self)
                continue;
            if (target->positionId != tile.id)           // visit each building once
                continue;
            if (target->owner != self.owner)
                continue;
            bool isEligible = false;
            if (category == SupplyCategory::Food)
                isEligible = target->HasComponent<GarrisonComponent>() || target->HasComponent<PopulationComponent>();
            else
                isEligible = target->HasComponent<GarrisonComponent>();
            if (!isEligible)
                continue;
            if (CategorySupplyDeficit(category, *target) > 0)
                targets.push_back(target);
        }
        std::sort(targets.begin(), targets.end(), [category](Building* a, Building* b)
        {
            int da = CategorySupplyDeficit(category, *a);
            int db = CategorySupplyDeficit(category, *b);
            if (da != db) return da > db;
            return a->positionId < b->positionId;
        });

        for (Building* target : targets)
        {
            if (queue.empty())
                break;

            auto path = self.owner->roadNetwork->CalculatePath(&self, target);
            if (path.empty())
                continue;   // no route yet — leave the package queued, retry next cycle

            auto carrier = std::make_unique<SupplyPackageTransportable>();
            carrier->payload = std::move(queue.front());
            queue.pop_front();
            SupplyPackageTransportable* raw = carrier.get();
            inFlight.push_back(std::move(carrier));
            if (!self.owner->roadNetwork->BeginTransport(&self, target, raw))
            {
                // Reservation failed (road/destination full) — return the goods
                // to the ready queue and drop the failed carrier.
                readyPackages[static_cast<size_t>(raw->payload.category)].push_back(std::move(raw->payload));
                inFlight.pop_back();
            }
        }
    }
}

// ─── package distribution helpers ─────────────────────────────────────────────

bool MilitaryNeedsSupply(Building& target)
{
    auto* garrison = target.GetComponent<GarrisonComponent>();
    if (garrison == nullptr)
        return false;

    // BUG 3a: a building needs supply while its weapon/materiel stockpile has
    // room (deployed divisions draw from these) or its food buffer isn't full.
    auto* supply = target.GetComponent<SupplyBufferComponent>();
    if (supply != nullptr)
    {
        if (static_cast<int>(supply->buffer.buffer.size()) < supply->buffer.bufferSize)
            return true;
        if (supply->weaponStock < SupplyBufferComponent::kStockCap ||
            supply->materielStock < SupplyBufferComponent::kStockCap)
            return true;
    }

    for (const auto& division : garrison->divisions)
    {
        if (division->weaponSupply < division->weaponSupplyCapacity ||
            division->foodSupply < division->foodSupplyCapacity ||
            division->materielSupply < division->materielSupplyCapacity)
            return true;
    }

    return false;
}

int MilitaryWeaponDeficit(Building& target)
{
    auto* garrison = target.GetComponent<GarrisonComponent>();
    if (garrison == nullptr)
        return 0;

    int deficit = 0;
    for (const auto& division : garrison->divisions)
        deficit += std::max(0, division->weaponSupplyCapacity - division->weaponSupply);
    return deficit;
}

namespace
{
    // Food package: rations first top up the building's own buffer, then whatever
    // is left tops up divisions (neediest first).
    bool ApplyFoodPackage(SupplyPackage& package, Building& target, GarrisonComponent& garrison)
    {
        bool used = false;

        if (auto* supply = target.GetComponent<SupplyBufferComponent>(); supply != nullptr)
        {
            int capacity = supply->buffer.bufferSize;
            while (package.rations > 0 && static_cast<int>(supply->buffer.buffer.size()) < capacity)
            {
                supply->buffer.GenerateResource(ResourceType::FOOD_PROVISIONS);
                package.rations--;
                used = true;
            }
            supply->stored = static_cast<int>(supply->buffer.buffer.size());
        }

        std::vector<SoldierDivision*> order(garrison.divisions.begin(), garrison.divisions.end());
        std::sort(order.begin(), order.end(), [](const SoldierDivision* a, const SoldierDivision* b)
        {
            float ra = a->foodSupplyCapacity > 0 ? static_cast<float>(a->foodSupply) / a->foodSupplyCapacity : 1.0f;
            float rb = b->foodSupplyCapacity > 0 ? static_cast<float>(b->foodSupply) / b->foodSupplyCapacity : 1.0f;
            return ra < rb;
        });
        for (SoldierDivision* division : order)
        {
            if (package.rations <= 0)
                break;
            int need = division->foodSupplyCapacity - division->foodSupply;
            if (need <= 0)
                continue;
            int give = std::min(need, package.rations);
            division->foodSupply += give;
            package.rations -= give;
            used = true;
        }
        return used;
    }

    // Materiel package: WOOD/PLANKS/TOOLS distributed to divisions' materiel pool,
    // neediest first (fuels cohesion regen — Phase C).
    bool ApplyMaterielPackage(SupplyPackage& package, GarrisonComponent& garrison)
    {
        int pool = package.TotalItems();
        if (pool <= 0)
            return false;

        bool used = false;
        std::vector<SoldierDivision*> order(garrison.divisions.begin(), garrison.divisions.end());
        std::sort(order.begin(), order.end(), [](const SoldierDivision* a, const SoldierDivision* b)
        {
            float ra = a->materielSupplyCapacity > 0 ? static_cast<float>(a->materielSupply) / a->materielSupplyCapacity : 1.0f;
            float rb = b->materielSupplyCapacity > 0 ? static_cast<float>(b->materielSupply) / b->materielSupplyCapacity : 1.0f;
            return ra < rb;
        });
        for (SoldierDivision* division : order)
        {
            if (pool <= 0)
                break;
            int need = division->materielSupplyCapacity - division->materielSupply;
            if (need <= 0)
                continue;
            int give = std::min(need, pool);
            division->materielSupply += give;
            pool -= give;
            used = true;
        }

        // Drain whatever was actually handed out from the package's item lines.
        int consumed = package.TotalItems() - pool;
        for (auto& item : package.items)
        {
            if (consumed <= 0)
                break;
            int take = std::min(item.amount, consumed);
            item.amount -= take;
            consumed -= take;
        }
        return used;
    }

    // Weapons package: equips/resupplies divisions from the shared weapon pool
    // (neediest first), upgrading gear to whatever the package carries.
    bool ApplyWeaponsPackage(SupplyPackage& package, GarrisonComponent& garrison)
    {
        bool used = false;

        // Pick the best gear the package carries for each slot.
        ResourceType bestMelee = package.BestOfCategory(EquipmentCategory::Sword);
        if (bestMelee == ResourceType::Null)
            bestMelee = package.BestOfCategory(EquipmentCategory::Spear);
        // Best ranged weapon across bow / crossbow / firearm, by quality (firearms win).
        ResourceType bestRanged = ResourceType::Null;
        float bestRangedQuality = -1.0f;
        for (EquipmentCategory rangedCategory : {EquipmentCategory::Bow, EquipmentCategory::Crossbow, EquipmentCategory::Firearm})
        {
            ResourceType candidate = package.BestOfCategory(rangedCategory);
            const EquipmentProfile* profile = candidate != ResourceType::Null ? FindEquipmentProfile(candidate) : nullptr;
            if (profile != nullptr && profile->quality > bestRangedQuality)
            {
                bestRangedQuality = profile->quality;
                bestRanged = candidate;
            }
        }
        ResourceType bestArmor = package.BestOfCategory(EquipmentCategory::Armor);
        ResourceType bestAmmo  = package.BestOfCategory(EquipmentCategory::Ammo);

        // The package's weapons become a shared "weapon supply" pool (numbers) that
        // the building hands out to its divisions.
        int weaponPool = 0;
        for (const auto& item : package.items)
        {
            const EquipmentProfile* profile = FindEquipmentProfile(item.type);
            if (profile == nullptr)
                continue;
            if (profile->category == EquipmentCategory::Sword || profile->category == EquipmentCategory::Spear ||
                profile->category == EquipmentCategory::Bow   || profile->category == EquipmentCategory::Crossbow ||
                profile->category == EquipmentCategory::Firearm)
                weaponPool += item.amount;
        }

        // Distribute to the neediest divisions first (lowest weapon-supply ratio).
        std::vector<SoldierDivision*> order(garrison.divisions.begin(), garrison.divisions.end());
        std::sort(order.begin(), order.end(), [](const SoldierDivision* a, const SoldierDivision* b)
        {
            float ra = a->weaponSupplyCapacity > 0 ? static_cast<float>(a->weaponSupply) / a->weaponSupplyCapacity : 1.0f;
            float rb = b->weaponSupplyCapacity > 0 ? static_cast<float>(b->weaponSupply) / b->weaponSupplyCapacity : 1.0f;
            return ra < rb;
        });

        for (SoldierDivision* division : order)
        {
            const bool ranged = division->type == MilitaryUnitType::Archer;

            int weaponNeed = division->weaponSupplyCapacity - division->weaponSupply;
            if (weaponNeed <= 0 || weaponPool <= 0)
                continue;

            int give = std::min(weaponNeed, weaponPool);
            division->weaponSupply += give;
            weaponPool -= give;
            used = true;

            // Re-equip with the freshest gear in the package as it is resupplied.
            if (ranged && bestRanged != ResourceType::Null)
            {
                division->equipment.rangedWeapon = bestRanged;
                if (bestAmmo != ResourceType::Null)
                    division->equipment.ammo = bestAmmo;
            }
            else if (bestMelee != ResourceType::Null)
            {
                division->equipment.weapon = bestMelee;
            }
            if (bestArmor != ResourceType::Null)
                division->equipment.armor = bestArmor;
        }

        return used;
    }

    // Manpower package: reinforcements distributed to divisions, neediest first
    // (lowest strength relative to baseline/role). Manpower is numeric (one unit
    // adds one strength point directly).
    bool ApplyManpowerPackage(SupplyPackage& package, GarrisonComponent& garrison)
    {
        if (package.manpower <= 0)
            return false;

        bool used = false;
        std::vector<SoldierDivision*> order(garrison.divisions.begin(), garrison.divisions.end());
        std::sort(order.begin(), order.end(), [](const SoldierDivision* a, const SoldierDivision* b)
        {
            return a->strength < b->strength;
        });
        for (SoldierDivision* division : order)
        {
            if (package.manpower <= 0)
                break;
            int give = std::min(package.manpower, 10);  // cap per-division reinforcement to avoid debalance
            division->strength += give;
            package.manpower -= give;
            used = true;
        }
        return used;
    }
}

bool ApplyPackageToMilitary(SupplyPackage& package, Building& target)
{
    auto* garrison = target.GetComponent<GarrisonComponent>();
    if (garrison == nullptr)
        return false;

    // Log what arrived into the building's demand registry (the "co mu przyszło"
    // side). Recorded up-front from the package manifest, before distribution.
    if (auto* registry = target.GetComponent<SupplyBufferComponent>())
    {
        switch (package.category)
        {
            case SupplyCategory::Food:     registry->receivedFood     += package.rations;      break;
            case SupplyCategory::Materiel: registry->receivedMateriel += package.TotalItems(); break;
            case SupplyCategory::Weapons:  registry->receivedWeapons  += package.TotalItems(); break;
            case SupplyCategory::Manpower: registry->receivedManpower += package.manpower;    break;
        }
    }

    // BUG 3a/3b: packages fill the building-level stockpile first; garrisoned
    // divisions (occupiedTile.x < 0, at home) are then topped up directly from
    // the stockpile in the same call so they never starve at base. Deployed
    // divisions (occupiedTile.x >= 0) are resupplied later by
    // ResupplyDeployedDivisions (GameWorld.Render.cpp), which pulls from the
    // nearest building's stockpile each tick.
    auto* depot = target.GetComponent<SupplyBufferComponent>();

    switch (package.category)
    {
        case SupplyCategory::Food:
        {
            // Food: fill rations buffer (existing ResourceBuffer), then
            // distribute to garrisoned divisions — unchanged from original.
            return ApplyFoodPackage(package, target, *garrison);
        }
        case SupplyCategory::Materiel:
        {
            if (depot != nullptr)
            {
                // Step 1: pour materiel points into the building stockpile.
                int pool = package.TotalItems();
                int room = SupplyBufferComponent::kStockCap - depot->materielStock;
                int pour = std::min(pool, room);
                if (pour > 0)
                {
                    depot->materielStock += pour;
                    int remaining = pour;
                    for (auto& item : package.items)
                    {
                        if (remaining <= 0) break;
                        int take = std::min(item.amount, remaining);
                        item.amount -= take;
                        remaining -= take;
                    }
                }

                // Step 2: distribute from the stockpile to garrisoned (home)
                // divisions immediately so they don't starve between ticks.
                bool used = pour > 0;
                if (depot->materielStock > 0 && !garrison->divisions.empty())
                {
                    std::vector<SoldierDivision*> order(garrison->divisions.begin(), garrison->divisions.end());
                    std::sort(order.begin(), order.end(), [](const SoldierDivision* a, const SoldierDivision* b)
                    {
                        float ra = a->materielSupplyCapacity > 0
                            ? static_cast<float>(a->materielSupply) / a->materielSupplyCapacity : 1.0f;
                        float rb = b->materielSupplyCapacity > 0
                            ? static_cast<float>(b->materielSupply) / b->materielSupplyCapacity : 1.0f;
                        return ra < rb;
                    });
                    for (SoldierDivision* div : order)
                    {
                        if (div->occupiedTile.x >= 0) continue;   // skip deployed
                        if (depot->materielStock <= 0) break;
                        int need = div->materielSupplyCapacity - div->materielSupply;
                        if (need <= 0) continue;
                        int give = std::min(need, depot->materielStock);
                        div->materielSupply  += give;
                        depot->materielStock -= give;
                        used = true;
                    }
                }
                // Distribute any remaining package items (if stockpile was full) directly.
                if (package.TotalItems() > 0)
                    used |= ApplyMaterielPackage(package, *garrison);
                return used;
            }
            return ApplyMaterielPackage(package, *garrison);
        }
        case SupplyCategory::Manpower:
        {
            return ApplyManpowerPackage(package, *garrison);
        }
        case SupplyCategory::Weapons:
        default:
        {
            if (depot != nullptr)
            {
                // Step 1a: capture best gear from the package BEFORE draining
                // items (gear upgrade uses item quality, not stockpile counts).
                ResourceType bestMelee = package.BestOfCategory(EquipmentCategory::Sword);
                if (bestMelee == ResourceType::Null)
                    bestMelee = package.BestOfCategory(EquipmentCategory::Spear);
                ResourceType bestRanged = ResourceType::Null;
                float bestRangedQ = -1.0f;
                for (EquipmentCategory cat : {EquipmentCategory::Bow, EquipmentCategory::Crossbow, EquipmentCategory::Firearm})
                {
                    ResourceType cand = package.BestOfCategory(cat);
                    const EquipmentProfile* prof = cand != ResourceType::Null ? FindEquipmentProfile(cand) : nullptr;
                    if (prof != nullptr && prof->quality > bestRangedQ)
                    { bestRangedQ = prof->quality; bestRanged = cand; }
                }
                ResourceType bestArmor = package.BestOfCategory(EquipmentCategory::Armor);
                ResourceType bestAmmo  = package.BestOfCategory(EquipmentCategory::Ammo);

                // Step 1b: pour weapon points into the building stockpile.
                int pool = 0;
                for (const auto& item : package.items)
                    pool += item.amount;
                int room = SupplyBufferComponent::kStockCap - depot->weaponStock;
                int pour = std::min(pool, room);
                if (pour > 0)
                {
                    depot->weaponStock += pour;
                    int remaining = pour;
                    for (auto& item : package.items)
                    {
                        if (remaining <= 0) break;
                        int take = std::min(item.amount, remaining);
                        item.amount -= take;
                        remaining -= take;
                    }
                }

                // Step 2: distribute from the stockpile to garrisoned (home)
                // divisions immediately, neediest first; also upgrade equipment.
                bool used = pour > 0;
                if (depot->weaponStock > 0 && !garrison->divisions.empty())
                {
                    std::vector<SoldierDivision*> order(garrison->divisions.begin(), garrison->divisions.end());
                    std::sort(order.begin(), order.end(), [](const SoldierDivision* a, const SoldierDivision* b)
                    {
                        float ra = a->weaponSupplyCapacity > 0
                            ? static_cast<float>(a->weaponSupply) / a->weaponSupplyCapacity : 1.0f;
                        float rb = b->weaponSupplyCapacity > 0
                            ? static_cast<float>(b->weaponSupply) / b->weaponSupplyCapacity : 1.0f;
                        return ra < rb;
                    });
                    for (SoldierDivision* div : order)
                    {
                        if (div->occupiedTile.x >= 0) continue;   // skip deployed
                        if (depot->weaponStock <= 0) break;
                        int need = div->weaponSupplyCapacity - div->weaponSupply;
                        if (need <= 0) continue;
                        int give = std::min(need, depot->weaponStock);
                        div->weaponSupply   += give;
                        depot->weaponStock  -= give;
                        used = true;

                        // Upgrade equipment slots from whatever the package carried.
                        const bool ranged = div->type == MilitaryUnitType::Archer;
                        if (ranged && bestRanged != ResourceType::Null)
                        {
                            div->equipment.rangedWeapon = bestRanged;
                            if (bestAmmo != ResourceType::Null)
                                div->equipment.ammo = bestAmmo;
                        }
                        else if (!ranged && bestMelee != ResourceType::Null)
                            div->equipment.weapon = bestMelee;
                        if (bestArmor != ResourceType::Null)
                            div->equipment.armor = bestArmor;
                    }
                }
                // Distribute any remaining package items (if stockpile was full) directly.
                if (package.TotalItems() > 0 || package.rations > 0)
                    used |= ApplyWeaponsPackage(package, *garrison);
                return used;
            }
            return ApplyWeaponsPackage(package, *garrison);
        }
    }
}
