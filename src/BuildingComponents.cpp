#include "../inc/Building.h"
#include "../inc/Player.h"
#include "../inc/MapGenerator.h"
#include "../inc/DivisionSector.h"
#include "../inc/MovementPlanner.h"
#include "../inc/SectorGraph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

// ─── helpers (file-local) ────────────────────────────────────────────────────

namespace
{
    int CountIncomingResources(Building* target, ResourceType type)
    {
        if (target == nullptr || target->owner == nullptr)
            return 0;

        int incoming = 0;
        for (auto& tile : target->owner->tilemap.tilemap)
        {
            Building* carrier = tile.building.get();
            if (carrier == nullptr)
                continue;

            for (auto* t : carrier->transportables)
            {
                auto* res = dynamic_cast<Resource*>(t);
                if (res != nullptr && res->targetBuilding == target && res->type == type)
                    incoming++;
            }
        }
        return incoming;
    }

    int GetReceiveCapacity(Building* target, ResourceType type)
    {
        if (target == nullptr || !target->CanReceiveResource(type))
            return 0;

        auto findCap = [type](const std::vector<ResourceBufferView>& views) -> int
        {
            for (const auto& v : views)
                if (v.type == type)
                    return std::max(0, v.capacity - v.amount);
            return -1;
        };

        int free = findCap(target->GetInputBufferViews());
        if (free < 0) free = findCap(target->GetOutputBufferViews());
        if (free < 0) free = target->CanReceiveResource(type) ? 1 : 0;

        return std::max(0, free - CountIncomingResources(target, type));
    }

    void RecountDivisionTypes(GarrisonComponent& g)
    {
        g.militia = g.swordsmen = g.archers = 0;
        for (const auto& div : g.divisions)
        {
            switch (div.type)
            {
                case MilitaryUnitType::Swordsman: g.swordsmen++; break;
                case MilitaryUnitType::Archer:    g.archers++;   break;
                default:                          g.militia++;   break;
            }
        }
        g.garrison = g.GetTotalTroops();
    }

    int DivisionAttackDamage(const MilitaryDivision& d)
    {
        float health = d.HealthRatio();
        float weaponSupply = d.weaponSupplyCapacity > 0
            ? std::clamp(d.weaponSupply / static_cast<float>(d.weaponSupplyCapacity), 0.25f, 1.0f)
            : 1.0f;
        return std::max(1, static_cast<int>(std::round(d.strength * health * weaponSupply)));
    }

    // Returns the world-space center of a tile id.
    Vec2f TileWorldCenter(const TileMap& tilemap, int tileId)
    {
        Vec2i c = tilemap.GetCoordsFromId(tileId);
        return {(c.x + 0.5f) * TILE_SIZE, (c.y + 0.5f) * TILE_SIZE};
    }

    // Returns the world-space center of a building's footprint.
    Vec2f BuildingWorldCenter(const Building& b, const TileMap& tilemap)
    {
        Vec2i c = tilemap.GetCoordsFromId(b.positionId);
        Vec2i fp = b.GetFootprint();
        return {(c.x + fp.x * 0.5f) * TILE_SIZE, (c.y + fp.y * 0.5f) * TILE_SIZE};
    }

    // Starts physical movement of a division from its home building to a target.
    // Uses road pathfinding when available, otherwise direct march with +30% distance.
    void StartDivisionMovement(MilitaryDivision& div, Building& self, Building& target)
    {
        if (self.owner == nullptr || div.speedTilesPerMinute <= 0.0) return;
        TileMap& tilemap = self.owner->tilemap;

        // Try road path
        if (self.owner->roadNetwork != nullptr)
        {
            auto path = self.owner->roadNetwork->CalculatePath(&self, &target);
            if (!path.empty())
            {
                // Base 1 tile/sec on roads; army logistics can modify this.
                double roadSpeedTpm = 60.0; // tiles/minute = 1/sec
                if (self.owner != nullptr)
                {
                    const ArmyGroup* army = self.owner->armyGroups.FindArmyByDivision(div.id);
                    if (army != nullptr)
                        roadSpeedTpm = army->ModifyStat(BalanceStat::ArmyRoadSpeed, roadSpeedTpm);
                }
                div.travelPath = std::move(path);
                div.travelPathStep = 0;
                div.travelElapsed = 0.0;
                div.travelStepTime = 60.0 / std::max(1.0, roadSpeedTpm);
                div.worldPos = TileWorldCenter(tilemap, div.travelPath.front());
                div.travelFromPos = div.worldPos;
                div.travelToPos = BuildingWorldCenter(target, tilemap);
                div.inTransit = true;
                return;
            }
        }

        // Direct march: straight-line distance + 30% off-road penalty
        double marchSpeedTpm = div.speedTilesPerMinute;
        if (self.owner != nullptr)
        {
            const ArmyGroup* army = self.owner->armyGroups.FindArmyByDivision(div.id);
            if (army != nullptr)
                marchSpeedTpm = army->ModifyStat(BalanceStat::ArmyMarchSpeed, marchSpeedTpm);
        }
        Vec2f from = (div.worldPos.x >= 0.0f) ? div.worldPos : BuildingWorldCenter(self, tilemap);
        Vec2f to = BuildingWorldCenter(target, tilemap);
        float dx = to.x - from.x;
        float dy = to.y - from.y;
        float pixelDist = std::sqrt(dx * dx + dy * dy);
        float tileDist = pixelDist / TILE_SIZE * 1.3f;
        double totalSec = (tileDist / std::max(1.0, marchSpeedTpm)) * 60.0;
        div.travelPath.clear();
        div.travelFromPos = from;
        div.travelToPos = to;
        div.travelStepTime = std::max(totalSec, 0.1);
        div.travelElapsed = 0.0;
        div.worldPos = from;
        div.inTransit = true;
    }

    // Advances division movement for one tick. Returns true when the division has just arrived.
    bool UpdateDivisionMovement(MilitaryDivision& div, const TileMap& tilemap, double dt)
    {
        if (!div.inTransit) return false;

        div.travelElapsed += dt;

        if (!div.travelPath.empty())
        {
            // Duration of the hop leaving tile `step`. Per-step durations let a
            // mixed road/off-road path move fast on roads and slower across open
            // ground; falls back to a uniform step time when not provided.
            auto stepDuration = [&](int step) -> double
            {
                if (step >= 0 && step < static_cast<int>(div.travelStepDurations.size()))
                    return std::max(0.0001, div.travelStepDurations[step]);
                return std::max(0.0001, div.travelStepTime);
            };

            // Road movement: hop between tile centers
            while (div.inTransit && div.travelElapsed >= stepDuration(div.travelPathStep))
            {
                div.travelElapsed -= stepDuration(div.travelPathStep);
                div.travelPathStep++;
                if (div.travelPathStep >= static_cast<int>(div.travelPath.size()))
                {
                    div.travelPath.clear();
                    div.travelStepDurations.clear();
                    div.worldPos = div.travelToPos;  // settle at the quadrant centre
                    div.inTransit = false;
                    return true;
                }
            }
            if (!div.inTransit) return true;

            // First hop starts at the division's real position (travelFromPos) for
            // a smooth departure; later hops snap to tile centres.
            Vec2f cur = div.travelPathStep == 0
                ? div.travelFromPos
                : TileWorldCenter(tilemap, div.travelPath[div.travelPathStep]);
            if (div.travelPathStep + 1 < static_cast<int>(div.travelPath.size()))
            {
                Vec2f nxt = TileWorldCenter(tilemap, div.travelPath[div.travelPathStep + 1]);
                float t = static_cast<float>(std::min(1.0, div.travelElapsed / stepDuration(div.travelPathStep)));
                div.worldPos = {cur.x + (nxt.x - cur.x) * t, cur.y + (nxt.y - cur.y) * t};
            }
            else
            {
                float t = static_cast<float>(std::min(1.0, div.travelElapsed / stepDuration(div.travelPathStep)));
                div.worldPos = {
                    cur.x + (div.travelToPos.x - cur.x) * t,
                    cur.y + (div.travelToPos.y - cur.y) * t};
            }
        }
        else
        {
            // Direct march: linear interpolation
            float t = static_cast<float>(std::min(1.0, div.travelElapsed / div.travelStepTime));
            div.worldPos = {
                div.travelFromPos.x + (div.travelToPos.x - div.travelFromPos.x) * t,
                div.travelFromPos.y + (div.travelToPos.y - div.travelFromPos.y) * t};
            if (div.travelElapsed >= div.travelStepTime)
            {
                div.inTransit = false;
                return true;
            }
        }
        return false;
    }
} // namespace

