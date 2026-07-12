#include "core/GameWorldInternal.h"

using namespace GameWorldInternal;

// Serializes current runtime state.
bool GameWorld::SaveToFile(const std::string& path) const
{
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open())
        return false;

    out << "RTS_SAVE 22\n";
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
        int diploCount = 0;
        for (const auto& [otherId, rel] : player->diplomatic.relations)
            if (rel != DiplomaticRelation::Neutral) diploCount++;
        int warCount = static_cast<int>(player->diplomatic.wars.size());
        out << "PLAYER " << id << ' ' << player->strategicResources.values.size() << ' '
            << player->technologies.GetUnlocked().size() << ' '
            << player->focuses.GetUnlocked().size() << ' '
            << diploCount << ' ' << warCount << '\n';
        for (const auto& [type, value] : player->strategicResources.values)
            out << "STRAT " << static_cast<int>(type) << ' ' << value << '\n';
        for (const auto& techId : player->technologies.GetUnlocked())
            out << "TECH " << std::quoted(techId) << '\n';
        for (const auto& focusId : player->focuses.GetUnlocked())
            out << "FOCUS " << std::quoted(focusId) << '\n';
        for (const auto& [otherId, rel] : player->diplomatic.relations)
            if (rel != DiplomaticRelation::Neutral)
                out << "DIPLO " << otherId << ' ' << static_cast<int>(rel) << '\n';
        for (const auto& w : player->diplomatic.wars)
            out << "WAR " << w.id << ' ' << w.attackerId << ' ' << w.defenderId << ' '
                << w.startTime << ' ' << static_cast<int>(w.active) << '\n';

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
            const auto* research = building->GetComponent<ResearchComponent>();
            const auto* logistics = building->GetComponent<LogisticsComponent>();
            if (workers == nullptr || recipes == nullptr || research == nullptr || logistics == nullptr)
                return false;

            out << "PROD " << static_cast<int>(prod->terrainType) << ' '
                << prod->cycleTime.GetBase() << ' ' << prod->elapsed << ' '
                << prod->started << '\n';
            out << "WORKERS " << workers->capacity.GetBase() << ' ' << workers->assigned << '\n';
            out << "RECIPE " << recipes->activeRecipeIndex << '\n';
            out << "RESEARCH " << std::quoted(research->technologyId) << ' '
                << research->remaining << ' '
                << research->total << '\n';

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
                out << "RQ " << std::quoted(entry.unitDefId) << ' ' << entry.total << ' ' << entry.remaining << '\n';
        }

        out << "ENDB\n";
    }

    return true;
}

// Loads the requested data into runtime state.
bool GameWorld::LoadFromFile(const std::string& path, Renderer* renderer, AudioSystem* a)
{
    std::ifstream in(path);
    if (!in.is_open())
        return false;

    std::string tag;
    int version = 0;
    in >> tag >> version;
    // TD(etap-1): the old war system's save fields (HQ/MIL/DIVS/RECRUIT) were
    // dropped, not merely extended — a breaking change per the rework plan.
    // Older saves are rejected outright rather than partially parsed.
    if (tag != "RTS_SAVE" || version != 22)
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
        int diploCount = 0;
        int warCount = 0;
        in >> tag >> playerId >> strategicCount;
        if (tag != "PLAYER")
            return false;
        if (version >= 7)
            in >> technologyCount;
        if (version >= 11)
            in >> focusCount;
        if (version >= 14)
            in >> diploCount;
        if (version >= 15)
            in >> warCount;

        auto player = std::make_unique<Player>(playerId, tilemap);
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
        for (int d = 0; d < diploCount; d++)
        {
            int otherId = 0;
            int rel = 0;
            in >> tag >> otherId >> rel;
            if (tag != "DIPLO")
                return false;
            player->diplomatic.SetRelation(otherId, static_cast<DiplomaticRelation>(rel));
        }
        for (int w = 0; w < warCount; w++)
        {
            int wid = 0, atk = 0, def = 0, active = 0;
            double startTime = 0.0;
            in >> tag >> wid >> atk >> def >> startTime >> active;
            if (tag != "WAR")
                return false;
            player->diplomatic.wars.push_back({wid, atk, def, startTime, active != 0});
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
                auto* research = placed->GetComponent<ResearchComponent>();
                auto* logistics = placed->GetComponent<LogisticsComponent>();
                if (prod == nullptr || workers == nullptr || recipes == nullptr ||
                    research == nullptr || logistics == nullptr)
                    return false;

                int tileType = 0;
                in >> tileType >> prod->cycleTime >> prod->elapsed >> prod->started;
                prod->terrainType = static_cast<TileType>(tileType);

                int count = 0;
                in >> tag >> count;
                if (version >= 5 && tag == "WORKERS")
                {
                    in >> workers->capacity >> workers->assigned;
                    in >> tag >> count;
                }
                if (version >= 12 && tag == "RECIPE")
                {
                    recipes->SetActiveRecipe(count, *placed, *prod, *logistics, *workers);
                    in >> tag;
                }
                if (version >= 12 && tag == "RESEARCH")
                {
                    in >> std::quoted(research->technologyId)
                       >> research->remaining
                       >> research->total;
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
                    in >> tag >> std::quoted(unitDefId) >> total >> remaining;
                    if (tag != "RQ") return false;
                    recruitment->queue.push_back(RecruitmentQueueEntry{unitDefId, total, remaining});
                }
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
    tilemap.territoryDirty = true;

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

    return true;
}

