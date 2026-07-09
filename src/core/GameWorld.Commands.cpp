#include "core/GameWorldInternal.h"
#include "ui/AudioSystem.h"
#include "warfare/DivisionSector.h"
#include "simulation/SectorGraph.h"

#include <algorithm>
#include <limits>
#include <set>

using namespace GameWorldInternal;

namespace
{
    // A marching division plans its route once and doesn't notice later map changes.
    // After a road is built, re-plan every in-transit division toward its current
    // target quadrant so it can take advantage of the (possibly faster) new road.
    void RePlanInTransitDivisions(Player* player, TileMap& tilemap)
    {
        if (player == nullptr)
            return;

        for (Building* building : player->GetTrackedBuildingsWithComponent<GarrisonComponent>())
        {
            auto* garrison = building != nullptr ? building->GetComponent<GarrisonComponent>() : nullptr;
            if (garrison == nullptr)
                continue;

            for (auto& divisionPtr : garrison->divisions)
            {
                auto& division = *divisionPtr;
                if (!division.inTransit || division.occupiedTile.x < 0)
                    continue;
                garrison->MoveDivisionTo(division.id, division.occupiedTile, *building);
            }
        }
    }

    SoldierDivision* FindDivision(GarrisonComponent& garrison, int divisionId)
    {
        for (auto* division : garrison.divisions)
            if (division->id == divisionId)
                return division;
        return nullptr;
    }

    bool AnyDivisionOnTile(const GameWorld& world, Vec2i tile, int excludingPlayerId = -1, int excludingDivisionId = -1)
    {
        if (!world.GetTileMap().IsInside(tile))
            return false;

        for (const auto& [pid, player] : world.GetPlayerHandler().players)
        {
            if (player == nullptr)
                continue;
            for (Building* building : player->GetTrackedBuildingsWithComponent<GarrisonComponent>())
            {
                auto* garrison = building != nullptr ? building->GetComponent<GarrisonComponent>() : nullptr;
                if (garrison == nullptr)
                    continue;
                for (const auto& division : garrison->divisions)
                {
                    if (pid == excludingPlayerId && division->id == excludingDivisionId)
                        continue;
                    if (division->occupiedTile == tile)
                        return true;
                }
            }
        }
        return false;
    }