// ─── ProductionComponent ─────────────────────────────────────────────────────

int RoadComponent::GetModifiedMaxCapacity(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(maxCapacity, &self, ResourceType::Null, std::nullopt, 0)
        : maxCapacity.GetBase();
}

double RoadComponent::GetModifiedSpeedModifier(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(speedModifier, &self)
        : speedModifier.GetBase();
}

ProductionComponent::ProductionComponent()
    : terrainType(TileType::GRASS)
{}

void ProductionComponent::Update(Building& self, double dt)
{
    if (self.owner != nullptr)
        self.owner->AutoAssignWorkers(&self);

    auto* workers   = self.GetComponent<WorkerComponent>();
    auto* logistics = self.GetComponent<LogisticsComponent>();
    if (workers != nullptr && workers->GetRatio() > 0.0f && logistics != nullptr)
        logistics->MaintainRequests(self, *this);

    Produce(self, dt);

    if (logistics != nullptr)
        logistics->DispatchOutputs(self, *this);
}

void ProductionComponent::Produce(Building& self, double dt)
{
    if (self.IsProductionBlocked()) return;

    auto* workers   = self.GetComponent<WorkerComponent>();
    auto* logistics = self.GetComponent<LogisticsComponent>();
    double ratio = workers != nullptr ? workers->GetRatio() : 0.0f;
    double workerEff = self.owner != nullptr ? ratio * self.owner->GetFoodProductivity() : ratio;
    if (workerEff <= 0.0) return;

    bool terrainBased = ingredients.empty() && terrainType != TileType::GRASS;
    double effectiveCycleTime = GetModifiedCycleTime(self);

    if (started)
    {
        if (elapsed >= effectiveCycleTime)
        {
            for (auto& [res, amount] : products)
            {
                int modAmount = GetModifiedOutputAmount(self, res, amount);
                for (int i = 0; i < modAmount; i++)
                {
                    if (terrainBased && consumesTerrain && !ConsumeTerrainRichness(self))
                        break;

                    outputBuffers[res].GenerateResource(res);
                    if (self.owner != nullptr)
                        self.owner->economyTelemetry.RecordProduction(res);
                    self.totalProduced++;
                    totalProduced++;
                    Log::Msg(self.tag, "Created a resource: ", rt2s(res));
                    if (logistics != nullptr)
                        logistics->DispatchOutputs(self, *this);
                }
            }
            started = false;
            elapsed = 0.0;
        }
        else
        {
            elapsed         += dt * workerEff;
            self.activeTime += dt * workerEff;
        }
    }
    else
    {
        for (auto& [res, buf] : outputBuffers)
            if (buf.buffer.size() >= static_cast<size_t>(buf.bufferSize))
                return;

        bool canStart = true;
        if (terrainBased && !HasTerrainRichness(self))
            canStart = false;

        for (auto& [res, amount] : ingredients)
            if (inputBuffers[res].buffer.size() < static_cast<size_t>(amount))
                canStart = false;

        if (canStart)
        {
            for (auto& [res, amount] : ingredients)
            {
                for (int i = 0; i < amount; i++)
                {
                    inputBuffers[res].FreeResource();
                    if (self.owner != nullptr)
                        self.owner->economyTelemetry.RecordConsumption(res);
                }
                if (logistics != nullptr)
                    logistics->RequestResource(res, amount, self);
            }
            started = true;
        }
    }
}

float ProductionComponent::GetProgress() const
{
    if (!started || cycleTime.GetBase() <= 0.0)
        return 0.0f;
    double effective = cycleTime.GetBase(); // caller applies modifiers
    return std::clamp(static_cast<float>(elapsed / effective), 0.0f, 1.0f);
}

double ProductionComponent::GetModifiedCycleTime(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(cycleTime, &self)
        : cycleTime.GetBase();
}

double ProductionComponent::GetEffectiveCycleTime(const Building& self) const
{
    double modified = GetModifiedCycleTime(self);
    if (modified <= 0.0) return 0.0;

    const auto* workers = self.GetComponent<WorkerComponent>();
    double eff = workers != nullptr ? workers->GetRatio() : 0.0;
    if (self.owner != nullptr) eff *= self.owner->GetFoodProductivity();
    if (eff <= 0.0) return std::numeric_limits<double>::infinity();

    return modified / eff;
}

int ProductionComponent::GetModifiedOutputAmount(const Building& self, ResourceType type,
                                                  int base) const
{
    return self.owner != nullptr
        ? self.owner->ModifyBalanceIntForBuilding(BalanceStat::ProductionOutputAmount,
                                                   base, &self, type, std::nullopt, 0)
        : base;
}

bool ProductionComponent::HasTerrainRichness(const Building& self) const
{
    if (self.owner == nullptr || self.positionId < 0 || terrainType == TileType::GRASS)
        return false;

    Vec2i anchor = self.owner->tilemap.GetCoordsFromId(self.positionId);
    for (int y = 0; y < self.footprint.y; y++)
    {
        for (int x = 0; x < self.footprint.x; x++)
        {
            Vec2i pos{anchor.x + x, anchor.y + y};
            if (!self.owner->tilemap.IsInside(pos))
                continue;
            const Tile& tile = self.owner->tilemap.tilemap[self.owner->tilemap.GetIdFromCoords(pos)];
            if (tile.tileType == terrainType && tile.resourceRichness > 0)
                return true;
        }
    }
    return false;
}

bool ProductionComponent::ConsumeTerrainRichness(Building& self)
{
    if (self.owner == nullptr || self.positionId < 0 || terrainType == TileType::GRASS)
        return false;

    Vec2i anchor = self.owner->tilemap.GetCoordsFromId(self.positionId);
    for (int y = 0; y < self.footprint.y; y++)
    {
        for (int x = 0; x < self.footprint.x; x++)
        {
            Vec2i pos{anchor.x + x, anchor.y + y};
            if (!self.owner->tilemap.IsInside(pos))
                continue;
            Tile& tile = self.owner->tilemap.tilemap[self.owner->tilemap.GetIdFromCoords(pos)];
            if (tile.tileType != terrainType || tile.resourceRichness <= 0)
                continue;

            tile.resourceRichness--;
            if (tile.resourceRichness <= 0)
            {
                tile.tileType = TileType::GRASS;
                std::mt19937 rng(static_cast<unsigned int>(tile.id + self.owner->tilemap.params.seed));
                tile.terrainTextureId = self.owner->tilemap.PickTerrainTexture(TileType::GRASS, rng);
                self.owner->tilemap.terrainDirty = true;
            }
            return true;
        }
    }
    return false;
}

