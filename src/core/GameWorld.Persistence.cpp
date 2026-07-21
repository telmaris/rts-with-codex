#include "core/GameWorldInternal.h"

using namespace GameWorldInternal;

// Serializes current runtime state.
bool GameWorld::SaveToFile(const std::string& path) const
{
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open())
        return false;

    // Save v29: added TowerCombatComponent target priority.
    // Save v28: added the UPG block (UpgradeComponent — generic per-instance
    // building upgrade progression, introduced for Road).
    out << "RTS_SAVE 29\n";
    out << "WORLD " << std::quoted(worldName) << '\n';
    out << "PARAMS " << tilemap.params.sizeX << ' ' << tilemap.params.sizeY << ' '
        << tilemap.params.seed << ' ' << static_cast<int>(tilemap.params.sizePreset) << ' '
        << tilemap.params.resourceDensity << ' ' << tilemap.params.resourceFieldSize << ' '
        << tilemap.params.resourceRichness << ' ' << tilemap.params.aiOpponentCount << ' '
        << tilemap.params.aiDifficulty << ' ' << tilemap.params.debugMode << '\n';
    if (render != nullptr)
    {
        out << "CAMERA " << render->camera.target.x << ' ' << render->camera.target.y << ' '
            << render->camera.zoom << ' ' << render->camera.rotation << '\n';
    }
    else
    {
        out << "CAMERA 0 0 1.25 0\n";
    }

    out << "PLAYERS " << playerHandler.players.size() << '\n';
    for (const auto& [id, player] : playerHandler.players)
    {
        // Save v27 (AI rework czystka, TODO #2): the dead DiplomaticState —
        // never read by any gameplay logic post-pivot — was removed, and the
        // DIPLO/WAR blocks (and their counts on this line) with it.
        out << "PLAYER " << id << ' ' << player->strategicResources.values.size() << ' '
            << player->technologies.GetUnlocked().size() << ' '
            << player->focuses.GetUnlocked().size() << ' '
            << (player->defeated ? 1 : 0) << '\n';
        for (const auto& [type, value] : player->strategicResources.values)
            out << "STRAT " << static_cast<int>(type) << ' ' << value << '\n';
        for (const auto& techId : player->technologies.GetUnlocked())
            out << "TECH " << std::quoted(techId) << '\n';
        for (const auto& focusId : player->focuses.GetUnlocked())
            out << "FOCUS " << std::quoted(focusId) << '\n';

        // TD(etap-3): recruited-but-not-deployed BattleUnit roster. Equipment
        // is always an empty list in v1 (ETAP 3.4 seam) but its count is
        // written from day one so a future DLC equipment system doesn't need
        // another breaking save-format change.
        out << "ROSTER " << player->nextUnitInstanceId << ' ' << player->roster.units.size() << '\n';
        for (const auto& [instanceId, unit] : player->roster.units)
        {
            out << "UNIT " << unit.instanceId << ' ' << unit.ownerPlayerId << ' '
                << std::quoted(unit.unitDefId) << ' ' << unit.currentHp << ' '
                << static_cast<int>(unit.state) << ' '
                << unit.routeFromPlayerId << ' ' << unit.routeToPlayerId << ' '
                << unit.tileIndex << ' ' << unit.tileProgress << ' ' << unit.attackTimer << ' '
                << unit.equipment.size() << '\n';
        }

        // TD(etap-6.3): productivity ramps on buildings captured from an
        // eliminated player.
        out << "CONQUERED " << player->conqueredEconomy.GetRamps().size() << '\n';
        for (const auto& ramp : player->conqueredEconomy.GetRamps())
            out << "RAMP " << ramp.buildingId << ' ' << ramp.elapsed << ' ' << ramp.rampDuration << '\n';

        out << "ENDPLAYER\n";
    }

    out << "TILES " << tilemap.tilemap.size() << '\n';
    for (const auto& tile : tilemap.tilemap)
    {
        int ownerId = tile.owner != nullptr ? tile.owner->id : -1;
        out << "T " << tile.id << ' ' << static_cast<int>(tile.tileType) << ' '
            << tile.terrainTextureId << ' ' << ownerId << ' ' << tile.resourceRichness << ' '
            << static_cast<int>(tile.biome) << '\n';
    }

    // Military road ring (TD etap-2): written explicitly rather than
    // regenerated from seed, so generator changes never invalidate an
    // existing save's ring layout.
    const auto& militaryRoutes = militaryRoads.GetRoutes();
    out << "MILROADS " << militaryRoutes.size() << '\n';
    for (const auto& route : militaryRoutes)
    {
        out << "MROUTE " << route.playerA << ' ' << route.playerB << ' ' << route.tiles.size();
        for (int tileId : route.tiles)
            out << ' ' << tileId;
        out << '\n';
    }

    int buildingCount = 0;
    for (const auto& tile : tilemap.tilemap)
    {
        if (tile.building != nullptr)
            buildingCount++;
    }

    out << "BUILDINGS " << buildingCount << '\n';
    for (const auto& tile : tilemap.tilemap)
    {
        const auto* building = tile.building.get();
        if (building == nullptr)
            continue;

        int ownerId = building->owner != nullptr ? building->owner->id : -1;
        out << "B " << building->positionId << ' ' << static_cast<int>(building->buildingType) << ' '
            << building->id << ' ' << ownerId << ' ' << building->textureId << ' '
            << building->footprint.x << ' ' << building->footprint.y << ' '
            << building->productionBlocked << ' ' << building->lifetime << ' '
            << building->activeTime << ' ' << building->totalProduced << ' '
            << building->transportTime.GetBase() << '\n';
        out << "CONSTRUCTION " << building->buildTime.GetBase() << ' ' << building->constructionRemaining << '\n';

        if (const auto* prod = building->GetComponent<ProductionComponent>())
        {
            const auto* workers = building->GetComponent<WorkerComponent>();
            const auto* recipes = building->GetComponent<RecipeComponent>();
            // Only University actually has a ResearchComponent alongside
            // ProductionComponent — every other production building legitimately
            // has none. Write an empty placeholder rather than requiring one
            // (a pre-existing bug: this whole PROD block used to bail out with
            // `return false` for every non-University production building,
            // silently breaking SaveToFile for any game with e.g. a Woodcutter).
            const auto* research = building->GetComponent<ResearchComponent>();
            const auto* logistics = building->GetComponent<LogisticsComponent>();
            if (workers == nullptr || recipes == nullptr || logistics == nullptr)
                return false;

            out << "PROD " << static_cast<int>(prod->terrainType) << ' '
                << prod->cycleTime.GetBase() << ' ' << prod->elapsed << ' '
                << prod->started << '\n';
            out << "WORKERS " << workers->capacity.GetBase() << ' ' << workers->assigned << '\n';
            out << "RECIPE " << recipes->activeRecipeIndex << '\n';
            out << "RESEARCH " << std::quoted(research != nullptr ? research->technologyId : std::string{}) << ' '
                << (research != nullptr ? research->remaining : 0.0) << ' '
                << (research != nullptr ? research->total : 0.0) << '\n';

            out << "INGREDIENTS " << prod->ingredients.size() << '\n';
            for (const auto& [type, amount] : prod->ingredients)
                out << "ING " << static_cast<int>(type) << ' ' << amount << '\n';

            out << "PRODUCTS " << prod->products.size() << '\n';
            for (const auto& [type, amount] : prod->products)
                out << "PRODUCT " << static_cast<int>(type) << ' ' << amount << '\n';

            out << "INPUTS " << prod->inputBuffers.size() << '\n';
            for (const auto& [type, buffer] : prod->inputBuffers)
                SaveResourceBuffer(out, "INPUT", buffer);

            out << "OUTPUTS " << prod->outputBuffers.size() << '\n';
            for (const auto& [type, buffer] : prod->outputBuffers)
                SaveResourceBuffer(out, "OUTPUT", buffer);

            int supplierCount = 0;
            for (const auto& [type, suppliers] : logistics->suppliers)
                for (const auto* supplier : suppliers)
                    if (supplier != nullptr) supplierCount++;

            out << "SUPPLIERS " << supplierCount << '\n';
            for (const auto& [type, suppliers] : logistics->suppliers)
                for (const auto* supplier : suppliers)
                    if (supplier != nullptr)
                        out << "SUP " << static_cast<int>(type) << ' ' << supplier->positionId << '\n';

            out << "RECEIVERS " << logistics->receivers.size() << '\n';
            for (const auto& [type, receiver] : logistics->receivers)
                out << "REC " << static_cast<int>(type) << ' '
                    << (receiver != nullptr ? receiver->positionId : -1) << '\n';
            out << "ALT_RECEIVERS " << logistics->altReceivers.size() << '\n';
            for (const auto& [type, receiver] : logistics->altReceivers)
                out << "ALTREC " << static_cast<int>(type) << ' '
                    << (receiver != nullptr ? receiver->positionId : -1) << '\n';

            out << "ENDPROD\n";
        }

        if (const auto* storage = building->GetComponent<StorageComponent>())
        {
            out << "STOR " << storage->buffers.size() << '\n';
            for (const auto& [type, buffer] : storage->buffers)
                SaveResourceBuffer(out, "BUF", buffer);
            out << "ENDSTOR\n";
        }

        if (const auto* hq = building->GetComponent<HqComponent>())
        {
            out << "HQ " << hq->maxHp.GetBase() << ' ' << hq->currentHp << ' '
                << hq->hardDefense.GetBase() << ' ' << hq->thornsDamage.GetBase() << ' '
                << hq->thornsInterval << ' ' << hq->thornsTimer << ' '
                << hq->captureStockFraction << ' ' << hq->conquestRampDuration << '\n';
        }

        if (const auto* tower = building->GetComponent<TowerCombatComponent>())
        {
            // Ammo itself is already covered by the generic STOR block above
            // (an ordinary StorageComponent buffer) — only the attack
            // cooldown needs its own field here.
            out << "TOWER " << tower->damage.GetBase() << ' ' << tower->range.GetBase() << ' '
                << tower->attackSpeed.GetBase() << ' ' << tower->attackTimer << ' '
                << static_cast<int>(tower->ammoResource) << ' ' << tower->ammoPerShot.GetBase() << ' '
                << static_cast<int>(tower->targetMode) << '\n';
        }

        if (const auto* pop = building->GetComponent<PopulationComponent>())
        {
            out << "VIL " << pop->manpowerRate.GetBase() << ' ' << pop->upkeepTimer << ' '
                << pop->upkeepInterval << ' ' << pop->foodPackageUpkeep << ' '
                << pop->hasFood << ' ' << pop->populationCap.GetBase() << ' '
                << pop->foodSupplyLevel << ' ' << pop->foodBuffer.bufferSize << ' '
                << pop->foodBuffer.buffer.size() << '\n';
        }

        if (const auto* recruitment = building->GetComponent<RecruitmentComponent>())
        {
            out << "RECRUIT " << recruitment->queue.size() << '\n';
            for (const auto& entry : recruitment->queue)
                out << "RQ " << std::quoted(entry.unitDefId) << ' ' << entry.total << ' ' << entry.remaining
                    << ' ' << (entry.resourcesReady ? 1 : 0) << '\n';
        }

        if (const auto* upgrade = building->GetComponent<UpgradeComponent>())
        {
            out << "UPG " << upgrade->level << ' ' << (upgrade->isUpgrading ? 1 : 0) << ' '
                << upgrade->upgradeRemaining << '\n';
        }

        out << "ENDB\n";
    }

    // TD(etap-4): deployed (marching/fighting/arrived) units, world-scoped
    // since a column can include units from either side of a route.
    out << "DEPLOYEDUNITS " << deployedUnits.size() << '\n';
    for (const auto& [instanceId, unit] : deployedUnits)
    {
        out << "DUNIT " << unit.instanceId << ' ' << unit.ownerPlayerId << ' '
            << std::quoted(unit.unitDefId) << ' ' << unit.currentHp << ' '
            << static_cast<int>(unit.state) << ' '
            << unit.routeFromPlayerId << ' ' << unit.routeToPlayerId << ' '
            << unit.tileIndex << ' ' << unit.tileProgress << ' ' << unit.attackTimer << ' '
            << unit.equipment.size() << '\n';
    }

    out << "SPAWNQUEUES " << spawnQueues.size() << '\n';
    for (const auto& [routeKey, queue] : spawnQueues)
    {
        out << "SQ " << routeKey.first << ' ' << routeKey.second << ' ' << queue.size();
        for (int unitInstanceId : queue)
            out << ' ' << unitInstanceId;
        out << '\n';
    }

    return true;
}

