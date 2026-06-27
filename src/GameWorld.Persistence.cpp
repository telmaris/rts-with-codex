#include "../inc/GameWorldInternal.h"

using namespace GameWorldInternal;

// Serializes current runtime state.
bool GameWorld::SaveToFile(const std::string& path) const
{
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open())
        return false;

    out << "RTS_SAVE 12\n";
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
        out << "PLAYER " << id << ' ' << player->strategicResources.values.size() << ' '
            << player->technologies.GetUnlocked().size() << ' '
            << player->focuses.GetUnlocked().size() << '\n';
        for (const auto& [type, value] : player->strategicResources.values)
            out << "STRAT " << static_cast<int>(type) << ' ' << value << '\n';
        for (const auto& techId : player->technologies.GetUnlocked())
            out << "TECH " << std::quoted(techId) << '\n';
        for (const auto& focusId : player->focuses.GetUnlocked())
            out << "FOCUS " << std::quoted(focusId) << '\n';
        out << "ENDPLAYER\n";
    }

    out << "TILES " << tilemap.tilemap.size() << '\n';
    for (const auto& tile : tilemap.tilemap)
    {
        int ownerId = tile.owner != nullptr ? tile.owner->id : -1;
        out << "T " << tile.id << ' ' << static_cast<int>(tile.tileType) << ' '
            << tile.terrainTextureId << ' ' << ownerId << ' ' << tile.resourceRichness << '\n';
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

        if (const auto* production = dynamic_cast<const ProductionBuilding*>(building))
        {
            out << "PROD " << static_cast<int>(production->type) << ' '
                << production->productionTime.GetBase() << ' ' << production->elapsedTime << ' '
                << production->productionStarted << '\n';
            out << "WORKERS " << production->workerCapacity.GetBase() << ' ' << production->assignedWorkers << '\n';
            out << "RECIPE " << production->GetActiveRecipeIndex() << '\n';
            out << "RESEARCH " << std::quoted(production->GetActiveTechnologyId()) << ' '
                << production->GetActiveTechnologyRemaining() << ' '
                << production->GetActiveTechnologyTotal() << '\n';

            out << "INGREDIENTS " << production->ingredients.size() << '\n';
            for (const auto& [type, amount] : production->ingredients)
                out << "ING " << static_cast<int>(type) << ' ' << amount << '\n';

            out << "PRODUCTS " << production->products.size() << '\n';
            for (const auto& [type, amount] : production->products)
                out << "PRODUCT " << static_cast<int>(type) << ' ' << amount << '\n';

            out << "INPUTS " << production->inputBuffers.size() << '\n';
            for (const auto& [type, buffer] : production->inputBuffers)
                SaveResourceBuffer(out, "INPUT", buffer);

            out << "OUTPUTS " << production->outputBuffers.size() << '\n';
            for (const auto& [type, buffer] : production->outputBuffers)
                SaveResourceBuffer(out, "OUTPUT", buffer);

            int supplierCount = 0;
            for (const auto& [type, suppliers] : production->suppliersMap)
            {
                for (const auto* supplier : suppliers)
                {
                    if (supplier != nullptr)
                        supplierCount++;
                }
            }

            out << "SUPPLIERS " << supplierCount << '\n';
            for (const auto& [type, suppliers] : production->suppliersMap)
            {
                for (const auto* supplier : suppliers)
                {
                    if (supplier != nullptr)
                        out << "SUP " << static_cast<int>(type) << ' ' << supplier->positionId << '\n';
                }
            }

            out << "RECEIVERS " << production->receiversMap.size() << '\n';
            for (const auto& [type, receiver] : production->receiversMap)
                out << "REC " << static_cast<int>(type) << ' ' << (receiver != nullptr ? receiver->positionId : -1) << '\n';

            out << "ENDPROD\n";
        }

        if (const auto* storage = dynamic_cast<const StorageBuilding*>(building))
        {
            out << "STOR " << storage->resourceBuffers.size() << '\n';
            for (const auto& [type, buffer] : storage->resourceBuffers)
                SaveResourceBuffer(out, "BUF", buffer);
            out << "ENDSTOR\n";
        }

        if (const auto* headquarters = dynamic_cast<const Headquarters*>(building))
        {
            out << "HQ " << headquarters->territoryRadius.GetBase() << ' ' << headquarters->hitPoints << ' '
                << headquarters->maxHitPoints.GetBase() << '\n';
        }

        if (const auto* village = dynamic_cast<const Village*>(building))
        {
            out << "VIL " << village->manpowerRate.GetBase() << ' ' << village->upkeepTimer << ' '
                << village->upkeepInterval << ' ' << village->foodPackageUpkeep << ' '
                << village->hasFood << ' ' << village->populationCap.GetBase() << ' '
                << village->foodSupplyLevel << ' ' << village->foodSupplyBuffer.bufferSize << ' '
                << village->foodSupplyBuffer.buffer.size() << '\n';
        }

        if (const auto* military = dynamic_cast<const MilitaryBuilding*>(building))
        {
            int supplyAmount = static_cast<int>(military->supplyBuffer.buffer.size());
            out << "MIL " << military->territoryRadius.GetBase() << ' ' << military->hitPoints << ' '
                << military->maxHitPoints.GetBase() << ' ' << military->combatStrength.GetBase() << ' '
                << military->garrison << ' ' << military->garrisonCapacity.GetBase() << ' '
                << supplyAmount << ' ' << military->supplyCapacity.GetBase() << ' '
                << military->militia << ' ' << military->swordsmen << ' '
                << military->archers << ' ' << static_cast<int>(military->currentOrder) << ' '
                << military->orderTargetPositionId << ' ' << military->orderCooldown << '\n';
            out << "DIVS " << military->nextDivisionId << ' ' << military->divisions.size() << '\n';
            for (const auto& division : military->divisions)
            {
                out << "DIV " << division.id << ' ' << static_cast<int>(division.type) << ' '
                    << division.manpowerScale << ' ' << division.maxHealth << ' ' << division.health << ' '
                    << division.endurance << ' ' << division.strength << ' ' << division.morale << ' '
                    << division.experience << ' ' << static_cast<int>(division.equipment.weapon) << ' '
                    << static_cast<int>(division.equipment.armor) << ' '
                    << static_cast<int>(division.equipment.rangedWeapon) << ' '
                    << static_cast<int>(division.equipment.ammo) << ' '
                    << division.foodSupply << ' ' << division.foodSupplyCapacity << ' '
                    << division.weaponSupply << ' ' << division.weaponSupplyCapacity << ' '
                    << division.speedTilesPerMinute << ' '
                    << static_cast<int>(division.currentOrder) << ' '
                    << division.orderTargetPositionId << ' ' << division.orderCooldown << '\n';
            }
            out << "ENDDIVS\n";
        }

        if (const auto* barracks = dynamic_cast<const Barracks*>(building))
        {
            out << "RECRUIT " << barracks->recruitmentQueue.size() << '\n';
            for (const auto& job : barracks->recruitmentQueue)
                out << "JOB " << static_cast<int>(job.type) << ' ' << job.remaining << '\n';
            out << "ENDRECRUIT\n";
        }

        out << "ENDB\n";
    }

    return true;
}