std::vector<ResourceBufferView> ProductionComponent::GetInputBufferViews(
    const std::map<ResourceType, int>& recipe) const
{
    std::vector<ResourceBufferView> result;
    for (const auto& [res, buf] : inputBuffers)
    {
        int recipeAmount = 0;
        auto it = recipe.find(res);
        if (it != recipe.end())
            recipeAmount = it->second;
        result.push_back({res, static_cast<int>(buf.buffer.size()), buf.bufferSize, recipeAmount});
    }
    return result;
}

std::vector<ResourceBufferView> ProductionComponent::GetOutputBufferViews(
    const Building& self) const
{
    std::vector<ResourceBufferView> result;
    for (const auto& [res, buf] : outputBuffers)
    {
        int recipeAmount = 0;
        auto it = products.find(res);
        if (it != products.end())
            recipeAmount = GetModifiedOutputAmount(self, res, it->second);
        result.push_back({res, static_cast<int>(buf.buffer.size()), buf.bufferSize, recipeAmount});
    }
    return result;
}

// ─── LogisticsComponent ──────────────────────────────────────────────────────

bool LogisticsComponent::HasSupplier(ResourceType type) const
{
    auto it = suppliers.find(type);
    if (it == suppliers.end())
        return false;
    for (auto* s : it->second)
        if (s != nullptr) return true;
    return false;
}

bool LogisticsComponent::HasReceiver(ResourceType type) const
{
    return receivers.contains(type) && receivers.at(type) != nullptr;
}

void LogisticsComponent::SetSupplier(ResourceType type, Building* supplier, Building& self)
{
    if (supplier == nullptr)
        return;

    auto& sups = suppliers[type];
    bool changed = false;

    if (!supplier->IsStorageLike())
    {
        size_t before = sups.size();
        sups.erase(std::remove_if(sups.begin(), sups.end(),
            [](Building* e){ return e != nullptr && e->IsStorageLike(); }), sups.end());
        changed = sups.size() != before;
    }

    if (std::find(sups.begin(), sups.end(), supplier) == sups.end())
    {
        sups.push_back(supplier);
        changed = true;
    }

    if (changed)
        pendingRequests[type] = CountIncomingResources(&self, type);
}

void LogisticsComponent::SetReceiver(ResourceType type, Building* receiver, Building& self,
                                     ProductionComponent& prod)
{
    auto it = receivers.find(type);
    Building* prev = (it != receivers.end()) ? it->second : nullptr;
    if (prev != nullptr && prev != receiver)
    {
        prev->RemoveSupplier(type, &self);
        if (prev->owner != nullptr && prev->CanAcceptResource(type) && !prev->HasSupplier(type))
        {
            Building* storage = prev->owner->tilemap.FindNearestStorage(prev, prev->owner);
            if (storage != nullptr && storage != prev)
                prev->SetSupplier(type, storage);
        }
    }

    receivers[type] = receiver;
    auto alt = altReceivers.find(type);
    if (alt != altReceivers.end() && alt->second == receiver)
        altReceivers.erase(alt);
    if (receiver != nullptr)
        receiver->SetSupplier(type, &self);
}

void LogisticsComponent::SetAltReceiver(ResourceType type, Building* receiver, Building& self)
{
    if (receiver == nullptr)
        return;

    auto primary = receivers.find(type);
    if (primary != receivers.end() && primary->second == receiver)
        return;

    auto prev = altReceivers.find(type);
    if (prev != altReceivers.end() && prev->second != nullptr && prev->second != receiver)
        prev->second->RemoveSupplier(type, &self);

    altReceivers[type] = receiver;
    receiver->SetSupplier(type, &self);
}

void LogisticsComponent::RemoveSupplier(ResourceType type, Building* supplier)
{
    auto it = suppliers.find(type);
    if (it == suppliers.end())
        return;

    auto& sups = it->second;
    sups.erase(std::remove(sups.begin(), sups.end(), supplier), sups.end());
    if (sups.empty())
        suppliers.erase(it);

    pendingRequests.erase(type);
}

void LogisticsComponent::RemoveReceiver(ResourceType type, Building* receiver, Building& self,
                                         ProductionComponent&)
{
    auto it = receivers.find(type);
    if (it != receivers.end() && it->second == receiver)
    {
        receivers.erase(it);
        auto alt = altReceivers.find(type);
        if (alt != altReceivers.end() && alt->second != nullptr && alt->second != receiver)
        {
            receivers[type] = alt->second;
            altReceivers.erase(alt);
        }
        return;
    }

    auto alt = altReceivers.find(type);
    if (alt != altReceivers.end() && alt->second == receiver)
        altReceivers.erase(alt);
}

int LogisticsComponent::RequestResource(ResourceType type, int amount, Building& self)
{
    if (amount <= 0)
        return 0;

    if (!suppliers.contains(type))
    {
        requestBlocked = true;
        return 0;
    }

    int sent = 0;
    for (auto* sup : suppliers[type])
    {
        if (sup == nullptr) continue;
        int missing = amount - sent;
        if (missing <= 0) break;
        sent += sup->HandleTransport(type, missing, &self);
    }

    if (sent < amount)
        requestBlocked = true;

    pendingRequests[type] += sent;
    return sent;
}

void LogisticsComponent::MaintainRequests(Building& self, ProductionComponent& prod)
{
    requestBlocked = false;
    for (auto& [res, buf] : prod.inputBuffers)
    {
        int stored  = static_cast<int>(buf.buffer.size());
        int pending = CountIncomingResources(&self, res);
        pendingRequests[res] = pending;
        int missing = buf.bufferSize - stored - pending;
        if (missing > 0)
            RequestResource(res, missing, self);
    }
}

void LogisticsComponent::DispatchOutputs(Building& self, ProductionComponent& prod)
{
    for (auto& [res, amount] : prod.products)
    {
        auto recvIt = receivers.find(res);
        Building* recv = recvIt != receivers.end() ? recvIt->second : nullptr;
        if (recv == nullptr && self.owner != nullptr)
        {
            Building* storage = self.owner->tilemap.FindNearestStorage(&self, self.owner);
            if (storage != nullptr && storage->CanAcceptResource(res))
            {
                receivers[res] = storage;
                recv = storage;
            }
        }

        std::vector<Building*> targets;
        if (recv != nullptr)
            targets.push_back(recv);
        auto alt = altReceivers.find(res);
        if (alt != altReceivers.end() && alt->second != nullptr && alt->second != recv)
            targets.push_back(alt->second);
        if (self.owner != nullptr)
        {
            Building* storage = self.owner->tilemap.FindNearestStorage(&self, self.owner);
            if (storage != nullptr && storage != recv && storage->CanAcceptResource(res) &&
                std::find(targets.begin(), targets.end(), storage) == targets.end())
                targets.push_back(storage);
        }

        for (auto* target : targets)
        {
            int freeCapacity = GetReceiveCapacity(target, res);
            while (freeCapacity > 0)
            {
                auto [avail, r] = prod.outputBuffers[res].GetResource();
                if (!avail)
                    break;

                Log::Msg(self.tag, "ID: ", self.id, " ", rt2s(r->type),
                         " transport started to ", target->name, " with ID ", target->id);
                if (!self.owner->BeginTransport(&self, target, r))
                {
                    prod.outputBuffers[res].AddResource(r);
                    break;
                }
                freeCapacity--;
            }
        }
    }
}