    // Movement is per-QUADRANT (HoI4-style): a single enemy division holding any
    // tile of a 2x2 quadrant blocks the WHOLE quadrant — the enemy army occupies
    // the province, so you cannot slip past it through the other three tiles. This
    // returns every tile of every quadrant held by a division not belonging to
    // `mover`. Deterministic (std::map + std::set) for lockstep.
    std::set<int> EnemyOccupiedTiles(const GameWorld& world, const Player* mover)
    {
        std::set<int> blocked;
        if (mover == nullptr)
            return blocked;
        for (const auto& [pid, player] : world.GetPlayerHandler().players)
        {
            if (player == nullptr || player.get() == mover)
                continue;
            // No alliances yet: everyone who isn't us blocks. Once diplomacy has
            // neutral/allied states, filter hostile owners here.
            for (Building* building : player->GetTrackedBuildingsWithComponent<GarrisonComponent>())
            {
                auto* garrison = building != nullptr ? building->GetComponent<GarrisonComponent>() : nullptr;
                if (garrison == nullptr)
                    continue;
                for (const auto& division : garrison->divisions)
                {
                    if (division->occupiedTile.x < 0)
                        continue;
                    Vec2i cell = SectorCellOf(division->occupiedTile);   // 2x2 quadrant
                    Vec2i anchor{cell.x * 2, cell.y * 2};
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++)
                        {
                            Vec2i t{anchor.x + dx, anchor.y + dy};
                            if (world.GetTileMap().IsInside(t))
                                blocked.insert(world.GetTileMap().GetIdFromCoords(t));
                        }
                }
            }
        }
        return blocked;
    }

    // Full movement mask for `mover`: an army may cross ground it owns, neutral /
    // unowned ground (so it can advance into and RECLAIM contested land — e.g.
    // territory that fell neutral when a projecting building was lost), an ally's
    // ground, or the ground of an enemy it is at war with. Only a non-hostile third
    // party's owned land is impassable. Enemy-held quadrants (from
    // EnemyOccupiedTiles) are still folded in so a hostile army physically blocks,
    // which is what actually seals a front. Deterministic. (The path's start tile
    // is exempt in the planner, so a unit just outside its territory can set off.)
    std::set<int> MovementBlockedTiles(const GameWorld& world, const Player* mover)
    {
        std::set<int> blocked = EnemyOccupiedTiles(world, mover);
        if (mover == nullptr)
            return blocked;
        const auto& tiles = world.GetTileMap().tilemap;
        for (int id = 0; id < static_cast<int>(tiles.size()); id++)
        {
            const Player* o = tiles[id].owner;
            // Allowed: our own land, neutral ground, or a war-enemy's land (advance/
            // reclaim). Ally land will be allowed here once diplomacy has alliances.
            const bool allowed = (o == mover) || (o == nullptr) ||
                                 mover->diplomatic.IsAtWar(o->id);
            if (!allowed)
                blocked.insert(id);
        }
        return blocked;
    }

    bool AnyDivisionInFootprint(const GameWorld& world, Vec2i anchor, Vec2i footprint)
    {
        for (int y = 0; y < footprint.y; y++)
            for (int x = 0; x < footprint.x; x++)
                if (AnyDivisionOnTile(world, {anchor.x + x, anchor.y + y}))
                    return true;
        return false;
    }

    std::vector<Vec2i> AdjacentWalkableTilesAroundBuilding(const GameWorld& world, const Building& target, Vec2i prefer)
    {
        std::vector<Vec2i> result;
        Vec2i anchor = world.GetTileMap().GetCoordsFromId(target.positionId);
        Vec2i footprint = target.GetFootprint();
        for (int y = anchor.y - 1; y <= anchor.y + footprint.y; y++)
        {
            for (int x = anchor.x - 1; x <= anchor.x + footprint.x; x++)
            {
                bool insideFootprint = x >= anchor.x && x < anchor.x + footprint.x &&
                                       y >= anchor.y && y < anchor.y + footprint.y;
                if (insideFootprint)
                    continue;
                Vec2i tile{x, y};
                if (!world.GetTileMap().IsInside(tile) || !IsTileWalkableForDivision(world.GetTileMap(), tile))
                    continue;
                if (AnyDivisionOnTile(world, tile))
                    continue;
                result.push_back(tile);
            }
        }
        std::stable_sort(result.begin(), result.end(), [prefer](Vec2i a, Vec2i b)
        {
            int da = std::abs(a.x - prefer.x) + std::abs(a.y - prefer.y);
            int db = std::abs(b.x - prefer.x) + std::abs(b.y - prefer.y);
            if (da != db)
                return da < db;
            if (a.y != b.y)
                return a.y < b.y;
            return a.x < b.x;
        });
        return result;
    }

    bool TileAdjacentToBuilding(const GameWorld& world, Vec2i tile, const Building& target)
    {
        Vec2i anchor = world.GetTileMap().GetCoordsFromId(target.positionId);
        Vec2i footprint = target.GetFootprint();
        int clampedX = std::clamp(tile.x, anchor.x, anchor.x + footprint.x - 1);
        int clampedY = std::clamp(tile.y, anchor.y, anchor.y + footprint.y - 1);
        int dx = std::abs(tile.x - clampedX);
        int dy = std::abs(tile.y - clampedY);
        return dx <= 1 && dy <= 1 && !(dx == 0 && dy == 0);
    }

    std::vector<Vec2i> AdjacentWalkableTilesAroundTile(const GameWorld& world, Vec2i target, Vec2i prefer)
    {
        std::vector<Vec2i> result;
        for (int dy = -1; dy <= 1; dy++)
        {
            for (int dx = -1; dx <= 1; dx++)
            {
                if (dx == 0 && dy == 0)
                    continue;
                Vec2i tile{target.x + dx, target.y + dy};
                if (!world.GetTileMap().IsInside(tile) || !IsTileWalkableForDivision(world.GetTileMap(), tile))
                    continue;
                if (AnyDivisionOnTile(world, tile))
                    continue;
                result.push_back(tile);
            }
        }
        std::stable_sort(result.begin(), result.end(), [prefer](Vec2i a, Vec2i b)
        {
            int da = std::abs(a.x - prefer.x) + std::abs(a.y - prefer.y);
            int db = std::abs(b.x - prefer.x) + std::abs(b.y - prefer.y);
            if (da != db)
                return da < db;
            if (a.y != b.y)
                return a.y < b.y;
            return a.x < b.x;
        });
        return result;
    }

    Vec2i DivisionStartTile(const GameWorld& world, const Building& source, const SoldierDivision& division)
    {
        // When stopped, use the logical occupiedTile (avoids sector-centre mismatch).
        if (!division.inTransit && division.occupiedTile.x >= 0)
            return division.occupiedTile;
        // When in transit, use physical worldPos (most accurate current position).
        if (division.worldPos.x >= 0.0f)
        {
            return {
                std::clamp(static_cast<int>(division.worldPos.x / TILE_SIZE), 0, world.GetTileMap().params.sizeX - 1),
                std::clamp(static_cast<int>(division.worldPos.y / TILE_SIZE), 0, world.GetTileMap().params.sizeY - 1)};
        }
        return world.GetTileMap().GetCoordsFromId(source.positionId);
    }

    bool SetDivisionOrder(GarrisonComponent& garrison, int divisionId, MilitaryOrderType order, int targetId)
    {
        SoldierDivision* division = FindDivision(garrison, divisionId);
        if (division == nullptr)
            return false;
        division->currentOrder = order;
        division->orderTargetPositionId = targetId;
        division->orderCooldown = 0.0;
        return true;
    }

    bool MoveDivisionToAttackBuilding(GameWorld& world, Building& source, GarrisonComponent& garrison,
                                      int divisionId, Building& target)
    {
        SoldierDivision* division = FindDivision(garrison, divisionId);
        if (division == nullptr)
            return false;

        Vec2i start = DivisionStartTile(world, source, *division);
        if (division->occupiedTile.x >= 0 && TileAdjacentToBuilding(world, division->occupiedTile, target))
            return SetDivisionOrder(garrison, divisionId, MilitaryOrderType::Attack, target.positionId);

        auto candidates = AdjacentWalkableTilesAroundBuilding(world, target, start);
        std::set<int> blocked = MovementBlockedTiles(world, source.owner);
        bool moved = false;
        for (Vec2i candidate : candidates)
        {
            if (moved)
                break;
            moved = garrison.MoveDivisionTo(divisionId, candidate, source,
                                            false, false, &blocked);
        }
        return moved && SetDivisionOrder(garrison, divisionId, MilitaryOrderType::Attack, target.positionId);
    }

    bool MoveDivisionToDefendBuilding(GameWorld& world, Building& source, GarrisonComponent& garrison,
                                     int divisionId, Building& target)
    {
        SoldierDivision* division = FindDivision(garrison, divisionId);
        if (division == nullptr)
            return false;

        if (division->occupiedTile.x >= 0 && TileAdjacentToBuilding(world, division->occupiedTile, target))
            return SetDivisionOrder(garrison, divisionId, MilitaryOrderType::Defend, target.positionId);

        Vec2i start = DivisionStartTile(world, source, *division);
        auto candidates = AdjacentWalkableTilesAroundBuilding(world, target, start);
        std::set<int> blocked = MovementBlockedTiles(world, source.owner);
        bool moved = false;
        for (Vec2i candidate : candidates)
        {
            if (moved)
                break;
            moved = garrison.MoveDivisionTo(divisionId, candidate, source, false, false, &blocked);
        }
        return moved && SetDivisionOrder(garrison, divisionId, MilitaryOrderType::Defend, target.positionId);
    }

    bool MoveDivisionToAttackTile(GameWorld& world, Building& source, GarrisonComponent& garrison,
                                  int divisionId, Vec2i targetTile)
    {
        SoldierDivision* division = FindDivision(garrison, divisionId);
        if (division == nullptr)
            return false;

        int targetTileId = world.GetTileMap().GetIdFromCoords(targetTile);

        Vec2i start = DivisionStartTile(world, source, *division);
        if (division->occupiedTile.x >= 0 &&
            std::abs(division->occupiedTile.x - targetTile.x) <= 1 &&
            std::abs(division->occupiedTile.y - targetTile.y) <= 1 &&
            division->occupiedTile != targetTile)
        {
            // Already adjacent — just flag the attack order; combat seeds from it.
            return SetDivisionOrder(garrison, divisionId, MilitaryOrderType::Attack, targetTileId);
        }

        auto candidates = AdjacentWalkableTilesAroundTile(world, targetTile, start);
        std::set<int> blocked = MovementBlockedTiles(world, source.owner);
        blocked.erase(targetTileId);  // the target enemy tile is the fight, not a wall
        bool moved = false;
        for (Vec2i candidate : candidates)
        {
            if (moved)
                break;
            moved = garrison.MoveDivisionTo(divisionId, candidate, source,
                                            false, false, &blocked);
        }
        // March adjacent and carry the attack order so combat starts on arrival.
        return moved && SetDivisionOrder(garrison, divisionId, MilitaryOrderType::Attack, targetTileId);
    }
}

