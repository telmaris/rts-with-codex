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

namespace
{
    void RecountDivisionTypes(GarrisonComponent& g)
    {
        g.militia = g.swordsmen = g.archers = 0;
        for (const auto& divPtr : g.divisions)
        {
            const auto& div = *divPtr;
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
                div.travelStepDurations.clear();  // uniform step time below; drop any stale per-hop timings
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
        div.travelStepDurations.clear();  // direct march uses travelStepTime, not per-hop timings
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
            // a smooth departure; later hops snap to tile centres. The march aims
            // its LAST hop straight at the settle point (travelToPos = the quadrant
            // centre) instead of the goal tile's centre — otherwise the unit walks
            // into a corner tile and then teleports to the centre on arrival.
            int pathSize = static_cast<int>(div.travelPath.size());
            Vec2f cur = div.travelPathStep == 0
                ? div.travelFromPos
                : (div.travelPathStep >= pathSize - 1
                    ? div.travelToPos
                    : TileWorldCenter(tilemap, div.travelPath[div.travelPathStep]));
            Vec2f nxt = div.travelPathStep + 1 >= pathSize - 1
                ? div.travelToPos
                : TileWorldCenter(tilemap, div.travelPath[div.travelPathStep + 1]);
            float t = static_cast<float>(std::min(1.0, div.travelElapsed / stepDuration(div.travelPathStep)));
            div.worldPos = {cur.x + (nxt.x - cur.x) * t, cur.y + (nxt.y - cur.y) * t};
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
        auto& div = *divisions[i];

        // Divisions still sitting in the building (not deployed on the map) draw
        // supply at the low garrison rate. Deployed ones are handled once, in
        // RunFieldCombat (GameWorld.Render.cpp), so nothing double-counts. A
        // garrisoned division is never `engaged`, so this is also where it
        // regenerates cohesion (full territory bonus — it's home) and reinforces
        // strength from the owner's manpower pool (Phase C).
        if (div.occupiedTile.x < 0)
        {
            const double conservation = self.owner != nullptr ? PlayerSupplyConservation(*self.owner) : 0.0;
            ConsumeDivisionSupply(div, dt, /*engaged=*/false, /*deployed=*/false, conservation);
            if (self.owner != nullptr)
            {
                RegenerateDivisionCohesion(div, dt, /*inOwnTerritory=*/true, &self.owner->balanceModifiers);
                ReinforceDivisionStrength(div, *self.owner, dt, &self.owner->balanceModifiers);
            }
        }

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
                // Re-home the division to the friendly building (both belong to the
                // same player; the division stays owned by Player::forces). Update
                // both non-owning views in place.
                SoldierDivision* d = divisions[i];
                d->currentOrder = MilitaryOrderType::None;
                d->orderTargetPositionId = -1;
                d->orderCooldown = 0.0;
                d->garrisonBuildingId = target->positionId;
                friendlyGarrison->divisions.push_back(d);
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
            SoldierDivision* d = divisions.front();
            d->garrisonBuildingId = target->positionId;
            friendlyGarrison->divisions.push_back(d);
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
    for (auto& divPtr : divisions)
    {
        auto& div = *divPtr;
        div.currentOrder = order;
        div.orderTargetPositionId = targetId;
        div.orderCooldown = 0.0;
    }
}

bool GarrisonComponent::IssueDivisionOrder(int divisionId, MilitaryOrderType order,
                                             int targetId, Building& self)
{
    for (auto& divPtr : divisions)
    {
        auto& div = *divPtr;
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
    for (auto& divPtr : divisions)
        StartDivisionMovement(*divPtr, self, target);
}

bool GarrisonComponent::MoveDivisionTo(int divisionId, Vec2i targetTile, Building& self,
                                       bool requireOwnedTerritory,
                                       bool snapToSector,
                                       const std::set<int>* blockedTiles)
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

    // When snapping to sector, all divisions follow the same first-candidate path
    // so they converge visually rather than fanning out to separate tiles.
    Vec2i sharedPathGoal = (snapToSector && !candidateTargets.empty())
        ? candidateTargets[0]
        : Vec2i{-1, -1};

    bool movedAny = false;
    std::set<int> claimedTargets;
    for (auto& divPtr : divisions)
    {
        auto& div = *divPtr;
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
        if (!div.inTransit && div.occupiedTile.x >= 0)
        {
            startTile = div.occupiedTile;
        }
        else if (div.worldPos.x >= 0.0f)
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

        // Divisions converge on the sector's first candidate for a tidy group, but
        // if that shared goal is unreachable (e.g. an enemy stands on it, or it's
        // walled off) fall back to this division's own assigned tile — otherwise a
        // single blocked convergence tile would stop the WHOLE group from moving.
        Vec2i pathGoal = (sharedPathGoal.x >= 0) ? sharedPathGoal : goalTile;
        std::vector<int> path = PlanDivisionPath(tilemap, startTile, pathGoal, {}, blockedTiles);
        if (path.size() < 2 && pathGoal != goalTile)
            path = PlanDivisionPath(tilemap, startTile, goalTile, {}, blockedTiles);
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
        // All divisions in the same sector converge to the sector centre for a
        // clean visual grouping. The occupiedTile/startTile logic uses
        // occupiedTile (not worldPos) so there's no "spinning in circles" drift.
        {
            Vec2i sc = SectorCellOf(goalTile);
            div.travelToPos = {static_cast<float>((sc.x * 2 + 1) * TILE_SIZE),
                               static_cast<float>((sc.y * 2 + 1) * TILE_SIZE)};
        }
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
    for (const auto& divPtr : divisions)
        if (divPtr->currentOrder != MilitaryOrderType::None)
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
    // Only divisions physically stationed inside take garrison space. Deployed
    // divisions live in the field and merely keep this building as their home,
    // so a Barracks keeps training while its earlier recruits fight elsewhere.
    int stationed = 0;
    for (const auto& d : divisions)
        if (d->occupiedTile.x < 0)
            stationed++;
    return std::max(0, GetDivisionCap(self) - stationed);
}

int GarrisonComponent::GetAverageMorale() const
{
    if (divisions.empty()) return 0;
    int total = 0;
    for (const auto& d : divisions) total += d->morale;
    return total / static_cast<int>(divisions.size());
}

int GarrisonComponent::GetAverageExperience() const
{
    if (divisions.empty()) return 0;
    int total = 0;
    for (const auto& d : divisions) total += d->experience;
    return total / static_cast<int>(divisions.size());
}

int GarrisonComponent::GetEffectiveStrength(const Building& self) const
{
    int base = self.owner != nullptr
        ? self.owner->ResolveStat(strength, &self, ResourceType::Null, std::nullopt, 0)
        : strength.GetBase();

    if (!divisions.empty())
    {
        // Only divisions physically stationed inside defend the building — a
        // deployed division fights in the field, not from these walls.
        int div_strength = 0;
        for (const auto& d : divisions)
            if (d->occupiedTile.x < 0)
                div_strength += d->strength * d->health / std::max(1, d->maxHealth);
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