int LogisticsComponent::HandleTransportFrom(ResourceType type, int amount, Building* receiver,
                                              Building& self, ProductionComponent& prod)
{
    int sent = 0;
    int requested = std::min(amount, GetReceiveCapacity(receiver, type));
    for (int i = 0; i < requested; i++)
    {
        auto [avail, res] = prod.outputBuffers[type].GetResource();
        if (avail)
        {
            if (receiver == nullptr || !receiver->CanReceiveResource(type))
            {
                prod.outputBuffers[type].AddResource(res);
                break;
            }
            Log::Msg(self.tag, "ID: ", self.id, " ", rt2s(res->type),
                     " transport started to ", receiver->name, " with ID ", receiver->id);
            if (self.owner->BeginTransport(&self, receiver, res))
                sent++;
            else
            {
                prod.outputBuffers[type].AddResource(res);
                break;
            }
        }
    }
    return sent;
}

std::vector<BuildingConnectionView> LogisticsComponent::GetSupplierViews(
    const ProductionComponent& prod) const
{
    std::vector<BuildingConnectionView> result;
    for (const auto& [res, amount] : prod.ingredients)
    {
        auto it = suppliers.find(res);
        if (it == suppliers.end() || it->second.empty())
        {
            result.push_back({res, nullptr});
            continue;
        }
        for (auto* sup : it->second)
            result.push_back({res, sup});
    }
    return result;
}

std::vector<BuildingConnectionView> LogisticsComponent::GetReceiverViews(
    const ProductionComponent& prod) const
{
    std::vector<BuildingConnectionView> result;
    for (const auto& [res, amount] : prod.products)
    {
        auto it = receivers.find(res);
        result.push_back({res, it != receivers.end() ? it->second : nullptr, false});
        auto alt = altReceivers.find(res);
        if (alt != altReceivers.end() && alt->second != nullptr &&
            (it == receivers.end() || alt->second != it->second))
            result.push_back({res, alt->second, true});
    }
    return result;
}

// ─── WorkerComponent ─────────────────────────────────────────────────────────

float WorkerComponent::GetRatio() const
{
    int cap = capacity.GetBase();
    if (cap <= 0) return 1.0f;
    return std::clamp(assigned / static_cast<float>(cap), 0.0f, 1.0f);
}

int WorkerComponent::GetModifiedCapacity(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(capacity, &self, ResourceType::Null, std::nullopt, 0)
        : capacity.GetBase();
}

bool RecipeComponent::HasSelectableRecipes() const
{
    return recipes.size() > 1;
}

std::string RecipeComponent::GetActiveRecipeName() const
{
    if (activeRecipeIndex < 0 || activeRecipeIndex >= static_cast<int>(recipes.size()))
        return "Default";
    return recipes[activeRecipeIndex].name.empty() ? "Default" : recipes[activeRecipeIndex].name;
}

void RecipeComponent::SetRecipes(std::vector<ProductionRecipeRuntime> newRecipes,
                                 Building& self,
                                 ProductionComponent& production,
                                 LogisticsComponent& logistics,
                                 WorkerComponent& workers)
{
    recipes = std::move(newRecipes);
    activeRecipeIndex = 0;
    if (!recipes.empty())
        SetActiveRecipe(0, self, production, logistics, workers);
}

bool RecipeComponent::SetActiveRecipe(int index,
                                      Building&,
                                      ProductionComponent& production,
                                      LogisticsComponent& logistics,
                                      WorkerComponent& workers)
{
    if (index < 0 || index >= static_cast<int>(recipes.size()))
        return false;

    for (auto& [res, buf] : production.inputBuffers)  buf.Clear();
    for (auto& [res, buf] : production.outputBuffers) buf.Clear();

    activeRecipeIndex = index;
    const auto& recipe = recipes[activeRecipeIndex];

    production.cycleTime = recipe.cycleTime;
    production.ingredients = recipe.inputs;
    production.products    = recipe.outputs;
    production.inputBuffers.clear();
    production.outputBuffers.clear();
    logistics.suppliers.clear();
    logistics.receivers.clear();
    logistics.altReceivers.clear();
    logistics.pendingRequests.clear();
    logistics.requestBlocked = false;
    production.elapsed  = 0.0;
    production.started  = false;

    for (const auto& [res, cap] : recipe.inputBufferCapacities)
        production.inputBuffers[res] = ResourceBuffer{res, cap};
    for (const auto& [res, cap] : recipe.outputBufferCapacities)
        production.outputBuffers[res] = ResourceBuffer{res, cap};

    workers.capacity = std::max(0, recipe.workerCapacity);
    workers.assigned = std::min(workers.assigned, workers.capacity.GetBase());
    return true;
}

bool RecipeComponent::CycleRecipe(Building& self,
                                  ProductionComponent& production,
                                  LogisticsComponent& logistics,
                                  WorkerComponent& workers)
{
    if (recipes.size() <= 1)
        return false;
    int next = (activeRecipeIndex + 1) % static_cast<int>(recipes.size());
    return SetActiveRecipe(next, self, production, logistics, workers);
}

// ─── ResearchComponent ───────────────────────────────────────────────────────

bool ResearchComponent::Start(const std::string& id, double time)
{
    if (!technologyId.empty())
        return false;
    technologyId = id;
    total = std::max(0.0, time);
    remaining = total;
    return true;
}

bool ResearchComponent::Tick(double dt)
{
    if (technologyId.empty())
        return false;
    remaining = std::max(0.0, remaining - std::max(0.0, dt));
    return remaining <= 0.0;
}

double ResearchComponent::GetProgress() const
{
    if (technologyId.empty() || total <= 0.0)
        return 0.0;
    return std::clamp(1.0 - remaining / total, 0.0, 1.0);
}

// ─── StorageComponent ────────────────────────────────────────────────────────

bool StorageComponent::CanAccept(ResourceType type) const
{
    return buffers.contains(type);
}

bool StorageComponent::CanReceive(ResourceType type) const
{
    auto it = buffers.find(type);
    return it != buffers.end() &&
           static_cast<int>(it->second.buffer.size()) < it->second.bufferSize;
}

void StorageComponent::AddResource(Resource* res, Building& self)
{
    if (res == nullptr)
        return;

    auto it = buffers.find(res->type);
    if (it == buffers.end() || static_cast<int>(it->second.buffer.size()) >= it->second.bufferSize)
    {
        if (res->sourceBuilding != nullptr)
            res->sourceBuilding->ReturnOutgoingResource(res);
        return;
    }

    Log::Msg(self.tag, "resource added!");
    it->second.AddResource(res);
}

void StorageComponent::ReturnOutgoingResource(Resource* res)
{
    if (res == nullptr)
        return;
    buffers[res->type].AddResource(res);
}

Resource StorageComponent::GetResource(ResourceType type)
{
    auto [avail, res] = buffers[type].GetResource();
    return avail ? *res : Resource{};
}

int StorageComponent::HandleTransport(ResourceType type, int amount, Building* receiver,
                                       Building& self)
{
    int sent = 0;
    int requested = std::min(amount, GetReceiveCapacity(receiver, type));
    for (int i = 0; i < requested; i++)
    {
        auto [avail, res] = buffers[type].GetResource();
        if (avail)
        {
            if (receiver == nullptr || !receiver->CanReceiveResource(type))
            {
                buffers[type].AddResource(res);
                break;
            }
            Log::Msg(self.tag, "ID: ", self.id, " ", rt2s(res->type),
                     " transport started to ", receiver->name, " with ID ", receiver->id);
            if (self.owner->BeginTransport(&self, receiver, res))
                sent++;
            else
            {
                buffers[type].AddResource(res);
                break;
            }
        }
    }
    return sent;
}