// Loads the requested data into runtime state.
bool GameWorld::LoadFromFile(const std::string& path, Renderer* renderer)
{
    std::ifstream in(path);
    if (!in.is_open())
        return false;

    std::string tag;
    int version = 0;
    in >> tag >> version;
    if (tag != "RTS_SAVE" || (version < 1 || version > 12))
        return false;

    render = renderer;

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
        in >> tag >> render->camera.target.x >> render->camera.target.y
            >> render->camera.zoom >> render->camera.rotation;
        if (tag != "CAMERA")
            return false;
        render->ClampCameraToMap({tilemap.params.sizeX, tilemap.params.sizeY});
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
        in >> tag >> playerId >> strategicCount;
        if (tag != "PLAYER")
            return false;
        if (version >= 7)
            in >> technologyCount;
        if (version >= 11)
            in >> focusCount;

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
        player->RefreshTechnologyModifiers();

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
        auto ownerIt = playerHandler.players.find(ownerId);
        tile.owner = ownerIt != playerHandler.players.end() ? ownerIt->second.get() : nullptr;
        tilemap.tilemap[id] = std::move(tile);
    }

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
                auto* production = dynamic_cast<ProductionBuilding*>(placed);
                if (production == nullptr)
                    return false;

                int tileType = 0;
                in >> tileType >> production->productionTime >> production->elapsedTime >> production->productionStarted;
                production->type = static_cast<TileType>(tileType);

                int count = 0;
                in >> tag >> count;
                if (version >= 5 && tag == "WORKERS")
                {
                    in >> production->workerCapacity >> production->assignedWorkers;
                    in >> tag >> count;
                }
                if (version >= 12 && tag == "RECIPE")
                {
                    production->SetActiveRecipe(count);
                    in >> tag;
                }
                if (version >= 12 && tag == "RESEARCH")
                {
                    in >> std::quoted(production->activeTechnologyId)
                       >> production->activeTechnologyRemaining
                       >> production->activeTechnologyTotal;
                    in >> tag >> count;
                }
                if (tag != "INGREDIENTS")
                    return false;
                production->ingredients.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0;
                    int amount = 0;
                    in >> tag >> type >> amount;
                    if (tag != "ING")
                        return false;
                    production->ingredients[static_cast<ResourceType>(type)] = amount;
                }

                in >> tag >> count;
                if (tag != "PRODUCTS")
                    return false;
                production->products.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0;
                    int amount = 0;
                    in >> tag >> type >> amount;
                    if (tag != "PRODUCT")
                        return false;
                    production->products[static_cast<ResourceType>(type)] = amount;
                }

                in >> tag >> count;
                if (tag != "INPUTS")
                    return false;
                production->inputBuffers.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0;
                    int capacity = 0;
                    int amount = 0;
                    in >> tag >> type >> capacity >> amount;
                    if (tag != "INPUT")
                        return false;
                    ResourceBuffer buffer{static_cast<ResourceType>(type), capacity};
                    LoadResourceBuffer(buffer, static_cast<ResourceType>(type), capacity, amount);
                    production->inputBuffers[static_cast<ResourceType>(type)] = std::move(buffer);
                }

                in >> tag >> count;
                if (tag != "OUTPUTS")
                    return false;
                production->outputBuffers.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0;
                    int capacity = 0;
                    int amount = 0;
                    in >> tag >> type >> capacity >> amount;
                    if (tag != "OUTPUT")
                        return false;
                    ResourceBuffer buffer{static_cast<ResourceType>(type), capacity};
                    LoadResourceBuffer(buffer, static_cast<ResourceType>(type), capacity, amount);
                    production->outputBuffers[static_cast<ResourceType>(type)] = std::move(buffer);
                }

                in >> tag >> count;
                if (tag != "SUPPLIERS")
                    return false;
                production->suppliersMap.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0;
                    int target = -1;
                    in >> tag >> type >> target;
                    if (tag != "SUP")
                        return false;
                    pendingConnections.push_back({positionId, static_cast<ResourceType>(type), target, false});
                }

                in >> tag >> count;
                if (tag != "RECEIVERS")
                    return false;
                production->receiversMap.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0;
                    int target = -1;
                    in >> tag >> type >> target;
                    if (tag != "REC")
                        return false;
                    pendingConnections.push_back({positionId, static_cast<ResourceType>(type), target, true});
                }

                in >> tag;
                if (tag != "ENDPROD")
                    return false;
            }
            else if (tag == "STOR")
            {
                auto* storage = dynamic_cast<StorageBuilding*>(placed);
                if (storage == nullptr)
                    return false;

                int count = 0;
                in >> count;
                storage->resourceBuffers.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0;
                    int capacity = 0;
                    int amount = 0;
                    in >> tag >> type >> capacity >> amount;
                    if (tag != "BUF")
                        return false;
                    ResourceBuffer buffer{static_cast<ResourceType>(type), capacity};
                    LoadResourceBuffer(buffer, static_cast<ResourceType>(type), capacity, amount);
                    storage->resourceBuffers[static_cast<ResourceType>(type)] = std::move(buffer);
                }

                in >> tag;
                if (tag != "ENDSTOR")
                    return false;
            }
            else if (tag == "HQ")
            {
                auto* headquarters = dynamic_cast<Headquarters*>(placed);
                if (headquarters == nullptr)
                    return false;
                in >> headquarters->territoryRadius >> headquarters->hitPoints >> headquarters->maxHitPoints;
            }
            else if (tag == "VIL")
            {
                auto* village = dynamic_cast<Village*>(placed);
                if (village == nullptr)
                    return false;
                in >> village->manpowerRate >> village->upkeepTimer >> village->upkeepInterval
                    >> village->foodPackageUpkeep >> village->hasFood;
                if (version >= 5)
                    in >> village->populationCap;
                if (version >= 8)
                {
                    int foodSupplyAmount = 0;
                    in >> village->foodSupplyLevel >> village->foodSupplyBuffer.bufferSize >> foodSupplyAmount;
                    village->foodSupplyBuffer.Clear();
                    village->foodSupplyBuffer = ResourceBuffer{ResourceType::FOOD_PROVISIONS, village->foodSupplyBuffer.bufferSize};
                    village->foodSupplyBuffer.SetStoredAmount(foodSupplyAmount);
                    village->hasFood = village->foodSupplyLevel > 0.0;
                }
            }
            else if (tag == "MIL")
            {
                auto* military = dynamic_cast<MilitaryBuilding*>(placed);
                if (military == nullptr)
                    return false;
                in >> military->territoryRadius >> military->hitPoints >> military->maxHitPoints
                    >> military->combatStrength >> military->garrison >> military->garrisonCapacity
                    >> military->supply >> military->supplyCapacity;
                military->supplyBuffer.Clear();
                military->supplyBuffer = ResourceBuffer{ResourceType::FOOD_PROVISIONS, military->supplyCapacity.GetBase()};
                military->supplyBuffer.SetStoredAmount(military->supply);
                military->supply = static_cast<int>(military->supplyBuffer.buffer.size());
                if (version >= 6)
                {
                    int order = 0;
                    in >> military->militia >> military->swordsmen >> military->archers
                       >> order >> military->orderTargetPositionId >> military->orderCooldown;
                    military->currentOrder = static_cast<MilitaryOrderType>(order);
                }
            }
            else if (tag == "DIVS")
            {
                auto* military = dynamic_cast<MilitaryBuilding*>(placed);
                if (military == nullptr || version < 8)
                    return false;

                int count = 0;
                in >> military->nextDivisionId >> count;
                military->divisions.clear();
                for (int n = 0; n < count; n++)
                {
                    int unitType = 0;
                    int weapon = static_cast<int>(ResourceType::Null);
                    int armor = static_cast<int>(ResourceType::Null);
                    int ranged = static_cast<int>(ResourceType::Null);
                    int ammo = static_cast<int>(ResourceType::Null);
                    int order = 0;
                    MilitaryDivision division;
                    in >> tag >> division.id >> unitType >> division.manpowerScale
                       >> division.maxHealth >> division.health >> division.endurance
                       >> division.strength >> division.morale >> division.experience
                       >> weapon >> armor >> ranged >> ammo;
                    if (tag != "DIV")
                        return false;
                    division.type = static_cast<MilitaryUnitType>(unitType);
                    division.equipment.weapon = static_cast<ResourceType>(weapon);
                    division.equipment.armor = static_cast<ResourceType>(armor);
                    division.equipment.rangedWeapon = static_cast<ResourceType>(ranged);
                    division.equipment.ammo = static_cast<ResourceType>(ammo);
                    if (version >= 9)
                    {
                        in >> division.foodSupply >> division.foodSupplyCapacity
                           >> division.weaponSupply >> division.weaponSupplyCapacity;
                        if (version >= 10)
                            in >> division.speedTilesPerMinute;
                        in >> order >> division.orderTargetPositionId >> division.orderCooldown;
                        division.currentOrder = static_cast<MilitaryOrderType>(order);
                    }
                    else
                    {
                        division.foodSupplyCapacity = division.manpowerScale;
                        division.foodSupply = division.manpowerScale;
                        division.weaponSupplyCapacity = division.manpowerScale;
                        division.weaponSupply = division.manpowerScale;
                    }
                    military->divisions.push_back(division);
                }

                in >> tag;
                if (tag != "ENDDIVS")
                    return false;
                military->militia = military->swordsmen = military->archers = 0;
                for (const auto& division : military->divisions)
                {
                    switch (division.type)
                    {
                        case MilitaryUnitType::Swordsman: military->swordsmen++; break;
                        case MilitaryUnitType::Archer: military->archers++; break;
                        case MilitaryUnitType::Militia:
                        default: military->militia++; break;
                    }
                }
                military->garrison = military->GetTotalTroops();
            }
            else if (tag == "RECRUIT")
            {
                auto* barracks = dynamic_cast<Barracks*>(placed);
                if (barracks == nullptr || version < 6)
                    return false;

                int count = 0;
                in >> count;
                barracks->recruitmentQueue.clear();
                for (int n = 0; n < count; n++)
                {
                    int unitType = 0;
                    double remaining = 0.0;
                    in >> tag >> unitType >> remaining;
                    if (tag != "JOB")
                        return false;
                    barracks->recruitmentQueue.push_back({static_cast<MilitaryUnitType>(unitType), remaining});
                }

                in >> tag;
                if (tag != "ENDRECRUIT")
                    return false;
            }
            else
            {
                return false;
            }
        }
    }

    for (auto& [id, player] : playerHandler.players)
    {
        tilemap.RecalculateTerritory(player.get());
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

        if (pending.receiver)
            source->SetReceiver(pending.resource, target);
        else
            source->SetSupplier(pending.resource, target);
    }

    return true;
}