// Submits this command to the simulation.
std::uint64_t GameWorld::SubmitCommand(const GameCommand& command)
{
    return SubmitCommand(command, simulationTick + 1);
}

// Queues an intent and assigns an authoritative target tick when needed.
std::uint64_t GameWorld::SubmitCommand(const GameCommand& command, std::uint64_t minimumTargetTick)
{
    GameCommand queued = command;
    if (queued.commandId == 0)
        queued.commandId = nextCommandId++;
    else
        nextCommandId = std::max(nextCommandId, queued.commandId + 1);
    if (queued.targetTick == 0 || queued.targetTick < minimumTargetTick)
        queued.targetTick = minimumTargetTick;
    std::uint64_t commandId = queued.commandId;
    pendingCommands.push_back(std::move(queued));
    return commandId;
}

// Returns command accept/reject results emitted since the last consume.
std::vector<GameCommandResult> GameWorld::ConsumeCommandResults()
{
    std::vector<GameCommandResult> results = std::move(commandResults);
    commandResults.clear();
    return results;
}

// Applies a command received from an authoritative host to a client-side mirror.
bool GameWorld::ApplyAuthoritativeCommand(const GameCommand& command)
{
    return ExecuteCommand(command);
}

// Initializes GameWorld::AttachControllerForPlayer.
void GameWorld::AttachControllerForPlayer(Player* player)
{
    if (player == nullptr)
        return;

    switch (player->controllerType)
    {
        case PlayerControllerType::AI:
            controllers.push_back(std::make_unique<AIController>(player->id));
            break;
        case PlayerControllerType::Remote:
            controllers.push_back(std::make_unique<RemoteController>(player->id));
            break;
        case PlayerControllerType::LocalHuman:
        default:
            controllers.push_back(std::make_unique<LocalController>(player->id));
            break;
    }
}