void StorageComponent::Update(Building& self, double dt)
{
    if (self.owner == nullptr)
        return;

    std::vector<Building*> visitedReceivers;
    for (auto& [res, buf] : buffers)
    {
        if (buf.buffer.empty())
            continue;

        visitedReceivers.clear();
        for (auto& tile : self.owner->tilemap.tilemap)
        {
            Building* receiver = tile.building.get();
            if (receiver == nullptr || receiver == &self || receiver->owner != self.owner)
                continue;
            if (std::find(visitedReceivers.begin(), visitedReceivers.end(), receiver) != visitedReceivers.end())
                continue;
            visitedReceivers.push_back(receiver);
            if (!receiver->CanAcceptResource(res))
                continue;

            int free = GetReceiveCapacity(receiver, res);
            if (free <= 0)
                continue;

            HandleTransport(res, free, receiver, self);
            if (buf.buffer.empty())
                break;
        }
    }
}

std::vector<ResourceBufferView> StorageComponent::GetBufferViews() const
{
    std::vector<ResourceBufferView> result;
    for (const auto& [res, buf] : buffers)
        result.push_back({res, static_cast<int>(buf.buffer.size()), buf.bufferSize, 0});
    return result;
}

// ─── TerritoryComponent ──────────────────────────────────────────────────────

int TerritoryComponent::GetRadius(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(radius, &self, ResourceType::Null, std::nullopt, 0)
        : radius.GetBase();
}

int TerritoryComponent::GetMaxHp(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(maxHp, &self, ResourceType::Null, std::nullopt, 1)
        : maxHp.GetBase();
}

void TerritoryComponent::ReceiveDamage(int damage)
{
    hp = std::max(0, hp - std::max(0, damage));
}

// ─── GarrisonComponent ───────────────────────────────────────────────────────

GarrisonComponent::GarrisonComponent()
    : currentOrder(MilitaryOrderType::None)
{}

void GarrisonComponent::Update(Building& self, double dt)
{
    self.activeTime += dt;

    garrison = GetTotalTroops();

    // Supply bookkeeping only when this building actually has a supply buffer
    // (e.g. the HQ does not, but can still host re-homed field divisions).
    if (auto* supplyPtr = self.GetComponent<SupplyBufferComponent>())
    {
        supplyPtr->buffer.bufferSize = supplyPtr->GetModifiedCapacity(self);
        supplyPtr->stored = static_cast<int>(supplyPtr->buffer.buffer.size());
    }

    for (size_t i = 0; i < divisions.size();)
    {
        auto& div = divisions[i];

        // Advance physical movement
        if (div.inTransit && self.owner != nullptr)
            UpdateDivisionMovement(div, self.owner->tilemap, dt);

        if (div.currentOrder == MilitaryOrderType::None || div.orderTargetPositionId < 0)
        {
            i++;
            continue;
        }

        // Only engage when the division has arrived at its target
        if (div.inTransit) { i++; continue; }

        // Field combat (Attack orders) is resolved centrally in RunFieldCombat —
        // see GameWorld.Render.cpp. It is order-to-start then sticky, and targets
        // can be enemy divisions on a tile OR enemy buildings, so we deliberately
        // do nothing here (and do not clear the order).
        if (div.currentOrder == MilitaryOrderType::Attack)
        {
            i++;
            continue;
        }

        div.orderCooldown = std::max(0.0, div.orderCooldown - dt);
        if (div.orderCooldown > 0.0) { i++; continue; }

        // Support / Defend target a friendly building.
        Building* target = self.owner != nullptr
            ? self.owner->tilemap.GetBuilding(div.orderTargetPositionId) : nullptr;
        if (target == nullptr || target == &self || target->IsUnderConstruction())
        {
            div.currentOrder = MilitaryOrderType::None;
            div.orderTargetPositionId = -1;
            div.orderCooldown = 0.0;
            i++;
            continue;
        }

        if (div.currentOrder == MilitaryOrderType::Support)
        {
            auto* friendlyGarrison = target->GetComponent<GarrisonComponent>();
            if (friendlyGarrison == nullptr || target->owner != self.owner)
            {
                div.currentOrder = MilitaryOrderType::None;
                div.orderTargetPositionId = -1;
                div.orderCooldown = 0.0;
                i++;
                continue;
            }

            if (friendlyGarrison->GetFreeDivisionSpace(*target) > 0)
            {
                MilitaryDivision moved = div;
                moved.currentOrder = MilitaryOrderType::None;
                moved.orderTargetPositionId = -1;
                moved.orderCooldown = 0.0;
                friendlyGarrison->divisions.push_back(moved);
                divisions.erase(divisions.begin() + static_cast<std::ptrdiff_t>(i));
                RecountDivisionTypes(*this);
                RecountDivisionTypes(*friendlyGarrison);
                continue;
            }

            div.orderCooldown = 3.0;
            i++;
            continue;
        }

        if (div.currentOrder == MilitaryOrderType::Defend)
            div.orderCooldown = 3.0;
        i++;
    }

    // Building-level orders (not used by Barracks)
    if (self.buildingType == BuildingType::Barracks)
    {
        ClearOrder();
        return;
    }

    if (currentOrder == MilitaryOrderType::None || self.owner == nullptr || orderTargetId < 0)
        return;

    orderCooldown = std::max(0.0, orderCooldown - dt);
    if (orderCooldown > 0.0)
        return;

    Building* target = self.owner->tilemap.GetBuilding(orderTargetId);
    if (target == nullptr || target == &self || target->IsUnderConstruction())
    {
        ClearOrder();
        return;
    }

    if (currentOrder == MilitaryOrderType::Attack)
    {
        // Building-level direct attack is disabled; divisions handle all combat.
        // Keep the state for battle tracking and clear when the target is gone.
        auto* defenderTerritory = target->GetComponent<TerritoryComponent>();
        if (target->owner == self.owner || defenderTerritory == nullptr || defenderTerritory->hp <= 0)
            ClearOrder();
        return;
    }

    auto* friendlyGarrison = target->GetComponent<GarrisonComponent>();
    if (friendlyGarrison == nullptr || target->owner != self.owner)
    {
        ClearOrder();
        return;
    }

    if (currentOrder == MilitaryOrderType::Support)
    {
        bool transferred = false;
        if (friendlyGarrison->GetFreeDivisionSpace(*target) > 0 && !divisions.empty())
        {
            friendlyGarrison->divisions.push_back(divisions.front());
            divisions.erase(divisions.begin());
            RecountDivisionTypes(*this);
            RecountDivisionTypes(*friendlyGarrison);
            transferred = true;
        }
        else if (friendlyGarrison->GetFreeGarrisonSpace(*target) > 0 && GetTotalTroops() > 0)
        {
            if (militia > 0)        { militia--;   friendlyGarrison->militia++;   }
            else if (swordsmen > 0) { swordsmen--; friendlyGarrison->swordsmen++; }
            else if (archers > 0)   { archers--;   friendlyGarrison->archers++;   }
            garrison = GetTotalTroops();
            friendlyGarrison->garrison = friendlyGarrison->GetTotalTroops();
            transferred = true;
        }

        if (auto* supplyPtr = self.GetComponent<SupplyBufferComponent>();
            supplyPtr != nullptr && !supplyPtr->buffer.buffer.empty() &&
            target->CanReceiveResource(ResourceType::FOOD_PROVISIONS))
        {
            auto [avail, res] = supplyPtr->buffer.GetResource();
            if (avail)
            {
                if (self.owner->BeginTransport(&self, target, res))
                {
                    supplyPtr->stored = static_cast<int>(supplyPtr->buffer.buffer.size());
                    transferred = true;
                }
                else
                    supplyPtr->buffer.AddResource(res);
            }
        }

        orderCooldown = transferred ? 1.0 : 3.0;
        return;
    }

    if (currentOrder == MilitaryOrderType::Defend)
        orderCooldown = 3.0;
}

