#include "core/GameWorldInternal.h"

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

using namespace GameWorldInternal;

// TD(etap-6.3): elimination, conquest and storage drain. Triggered from
// HqCombatSystem the tick a player's HQ HP reaches 0.
void GameWorld::EliminatePlayer(int defeatedPlayerId, int conquerorPlayerId)
{
    auto defeatedIt = playerHandler.players.find(defeatedPlayerId);
    auto conquerorIt = playerHandler.players.find(conquerorPlayerId);
    if (defeatedIt == playerHandler.players.end() || defeatedIt->second == nullptr)
        return;
    if (conquerorIt == playerHandler.players.end() || conquerorIt->second == nullptr)
        return;

    Player* defeated = defeatedIt->second.get();
    Player* conqueror = conquerorIt->second.get();
    if (defeated->defeated)
        return; // already processed — idempotent

    defeated->defeated = true;

    // 1. The defeated player's deployed units, roster and recruitment orders
    // vanish immediately. The conqueror's units already at the fallen HQ have
    // completed their deployment and return to the roster; units still on the
    // road keep marching to that HQ and return when they arrive.
    // Any surviving road-combat opponent left mid-FightingUnit self-heals
    // back to Marching on UnitCombatSystem's next tick (it re-resolves its
    // opponent dynamically rather than holding a stale reference).
    auto returnUnitToConqueror = [&](std::map<int, BattleUnit>::iterator it)
    {
        BattleUnit unit = std::move(it->second);
        unit.state = BattleUnitState::InRoster;
        unit.routeFromPlayerId = -1;
        unit.routeToPlayerId = -1;
        unit.tileIndex = 0;
        unit.tileProgress = 0.0;
        unit.attackTimer = 0.0;
        conqueror->roster.AddUnit(std::move(unit));
        return deployedUnits.erase(it);
    };
    for (auto it = deployedUnits.begin(); it != deployedUnits.end();)
    {
        if (it->second.ownerPlayerId == defeatedPlayerId)
            it = deployedUnits.erase(it);
        else if (it->second.ownerPlayerId == conquerorPlayerId &&
                 it->second.routeToPlayerId == defeatedPlayerId &&
                 (it->second.state == BattleUnitState::AttackingHq || it->second.tileIndex < 0))
            it = returnUnitToConqueror(it);
        else if (it->second.ownerPlayerId == conquerorPlayerId &&
                 it->second.routeToPlayerId == defeatedPlayerId &&
                 it->second.state == BattleUnitState::FightingUnit)
        {
            it->second.state = BattleUnitState::Marching;
            ++it;
        }
        else
            ++it;
    }
    defeated->roster.units.clear();
    for (auto it = spawnQueues.begin(); it != spawnQueues.end();)
    {
        if (it->first.first == defeatedPlayerId ||
            (it->first.first == conquerorPlayerId && it->first.second == defeatedPlayerId))
            it = spawnQueues.erase(it);
        else
            ++it;
    }

    // Stable snapshot before mutating dataTracker's sets (Register/Unregister
    // below erase from them mid-iteration otherwise) — sorted by id so the
    // capture order (and therefore ramp bookkeeping/storage sums) is
    // identical on host and client regardless of heap layout.
    std::vector<Building*> defeatedBuildings(defeated->GetTrackedBuildings().begin(),
                                              defeated->GetTrackedBuildings().end());
    std::sort(defeatedBuildings.begin(), defeatedBuildings.end(),
              [](const Building* a, const Building* b) { return a->id < b->id; });

    // 2. Storage drain: sum every resource across all of the defeated
    // player's storage-like buildings (HQ + StorageBuilding), credit the
    // conqueror's own HQ with capturedStockFraction of the total (floored),
    // then zero out every one of the defeated's buffers.
    HqComponent* defeatedHq = nullptr;
    Building* defeatedHqBuilding = nullptr;
    for (Building* building : defeatedBuildings)
        if (auto* hq = building->GetComponent<HqComponent>(); hq != nullptr)
        {
            defeatedHq = hq;
            defeatedHqBuilding = building;
            break;
        }

    HqComponent* conquerorHq = nullptr;
    Building* conquerorHqBuilding = nullptr;
    std::vector<Building*> conquerorBuildings(conqueror->GetTrackedBuildings().begin(),
                                               conqueror->GetTrackedBuildings().end());
    std::sort(conquerorBuildings.begin(), conquerorBuildings.end(),
              [](const Building* a, const Building* b) { return a->id < b->id; });
    for (Building* building : conquerorBuildings)
        if (auto* hq = building->GetComponent<HqComponent>(); hq != nullptr)
        {
            conquerorHq = hq;
            conquerorHqBuilding = building;
            break;
        }

    double baseCaptureStockFraction = defeatedHq != nullptr ? defeatedHq->captureStockFraction : 0.4;
    double captureStockFraction = std::clamp(
        conqueror->ModifyBalance(BalanceStat::ConquestSpoilsFraction, baseCaptureStockFraction), 0.0, 1.0);
    double conquestRampDuration = defeatedHq != nullptr ? defeatedHq->conquestRampDuration : 600.0;

    if (conquerorHq != nullptr && conquerorHqBuilding != nullptr)
    {
        std::map<ResourceType, int> totals;
        for (Building* building : defeatedBuildings)
        {
            auto* storage = building->GetComponent<StorageComponent>();
            if (storage == nullptr)
                continue;
            for (auto& [type, buffer] : storage->buffers)
                totals[type] += static_cast<int>(buffer.buffer.size());
        }

        auto* conquerorStorage = conquerorHqBuilding->GetComponent<StorageComponent>();
        if (conquerorStorage != nullptr)
        {
            for (const auto& [type, total] : totals)
            {
                int gain = static_cast<int>(std::floor(total * captureStockFraction));
                if (gain <= 0)
                    continue;
                auto bufferIt = conquerorStorage->buffers.find(type);
                if (bufferIt == conquerorStorage->buffers.end())
                    continue;
                int current = static_cast<int>(bufferIt->second.buffer.size());
                bufferIt->second.SetStoredAmount(current + gain);
            }
        }

        for (Building* building : defeatedBuildings)
        {
            auto* storage = building->GetComponent<StorageComponent>();
            if (storage == nullptr)
                continue;
            for (auto& [type, buffer] : storage->buffers)
                buffer.SetStoredAmount(0);
        }
    }

    // 3. Replace the fallen HQ with a ready StorageBuilding. This preserves
    // its useful logistics location without giving the conqueror a second HQ
    // (and therefore a second combat target). The spoils were already moved
    // above, so this new depot deliberately starts empty.
    if (defeatedHqBuilding != nullptr)
    {
        int hqPositionId = defeatedHqBuilding->positionId;
        tilemap.DestroyBuildingAt(hqPositionId);
        auto capturedStorage = std::make_unique<StorageBuilding>(
            conqueror->id * 100000 + conqueror->build.buildingId++);
        capturedStorage->constructionRemaining = 0.0;
        Building* placedStorage = tilemap.PlaceLoadedBuilding(hqPositionId, conqueror, std::move(capturedStorage));
        if (placedStorage != nullptr && conqueror->roadNetwork != nullptr)
            for (int occupiedTileId : tilemap.GetBuildingTileIds(placedStorage))
                conqueror->roadNetwork->UpdateNavMap(occupiedTileId, placedStorage);
    }

    // 4. All surviving infrastructure changes hands. Production buildings
    // receive the 30% -> 100% compliance ramp; non-production infrastructure
    // is immediately usable by its new owner.
    for (Building* building : defeatedBuildings)
    {
        if (building == nullptr || building == defeatedHqBuilding || building->HasComponent<HqComponent>())
            continue;

        if (auto* recruitment = building->GetComponent<RecruitmentComponent>(); recruitment != nullptr)
            recruitment->queue.clear();

        defeated->UnregisterBuilding(building);
        building->owner = conqueror;
        conqueror->RegisterBuilding(building);
        if (building->HasComponent<ProductionComponent>() || building->HasComponent<PopulationComponent>() ||
            building->HasComponent<TowerCombatComponent>())
            conqueror->conqueredEconomy.AddRamp(building->id, conquestRampDuration);

        // T12 (docs/post_pivot_audit_2026-07-12.md): each Player owns an
        // independent RoadNetwork/NavigationMap — reassigning `owner` above
        // does nothing to either one, so without this the conqueror's
        // CalculatePath has no idea these footprint tiles exist even after
        // building a fresh road up to them. Roads themselves are deliberately
        // NOT transferred (no ProductionComponent) — the conqueror must
        // build their own connecting road, same as any other new building.
        for (int occupiedTileId : tilemap.GetBuildingTileIds(building))
        {
            defeated->roadNetwork->UpdateNavMap(occupiedTileId, nullptr);
            conqueror->roadNetwork->UpdateNavMap(occupiedTileId, building);
        }
    }

    // 5. Victory is derived on demand from Player::defeated (GetVictorPlayerId)
    // — nothing further to record here.
}
