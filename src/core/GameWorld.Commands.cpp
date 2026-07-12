#include "core/GameWorldInternal.h"
#include "ui/AudioSystem.h"

#include <algorithm>

using namespace GameWorldInternal;

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
    // TD(etap-6.3): an eliminated player's roster/units already vanished and
    // their AI/input is expected to stop — reject any stray command from a
    // stale client/controller rather than letting it silently re-mutate a
    // defeated player's (mostly inert) remaining state.
    if (player->defeated)
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

    if (command.type == GameCommandType::RecruitUnit)
    {
        Building* source = tilemap.GetBuilding(command.sourceTileId);
        if (source == nullptr || source->owner != player || source->IsUnderConstruction())
            return false;

        auto* recruitment = source->GetComponent<RecruitmentComponent>();
        if (recruitment == nullptr || command.researchId.empty())
            return false;

        if (!recruitment->QueueRecruitment(*source, command.researchId))
            return false;

        playFx("build");
        return acceptCommand();
    }

    if (command.type == GameCommandType::DeployUnits)
    {
        int targetPlayerId = command.targetTileId;
        if (command.unitInstanceIds.empty() || targetPlayerId == player->id)
            return false;
        auto targetPlayerIt = playerHandler.players.find(targetPlayerId);
        if (targetPlayerIt == playerHandler.players.end())
            return false;
        // TD(etap-6.3): a target need not be a direct ring neighbor as long as
        // every player in between has been eliminated — the route then runs
        // through their conquered HQ.
        auto isEliminated = [&](int playerId)
        {
            auto it = playerHandler.players.find(playerId);
            return it != playerHandler.players.end() && it->second != nullptr && it->second->defeated;
        };
        if (!PathingService::AreHqsConnected(militaryRoads, player->id, targetPlayerId, isEliminated))
            return false;

        // All-or-nothing: every listed unit must be a valid, currently-rostered
        // instance before any of them are moved, so a malformed/stale command
        // never partially deploys a column.
        for (int unitInstanceId : command.unitInstanceIds)
        {
            const BattleUnit* unit = player->roster.FindUnit(unitInstanceId);
            if (unit == nullptr || unit->state != BattleUnitState::InRoster)
                return false;
        }

        auto routeKey = std::make_pair(player->id, targetPlayerId);
        for (int unitInstanceId : command.unitInstanceIds)
        {
            auto removed = player->roster.RemoveUnit(unitInstanceId);
            if (!removed.has_value())
                continue;

            BattleUnit unit = std::move(removed.value());
            unit.state = BattleUnitState::Marching;
            unit.routeFromPlayerId = player->id;
            unit.routeToPlayerId = targetPlayerId;
            unit.tileIndex = -1;
            unit.tileProgress = 0.0;
            deployedUnits[unitInstanceId] = std::move(unit);
            spawnQueues[routeKey].push_back(unitInstanceId);
        }

        playFx("build");
        return acceptCommand();
    }

    return false;
}