void GarrisonComponent::IssueOrder(MilitaryOrderType order, int targetId)
{
    currentOrder = order;
    orderTargetId = targetId;
    orderCooldown = 0.0;
    // Cascade combat orders to all stationed divisions (AI path)
    for (auto& div : divisions)
    {
        div.currentOrder = order;
        div.orderTargetPositionId = targetId;
        div.orderCooldown = 0.0;
    }
}

bool GarrisonComponent::IssueDivisionOrder(int divisionId, MilitaryOrderType order,
                                             int targetId, Building& self)
{
    for (auto& div : divisions)
    {
        if (div.id != divisionId) continue;
        div.currentOrder = order;
        div.orderTargetPositionId = targetId;
        div.orderCooldown = 0.0;
        if (self.owner != nullptr)
        {
            Building* target = self.owner->tilemap.GetBuilding(targetId);
            if (target != nullptr)
                StartDivisionMovement(div, self, *target);
        }
        return true;
    }
    return false;
}

void GarrisonComponent::StartAllDivisionsMovement(Building& self, Building& target)
{
    for (auto& div : divisions)
        StartDivisionMovement(div, self, target);
}

bool GarrisonComponent::MoveDivisionTo(int divisionId, Vec2i targetTile, Building& self,
                                       bool requireOwnedTerritory,
                                       bool snapToSector)
{
    if (self.owner == nullptr)
        return false;

    TileMap& tilemap = self.owner->tilemap;
    if (!tilemap.IsInside(targetTile))
        return false;

    std::vector<Vec2i> candidateTargets;
    if (snapToSector)
    {
        DivisionSector sector = ResolveDivisionSector(
            tilemap, targetTile, requireOwnedTerritory ? self.owner : nullptr);
        if (!sector.IsValid())
            return false;

        auto addIfInSector = [&](Vec2i pos)
        {
            if (!tilemap.IsInside(pos) || SectorCellOf(pos) != sector.cell)
                return;
            int localX = pos.x - sector.anchor.x;
            int localY = pos.y - sector.anchor.y;
            int bit = localY * 2 + localX;
            if (bit < 0 || bit >= 4 || (sector.mask & (1u << bit)) == 0)
                return;
            if (std::find(candidateTargets.begin(), candidateTargets.end(), pos) == candidateTargets.end())
                candidateTargets.push_back(pos);
        };

        addIfInSector(targetTile);
        for (int tileId : sector.TileIds(tilemap))
            addIfInSector(tilemap.GetCoordsFromId(tileId));

        Vec2f center = sector.CenterTile();
        std::vector<Vec2i> neighbours;
        for (int radius = 1; radius <= 8; radius++)
        {
            for (int y = sector.anchor.y - radius; y <= sector.anchor.y + 1 + radius; y++)
            {
                for (int x = sector.anchor.x - radius; x <= sector.anchor.x + 1 + radius; x++)
                {
                    Vec2i pos{x, y};
                    if (!tilemap.IsInside(pos) || SectorCellOf(pos) == sector.cell)
                        continue;
                    if (!IsTileWalkableForDivision(tilemap, pos))
                        continue;
                    if (requireOwnedTerritory && tilemap.tilemap[tilemap.GetIdFromCoords(pos)].owner != self.owner)
                        continue;
                    if (std::find(candidateTargets.begin(), candidateTargets.end(), pos) != candidateTargets.end() ||
                        std::find(neighbours.begin(), neighbours.end(), pos) != neighbours.end())
                        continue;
                    neighbours.push_back(pos);
                }
            }
            if (static_cast<int>(neighbours.size()) >= std::max(8, static_cast<int>(divisions.size())))
                break;
        }
        std::stable_sort(neighbours.begin(), neighbours.end(), [center](Vec2i a, Vec2i b)
        {
            float acx = a.x + 0.5f - center.x;
            float acy = a.y + 0.5f - center.y;
            float bcx = b.x + 0.5f - center.x;
            float bcy = b.y + 0.5f - center.y;
            float da = acx * acx + acy * acy;
            float db = bcx * bcx + bcy * bcy;
            if (da != db)
                return da < db;
            if (a.y != b.y)
                return a.y < b.y;
            return a.x < b.x;
        });
        candidateTargets.insert(candidateTargets.end(), neighbours.begin(), neighbours.end());
    }
    else
    {
        if (!IsTileWalkableForDivision(tilemap, targetTile))
            return false;
        if (requireOwnedTerritory && tilemap.tilemap[tilemap.GetIdFromCoords(targetTile)].owner != self.owner)
            return false;
        candidateTargets.push_back(targetTile);
    }

    auto isRoad = [&](int tileId)
    {
        const Tile& tile = tilemap.tilemap[tileId];
        const Building* b = tile.GetBuilding();
        return b != nullptr && b->buildingType == BuildingType::Road;
    };

    bool movedAny = false;
    std::set<int> claimedTargets;
    for (auto& div : divisions)
    {
        if (divisionId >= 0 && div.id != divisionId)
            continue;

        Vec2i goalTile{-1, -1};
        for (Vec2i candidate : candidateTargets)
        {
            int candidateId = tilemap.GetIdFromCoords(candidate);
            if (claimedTargets.contains(candidateId))
                continue;
            if (!IsTileFree(*self.owner, candidate, div.id))
                continue;
            goalTile = candidate;
            claimedTargets.insert(candidateId);
            break;
        }
        if (goalTile.x < 0)
            continue;

        Vec2i startTile;
        if (div.worldPos.x >= 0.0f)
        {
            startTile = {static_cast<int>(div.worldPos.x / TILE_SIZE),
                         static_cast<int>(div.worldPos.y / TILE_SIZE)};
        }
        else
        {
            startTile = tilemap.GetCoordsFromId(self.positionId);
            int bestDist = std::numeric_limits<int>::max();
            for (int adjId : tilemap.GetAdjacentTileIds(&self))
            {
                Vec2i adj = tilemap.GetCoordsFromId(adjId);
                if (!IsTileWalkableForDivision(tilemap, adj))
                    continue;
                int dist = std::abs(adj.x - goalTile.x) + std::abs(adj.y - goalTile.y);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    startTile = adj;
                }
            }
        }

        std::vector<int> path = PlanDivisionPath(tilemap, startTile, goalTile);
        if (path.size() < 2)
            continue;

        double marchTpm = std::max(1.0, div.speedTilesPerMinute);
        double roadTpm = 60.0;
        const ArmyGroup* army = self.owner->armyGroups.FindArmyByDivision(div.id);
        if (army != nullptr)
        {
            marchTpm = std::max(1.0, army->ModifyStat(BalanceStat::ArmyMarchSpeed, marchTpm));
            roadTpm  = std::max(1.0, army->ModifyStat(BalanceStat::ArmyRoadSpeed, roadTpm));
        }

        std::vector<double> durations(path.size(), 0.0001);
        for (std::size_t i = 0; i + 1 < path.size(); i++)
        {
            Vec2i a = tilemap.GetCoordsFromId(path[i]);
            Vec2i b = tilemap.GetCoordsFromId(path[i + 1]);
            bool diagonal = (a.x != b.x) && (a.y != b.y);
            double tilesMoved = diagonal ? 1.41421356 : 1.0;
            double tpm = isRoad(path[i + 1]) ? roadTpm : marchTpm;
            durations[i] = (tilesMoved / tpm) * 60.0;
        }

        Vec2f startWorld = (div.worldPos.x >= 0.0f)
            ? div.worldPos
            : BuildingWorldCenter(self, tilemap);

        div.travelPath = std::move(path);
        div.travelStepDurations = std::move(durations);
        div.travelPathStep = 0;
        div.travelElapsed = 0.0;
        div.travelStepTime = div.travelStepDurations.front();
        div.worldPos = startWorld;
        div.travelFromPos = startWorld;
        // Each division settles on its OWN assigned tile (one division per tile),
        // not a shared sector centroid — otherwise units on outer tiles would drift
        // to the middle after arriving (the "spinning in circles" on arrival).
        div.travelToPos = TileWorldCenter(tilemap, tilemap.GetIdFromCoords(goalTile));
        div.occupiedTile = goalTile;
        div.sectorCell = SectorCellOf(goalTile);
        div.inTransit = true;

        div.currentOrder = MilitaryOrderType::None;
        div.orderTargetPositionId = -1;
        div.orderCooldown = 0.0;
        movedAny = true;
    }

    return movedAny;
}
void GarrisonComponent::ClearOrder()
{
    currentOrder = MilitaryOrderType::None;
    orderTargetId = -1;
    orderCooldown = 0.0;
}