// Loads the requested data into runtime state.
bool GameWorld::LoadFromFile(const std::string& path, Renderer* renderer, AudioSystem* a)
{
    std::ifstream in(path);
    if (!in.is_open())
        return false;

    combatTelemetry.Clear();

    std::string tag;
    int version = 0;
    in >> tag >> version;
    // TD(etap-1): the old war system's save fields (HQ/MIL/DIVS/RECRUIT) were
    // dropped, not merely extended — a breaking change per the rework plan.
    // Older saves are rejected outright rather than partially parsed.
    // v27 (AI rework czystka): DiplomaticState removed from the format.
    // v29: added tower target priority.
    // v28: added the UPG block (UpgradeComponent).
    if (tag != "RTS_SAVE" || version != 29)
        return false;

    render = renderer;
    audio  = a;

    in >> tag >> std::quoted(worldName);
    if (tag != "WORLD")
        return false;

    int preset = 0;
    in >> tag >> tilemap.params.sizeX >> tilemap.params.sizeY >> tilemap.params.seed >> preset;
    if (tag != "PARAMS")
        return false;
    tilemap.params.sizePreset = static_cast<MapSizePreset>(preset);
    if (version >= 3)
    {
        in >> tilemap.params.resourceDensity >> tilemap.params.resourceFieldSize
            >> tilemap.params.resourceRichness >> tilemap.params.aiOpponentCount
            >> tilemap.params.aiDifficulty;
        if (version >= 4)
            in >> tilemap.params.debugMode;
        else
            tilemap.params.debugMode = false;
    }

    if (version >= 2)
    {
        Camera2D camera{};
        in >> tag >> camera.target.x >> camera.target.y >> camera.zoom >> camera.rotation;
        if (tag != "CAMERA")
            return false;
        if (render != nullptr)
        {
            render->camera = camera;
            render->ClampCameraToMap({tilemap.params.sizeX, tilemap.params.sizeY});
        }
    }

    playerHandler.players.clear();
    controllers.clear();
    int playerCount = 0;
    in >> tag >> playerCount;
    if (tag != "PLAYERS")
        return false;

    for (int i = 0; i < playerCount; i++)
    {
        int playerId = 0;
        int strategicCount = 0;
        int technologyCount = 0;
        int focusCount = 0;
        int defeatedFlag = 0;
        in >> tag >> playerId >> strategicCount >> technologyCount >> focusCount >> defeatedFlag;
        if (tag != "PLAYER")
            return false;

        auto player = std::make_unique<Player>(playerId, tilemap);
        player->defeated = defeatedFlag != 0;
        player->name = playerId == localPlayerId ? "Player" : "AI Opponent";
        player->controllerType = playerId == localPlayerId ? PlayerControllerType::LocalHuman : PlayerControllerType::AI;
        player->color = playerId == localPlayerId ? Color{66, 154, 255, 255} : Color{220, 72, 72, 255};
        for (int s = 0; s < strategicCount; s++)
        {
            int type = 0;
            double value = 0.0;
            in >> tag >> type >> value;
            if (tag != "STRAT")
                return false;
            player->strategicResources.values[static_cast<StrategicResourceType>(type)] = value;
        }

        for (int t = 0; t < technologyCount; t++)
        {
            std::string techId;
            in >> tag >> std::quoted(techId);
            if (tag != "TECH")
                return false;
            player->technologies.RestoreTechnology(techId);
        }
        for (int f = 0; f < focusCount; f++)
        {
            std::string focusId;
            in >> tag >> std::quoted(focusId);
            if (tag != "FOCUS")
                return false;
            player->focuses.RestoreFocus(focusId);
        }
        player->RefreshTechnologyModifiers();

        int rosterCount = 0;
        in >> tag >> player->nextUnitInstanceId >> rosterCount;
        if (tag != "ROSTER")
            return false;
        for (int u = 0; u < rosterCount; u++)
        {
            int instanceId = 0;
            int ownerPlayerId = 0;
            std::string unitDefId;
            double currentHp = 0.0;
            int state = 0;
            int routeFromPlayerId = -1;
            int routeToPlayerId = -1;
            int tileIndex = 0;
            double tileProgress = 0.0;
            double attackTimer = 0.0;
            size_t equipmentCount = 0;
            in >> tag >> instanceId >> ownerPlayerId >> std::quoted(unitDefId) >> currentHp
               >> state >> routeFromPlayerId >> routeToPlayerId >> tileIndex >> tileProgress
               >> attackTimer >> equipmentCount;
            if (tag != "UNIT")
                return false;

            BattleUnit unit(instanceId, ownerPlayerId, unitDefId);
            unit.currentHp = currentHp;
            unit.state = static_cast<BattleUnitState>(state);
            unit.routeFromPlayerId = routeFromPlayerId;
            unit.routeToPlayerId = routeToPlayerId;
            unit.tileIndex = tileIndex;
            unit.tileProgress = tileProgress;
            unit.attackTimer = attackTimer;
            // Equipment is always empty in v1 (ETAP 3.4 seam) — equipmentCount
            // is read but not yet parsed into instances.
            player->roster.AddUnit(std::move(unit));
        }

        // TD(etap-6.3): productivity ramps on buildings captured from an
        // eliminated player.
        int conqueredCount = 0;
        in >> tag >> conqueredCount;
        if (tag != "CONQUERED")
            return false;
        std::vector<ConqueredBuildingRamp> ramps;
        ramps.reserve(conqueredCount);
        for (int r = 0; r < conqueredCount; r++)
        {
            ConqueredBuildingRamp ramp;
            in >> tag >> ramp.buildingId >> ramp.elapsed >> ramp.rampDuration;
            if (tag != "RAMP")
                return false;
            ramps.push_back(ramp);
        }
        player->conqueredEconomy.SetRamps(std::move(ramps));
        // Re-derive each ramp's BalanceModifier from the restored elapsed
        // time immediately (zero-dt tick), mirroring RefreshTechnologyModifiers()
        // above rather than leaving the captured buildings' cycle time
        // unmodified until the next real simulation tick.
        player->conqueredEconomy.Tick(*player, 0.0);

        in >> tag;
        if (tag != "ENDPLAYER")
            return false;
        playerHandler.players[playerId] = std::move(player);
        AttachControllerForPlayer(playerHandler.players[playerId].get());
    }

    int tileCount = 0;
    in >> tag >> tileCount;
    if (tag != "TILES")
        return false;

    tilemap.tilemap.clear();
    tilemap.tilemap.resize(tileCount);
    for (int i = 0; i < tileCount; i++)
    {
        int id = 0;
        int tileType = 0;
        int terrainTextureId = 0;
        int ownerId = -1;
        in >> tag >> id >> tileType >> terrainTextureId >> ownerId;
        if (tag != "T")
            return false;

        Tile tile{id};
        tile.tileType = static_cast<TileType>(tileType);
        tile.terrainTextureId = terrainTextureId;
        if (version >= 3)
            in >> tile.resourceRichness;
        else
            tile.resourceRichness = tile.tileType == TileType::GRASS ? 0 : tilemap.params.resourceRichness;
        if (version >= 16)
        {
            int biome = 0;
            in >> biome;
            tile.biome = static_cast<BiomeType>(biome);
        }
        // Tile::owner is a relic of the pre-pivot territory system (ETAP 1
        // removed it; nothing in production sets it anymore, see
        // docs/post_pivot_audit_2026-07-12.md T2) — restored here only
        // because it's still part of the save format. Left in place rather
        // than bumping the save version to drop the field; ownerId always
        // reads back as whatever was written (effectively unused/-1 today).
        auto ownerIt = playerHandler.players.find(ownerId);
        tile.owner = ownerIt != playerHandler.players.end() ? ownerIt->second.get() : nullptr;
        tilemap.tilemap[id] = std::move(tile);
    }

    // Military road ring (TD etap-2): restored verbatim, not regenerated —
    // reapply Tile::isMilitaryRoad from the saved tile lists.
    int militaryRouteCount = 0;
    in >> tag >> militaryRouteCount;
    if (tag != "MILROADS")
        return false;

    std::vector<MilitaryRoute> loadedRoutes;
    loadedRoutes.reserve(militaryRouteCount);
    for (int i = 0; i < militaryRouteCount; i++)
    {
        MilitaryRoute route;
        size_t tileCountInRoute = 0;
        in >> tag >> route.playerA >> route.playerB >> tileCountInRoute;
        if (tag != "MROUTE")
            return false;

        route.tiles.reserve(tileCountInRoute);
        for (size_t t = 0; t < tileCountInRoute; t++)
        {
            int tileId = 0;
            in >> tileId;
            if (tileId >= 0 && tileId < static_cast<int>(tilemap.tilemap.size()))
                tilemap.tilemap[tileId].isMilitaryRoad = true;
            route.tiles.push_back(tileId);
        }
        loadedRoutes.push_back(std::move(route));
    }
    militaryRoads.RestoreRoutes(std::move(loadedRoutes));

    std::vector<PendingConnection> pendingConnections;
    int buildingCount = 0;
    in >> tag >> buildingCount;
    if (tag != "BUILDINGS")
        return false;

    for (int i = 0; i < buildingCount; i++)
    {
        int positionId = 0;
        int buildingType = 0;
        int buildingId = 0;
        int ownerId = -1;
        int textureId = 0;
        Vec2i footprint{};
        bool productionBlocked = false;
        double lifetime = 0.0;
        double activeTime = 0.0;
        int totalProduced = 0;
        double transportTime = 0.0;

        in >> tag >> positionId >> buildingType >> buildingId >> ownerId >> textureId
            >> footprint.x >> footprint.y >> productionBlocked >> lifetime >> activeTime
            >> totalProduced >> transportTime;
        if (tag != "B")
            return false;

        auto ownerIt = playerHandler.players.find(ownerId);
        Player* owner = ownerIt != playerHandler.players.end() ? ownerIt->second.get() : nullptr;
        auto building = CreateBuildingFromType(static_cast<BuildingType>(buildingType), buildingId);
        if (building == nullptr || owner == nullptr)
            return false;

        building->textureId = textureId;
        building->footprint = footprint;
        Building* placed = tilemap.PlaceLoadedBuilding(positionId, owner, std::move(building));
        if (placed == nullptr)
            return false;

        placed->id = buildingId;
        placed->textureId = textureId;
        placed->footprint = footprint;
        placed->productionBlocked = productionBlocked;
        placed->lifetime = lifetime;
        placed->activeTime = activeTime;
        placed->totalProduced = totalProduced;
        placed->transportTime = transportTime;

        while (in >> tag)
        {
            if (tag == "ENDB")
                break;

            if (tag == "CONSTRUCTION")
            {
                in >> placed->buildTime >> placed->constructionRemaining;
            }
            else if (tag == "PROD")
            {
                auto* prod = placed->GetComponent<ProductionComponent>();
                auto* workers = placed->GetComponent<WorkerComponent>();
                auto* recipes = placed->GetComponent<RecipeComponent>();
                // May be null — only University has one (see the matching
                // comment in SaveToFile).
                auto* research = placed->GetComponent<ResearchComponent>();
                auto* logistics = placed->GetComponent<LogisticsComponent>();
                if (prod == nullptr || workers == nullptr || recipes == nullptr || logistics == nullptr)
                    return false;

                int tileType = 0;
                in >> tileType >> prod->cycleTime >> prod->elapsed >> prod->started;
                prod->terrainType = static_cast<TileType>(tileType);

                int count = 0;
                in >> tag >> count;
                if (version >= 5 && tag == "WORKERS")
                {
                    // The generic prefetch above already consumed the first of
                    // WORKERS' two payload values (capacity) into `count` — a
                    // pre-existing bug used to re-read it as a *second* value,
                    // shifting every field after it by one token (silently
                    // corrupting RECIPE/RESEARCH/INGREDIENTS parsing for any
                    // production building, never caught before because no test
                    // exercised a full save/load round trip with one).
                    workers->capacity = count;
                    in >> workers->assigned;
                    in >> tag >> count;
                }
                if (version >= 12 && tag == "RECIPE")
                {
                    recipes->SetActiveRecipe(count, *placed, *prod, *logistics, *workers);
                    in >> tag;
                }
                if (version >= 12 && tag == "RESEARCH")
                {
                    std::string technologyId;
                    double remaining = 0.0;
                    double total = 0.0;
                    in >> std::quoted(technologyId) >> remaining >> total;
                    if (research != nullptr)
                    {
                        research->technologyId = technologyId;
                        research->remaining = remaining;
                        research->total = total;
                    }
                    in >> tag >> count;
                }
                if (tag != "INGREDIENTS")
                    return false;
                prod->ingredients.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0, amount = 0;
                    in >> tag >> type >> amount;
                    if (tag != "ING") return false;
                    prod->ingredients[static_cast<ResourceType>(type)] = amount;
                }

                in >> tag >> count;
                if (tag != "PRODUCTS") return false;
                prod->products.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0, amount = 0;
                    in >> tag >> type >> amount;
                    if (tag != "PRODUCT") return false;
                    prod->products[static_cast<ResourceType>(type)] = amount;
                }

                in >> tag >> count;
                if (tag != "INPUTS") return false;
                prod->inputBuffers.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0, capacity = 0, amount = 0;
                    in >> tag >> type >> capacity >> amount;
                    if (tag != "INPUT") return false;
                    ResourceBuffer buffer{static_cast<ResourceType>(type), capacity};
                    LoadResourceBuffer(buffer, static_cast<ResourceType>(type), capacity, amount);
                    prod->inputBuffers[static_cast<ResourceType>(type)] = std::move(buffer);
                }

                in >> tag >> count;
                if (tag != "OUTPUTS") return false;
                prod->outputBuffers.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0, capacity = 0, amount = 0;
                    in >> tag >> type >> capacity >> amount;
                    if (tag != "OUTPUT") return false;
                    ResourceBuffer buffer{static_cast<ResourceType>(type), capacity};
                    LoadResourceBuffer(buffer, static_cast<ResourceType>(type), capacity, amount);
                    prod->outputBuffers[static_cast<ResourceType>(type)] = std::move(buffer);
                }

                in >> tag >> count;
                if (tag != "SUPPLIERS") return false;
                logistics->suppliers.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0, target = -1;
                    in >> tag >> type >> target;
                    if (tag != "SUP") return false;
                    pendingConnections.push_back({positionId, static_cast<ResourceType>(type), target, false, false});
                }

                in >> tag >> count;
                if (tag != "RECEIVERS") return false;
                logistics->receivers.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0, target = -1;
                    in >> tag >> type >> target;
                    if (tag != "REC") return false;
                    pendingConnections.push_back({positionId, static_cast<ResourceType>(type), target, true, false});
                }

                in >> tag;
                if (version >= 13 && tag == "ALT_RECEIVERS")
                {
                    int altCount = 0;
                    in >> altCount;
                    logistics->altReceivers.clear();
                    for (int n = 0; n < altCount; n++)
                    {
                        int type = 0, target = -1;
                        in >> tag >> type >> target;
                        if (tag != "ALTREC") return false;
                        pendingConnections.push_back({positionId, static_cast<ResourceType>(type), target, true, true});
                    }
                    in >> tag;
                }
                if (tag != "ENDPROD") return false;
            }
            else if (tag == "STOR")
            {
                auto* storage = placed->GetComponent<StorageComponent>();
                if (storage == nullptr) return false;

                int count = 0;
                in >> count;
                storage->buffers.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0, capacity = 0, amount = 0;
                    in >> tag >> type >> capacity >> amount;
                    if (tag != "BUF") return false;
                    ResourceBuffer buffer{static_cast<ResourceType>(type), capacity};
                    LoadResourceBuffer(buffer, static_cast<ResourceType>(type), capacity, amount);
                    storage->buffers[static_cast<ResourceType>(type)] = std::move(buffer);
                }

                in >> tag;
                if (tag != "ENDSTOR") return false;
            }
            else if (tag == "VIL")
            {
                auto* population = placed->GetComponent<PopulationComponent>();
                if (population == nullptr) return false;
                auto& pop = *population;
                in >> pop.manpowerRate >> pop.upkeepTimer >> pop.upkeepInterval
                   >> pop.foodPackageUpkeep >> pop.hasFood;
                if (version >= 5)
                    in >> pop.populationCap;
                if (version >= 8)
                {
                    int foodSupplyAmount = 0;
                    in >> pop.foodSupplyLevel >> pop.foodBuffer.bufferSize >> foodSupplyAmount;
                    pop.foodBuffer.Clear();
                    pop.foodBuffer = ResourceBuffer{ResourceType::FOOD_PROVISIONS, pop.foodBuffer.bufferSize};
                    pop.foodBuffer.SetStoredAmount(foodSupplyAmount);
                    pop.hasFood = pop.foodSupplyLevel > 0.0;
                }
            }
            else if (tag == "RECRUIT")
            {
                auto* recruitment = placed->GetComponent<RecruitmentComponent>();
                if (recruitment == nullptr) return false;

                int count = 0;
                in >> count;
                recruitment->queue.clear();
                for (int n = 0; n < count; n++)
                {
                    std::string unitDefId;
                    double total = 0.0, remaining = 0.0;
                    int resourcesReady = 1;
                    in >> tag >> std::quoted(unitDefId) >> total >> remaining >> resourcesReady;
                    if (tag != "RQ") return false;
                    recruitment->queue.push_back(RecruitmentQueueEntry{unitDefId, total, remaining, resourcesReady != 0});
                }
            }
            else if (tag == "HQ")
            {
                auto* hq = placed->GetComponent<HqComponent>();
                if (hq == nullptr) return false;
                in >> hq->maxHp >> hq->currentHp >> hq->hardDefense >> hq->thornsDamage >>
                      hq->thornsInterval >> hq->thornsTimer >> hq->captureStockFraction >>
                      hq->conquestRampDuration;
            }
            else if (tag == "TOWER")
            {
                auto* tower = placed->GetComponent<TowerCombatComponent>();
                if (tower == nullptr) return false;
                int ammoResource = 0;
                int targetMode = 0;
                in >> tower->damage >> tower->range >> tower->attackSpeed >> tower->attackTimer >>
                      ammoResource >> tower->ammoPerShot >> targetMode;
                tower->ammoResource = static_cast<ResourceType>(ammoResource);
                if (targetMode < static_cast<int>(TowerTargetMode::NearestToHq) ||
                    targetMode > static_cast<int>(TowerTargetMode::StrongestUnit))
                    return false;
                tower->targetMode = static_cast<TowerTargetMode>(targetMode);
            }
            else if (tag == "UPG")
            {
                auto* upgrade = placed->GetComponent<UpgradeComponent>();
                if (upgrade == nullptr) return false;
                int isUpgrading = 0;
                in >> upgrade->level >> isUpgrading >> upgrade->upgradeRemaining;
                upgrade->isUpgrading = isUpgrading != 0;
                // Modifiers themselves aren't persisted (same as tech/focus) —
                // re-derive them from the loaded level right away; owner and
                // positionId are already valid at this point.
                if (upgrade->level > 1 && placed->owner != nullptr)
                    placed->owner->ApplyUpgradeLevelModifiers(*placed);
            }
            else
            {
                return false;
            }
        }
    }

    for (auto& [id, player] : playerHandler.players)
    {
        player->roadNetwork = std::make_unique<RoadNetwork>(tilemap);

        for (auto& tile : tilemap.tilemap)
        {
            if (tile.building != nullptr && tile.building->owner == player.get() && !tile.building->IsUnderConstruction())
                player->roadNetwork->UpdateNavMap(tile.id, tile.building.get());
        }
    }

    for (auto& tile : tilemap.tilemap)
    {
        if (tile.building != nullptr && tile.building->buildingType == BuildingType::Road)
            tilemap.RefreshRoadTilesAround(tilemap.GetCoordsFromId(tile.id));
    }
    tilemap.terrainDirty = true;
    tilemap.buildingsDirty = true;

    for (const auto& pending : pendingConnections)
    {
        Building* source = tilemap.GetBuilding(pending.sourcePosition);
        Building* target = pending.targetPosition >= 0 ? tilemap.GetBuilding(pending.targetPosition) : nullptr;
        if (source == nullptr || target == nullptr || source->IsUnderConstruction() || target->IsUnderConstruction())
            continue;

        if (pending.receiver && pending.alternative)
            source->SetAlternativeReceiver(pending.resource, target);
        else if (pending.receiver)
            source->SetReceiver(pending.resource, target);
        else
            source->SetSupplier(pending.resource, target);
    }

    // TD(etap-4): deployed units, world-scoped.
    int deployedCount = 0;
    in >> tag >> deployedCount;
    if (tag != "DEPLOYEDUNITS")
        return false;

    deployedUnits.clear();
    for (int i = 0; i < deployedCount; i++)
    {
        int instanceId = 0;
        int ownerPlayerId = 0;
        std::string unitDefId;
        double currentHp = 0.0;
        int state = 0;
        int routeFromPlayerId = -1;
        int routeToPlayerId = -1;
        int tileIndex = 0;
        double tileProgress = 0.0;
        double attackTimer = 0.0;
        size_t equipmentCount = 0;
        in >> tag >> instanceId >> ownerPlayerId >> std::quoted(unitDefId) >> currentHp
           >> state >> routeFromPlayerId >> routeToPlayerId >> tileIndex >> tileProgress
           >> attackTimer >> equipmentCount;
        if (tag != "DUNIT")
            return false;

        BattleUnit unit(instanceId, ownerPlayerId, unitDefId);
        unit.currentHp = currentHp;
        unit.state = static_cast<BattleUnitState>(state);
        unit.routeFromPlayerId = routeFromPlayerId;
        unit.routeToPlayerId = routeToPlayerId;
        unit.tileIndex = tileIndex;
        unit.tileProgress = tileProgress;
        unit.attackTimer = attackTimer;
        deployedUnits[instanceId] = std::move(unit);
    }

    int spawnQueueCount = 0;
    in >> tag >> spawnQueueCount;
    if (tag != "SPAWNQUEUES")
        return false;

    spawnQueues.clear();
    for (int i = 0; i < spawnQueueCount; i++)
    {
        int fromPlayerId = 0, toPlayerId = 0;
        size_t queueLength = 0;
        in >> tag >> fromPlayerId >> toPlayerId >> queueLength;
        if (tag != "SQ")
            return false;

        std::deque<int> queue;
        for (size_t q = 0; q < queueLength; q++)
        {
            int unitInstanceId = 0;
            in >> unitInstanceId;
            queue.push_back(unitInstanceId);
        }
        spawnQueues[{fromPlayerId, toPlayerId}] = std::move(queue);
    }

    return true;
}