// Advances UpdateControllers for one frame or simulation tick.
void GameWorld::UpdateControllers(double dt)
{
    for (auto& controller : controllers)
        if (controller != nullptr)
            controller->Update(*this, dt);
}

// Executes queued gameplay commands in order.
void GameWorld::ProcessCommands()
{
    std::deque<GameCommand> deferredCommands;
    while (!pendingCommands.empty())
    {
        GameCommand command = pendingCommands.front();
        pendingCommands.pop_front();
        if (command.targetTick > simulationTick)
        {
            deferredCommands.push_back(std::move(command));
            continue;
        }

        bool accepted = ExecuteCommand(command);
        commandResults.push_back(GameCommandResult{
            command.commandId,
            simulationTick,
            command.targetTick,
            command.playerId,
            command.type,
            accepted,
            accepted ? "accepted" : "rejected",
            command.Serialize()});
    }
    pendingCommands = std::move(deferredCommands);
}

// Validates and applies one gameplay command.
bool GameWorld::ExecuteCommand(const GameCommand& command)
{
    auto playerIt = playerHandler.players.find(command.playerId);
    if (playerIt == playerHandler.players.end())
        return false;

    Player* player = playerIt->second.get();
    if (player == nullptr)
        return false;
    auto acceptCommand = [&]()
    {
        player->TrackAcceptedCommand(command.type);
        return true;
    };

    auto playFx = [this, &command](const std::string& id, float vol = 1.0f)
    {
        if (audio != nullptr && command.playerId == localPlayerId)
            audio->PlaySound(id, vol);
    };

    if (command.type == GameCommandType::BuildBuilding)
    {
        if (!tilemap.IsInside(command.tilePos))
            return false;

        auto preview = CreateBuildingFromType(command.buildingType, 0);
        if (preview == nullptr)
            return false;

        if (!tilemap.CanPlaceBuilding(command.buildingType, command.tilePos, preview->GetFootprint(), player))
            return false;
        // Roads are traversable terrain — armies march on them, so a deployed
        // division does not block laying a road underneath it. Solid buildings
        // still cannot be raised on top of troops.
        if (command.buildingType != BuildingType::Road &&
            AnyDivisionInFootprint(*this, command.tilePos, preview->GetFootprint()))
        {
            Log::Msg("[GameWorld]", "Command rejected: cannot build on a deployed division");
            return false;
        }

        const auto& definition = GetBuildingDefinition(command.buildingType);
        bool debugFreeBuild = tilemap.params.debugMode && player->id == localPlayerId;
        bool chargeCost = command.chargeCost && !debugFreeBuild;
        if (!debugFreeBuild)
        {
            auto failures = player->GetBuildRequirementFailures(definition);
            if (!failures.empty())
            {
                Log::Msg("[GameWorld]", "Command rejected: ", definition.name, " locked by ", failures.front());
                return false;
            }
        }
        if (chargeCost && !player->TryPayBuildCost(player->GetEffectiveBuildCosts(definition)))
        {
            Log::Msg("[GameWorld]", "Command rejected: not enough resources to build ", definition.name);
            return false;
        }

        int tileId = tilemap.GetIdFromCoords(command.tilePos);
        auto building = CreateBuildingFromType(command.buildingType, player->id * 100000 + player->build.buildingId++);
        if (building == nullptr)
            return false;

        double buildTime = player->ModifyBalanceAt(BalanceStat::BuildTime, definition.buildTime, command.buildingType, command.tilePos);
        building->buildTime = buildTime;
        building->constructionRemaining = chargeCost ? buildTime : 0.0;
        tilemap.BuildOnTile(tileId, player, std::move(building));

        Building* placed = tilemap.GetBuilding(tileId);
        if (placed == nullptr)
            return false;

        // A new road tile may shorten in-transit divisions' routes — re-plan them.
        // (The road building occupies its tile immediately, so pathing sees it.)
        if (placed->buildingType == BuildingType::Road)
            RePlanInTransitDivisions(player, tilemap);

        if (placed->IsUnderConstruction())
        {
            playFx("build");
            return acceptCommand();
        }

        if (player->roadNetwork != nullptr)
        {
            for (int occupiedTileId : tilemap.GetBuildingTileIds(placed))
                player->roadNetwork->UpdateNavMap(occupiedTileId, placed);
        }
        tilemap.AutoConnectBuilding(placed);
        playFx("build");
        return acceptCommand();
    }

    if (command.type == GameCommandType::DestroyBuilding)
    {
        Building* building = tilemap.GetBuilding(command.sourceTileId);
        if (building == nullptr || building->owner != player)
            return false;
        if (!building->CanBeManuallyDestroyed())
        {
            Log::Msg("[GameWorld]", "Command rejected: ", building->name, " cannot be manually destroyed");
            return false;
        }

        // Cancelling a still-unfinished build refunds what was paid for it —
        // an under-construction building is always one the player was charged for.
        if (building->IsUnderConstruction())
        {
            const auto& definition = GetBuildingDefinition(building->buildingType);
            player->RefundBuildCost(player->GetEffectiveBuildCosts(definition));
        }

        tilemap.DestroyBuildingAt(building->positionId);
        playFx("destroy");
        return acceptCommand();
    }

    if (command.type == GameCommandType::SetReceiver)
    {
        Building* source = tilemap.GetBuilding(command.sourceTileId);
        Building* target = tilemap.GetBuilding(command.targetTileId);
        if (source == nullptr || target == nullptr || source == target)
            return false;
        if (source->owner != player || target->owner != player)
            return false;
        if (source->IsUnderConstruction() || target->IsUnderConstruction())
            return false;

        tilemap.ConnectReceiver(source, target, command.alternativeReceiver);
        return acceptCommand();
    }

    if (command.type == GameCommandType::IssueMilitaryOrder)
    {
        Building* source = tilemap.GetBuilding(command.sourceTileId);
        Building* target = tilemap.GetBuilding(command.targetTileId);
        if (source == nullptr || target == nullptr || source == target)
            return false;
        if (source->owner != player || source->IsUnderConstruction() || target->IsUnderConstruction())
            return false;

        auto* garrison = source->GetComponent<GarrisonComponent>();
        auto* targetTerritory = target->GetComponent<TerritoryComponent>();
        if (garrison == nullptr || targetTerritory == nullptr || targetTerritory->hp <= 0)
            return false;

        if (command.militaryOrderType == MilitaryOrderType::Attack && target->owner == player)
            return false;
        // Armies besiege only military targets (defensive works + HQ). Civil
        // buildings like the Barracks factory are not valid attack destinations.
        if (command.militaryOrderType == MilitaryOrderType::Attack && !IsMilitaryAttackTarget(*target))
            return false;
        if ((command.militaryOrderType == MilitaryOrderType::Support || command.militaryOrderType == MilitaryOrderType::Defend) &&
            target->owner != player)
            return false;
        if (command.militaryOrderType == MilitaryOrderType::Support &&
            target->GetComponent<GarrisonComponent>() == nullptr)
            return false;
        // Only defensive works (tower/fortress/castle) man a garrison. The
        // Barracks factory and the Headquarters merely home field divisions —
        // divisions cannot be sent to defend or garrison inside them.
        if ((command.militaryOrderType == MilitaryOrderType::Support ||
             command.militaryOrderType == MilitaryOrderType::Defend) &&
            !IsDefensiveGarrisonBuilding(*target))
            return false;

        // An attack order is the declaration of war, and it must be flagged
        // BEFORE the march is planned: movement is territory-gated on IsAtWar,
        // so until the war exists the enemy's ground is impassable and the
        // divisions could never set off toward the target.
        if (command.militaryOrderType == MilitaryOrderType::Attack &&
            target->owner != nullptr && target->owner != player)
        {
            player->diplomatic.DeclareWar(player->id, target->owner->id);
            target->owner->diplomatic.DeclareWar(player->id, target->owner->id);
        }

        if (command.divisionId >= 0)
        {
            if (command.militaryOrderType == MilitaryOrderType::Attack)
            {
                if (!MoveDivisionToAttackBuilding(*this, *source, *garrison, command.divisionId, *target))
                    return false;
            }
            else if (command.militaryOrderType == MilitaryOrderType::Defend)
            {
                if (!MoveDivisionToDefendBuilding(*this, *source, *garrison, command.divisionId, *target))
                    return false;
            }
            else
            {
                if (!garrison->IssueDivisionOrder(command.divisionId, command.militaryOrderType, target->positionId, *source))
                    return false;
            }
        }
        else
        {
            if (source->HasComponent<RecruitmentComponent>())
                return false;
            if (command.militaryOrderType == MilitaryOrderType::Attack)
            {
                bool orderedAny = false;
                for (const auto& division : garrison->divisions)
                    orderedAny = MoveDivisionToAttackBuilding(*this, *source, *garrison, division->id, *target) || orderedAny;
                if (!orderedAny)
                    return false;
                garrison->currentOrder = command.militaryOrderType;
                garrison->orderTargetId = target->positionId;
                garrison->orderCooldown = 0.0;
            }
            else
            {
                garrison->IssueOrder(command.militaryOrderType, target->positionId);
                garrison->StartAllDivisionsMovement(*source, *target);
            }
        }

        // Field combat was removed: an Attack order now only declares war (above)
        // and marches the divisions adjacent to the target. No damage is dealt until
        // the combat system is rebuilt.
        playFx(command.militaryOrderType == MilitaryOrderType::Attack ? "attack" : "march");

        return acceptCommand();
    }

    if (command.type == GameCommandType::MoveDivision)
    {
        Building* source = tilemap.GetBuilding(command.sourceTileId);
        if (source == nullptr || source->owner != player || source->IsUnderConstruction())
            return false;

        auto* garrison = source->GetComponent<GarrisonComponent>();
        if (garrison == nullptr)
            return false;

        if (command.targetTileId < 0 || command.targetTileId >= static_cast<int>(tilemap.tilemap.size()))
            return false;
        Vec2i target = tilemap.GetCoordsFromId(command.targetTileId);
        if (!tilemap.IsInside(target))
            return false;

        // HoI4 flow: ordering a move INTO a quadrant held by an enemy army IS an
        // attack on that army. Convert the move instead of rejecting it (the
        // province is plan-time blocked), so walking onto the enemy declares the
        // war, marches to contact and starts the battle naturally.
        Vec2i targetCell = SectorCellOf(target);
        for (const auto& [pid, other] : playerHandler.players)
        {
            if (other == nullptr || other.get() == player)
                continue;
            for (const auto& enemyDiv : other->forces)
            {
                if (enemyDiv == nullptr || enemyDiv->occupiedTile.x < 0 ||
                    SectorCellOf(enemyDiv->occupiedTile) != targetCell)
                    continue;

                player->diplomatic.DeclareWar(player->id, other->id);
                other->diplomatic.DeclareWar(player->id, other->id);
                if (!MoveDivisionToAttackTile(*this, *source, *garrison, command.divisionId, enemyDiv->occupiedTile))
                    return false;
                playFx("attack");
                return acceptCommand();
            }
        }

        // Armies move only within their own / war-enemy / ally territory, and an
        // enemy army's quadrant blocks the route.
        std::set<int> blocked = MovementBlockedTiles(*this, player);
        if (!garrison->MoveDivisionTo(command.divisionId, target, *source, false, true, &blocked))
            return false;

        // A move order is a deliberate withdrawal/reposition: break any current
        // engagement and grant a short regroup window so the division can actually
        // leave contact instead of being intercepted and re-locked next tick.
        for (auto& fptr : player->forces)
        {
            if (fptr == nullptr) continue;
            if (command.divisionId >= 0 && fptr->id != command.divisionId) continue;
            if (command.divisionId < 0 && fptr->garrisonBuildingId != source->positionId) continue;
            fptr->engaged = false;
            fptr->retreating = false;
            fptr->regroupTimer = std::max(fptr->regroupTimer, 3.0f);
        }
        playFx("march");
        return acceptCommand();
    }

    if (command.type == GameCommandType::AttackTile)
    {
        Building* source = tilemap.GetBuilding(command.sourceTileId);
        if (source == nullptr || source->owner != player || source->IsUnderConstruction())
            return false;

        auto* garrison = source->GetComponent<GarrisonComponent>();
        if (garrison == nullptr)
            return false;

        if (command.targetTileId < 0 || command.targetTileId >= static_cast<int>(tilemap.tilemap.size()))
            return false;
        Vec2i target = tilemap.GetCoordsFromId(command.targetTileId);
        if (!tilemap.IsInside(target))
            return false;

        Player* targetOwner = nullptr;
        for (const auto& [pid, other] : playerHandler.players)
        {
            if (other == nullptr || other.get() == player)
                continue;
            if (DivisionOnTile(*other, target, -1) >= 0)
            {
                targetOwner = other.get();
                break;
            }
        }
        if (targetOwner == nullptr)
            return false;

        // Attacking an enemy army is also a declaration of war, flagged BEFORE
        // the march is planned — movement is territory-gated on IsAtWar, so the
        // route into enemy ground only opens once the war exists.
        player->diplomatic.DeclareWar(player->id, targetOwner->id);
        targetOwner->diplomatic.DeclareWar(player->id, targetOwner->id);

        if (!MoveDivisionToAttackTile(*this, *source, *garrison, command.divisionId, target))
            return false;
        playFx("attack");
        return acceptCommand();
    }

    if (command.type == GameCommandType::FormArmy)
    {
        Building* home = tilemap.GetBuilding(command.sourceTileId);
        if (home == nullptr || home->owner != player)
            return false;

        // Parse the comma-separated division ids packed into researchId.
        std::vector<int> divisionIds;
        std::stringstream stream(command.researchId);
        std::string token;
        while (std::getline(stream, token, ','))
        {
            if (token.empty())
                continue;
            try { divisionIds.push_back(std::stoi(token)); }
            catch (...) { return false; }
        }
        if (divisionIds.empty())
            return false;

        int armyId = player->armyGroups.CreateArmy(
            std::to_string(static_cast<int>(player->armyGroups.GetArmies().size()) + 1) + ". Army");
        for (int divisionId : divisionIds)
            player->armyGroups.AddDivision(armyId, home->positionId, divisionId);
        player->armyGroups.RebuildAllModifiers(player->armyModifierSet);
        Log::Msg("[Army]", "Formed army with ", divisionIds.size(), " divisions");
        return acceptCommand();
    }

    if (command.type == GameCommandType::AssignToArmy)
    {
        Building* home = tilemap.GetBuilding(command.sourceTileId);
        if (home == nullptr || home->owner != player)
            return false;
        const int armyId = command.targetTileId;
        if (player->armyGroups.FindArmy(armyId) == nullptr)
            return false;

        std::vector<int> divisionIds;
        std::stringstream stream(command.researchId);
        std::string token;
        while (std::getline(stream, token, ','))
        {
            if (token.empty())
                continue;
            try { divisionIds.push_back(std::stoi(token)); }
            catch (...) { return false; }
        }
        if (divisionIds.empty())
            return false;

        for (int divisionId : divisionIds)
            player->armyGroups.AddDivision(armyId, home->positionId, divisionId);
        player->armyGroups.RebuildAllModifiers(player->armyModifierSet);
        Log::Msg("[Army]", "Transferred ", divisionIds.size(), " divisions into army #", armyId);
        return acceptCommand();
    }

    if (command.type == GameCommandType::RecruitUnit)
    {
        Building* source = tilemap.GetBuilding(command.sourceTileId);
        if (source == nullptr || source->owner != player || source->IsUnderConstruction())
            return false;

        auto* recruitment = source->GetComponent<RecruitmentComponent>();
        auto* garrison = source->GetComponent<GarrisonComponent>();
        if (recruitment == nullptr || garrison == nullptr)
            return false;

        if (!recruitment->QueueUnit(command.militaryUnitType, *source, *garrison))
            return false;
        playFx("recruit");
        return acceptCommand();
    }

    if (command.type == GameCommandType::StartFocus)
    {
        if (command.researchId.empty() || !player->CanUnlockFocus(command.researchId))
            return false;

        if (!player->StartFocus(command.researchId))
            return false;
        playFx("research");
        return acceptCommand();
    }

    if (command.type == GameCommandType::StartTechnologyResearch)
    {
        Building* source = tilemap.GetBuilding(command.sourceTileId);
        if (source == nullptr || source->owner != player || source->IsUnderConstruction())
            return false;

        if (source->buildingType != BuildingType::University ||
            source->GetComponent<ResearchComponent>() == nullptr)
            return false;

        if (command.researchId.empty() || !player->CanResearchTechnology(command.researchId))
            return false;

        if (!player->StartTechnologyResearch(command.researchId, source))
            return false;
        playFx("research");
        return acceptCommand();
    }

    return false;
}