bool GarrisonComponent::HasActiveDivisionOrders() const
{
    for (const auto& div : divisions)
        if (div.currentOrder != MilitaryOrderType::None)
            return true;
    return false;
}

int GarrisonComponent::GetTotalTroops() const
{
    if (!divisions.empty())
        return static_cast<int>(divisions.size());
    return militia + swordsmen + archers;
}

int GarrisonComponent::GetFreeGarrisonSpace(const Building& self) const
{
    int c = self.owner != nullptr
        ? self.owner->ResolveStat(cap, &self, ResourceType::Null, std::nullopt, 0)
        : cap.GetBase();
    return std::max(0, c - GetTotalTroops());
}

int GarrisonComponent::GetDivisionCap(const Building& self) const
{
    int c = self.owner != nullptr
        ? self.owner->ResolveStat(cap, &self, ResourceType::Null, std::nullopt, 0)
        : cap.GetBase();
    return std::max(0, c / 10);
}

int GarrisonComponent::GetFreeDivisionSpace(const Building& self) const
{
    return std::max(0, GetDivisionCap(self) - static_cast<int>(divisions.size()));
}

int GarrisonComponent::GetAverageMorale() const
{
    if (divisions.empty()) return 0;
    int total = 0;
    for (const auto& d : divisions) total += d.morale;
    return total / static_cast<int>(divisions.size());
}

int GarrisonComponent::GetAverageExperience() const
{
    if (divisions.empty()) return 0;
    int total = 0;
    for (const auto& d : divisions) total += d.experience;
    return total / static_cast<int>(divisions.size());
}

int GarrisonComponent::GetEffectiveStrength(const Building& self) const
{
    int base = self.owner != nullptr
        ? self.owner->ResolveStat(strength, &self, ResourceType::Null, std::nullopt, 0)
        : strength.GetBase();

    if (!divisions.empty())
    {
        int div_strength = 0;
        for (const auto& d : divisions)
            div_strength += d.strength * d.health / std::max(1, d.maxHealth);
        return base + div_strength;
    }
    return base + militia + swordsmen * 4 + archers * 3;
}

int GarrisonComponent::GetModifiedAttackDamage(const Building& self) const
{
    int base = std::max(4, GetEffectiveStrength(self) / 4);
    return self.owner != nullptr
        ? self.owner->ModifyBalanceIntForBuilding(BalanceStat::AttackDamage, base, &self,
                                                   ResourceType::Null, std::nullopt, 1)
        : base;
}

void GarrisonComponent::Recount()
{
    RecountDivisionTypes(*this);
}

// ─── SupplyBufferComponent ───────────────────────────────────────────────────

bool SupplyBufferComponent::CanReceive() const
{
    return static_cast<int>(buffer.buffer.size()) < buffer.bufferSize;
}

void SupplyBufferComponent::AddResource(Resource* res)
{
    if (res == nullptr) return;
    buffer.AddResource(res);
    stored = static_cast<int>(buffer.buffer.size());
}

void SupplyBufferComponent::ReturnOutgoingResource(Resource* res)
{
    if (res == nullptr) return;
    buffer.AddResource(res);
    stored = static_cast<int>(buffer.buffer.size());
}

Resource SupplyBufferComponent::GetResource()
{
    auto [avail, res] = buffer.GetResource();
    stored = static_cast<int>(buffer.buffer.size());
    return avail ? *res : Resource{};
}

int SupplyBufferComponent::HandleTransport(int amount, Building* receiver, Building& self)
{
    if (receiver == nullptr || amount <= 0)
        return 0;

    int sent = 0;
    for (int i = 0; i < amount; i++)
    {
        if (!receiver->CanReceiveResource(ResourceType::FOOD_PROVISIONS))
            break;

        auto [avail, res] = buffer.GetResource();
        if (!avail)
            break;

        if (self.owner->BeginTransport(&self, receiver, res))
            sent++;
        else
        {
            buffer.AddResource(res);
            break;
        }
    }

    stored = static_cast<int>(buffer.buffer.size());
    return sent;
}

int SupplyBufferComponent::GetModifiedCapacity(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(capacity, &self, ResourceType::Null, std::nullopt, 0)
        : capacity.GetBase();
}

int SupplyBufferComponent::GetSupplyConsumption(const Building& self,
                                                  const GarrisonComponent& g) const
{
    if (!g.divisions.empty())
    {
        int manpower = 0;
        for (const auto& d : g.divisions) manpower += d.manpowerScale;
        return self.owner != nullptr
            ? self.owner->ModifyBalanceIntForBuilding(BalanceStat::SupplyConsumption, manpower,
                                                       &self, ResourceType::Null, std::nullopt, 0)
            : manpower;
    }
    int troops = g.GetTotalTroops();
    return self.owner != nullptr
        ? self.owner->ModifyBalanceIntForBuilding(BalanceStat::SupplyConsumption, troops,
                                                   &self, ResourceType::Null, std::nullopt, 0)
        : troops;
}

// ─── RecruitmentComponent ────────────────────────────────────────────────────

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

    SoldierDivision div = CreateMilitaryDivision(job.type,
        self.id * 10000 + garrison.nextDivisionId++);
    garrison.divisions.push_back(div);
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
    if (!self.owner->TryPayBuildCost(materialCosts))
    {
        self.owner->strategicResources.Add(StrategicResourceType::Manpower, manpowerCost);
        return false;
    }

    double time = self.owner->debugMode
        ? 1.0
        : self.owner->ModifyBalanceForBuilding(
              BalanceStat::RecruitmentTime,
              GetBaseRecruitmentTime(type), &self, ResourceType::Null, type);
    queue.push_back({type, time});
    return true;
}

// ─── PopulationComponent ─────────────────────────────────────────────────────

void PopulationComponent::Update(Building& self, double dt)
{
    if (self.owner == nullptr)
        return;

    int rejected = RequestFoodSupply(self);
    bool hasBufferedFood = !foodBuffer.buffer.empty();
    bool hasIncomingFood = CountIncomingResources(&self, ResourceType::FOOD_PROVISIONS) > 0;
    if (rejected > 0 && !hasBufferedFood && !hasIncomingFood)
    {
        double pressure = std::clamp(static_cast<double>(rejected) /
                              std::max(1, foodBuffer.bufferSize), 0.0, 1.0);
        double dropRate = (0.025 + 0.055 * pressure) / std::max(0.45, foodSupplyLevel);
        foodSupplyLevel = std::max(0.0, foodSupplyLevel - dropRate * dt);
    }

    upkeepTimer += dt;
    if (upkeepTimer >= upkeepInterval)
    {
        upkeepTimer = 0.0;
        int needed = std::max(1, static_cast<int>(std::ceil(foodPackageUpkeep)));
        if (static_cast<int>(foodBuffer.buffer.size()) >= needed)
        {
            for (int i = 0; i < needed; i++)
            {
                foodBuffer.FreeResource();
                self.owner->economyTelemetry.RecordConsumption(ResourceType::FOOD_PROVISIONS);
            }
            foodSupplyLevel = std::min(1.0, foodSupplyLevel + 0.45);
        }
        else
        {
            foodSupplyLevel = std::max(0.0, foodSupplyLevel - foodSupplyDropPerMissedUpkeep);
        }
        hasFood = foodSupplyLevel > 0.0;
    }

    double efficiency  = GetManpowerProductivity();
    double modRate     = self.owner->ResolveStat(manpowerRate, &self);
    self.owner->AddManpower(modRate * efficiency * dt);
    self.activeTime += dt * efficiency;
}

double PopulationComponent::GetFoodSupplyRatio() const
{
    return std::clamp(foodSupplyLevel, 0.0, 1.0);
}

double PopulationComponent::GetManpowerProductivity() const
{
    return GetFoodSupplyRatio();
}

double PopulationComponent::GetWorkerProductivity() const
{
    return 0.3 + 0.7 * GetFoodSupplyRatio();
}

int PopulationComponent::RequestFoodSupply(Building& self)
{
    if (self.owner == nullptr || static_cast<int>(foodBuffer.buffer.size()) >= foodBuffer.bufferSize)
        return 0;

    int stored   = static_cast<int>(foodBuffer.buffer.size());
    int incoming = CountIncomingResources(&self, ResourceType::FOOD_PROVISIONS);
    int missing  = foodBuffer.bufferSize - stored - incoming;
    if (missing <= 0)
        return 0;

    for (auto& tile : self.owner->tilemap.tilemap)
    {
        Building* storage = tile.building.get();
        if (storage == nullptr || storage->owner != self.owner ||
            !storage->HasComponent<StorageComponent>())
            continue;

        missing -= storage->HandleTransport(ResourceType::FOOD_PROVISIONS, missing, &self);
        if (missing <= 0) break;
    }
    return std::max(0, missing);
}

// ─── SupplyPackageComponent ──────────────────────────────────────────────────

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
            if (type == ResourceType::FOOD_PROVISIONS || IsEquipment(type))
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

bool SupplyPackageComponent::AssemblePackage(Building& self)
{
    if (ReadyPackageCount() >= maxReadyPackages)
        return false;

    // Plan against what the network currently holds (no consumption yet).
    std::map<ResourceType, int> available = SurveyNetworkSupplies(self);
    SupplyPackage package;
    if (!PlanSupplyPackage(available, requestedCategories, soldiersPerPackage, rationsPerPackage, package))
        return false;

    // Draw exactly what the plan calls for out of the warehouses.
    package.rations = TakeFromNetwork(self, ResourceType::FOOD_PROVISIONS, package.rations);
    for (auto& item : package.items)
        item.amount = TakeFromNetwork(self, item.type, item.amount);

    readyPackages.push_back(std::move(package));
    totalPackagesAssembled++;
    return true;
}

void SupplyPackageComponent::Update(Building& self, double dt)
{
    timer += dt;
    if (timer < assembleInterval)
        return;
    timer = 0.0;

    // Assemble up to the ready-queue cap, then ship to needy armies. Both are
    // gated by the interval so the network scans run once per cycle, not every
    // simulation tick.
    while (AssemblePackage(self))
        ;
    DeliverPackages(self);
}

void SupplyPackageComponent::DeliverPackages(Building& self)
{
    if (readyPackages.empty() || self.owner == nullptr)
        return;

    // Gather every friendly military building that is short on supply.
    std::vector<Building*> targets;
    for (auto& tile : self.owner->tilemap.tilemap)
    {
        Building* target = tile.building.get();
        if (target == nullptr || target == &self)
            continue;
        if (target->positionId != tile.id)           // visit each building once
            continue;
        if (target->owner != self.owner || !target->HasComponent<GarrisonComponent>())
            continue;
        if (MilitaryNeedsSupply(*target))
            targets.push_back(target);
    }

    // Ship to the worst-off fronts first (priority where supply is most lacking).
    std::sort(targets.begin(), targets.end(), [](Building* a, Building* b)
    {
        return MilitaryWeaponDeficit(*a) > MilitaryWeaponDeficit(*b);
    });

    for (Building* target : targets)
    {
        if (readyPackages.empty())
            break;
        if (ApplyPackageToMilitary(readyPackages.front(), *target))
        {
            readyPackages.pop_front();
            totalPackagesDelivered++;
        }
    }
}

// ─── package distribution helpers ─────────────────────────────────────────────

bool MilitaryNeedsSupply(Building& target)
{
    auto* garrison = target.GetComponent<GarrisonComponent>();
    if (garrison == nullptr)
        return false;

    for (const auto& division : garrison->divisions)
    {
        if (division.weaponSupply < division.weaponSupplyCapacity ||
            division.foodSupply < division.foodSupplyCapacity)
            return true;
    }

    auto* supply = target.GetComponent<SupplyBufferComponent>();
    if (supply != nullptr &&
        static_cast<int>(supply->buffer.buffer.size()) < supply->buffer.bufferSize)
        return true;

    return false;
}

int MilitaryWeaponDeficit(Building& target)
{
    auto* garrison = target.GetComponent<GarrisonComponent>();
    if (garrison == nullptr)
        return 0;

    int deficit = 0;
    for (const auto& division : garrison->divisions)
        deficit += std::max(0, division.weaponSupplyCapacity - division.weaponSupply);
    return deficit;
}

bool ApplyPackageToMilitary(SupplyPackage& package, Building& target)
{
    auto* garrison = target.GetComponent<GarrisonComponent>();
    if (garrison == nullptr)
        return false;

    bool used = false;

    // Rations top up the building's food buffer (the building-level reserve).
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

    // Pick the best gear the package carries for each slot.
    ResourceType bestMelee = package.BestOfCategory(EquipmentCategory::Sword);
    if (bestMelee == ResourceType::Null)
        bestMelee = package.BestOfCategory(EquipmentCategory::Spear);
    ResourceType bestRanged = package.BestOfCategory(EquipmentCategory::Bow);
    if (bestRanged == ResourceType::Null)
        bestRanged = package.BestOfCategory(EquipmentCategory::Crossbow);
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
            profile->category == EquipmentCategory::Bow   || profile->category == EquipmentCategory::Crossbow)
            weaponPool += item.amount;
    }

    // Distribute to the neediest divisions first (lowest weapon-supply ratio).
    std::vector<SoldierDivision*> order;
    order.reserve(garrison->divisions.size());
    for (auto& division : garrison->divisions)
        order.push_back(&division);
    std::sort(order.begin(), order.end(), [](const SoldierDivision* a, const SoldierDivision* b)
    {
        float ra = a->weaponSupplyCapacity > 0 ? static_cast<float>(a->weaponSupply) / a->weaponSupplyCapacity : 1.0f;
        float rb = b->weaponSupplyCapacity > 0 ? static_cast<float>(b->weaponSupply) / b->weaponSupplyCapacity : 1.0f;
        return ra < rb;
    });

    for (SoldierDivision* division : order)
    {
        const bool ranged = division->type == MilitaryUnitType::Archer;

        // Refill weapon supply from the shared pool, neediest first; once the pool
        // runs dry the better-supplied divisions simply get nothing this delivery.
        int weaponNeed = division->weaponSupplyCapacity - division->weaponSupply;
        if (weaponNeed > 0 && weaponPool > 0)
        {
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

        if (division->foodSupply < division->foodSupplyCapacity)
        {
            division->foodSupply = division->foodSupplyCapacity;
            used = true;
        }
    }

    return used;
}
