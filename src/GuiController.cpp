#include "../inc/Scenes.h"
#include "../inc/BuildingConfig.h"
#include "../inc/ResearchCatalog.h"
#include "../inc/Player.h"
#include "../inc/DivisionSector.h"
#include "../inc/SectorGraph.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>

namespace
{
    // Returns the local player controlled by this client.
    Player* LocalPlayer(GameScene* scene)
    {
        if (scene == nullptr || scene->game == nullptr)
            return nullptr;

        auto it = scene->game->playerHandler.players.find(scene->game->GetLocalPlayerId());
        return it != scene->game->playerHandler.players.end() ? it->second.get() : nullptr;
    }

    bool HasNodeTag(const ResearchNodeView& node, const std::string& tag)
    {
        return tag.empty() || std::find(node.tags.begin(), node.tags.end(), tag) != node.tags.end();
    }

    std::vector<std::string> CollectVisibleTags(const std::vector<ResearchNodeView>& nodes)
    {
        std::vector<std::string> tags;
        for (const auto& node : nodes)
        {
            for (const auto& tag : node.tags)
            {
                if (std::find(tags.begin(), tags.end(), tag) == tags.end())
                    tags.push_back(tag);
            }
        }
        std::sort(tags.begin(), tags.end());
        if (tags.size() > 10)
            tags.resize(10);
        return tags;
    }

    void DrawTagFilterBar(Rectangle bounds, const std::vector<std::string>& tags, std::string& selectedTag)
    {
        Vector2 mouse = GetMousePosition();
        float x = bounds.x;
        auto drawButton = [&](const std::string& label, const std::string& value)
        {
            float width = std::min(112.0f, std::max(54.0f, static_cast<float>(MeasureText(label.c_str(), 14) + 22)));
            Rectangle rect{x, bounds.y, width, bounds.height};
            bool selected = selectedTag == value;
            bool hover = CheckCollisionPointRec(mouse, rect);
            DrawRectangleRounded(rect, 0.20f, 6, selected ? Color{64, 94, 128, 235} : hover ? Color{45, 55, 69, 235} : Color{31, 37, 47, 220});
            DrawRectangleRoundedLines(rect, 0.20f, 6, 1.0f, selected ? Color{140, 185, 240, 255} : Color{82, 96, 116, 230});
            UiText::DrawFit(label, Rectangle{rect.x + 8.0f, rect.y + 4.0f, rect.width - 16.0f, rect.height - 8.0f}, 14, selected ? RAYWHITE : Color{188, 198, 212, 255});
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                selectedTag = value;
            x += width + 8.0f;
            return x < bounds.x + bounds.width - 44.0f;
        };

        if (!drawButton("All", ""))
            return;
        for (const auto& tag : tags)
            if (!drawButton(tag, tag))
                break;
    }

    std::vector<std::string> WrapUiText(const std::string& text, int fontSize, float maxWidth)
    {
        std::vector<std::string> wrapped;
        std::istringstream words(text);
        std::string word;
        std::string line;
        while (words >> word)
        {
            std::string candidate = line.empty() ? word : line + " " + word;
            if (UiText::Measure(candidate, fontSize) <= maxWidth || line.empty())
            {
                line = candidate;
                continue;
            }
            wrapped.push_back(line);
            line = word;
        }
        if (!line.empty())
            wrapped.push_back(line);
        if (wrapped.empty())
            wrapped.push_back("");
        return wrapped;
    }

    void DrawUiTextWrappedCentered(const std::string& text, Rectangle bounds, int fontSize, Color color, int maxLines = 2)
    {
        std::vector<std::string> lines = WrapUiText(text, fontSize, bounds.width);
        if (static_cast<int>(lines.size()) > maxLines)
        {
            lines.resize(maxLines);
            while (!lines.back().empty() && UiText::Measure(lines.back() + "...", fontSize) > bounds.width)
                lines.back().pop_back();
            lines.back() += "...";
        }

        float lineH = static_cast<float>(fontSize) + 2.0f;
        float totalH = lineH * static_cast<float>(lines.size());
        float y = bounds.y + (bounds.height - totalH) * 0.5f;
        for (const auto& line : lines)
        {
            int measured = UiText::Measure(line, fontSize);
            UiText::Draw(line, bounds.x + (bounds.width - measured) * 0.5f, y, fontSize, color);
            y += lineH;
        }
    }

    const char* ResourceChipShortName(ResourceType type)
    {
        switch (type)
        {
            case ResourceType::WOOD: return "Wood";
            case ResourceType::PLANKS: return "Plank";
            case ResourceType::COAL: return "Coal";
            case ResourceType::IRON_ORE: return "Ore";
            case ResourceType::IRON: return "Iron";
            case ResourceType::STONE: return "Stone";
            case ResourceType::WHEAT: return "Wheat";
            case ResourceType::FLOUR: return "Flour";
            case ResourceType::BREAD: return "Bread";
            case ResourceType::MEAT: return "Meat";
            case ResourceType::WATER: return "Water";
            case ResourceType::BEER: return "Beer";
            case ResourceType::PAPER: return "Paper";
            case ResourceType::TOOLS: return "Tools";
            case ResourceType::FOOD_PROVISIONS: return "Food";
            case ResourceType::WEAPON_SUPPLY: return "Weapon";
            case ResourceType::COPPER_SWORD: return "CuSw";
            case ResourceType::IRON_SWORD: return "FeSw";
            case ResourceType::STEEL_SWORD: return "StSw";
            case ResourceType::BOW: return "Bow";
            case ResourceType::ARROWS: return "Arr";
            default: return "Res";
        }
    }

    // Finds the local player's headquarters building.
    Building* FindLocalHeadquarters(GameScene* scene)
    {
        Player* player = LocalPlayer(scene);
        if (player == nullptr)
            return nullptr;

        for (auto* building : player->GetTrackedBuildings())
        {
            if (building != nullptr && building->owner == player && building->buildingType == BuildingType::Headquarters)
                return building;
        }

        return nullptr;
    }

    // Counts a resource across owned storage-like buildings.
    int CountStoredResource(GameScene* scene, ResourceType type)
    {
        Player* player = LocalPlayer(scene);
        if (player == nullptr)
            return 0;

        int amount = 0;
        for (auto* building : player->GetTrackedBuildingsWithComponent<StorageComponent>())
        {
            auto* storage = building != nullptr ? building->GetComponent<StorageComponent>() : nullptr;
            if (storage == nullptr || building->owner != player)
                continue;

            auto it = storage->buffers.find(type);
            if (it != storage->buffers.end())
                amount += static_cast<int>(it->second.buffer.size());
        }
        return amount;
    }

    // Adds a debug resource package to one storage-like building.
    void GrantResourcesToStorage(StorageComponent* storage, int amount)
    {
        if (storage == nullptr || amount <= 0)
            return;

        for (ResourceType type : resourceTypes)
        {
            auto& buffer = storage->buffers[type];
            if (buffer.type == ResourceType::Null)
                buffer = ResourceBuffer{type, amount};
            buffer.bufferSize = std::max(buffer.bufferSize, static_cast<int>(buffer.buffer.size()) + amount);
            for (int i = 0; i < amount; i++)
                buffer.GenerateResource(type);
        }
    }

    // Grants local debug resources when the current world allows debug helpers.
    void GrantDebugResources(GameScene* scene, int amount)
    {
        if (scene == nullptr || scene->game == nullptr || !scene->game->tilemap.params.debugMode)
            return;

        Building* headquarters = FindLocalHeadquarters(scene);
        auto* storage = headquarters != nullptr ? headquarters->GetComponent<StorageComponent>() : nullptr;
        if (storage == nullptr)
            return;

        GrantResourcesToStorage(storage, amount);
        Log::Msg("[Debug]", "granted ", amount, " of every resource to local HQ");
    }

    struct PlayerStatsSnapshot
    {
        int freeManpower{0};
        int workers{0};
        int soldiers{0};
        ArmyRegistry army;
        int totalPopulation{0};
        int populationCap{0};
        double manpowerGainPerMinute{0.0};
        int foodSupplyPercent{100};
        double villageFoodConsumptionPerMinute{0.0};
        double militaryFoodConsumptionPerMinute{0.0};
        std::map<ResourceType, int> storedResources;
        std::map<ResourceType, int> productionRatesPerMinute;
        std::map<ResourceType, int> consumptionRatesPerMinute;
        int buildingCount{0};
        int roadCount{0};
        int totalProduced{0};
    };

    // Aggregates player-wide strategic stats for HUD and future economy panels.
    PlayerStatsSnapshot BuildPlayerStatsSnapshot(Player* player)
    {
        PlayerStatsSnapshot stats;
        if (player == nullptr)
            return stats;

        stats.freeManpower = static_cast<int>(std::floor(player->strategicResources.Get(StrategicResourceType::Manpower)));
        stats.workers = static_cast<int>(std::floor(player->strategicResources.Get(StrategicResourceType::Workers)));
        stats.army = player->GetArmyRegistry();
        stats.soldiers = stats.army.TotalTroops();
        stats.totalPopulation = static_cast<int>(std::floor(player->GetTotalPopulation()));
        stats.populationCap = player->GetPopulationCap();
        stats.foodSupplyPercent = static_cast<int>(std::round(player->GetFoodProductivity() * 100.0));
        stats.productionRatesPerMinute = player->economyTelemetry.current.productionRatesPerMinute;
        stats.consumptionRatesPerMinute = player->economyTelemetry.current.consumptionRatesPerMinute;

        for (const auto* building : player->GetTrackedBuildings())
        {
            if (building == nullptr || building->owner != player)
                continue;
            if (building->buildingType == BuildingType::Road)
                stats.roadCount++;
            else
                stats.buildingCount++;
        }

        for (const auto* building : player->GetTrackedBuildingsWithComponent<PopulationComponent>())
        {
            if (building == nullptr || building->owner != player || building->IsUnderConstruction())
                continue;

            const auto* population = building->GetComponent<PopulationComponent>();
            double productivity = population->GetManpowerProductivity();
            stats.manpowerGainPerMinute += player->ResolveStat(population->manpowerRate, building) * productivity * 60.0;
            if (population->upkeepInterval > 0.0)
                stats.villageFoodConsumptionPerMinute += population->foodPackageUpkeep * (60.0 / population->upkeepInterval);
        }
        for (const auto* building : player->GetTrackedBuildingsWithComponent<SupplyBufferComponent>())
        {
            if (building == nullptr || building->owner != player || building->IsUnderConstruction())
                continue;

            const auto* supply = building->GetComponent<SupplyBufferComponent>();
            const auto* garrison = building->GetComponent<GarrisonComponent>();
            if (supply != nullptr && garrison != nullptr)
                stats.militaryFoodConsumptionPerMinute += supply->GetSupplyConsumption(*building, *garrison);
        }
        for (const auto* building : player->GetTrackedBuildingsWithComponent<StorageComponent>())
        {
            if (building == nullptr || building->owner != player || building->IsUnderConstruction())
                continue;

            const auto* storage = building->GetComponent<StorageComponent>();
            for (const auto& [type, buffer] : storage->buffers)
                stats.storedResources[type] += static_cast<int>(buffer.buffer.size());
        }
        for (const auto* building : player->GetTrackedBuildingsWithComponent<ProductionComponent>())
        {
            if (building == nullptr || building->owner != player || building->IsUnderConstruction())
                continue;

            stats.totalProduced += building->GetTotalProduced();
        }

        return stats;
    }

    // Formats a compact fixed-point HUD value.
    std::string FormatOneDecimal(double value)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(1) << value;
        return stream.str();
    }

    // Returns a compact label for terrain requirements.
    std::string TileTypeLabel(TileType type)
    {
        switch (type)
        {
            case TileType::WOOD: return "WOOD";
            case TileType::COAL: return "COAL";
            case TileType::IRON_ORE: return "IRON";
            case TileType::STONE: return "STONE";
            case TileType::GRASS:
            default: return "GRASS";
        }
    }

    // Returns terrain types required by a building, if any.
    std::vector<TileType> RequiredTerrainTypes(BuildingType type)
    {
        if (type == BuildingType::Woodcutter)
            return {TileType::WOOD};

        std::vector<TileType> result;
        const auto& definition = GetBuildingDefinition(type);
        for (const auto& terrainProduction : definition.terrainProductions)
        {
            if (std::find(result.begin(), result.end(), terrainProduction.tileType) == result.end())
                result.push_back(terrainProduction.tileType);
        }
        return result;
    }

    // Returns the build panel category for a building type.
    std::string BuildCategory(BuildingType type)
    {
        switch (type)
        {
            case BuildingType::Woodcutter:
            case BuildingType::LumberMill:
            case BuildingType::Paperworks:
                return "WOOD";
            case BuildingType::Mine:
            case BuildingType::Foundry:
            case BuildingType::Smith:
                return "METALS";
            case BuildingType::HuntersHut:
            case BuildingType::Well:
            case BuildingType::WheatFarm:
            case BuildingType::Windmill:
            case BuildingType::Bakery:
            case BuildingType::Inn:
                return "FOOD";
            case BuildingType::Barracks:
            case BuildingType::GuardTower:
            case BuildingType::Fortress:
            case BuildingType::Castle:
            case BuildingType::SupplyHub:
                return "MILITARY";
            case BuildingType::StorageBuilding:
            case BuildingType::Village:
                return "LOGISTICS";
            case BuildingType::University:
                return "SCIENCE";
            case BuildingType::Road:
                return "ROADS";
            default:
                return "OTHER";
        }
    }

    // Returns category display order for build panel grouping.
    int BuildCategoryOrder(const std::string& category)
    {
        static const std::vector<std::string> order{"WOOD", "METALS", "FOOD", "LOGISTICS", "MILITARY", "SCIENCE", "ROADS", "OTHER"};
        auto it = std::find(order.begin(), order.end(), category);
        return it != order.end() ? static_cast<int>(std::distance(order.begin(), it)) : static_cast<int>(order.size());
    }

    // Returns current build lock reasons for the local player.
    std::vector<std::string> BuildLockReasons(GameScene* scene, const BuildOption& option)
    {
        Player* player = LocalPlayer(scene);
        if (player == nullptr)
            return {"No local player"};

        const auto& definition = GetBuildingDefinition(option.buildingType);
        return player->GetBuildRequirementFailures(definition, false);
    }

    // Draws the build tooltip with stockpile and terrain availability.
    void DrawBuildTooltip(GameScene* scene, const BuildOption& option, Vec2i hoveredTile)
    {
        const Color ok{154, 238, 166, 255};
        const Color missing{248, 126, 126, 255};
        const Color muted{188, 197, 208, 255};

        std::vector<std::pair<std::string, Color>> rows;
        rows.push_back({"Build time: " + std::to_string(static_cast<int>(option.buildTime)) + "s", muted});

        auto lockReasons = BuildLockReasons(scene, option);
        if (!lockReasons.empty())
        {
            rows.push_back({"Locked:", missing});
            for (const auto& reason : lockReasons)
                rows.push_back({reason, missing});
        }

        if (option.buildCosts.empty())
        {
            rows.push_back({"Cost: Free", ok});
        }
        else
        {
            rows.push_back({"Cost:", muted});
            for (const auto& cost : option.buildCosts)
            {
                int stored = CountStoredResource(scene, cost.type);
                Color color = stored >= cost.amount ? ok : missing;
                rows.push_back({rt2s(cost.type) + ": " + std::to_string(stored) + "/" + std::to_string(cost.amount), color});
            }
        }

        auto requiredTerrain = RequiredTerrainTypes(option.buildingType);
        if (!requiredTerrain.empty())
        {
            bool terrainOk = scene != nullptr && scene->game != nullptr && hoveredTile.x >= 0 && hoveredTile.y >= 0 &&
                scene->game->tilemap.HasRequiredTerrainForBuilding(option.buildingType, hoveredTile, option.footprint, 2);
            std::string terrainLabel = "Requires: ";
            for (size_t i = 0; i < requiredTerrain.size(); i++)
            {
                if (i > 0)
                    terrainLabel += "/";
                terrainLabel += TileTypeLabel(requiredTerrain[i]);
            }
            rows.push_back({terrainLabel, terrainOk ? ok : missing});
        }

        Vector2 mouse = GetMousePosition();
        float width = 280.0f;
        float rowH = 23.0f;
        float titleH = 28.0f;
        float height = titleH + rowH * rows.size() + 18.0f;
        Rectangle box{
            std::min(mouse.x + 18.0f, static_cast<float>(GetScreenWidth()) - width - 12.0f),
            std::min(mouse.y + 18.0f, static_cast<float>(GetScreenHeight()) - height - 12.0f),
            width,
            height};

        DrawRectangleRounded(box, 0.06f, 8, Color{18, 22, 28, 242});
        DrawRectangleRoundedLines(box, 0.06f, 8, 1.0f, Color{112, 126, 148, 255});
        UiText::DrawFit(option.name, Rectangle{box.x + 12.0f, box.y + 9.0f, box.width - 24.0f, 24.0f}, 22, RAYWHITE);

        float y = box.y + titleH + 8.0f;
        for (const auto& row : rows)
        {
            UiText::DrawFit(row.first, Rectangle{box.x + 14.0f, y, box.width - 28.0f, rowH}, 19, row.second);
            y += rowH;
        }
    }

    // Returns current map size for camera clamping helpers.
    Vec2i GetMapSize(GameScene* scene)
    {
        return Vec2i{scene->game->tilemap.params.sizeX, scene->game->tilemap.params.sizeY};
    }

    float StrategicHudHeightForWindow(Vec2i windowSize)
    {
        return std::clamp(windowSize.y * 0.066f, 58.0f, 76.0f);
    }

    float StrategicHudTopPaddingForWindow(Vec2i windowSize)
    {
        return StrategicHudHeightForWindow(windowSize) + 6.0f;
    }

    void UpdateStrategicHudLayout(StrategicResourceHudWidget& hud, Vec2i windowSize)
    {
        hud.ChangePosition(0, 0);
        hud.ChangeSize(windowSize.x, static_cast<int>(StrategicHudHeightForWindow(windowSize)));
    }

    void ApplyStrategicHudCameraPadding(GameScene* scene)
    {
        if (scene == nullptr || scene->game == nullptr)
            return;

        Vec2i windowSize{GetScreenWidth(), GetScreenHeight()};
        scene->render.SetTopScreenPadding(StrategicHudTopPaddingForWindow(windowSize));
        scene->render.ClampCameraToMap(GetMapSize(scene));
    }

    // Initializes MoveCamera.
    void MoveCamera(GameScene* scene, CameraMovement& cameraMovement)
    {
        if (!cameraMovement.isMoving)
            return;
        if (!IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
        {
            cameraMovement.isMoving = false;
            return;
        }

        Vector2 delta = GetMouseDelta();
        delta.x *= -1;
        delta.x /= scene->render.camera.zoom;
        delta.y /= scene->render.camera.zoom;
        scene->render.camera.target = Vector2Add(scene->render.camera.target, delta);
        ApplyStrategicHudCameraPadding(scene);
        scene->render.ClampCameraToMap(GetMapSize(scene));
    }

    // Adjusts camera or map-space geometry.
    void ZoomCamera(GameScene* scene)
    {
        float wheel = GetMouseWheelMove();
        if (wheel == 0.0f)
            return;

        ApplyStrategicHudCameraPadding(scene);
        scene->render.ZoomAtScreenPoint(GetMousePosition(), wheel, GetMapSize(scene));
    }

    // Initializes ScreenToTile.
    Vec2i ScreenToTile(GameScene* scene, Vector2 screen)
    {
        Vec2f world = scene->render.ScreenToWorld(screen);
        if (world.x < 0.0f || world.y < 0.0f)
            return {-1, -1};

        Vec2i tilePos{
            static_cast<int>(world.x / TILE_SIZE),
            static_cast<int>(world.y / TILE_SIZE)};
        if (!scene->game->tilemap.IsInside(tilePos))
            return {-1, -1};

        return tilePos;
    }

    // Assigns up to `count` distinct, free tiles in the clicked 2x2 sector. The
    // sector is clipped to owned territory and blockers, so border corners become
    // 3-tile L-shapes, strips or single-tile positions.
    std::vector<Vec2i> AssignSectorTiles(GameScene* scene, Player* player, Vec2i targetTile,
                                         int count, const std::vector<int>& movingDivisionIds)
    {
        std::vector<Vec2i> result;
        if (scene == nullptr || scene->game == nullptr || player == nullptr || count <= 0)
            return result;

        TileMap& map = scene->game->tilemap;
        DivisionSector sector = ResolveDivisionSector(map, targetTile, player);
        if (!sector.IsValid())
            return result;

        auto freeForGroup = [&](Vec2i t)
        {
            int occupant = DivisionOnTile(*player, t, -1);
            return occupant < 0 ||
                   std::find(movingDivisionIds.begin(), movingDivisionIds.end(), occupant) != movingDivisionIds.end();
        };

        auto addIfFree = [&](Vec2i tile)
        {
            if (static_cast<int>(result.size()) >= count)
                return;
            if (!map.IsInside(tile) || SectorCellOf(tile) != sector.cell)
                return;
            if (!freeForGroup(tile))
                return;
            if (std::find(result.begin(), result.end(), tile) == result.end())
                result.push_back(tile);
        };

        addIfFree(targetTile);
        for (int tileId : sector.TileIds(map))
            addIfFree(map.GetCoordsFromId(tileId));
        return result;
    }

    template <typename T>
    // Initializes MakeBuildOption.
    BuildOption MakeBuildOption(GameScene* scene, const BuildingDefinition& definition)
    {
        static_assert(std::is_base_of<Building, T>::value);

        BuildOption option;
        option.name = definition.name;
        option.costText = definition.buildCostText;
        option.buildCosts = definition.buildCosts;
        option.buildingType = definition.type;
        option.footprint = definition.footprint;
        option.buildTime = definition.buildTime;
        option.category = BuildCategory(definition.type);
        option.previewFactory = []()
        {
            return std::make_unique<T>(0);
        };
        option.buildAt = [scene, type = definition.type](Vec2i tilePos)
        {
            scene->SubmitLocalCommand(GameCommand::BuildBuilding(scene->game->GetLocalPlayerId(), type, tilePos));
        };

        return option;
    }

    // Initializes MakeBuildOption.
    BuildOption MakeBuildOption(GameScene* scene, BuildingType type)
    {
        const auto& definition = GetBuildingDefinition(type);
        switch (type)
        {
            case BuildingType::Woodcutter: return MakeBuildOption<Woodcutter>(scene, definition);
            case BuildingType::HuntersHut: return MakeBuildOption<HuntersHut>(scene, definition);
            case BuildingType::LumberMill: return MakeBuildOption<LumberMill>(scene, definition);
            case BuildingType::Mine: return MakeBuildOption<Mine>(scene, definition);
            case BuildingType::Foundry: return MakeBuildOption<Foundry>(scene, definition);
            case BuildingType::Well: return MakeBuildOption<Well>(scene, definition);
            case BuildingType::WheatFarm: return MakeBuildOption<WheatFarm>(scene, definition);
            case BuildingType::Windmill: return MakeBuildOption<Windmill>(scene, definition);
            case BuildingType::Bakery: return MakeBuildOption<Bakery>(scene, definition);
            case BuildingType::Inn: return MakeBuildOption<Inn>(scene, definition);
            case BuildingType::Paperworks: return MakeBuildOption<Paperworks>(scene, definition);
            case BuildingType::Smith: return MakeBuildOption<Smith>(scene, definition);
            case BuildingType::University: return MakeBuildOption<University>(scene, definition);
            case BuildingType::StorageBuilding: return MakeBuildOption<StorageBuilding>(scene, definition);
            case BuildingType::Village: return MakeBuildOption<Village>(scene, definition);
            case BuildingType::GuardTower: return MakeBuildOption<GuardTower>(scene, definition);
            case BuildingType::Fortress: return MakeBuildOption<Fortress>(scene, definition);
            case BuildingType::Castle: return MakeBuildOption<Castle>(scene, definition);
            case BuildingType::Barracks: return MakeBuildOption<Barracks>(scene, definition);
            case BuildingType::SupplyHub: return MakeBuildOption<SupplyHub>(scene, definition);
            case BuildingType::Road: return MakeBuildOption<Road>(scene, definition);
            default: return {};
        }
    }

    // Builds the requested map object or helper path.
    Rectangle BuildingScreenRect(GameScene* scene, Building* building)
    {
        Vec2i anchor = scene->game->tilemap.GetCoordsFromId(building->positionId);
        Vec2i footprint = building->GetFootprint();
        Vec2f worldTopLeft{
            static_cast<float>(anchor.x * TILE_SIZE),
            static_cast<float>(anchor.y * TILE_SIZE)};
        Vec2f worldBottomRight{
            worldTopLeft.x + footprint.x * TILE_SIZE,
            worldTopLeft.y + footprint.y * TILE_SIZE};
        Vec2f screenTopLeft = scene->render.WorldToScreen(worldTopLeft);
        Vec2f screenBottomRight = scene->render.WorldToScreen(worldBottomRight);
        return Rectangle{
            screenTopLeft.x,
            screenTopLeft.y,
            screenBottomRight.x - screenTopLeft.x,
            screenBottomRight.y - screenTopLeft.y};
    }

    // Builds the requested map object or helper path.
    Vector2 BuildingScreenCenter(GameScene* scene, Building* building)
    {
        Rectangle rect = BuildingScreenRect(scene, building);
        return Vector2{rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f};
    }

    // Initializes MilitaryOrderColor.
    Color MilitaryOrderColor(MilitaryOrderType order)
    {
        switch (order)
        {
            case MilitaryOrderType::Attack: return Color{238, 84, 84, 218};
            case MilitaryOrderType::Support: return Color{84, 166, 238, 210};
            case MilitaryOrderType::Defend: return Color{88, 216, 132, 210};
            case MilitaryOrderType::None:
            default: return Color{180, 190, 205, 180};
        }
    }

    // Draws an arrow between two screen-space points.
    void DrawOrderArrow(Vector2 from, Vector2 to, Color color)
    {
        Vector2 delta{to.x - from.x, to.y - from.y};
        float len = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (len < 8.0f)
            return;

        Vector2 dir{delta.x / len, delta.y / len};
        Vector2 end{to.x - dir.x * 14.0f, to.y - dir.y * 14.0f};
        DrawLineEx(from, end, 3.0f, color);

        Vector2 perp{-dir.y, dir.x};
        Vector2 left{end.x - dir.x * 14.0f + perp.x * 7.0f, end.y - dir.y * 14.0f + perp.y * 7.0f};
        Vector2 right{end.x - dir.x * 14.0f - perp.x * 7.0f, end.y - dir.y * 14.0f - perp.y * 7.0f};
        DrawTriangle(end, left, right, color);
    }

    // Sorts build options into stable gameplay categories.
    void SortBuildOptions(std::vector<BuildOption>& options)
    {
        std::stable_sort(options.begin(), options.end(), [](const BuildOption& lhs, const BuildOption& rhs)
        {
            int leftOrder = BuildCategoryOrder(lhs.category);
            int rightOrder = BuildCategoryOrder(rhs.category);
            if (leftOrder != rightOrder)
                return leftOrder < rightOrder;
            return lhs.name < rhs.name;
        });
    }

    // Returns the clickable resources button area in the strategic HUD.
    Rectangle StatsHudButtonRect(const StrategicResourceHudWidget& hud)
    {
        float height = static_cast<float>(hud.size.y);
        float buttonH = std::max(40.0f, height - 14.0f);
        float buttonW = 132.0f;
        return Rectangle{
            static_cast<float>(hud.pos.x + hud.size.x) - buttonW - 18.0f,
            static_cast<float>(hud.pos.y) + (height - buttonH) * 0.5f,
            buttonW,
            buttonH};
    }

    Rectangle FocusHudButtonRect(const StrategicResourceHudWidget& hud)
    {
        Rectangle stats = StatsHudButtonRect(hud);
        return Rectangle{stats.x - 166.0f - 10.0f, stats.y, 166.0f, stats.height};
    }

    Rectangle DestroyHudButtonRect(const StrategicResourceHudWidget& hud)
    {
        Rectangle focus = FocusHudButtonRect(hud);
        return Rectangle{focus.x - 110.0f - 10.0f, focus.y, 110.0f, focus.height};
    }

    Rectangle RoadHudButtonRect(const StrategicResourceHudWidget& hud)
    {
        Rectangle destroy = DestroyHudButtonRect(hud);
        return Rectangle{destroy.x - 92.0f - 10.0f, destroy.y, 92.0f, destroy.height};
    }

    Rectangle BuildHudButtonRect(const StrategicResourceHudWidget& hud)
    {
        Rectangle road = RoadHudButtonRect(hud);
        return Rectangle{road.x - 92.0f - 10.0f, road.y, 92.0f, road.height};
    }

    // Returns true when the stats HUD button is under the current cursor.
    bool IsStatsHudButtonHovered(const StrategicResourceHudWidget& hud)
    {
        return CheckCollisionPointRec(GetMousePosition(), StatsHudButtonRect(hud));
    }

    bool IsFocusHudButtonHovered(const StrategicResourceHudWidget& hud)
    {
        return CheckCollisionPointRec(GetMousePosition(), FocusHudButtonRect(hud));
    }

    bool IsDestroyHudButtonHovered(const StrategicResourceHudWidget& hud)
    {
        return CheckCollisionPointRec(GetMousePosition(), DestroyHudButtonRect(hud));
    }

    bool IsRoadHudButtonHovered(const StrategicResourceHudWidget& hud)
    {
        return CheckCollisionPointRec(GetMousePosition(), RoadHudButtonRect(hud));
    }

    bool IsBuildHudButtonHovered(const StrategicResourceHudWidget& hud)
    {
        return CheckCollisionPointRec(GetMousePosition(), BuildHudButtonRect(hud));
    }

    Rectangle PanelCloseButtonRect(Rectangle panel)
    {
        return Rectangle{panel.x + panel.width - 44.0f, panel.y + 10.0f, 30.0f, 30.0f};
    }

    void DrawCloseButton(Rectangle panel)
    {
        Rectangle close = PanelCloseButtonRect(panel);
        bool hover = CheckCollisionPointRec(GetMousePosition(), close);
        DrawRectangleRounded(close, 0.18f, 8, hover ? Color{110, 58, 64, 245} : Color{54, 42, 48, 230});
        DrawRectangleRoundedLines(close, 0.18f, 8, 1.0f, hover ? Color{244, 132, 142, 255} : Color{156, 104, 114, 235});
        UiText::DrawFit("X", Rectangle{close.x + 6.0f, close.y + 4.0f, close.width - 12.0f, close.height - 8.0f}, 20, RAYWHITE);
    }

    // Returns a stable chart color for one resource line.
    Color ResourceChartColor(ResourceType type)
    {
        int id = static_cast<int>(type);
        return Color{
            static_cast<unsigned char>(90 + (id * 47) % 150),
            static_cast<unsigned char>(120 + (id * 83) % 120),
            static_cast<unsigned char>(110 + (id * 31) % 140),
            235};
    }

    const char* FocusStatLabel(BalanceStat stat)
    {
        switch (stat)
        {
            case BalanceStat::BuildTime: return "Build time";
            case BalanceStat::ProductionCycleTime: return "Production cycle time";
            case BalanceStat::ProductionOutputAmount: return "Production output";
            case BalanceStat::WorkerCapacity: return "Worker capacity";
            case BalanceStat::TransportTime: return "Transport time";
            case BalanceStat::RoadCapacity: return "Road capacity";
            case BalanceStat::RoadSpeed: return "Road speed";
            case BalanceStat::MilitaryStrength: return "Military strength";
            case BalanceStat::AttackDamage: return "Attack damage";
            case BalanceStat::HitPoints: return "Hit points";
            case BalanceStat::TerritoryRadius: return "Territory radius";
            case BalanceStat::GarrisonCapacity: return "Garrison capacity";
            case BalanceStat::SupplyCapacity: return "Supply capacity";
            case BalanceStat::SupplyConsumption: return "Supply consumption";
            case BalanceStat::ManpowerRate: return "Manpower growth";
            case BalanceStat::PopulationCap: return "Population cap";
            case BalanceStat::RecruitmentTime: return "Recruitment time";
            case BalanceStat::RecruitmentManpowerCost: return "Recruitment manpower cost";
            default: return "Effect";
        }
    }

    bool LowerValueIsBetter(BalanceStat stat)
    {
        switch (stat)
        {
            case BalanceStat::BuildTime:
            case BalanceStat::ProductionCycleTime:
            case BalanceStat::TransportTime:
            case BalanceStat::SupplyConsumption:
            case BalanceStat::RecruitmentTime:
            case BalanceStat::RecruitmentManpowerCost:
                return true;
            default:
                return false;
        }
    }

    bool IsPositiveModifier(const BalanceModifier& modifier)
    {
        bool lowerIsBetter = LowerValueIsBetter(modifier.stat);
        if (std::abs(modifier.additive) > 0.001)
            return lowerIsBetter ? modifier.additive < 0.0 : modifier.additive > 0.0;
        if (std::abs(modifier.multiplier - 1.0) > 0.001)
            return lowerIsBetter ? modifier.multiplier < 1.0 : modifier.multiplier > 1.0;
        return true;
    }

    std::string TooltipBonusLine(const std::string& text)
    {
        return "{bonus}" + text;
    }

    std::string TooltipPenaltyLine(const std::string& text)
    {
        return "{penalty}" + text;
    }

    std::string TooltipSeparatorLine()
    {
        return "{separator}";
    }

    const char* ImprovedRateLabel(BalanceStat stat)
    {
        switch (stat)
        {
            case BalanceStat::BuildTime: return "Build speed";
            case BalanceStat::ProductionCycleTime: return "Production speed";
            case BalanceStat::TransportTime: return "Transport speed";
            case BalanceStat::RecruitmentTime: return "Recruitment speed";
            default: return FocusStatLabel(stat);
        }
    }

    const char* FocusBuildingLabel(BuildingType type)
    {
        switch (type)
        {
            case BuildingType::Headquarters: return "Headquarters";
            case BuildingType::Village: return "Village";
            case BuildingType::StorageBuilding: return "Storage";
            case BuildingType::Woodcutter: return "Woodcutter";
            case BuildingType::HuntersHut: return "Hunters Hut";
            case BuildingType::LumberMill: return "Lumber Mill";
            case BuildingType::Mine: return "Mine";
            case BuildingType::Foundry: return "Foundry";
            case BuildingType::Well: return "Well";
            case BuildingType::WheatFarm: return "Wheat Farm";
            case BuildingType::Windmill: return "Windmill";
            case BuildingType::Bakery: return "Bakery";
            case BuildingType::Inn: return "Inn";
            case BuildingType::Paperworks: return "Paperworks";
            case BuildingType::Smith: return "Smith";
            case BuildingType::University: return "University";
            case BuildingType::GuardTower: return "Guard Tower";
            case BuildingType::Fortress: return "Fortress";
            case BuildingType::Castle: return "Castle";
            case BuildingType::Barracks: return "Barracks";
            case BuildingType::Road: return "Road";
            default: return "Building";
        }
    }

    std::string FormatDuration(double seconds)
    {
        int total = std::max(0, static_cast<int>(std::round(seconds)));
        int minutes = total / 60;
        int remainingSeconds = total % 60;
        std::ostringstream stream;
        stream << minutes << ":";
        if (remainingSeconds < 10)
            stream << "0";
        stream << remainingSeconds;
        return stream.str();
    }

    std::string FormatModifierForFocusTooltip(const BalanceModifier& modifier)
    {
        std::ostringstream stream;
        bool lowerIsBetter = LowerValueIsBetter(modifier.stat);
        bool showAsRate = lowerIsBetter && std::abs(modifier.multiplier - 1.0) > 0.001 &&
                          (modifier.stat == BalanceStat::BuildTime ||
                           modifier.stat == BalanceStat::ProductionCycleTime ||
                           modifier.stat == BalanceStat::TransportTime ||
                           modifier.stat == BalanceStat::RecruitmentTime);
        stream << (showAsRate ? ImprovedRateLabel(modifier.stat) : FocusStatLabel(modifier.stat));
        if (modifier.buildingType.has_value())
            stream << " for " << FocusBuildingLabel(modifier.buildingType.value());
        if (modifier.resourceType.has_value())
            stream << " producing " << rt2s(modifier.resourceType.value());
        if (modifier.unitType.has_value())
            stream << " for " << MilitaryUnitLabel(modifier.unitType.value());
        stream << ": ";

        bool hasValue = false;
        if (std::abs(modifier.additive) > 0.001)
        {
            stream << (modifier.additive > 0.0 ? "+" : "") << modifier.additive;
            hasValue = true;
        }
        if (std::abs(modifier.multiplier - 1.0) > 0.001)
        {
            double percent = showAsRate
                ? (1.0 / modifier.multiplier - 1.0) * 100.0
                : (modifier.multiplier - 1.0) * 100.0;
            if (hasValue)
                stream << ", ";
            stream << (percent > 0.0 ? "+" : "") << static_cast<int>(std::round(percent)) << "%";
            hasValue = true;
        }
        if (!hasValue)
            stream << "No numeric modifier";
        return IsPositiveModifier(modifier) ? TooltipBonusLine(stream.str()) : TooltipPenaltyLine(stream.str());
    }


    // Returns a readable equipment label while keeping empty equipment compact.
    std::string EquipmentLabel(ResourceType type)
    {
        return type == ResourceType::Null ? "-" : rt2s(type);
    }

    // Returns a stable accent color for one division type.
    Color DivisionColor(MilitaryUnitType type)
    {
        switch (type)
        {
            case MilitaryUnitType::Swordsman: return Color{218, 96, 84, 255};
            case MilitaryUnitType::Archer: return Color{94, 180, 112, 255};
            case MilitaryUnitType::Militia:
            default: return Color{120, 150, 210, 255};
        }
    }

    // Returns a readable military order label for UI.
    const char* MilitaryOrderLabel(MilitaryOrderType order)
    {
        switch (order)
        {
            case MilitaryOrderType::Attack: return "Attack";
            case MilitaryOrderType::Support: return "Support";
            case MilitaryOrderType::Defend: return "Defend";
            case MilitaryOrderType::None:
            default: return "None";
        }
    }
}

// Handles the requested event or transfer.
void InputProcessor::HandleInputs()
{
    if (IsActionPressed(CLOSE_TOP_GUI))
        controller->MakeAction("esc");
    if (IsActionPressed(OPEN_BUILD_GUI))
        controller->MakeAction("q");
    if (IsActionPressed(OPEN_ROAD_BUILD_GUI))
        controller->MakeAction("r");
    if (IsActionPressed(OPEN_DESTROY_GUI))
        controller->MakeAction("d");
    if (IsActionPressed(OPEN_HEADQUARTERS_GUI))
        controller->MakeAction("e");
    if (IsActionPressed(OPEN_STATS_GUI))
        controller->MakeAction("s");
    if (IsActionPressed(OPEN_FOCUS_GUI))
        controller->MakeAction("f");
    if (IsActionPressed(CENTER_CAMERA_ON_HEADQUARTERS))
        controller->MakeAction("space");
    if (IsActionPressed(DEBUG_GRANT_RESOURCES))
        controller->MakeAction("debug_resources");
    if (IsActionPressed(LEFT_BUTTON_DOWN))
        controller->MakeAction("lmbp");
    if (IsActionReleased(LEFT_BUTTON_DOWN))
        controller->MakeAction("lmbr");
    if (IsActionPressed(RIGHT_BUTTON_DOWN))
        controller->MakeAction("rmbp");
    if (IsActionReleased(RIGHT_BUTTON_DOWN))
        controller->MakeAction("rmbr");
    if (IsActionPressed(MIDDLE_BUTTON_DOWN))
        controller->MakeAction("mmbp");
    if (IsActionReleased(MIDDLE_BUTTON_DOWN))
        controller->MakeAction("mmbr");
    if (GetMouseWheelMove() != 0.0f)
        controller->MakeAction("scroll");
}

// Initializes runtime state for this object.
void GuiController::Init(GameScene *s)
{
    scene = s;
    divisionMapOverlay = std::make_unique<DivisionMapWidget>();
    divisionMapOverlay->scene = scene;
}

// Advances this object's state for one frame.
void GuiController::Update(double dt)
{
    ui.clear();
    if (divisionMapOverlay != nullptr)
        AddUiWidget(divisionMapOverlay.get());
    activeSystem->Update(dt);
}

// Dispatches a named UI action to the active GUI system.
void GuiController::MakeAction(std::string action)
{
    if (action == "debug_resources")
    {
        GrantDebugResources(scene, 50);
        return;
    }

    auto it = activeSystem->actionMap.find(action);
    if (it != activeSystem->actionMap.end())
        it->second();
}

// Advances this object's state for one frame.
void BasicMapViewSystem::Update(double dt)
{
    if (!researchPanel.researchRequested)
    {
        researchPanel.researchRequested = [this](const std::string& technologyId, Building* university)
        {
            if (scene == nullptr || scene->game == nullptr || university == nullptr)
                return;

            scene->SubmitLocalCommand(GameCommand::StartTechnologyResearch(
                scene->game->GetLocalPlayerId(),
                technologyId,
                university->positionId));
        };
    }

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    owner->AddUiWidget(&productionWarningWidget);
    owner->AddUiWidget(&militaryOrderWidget);

    // Track drag-box selection.
    moveTargetWidget.drawBox = false;
    if (pendingBox && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
    {
        Vector2 m = GetMousePosition();
        boxEnd = {static_cast<int>(m.x), static_cast<int>(m.y)};
        int dx = boxEnd.x - boxStart.x;
        int dy = boxEnd.y - boxStart.y;
        if (dx * dx + dy * dy > 36)
            boxActive = true;
        if (boxActive)
        {
            moveTargetWidget.drawBox = true;
            moveTargetWidget.boxRect = {
                static_cast<float>(std::min(boxStart.x, boxEnd.x)),
                static_cast<float>(std::min(boxStart.y, boxEnd.y)),
                static_cast<float>(std::abs(dx)),
                static_cast<float>(std::abs(dy))};
        }
    }

    owner->AddUiWidget(&armyBarWidget);   // persistent bottom army strip

    bool hasSelection = militaryDivisionBarWidget.building != nullptr &&
                        !militaryDivisionBarWidget.selectedDivisionIds.empty();
    if (hasSelection || moveTargetWidget.drawBox)
        owner->AddUiWidget(&moveTargetWidget);

    if (selectedBattleId >= 0 && scene != nullptr && scene->game != nullptr)
    {
        if (scene->game->battles.FindBattle(selectedBattleId) == nullptr)
            selectedBattleId = -1;
        else
        {
            battleInfoPanel.SetBattle(selectedBattleId);
            owner->AddUiWidget(&battleInfoPanel);
        }
    }

    if (isDivisionOnlyMode && !isBuildingSelected)
    {
        // Show only the garrison bar — triggered by clicking a map division marker.
        // Clear when the home building disappears or user clicks elsewhere.
        auto* garrison = militaryDivisionBarWidget.building != nullptr
            ? militaryDivisionBarWidget.building->GetComponent<GarrisonComponent>() : nullptr;
        if (garrison == nullptr || garrison->divisions.empty())
        {
            isDivisionOnlyMode = false;
            militaryDivisionBarWidget.building = nullptr;
        }
        else
        {
            owner->AddUiWidget(&militaryDivisionBarWidget);
        }
    }

    if (isBuildingSelected)
    {
        isDivisionOnlyMode = false;
        GuiPanel* activePanel = researchPanel.HasBuilding()
            ? static_cast<GuiPanel*>(&researchPanel)
            : &buildingInfoPanel;

        if (!activePanel->HasBuilding())
        {
            isBuildingSelected = false;
            return;
        }

        selectedBuildingWidget.building = activePanel->GetBuilding();
        if (activePanel->ConsumeDestroyRequest())
        {
            Building* building = activePanel->GetBuilding();
            if (building != nullptr && building->CanBeManuallyDestroyed())
                scene->SubmitLocalCommand(GameCommand::DestroyBuilding(scene->game->GetLocalPlayerId(), building->positionId));
            isBuildingSelected = false;
            buildingInfoPanel.SetBuilding(nullptr);
            researchPanel.SetBuilding(nullptr);
            selectedBuildingWidget.building = nullptr;
            return;
        }

        owner->AddUiWidget(&selectedBuildingWidget);
        owner->AddUiWidget(activePanel);
        Building* selected = activePanel->GetBuilding();
        if (selected != nullptr && selected->HasComponent<GarrisonComponent>())
        {
            militaryDivisionBarWidget.building = selected;
            owner->AddUiWidget(&militaryDivisionBarWidget);
        }
        else
        {
            militaryDivisionBarWidget.building = nullptr;
        }
    }
    owner->AddUiWidget(&strategicHudWidget);
}

// Advances UpdateUiWidgets for one frame or simulation tick.
void BasicMapViewSystem::UpdateUiWidgets(Vec2i size)
{
    buildingInfoPanel.UpdateSize(size);
    researchPanel.UpdateSize(size);
    battleInfoPanel.UpdateSize(size);
    statsPanel.UpdateSize(size);
    militaryDivisionBarWidget.UpdateSize(size);
    armyBarWidget.UpdateSize(size);
    strategicHudWidget.UpdateSize(size);
    divisionMapWidget.UpdateSize(size);
}

// Initializes BasicMapViewSystem::EscPressed.
void BasicMapViewSystem::EscPressed()
{
    if(isBuildingSelected)
    {
        isBuildingSelected = false;
        buildingInfoPanel.SetBuilding(nullptr);
        researchPanel.SetBuilding(nullptr);
        return;
    }

    // Close the division / army selection panel before opening the game menu.
    if (isDivisionOnlyMode || !militaryDivisionBarWidget.selectedDivisionIds.empty())
    {
        isDivisionOnlyMode = false;
        militaryDivisionBarWidget.selectedDivisionIds.clear();
        militaryDivisionBarWidget.building = nullptr;
        return;
    }

    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = scene;
    msg->sceneName = "GameMenuScene";
    scene->broker->Broadcast(msg);

    Log::Msg("[Input]", "escape pressed");
}

// Builds the requested map object or helper path.
void BasicMapViewSystem::BuildPressed()
{
    isBuildingSelected = false;
    buildingInfoPanel.SetBuilding(nullptr);
    researchPanel.SetBuilding(nullptr);
    owner->ChangeSystem("build");
    Log::Msg("[Input]", "Q pressed");
}

// Initializes BasicMapViewSystem::RoadBuildPressed.
void BasicMapViewSystem::RoadBuildPressed()
{
    isBuildingSelected = false;
    buildingInfoPanel.SetBuilding(nullptr);
    researchPanel.SetBuilding(nullptr);
    cameraMovement.isMoving = false;
    owner->ChangeSystem("road_build");
    Log::Msg("[Input]", "R pressed");
}

// Initializes BasicMapViewSystem::DestroyPressed.
void BasicMapViewSystem::DestroyPressed()
{
    isBuildingSelected = false;
    buildingInfoPanel.SetBuilding(nullptr);
    researchPanel.SetBuilding(nullptr);
    cameraMovement.isMoving = false;
    owner->ChangeSystem("destroy");
    Log::Msg("[Input]", "D pressed");
}

// Opens the local headquarters storage panel.
void BasicMapViewSystem::HeadquartersPressed()
{
    Building* headquarters = FindLocalHeadquarters(scene);
    if (headquarters == nullptr)
        return;

    GuiPanel* activePanel = researchPanel.HasBuilding()
        ? static_cast<GuiPanel*>(&researchPanel)
        : &buildingInfoPanel;
    if (isBuildingSelected && activePanel->GetBuilding() == headquarters)
    {
        isBuildingSelected = false;
        buildingInfoPanel.SetBuilding(nullptr);
        researchPanel.SetBuilding(nullptr);
        selectedBuildingWidget.building = nullptr;
        Log::Msg("[Input]", "E pressed - headquarters panel closed");
        return;
    }

    OpenHeadquartersPanel();
}

// Opens the full-screen statistics panel.
void BasicMapViewSystem::StatsPressed()
{
    isBuildingSelected = false;
    buildingInfoPanel.SetBuilding(nullptr);
    researchPanel.SetBuilding(nullptr);
    selectedBuildingWidget.building = nullptr;
    owner->ChangeSystem("stats");
    Log::Msg("[Input]", "S pressed - stats panel opened");
}

void BasicMapViewSystem::FocusPressed()
{
    isBuildingSelected = false;
    buildingInfoPanel.SetBuilding(nullptr);
    researchPanel.SetBuilding(nullptr);
    selectedBuildingWidget.building = nullptr;
    owner->ChangeSystem("focus");
    Log::Msg("[Input]", "F pressed - focus panel opened");
}

// Opens the local headquarters panel without toggling it closed.
void BasicMapViewSystem::OpenHeadquartersPanel()
{
    Building* headquarters = FindLocalHeadquarters(scene);
    if (headquarters == nullptr)
        return;

    isBuildingSelected = true;
    researchPanel.SetBuilding(nullptr);
    buildingInfoPanel.SetBuilding(headquarters);
    Log::Msg("[Input]", "E pressed - headquarters panel opened");
}

// Centers the camera on the local headquarters.
void BasicMapViewSystem::CenterOnHeadquartersPressed()
{
    Building* headquarters = FindLocalHeadquarters(scene);
    if (headquarters == nullptr)
        return;

    Vec2i anchor = scene->game->tilemap.GetCoordsFromId(headquarters->positionId);
    Vec2i footprint = headquarters->GetFootprint();
    Vec2f hqWorldCenter{
        static_cast<float>((anchor.x + footprint.x * 0.5f) * TILE_SIZE),
        static_cast<float>((anchor.y + footprint.y * 0.5f) * TILE_SIZE)};
    ApplyStrategicHudCameraPadding(scene);
    scene->render.CenterCameraOnWorld(hqWorldCenter, GetMapSize(scene));
    Log::Msg("[Input]", "space pressed - camera centered on headquarters");
}

// Initializes BasicMapViewSystem::LmbPressed.
void BasicMapViewSystem::LmbPressed()
{
    auto mousePos = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mousePos.x), static_cast<int>(mousePos.y)};
    if (IsBuildHudButtonHovered(strategicHudWidget))
    {
        BuildPressed();
        return;
    }
    if (IsRoadHudButtonHovered(strategicHudWidget))
    {
        RoadBuildPressed();
        return;
    }
    if (IsDestroyHudButtonHovered(strategicHudWidget))
    {
        DestroyPressed();
        return;
    }
    if (IsStatsHudButtonHovered(strategicHudWidget))
    {
        StatsPressed();
        return;
    }
    if (IsFocusHudButtonHovered(strategicHudWidget))
    {
        FocusPressed();
        return;
    }

    GuiPanel* activePanel = researchPanel.HasBuilding()
        ? static_cast<GuiPanel*>(&researchPanel)
        : &buildingInfoPanel;
    if (isBuildingSelected && activePanel->ContainsPoint(screenPos))
    {
        Log::Msg("[Input]", "building info panel clicked");
        return;
    }
    if ((isBuildingSelected || isDivisionOnlyMode) && militaryDivisionBarWidget.building != nullptr &&
        militaryDivisionBarWidget.ContainsPoint(screenPos))
    {
        militaryDivisionBarWidget.HandleClick(screenPos);
        Log::Msg("[Input]", "military division bar clicked");
        return;
    }

    // Army strip (bottom) handles its own clicks: form / select armies. Only the
    // cards and the "+" consume the click — empty space falls through to the map.
    if (armyBarWidget.HandleClick(screenPos))
    {
        // Selecting an army opens the division side panel so its units are visible.
        if (militaryDivisionBarWidget.building != nullptr &&
            !militaryDivisionBarWidget.selectedDivisionIds.empty())
        {
            isDivisionOnlyMode = true;
            isBuildingSelected = false;
            buildingInfoPanel.SetBuilding(nullptr);
            researchPanel.SetBuilding(nullptr);
        }
        return;
    }

    // Check division map markers before tile selection
    {
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const DivisionMapMarker* hit = owner->divisionMapOverlay != nullptr
            ? owner->divisionMapOverlay->HitTest(screenPos)
            : nullptr;
        auto localPlayerIt = scene->game->playerHandler.players.find(scene->game->GetLocalPlayerId());
        Player* localPlayer = localPlayerIt != scene->game->playerHandler.players.end() ? localPlayerIt->second.get() : nullptr;
        if (hit != nullptr && hit->homeBuilding != nullptr && hit->owner == localPlayer)
        {
            Building* home = hit->homeBuilding;
            // Division-only mode: show only garrison bar, not full building panel
            isBuildingSelected = false;
            isDivisionOnlyMode = true;
            selectedBattleId = -1;
            buildingInfoPanel.SetBuilding(nullptr);
            researchPanel.SetBuilding(nullptr);
            militaryDivisionBarWidget.building = home;
            // A counter is a stack of divisions — selecting it takes the whole stack.
            if (ctrl)
            {
                for (int id : hit->divisionIds)
                {
                    auto it = std::find(militaryDivisionBarWidget.selectedDivisionIds.begin(),
                                        militaryDivisionBarWidget.selectedDivisionIds.end(), id);
                    if (it != militaryDivisionBarWidget.selectedDivisionIds.end())
                        militaryDivisionBarWidget.selectedDivisionIds.erase(it);
                    else
                        militaryDivisionBarWidget.selectedDivisionIds.push_back(id);
                }
            }
            else
            {
                militaryDivisionBarWidget.selectedDivisionIds = hit->divisionIds;
            }
            Log::Msg("[Input]", hit->divisionIds.size(), " division(s) selected via map counter");
            return;
        }
    }

    // Field-battle circle → open its details panel.
    if (const FieldBattleMarker* fb = militaryOrderWidget.HitTest(screenPos))
    {
        militaryOrderWidget.SelectBattle(*fb);
        return;
    }

    // Check battle indicator circles before tile selection
    if (scene != nullptr && scene->game != nullptr)
    {
        for (const auto& battle : scene->game->battles.GetBattles())
        {
            if (battle.IsOver()) continue;
            Building* atk = scene->game->tilemap.GetBuilding(battle.attackerTileId);
            Building* def = scene->game->tilemap.GetBuilding(battle.defenderTileId);
            if (atk == nullptr || def == nullptr) continue;

            Vector2 atkPos = BuildingScreenCenter(scene, atk);
            Vector2 defPos = BuildingScreenCenter(scene, def);
            Vector2 mid{(atkPos.x + defPos.x) * 0.5f, (atkPos.y + defPos.y) * 0.5f};
            float dx = mousePos.x - mid.x;
            float dy = mousePos.y - mid.y;
            if (dx * dx + dy * dy <= 16.0f * 16.0f)
            {
                selectedBattleId = battle.id;
                isBuildingSelected = false;
                buildingInfoPanel.SetBuilding(nullptr);
                researchPanel.SetBuilding(nullptr);
                Log::Msg("[Input]", "Battle #", battle.id, " selected");
                return;
            }
        }
    }

    // Clicking on the map clears battle + division-only selection
    selectedBattleId = -1;
    isDivisionOnlyMode = false;
    militaryOrderWidget.CloseDetails();

    Vec2i tilePos = ScreenToTile(scene, mousePos);
    if (tilePos.x < 0 || tilePos.y < 0)
        return;

    auto &tile = scene->game->tilemap[tilePos];

    Log::Msg("[Input]", "Tile ID: ", tile.id, " clicked!");

    auto building = tile.GetBuilding();
    if (building != nullptr)
    {
        // Circular click hitbox: only register a building hit near its footprint
        // centre (diameter 0.85 of the footprint side), so corner clicks feel like
        // clicking the ground rather than the building.
        Vec2i anchor = scene->game->tilemap.GetCoordsFromId(building->positionId);
        Vec2i fp = building->GetFootprint();
        Vec2f centerWorld{(anchor.x + fp.x * 0.5f) * TILE_SIZE, (anchor.y + fp.y * 0.5f) * TILE_SIZE};
        Vec2f centerScreen = scene->render.WorldToScreen(centerWorld);
        float radiusScreen = 0.425f * std::min(fp.x, fp.y) * TILE_SIZE * scene->render.camera.zoom;
        float ddx = mousePos.x - centerScreen.x;
        float ddy = mousePos.y - centerScreen.y;
        if (ddx * ddx + ddy * ddy > radiusScreen * radiusScreen)
            building = nullptr;  // outside the circle → treat as an empty-ground click
    }

    if (building != nullptr)
    {
        auto localPlayerIt = scene->game->playerHandler.players.find(scene->game->GetLocalPlayerId());
        Player* localPlayer = localPlayerIt != scene->game->playerHandler.players.end() ? localPlayerIt->second.get() : nullptr;
        if (building->owner == localPlayer)
        {
            isBuildingSelected = true;
            if (building->buildingType == BuildingType::University)
            {
                buildingInfoPanel.SetBuilding(nullptr);
                researchPanel.SetBuilding(building);
            }
            else
            {
                researchPanel.SetBuilding(nullptr);
                buildingInfoPanel.SetBuilding(building);
            }
            Log::Msg("[Input]", building->name, " selected!");
        }
        else
        {
            isBuildingSelected = false;
            buildingInfoPanel.SetBuilding(nullptr);
            researchPanel.SetBuilding(nullptr);
            Log::Msg("[Input]", "enemy building clicked - intel unavailable");
        }
    }
    else
    {
        isBuildingSelected = false;
        buildingInfoPanel.SetBuilding(nullptr);
        researchPanel.SetBuilding(nullptr);
        // Begin a potential drag-box selection on open ground.
        pendingBox = true;
        boxActive = false;
        boxStart = screenPos;
        boxEnd = screenPos;
    }
}

// Finalizes a drag-box division selection, or clears selection on a plain click.
void BasicMapViewSystem::LmbReleased()
{
    if (!pendingBox)
        return;

    if (boxActive)
    {
        float x0 = static_cast<float>(std::min(boxStart.x, boxEnd.x));
        float x1 = static_cast<float>(std::max(boxStart.x, boxEnd.x));
        float y0 = static_cast<float>(std::min(boxStart.y, boxEnd.y));
        float y1 = static_cast<float>(std::max(boxStart.y, boxEnd.y));

        // Group-select boxed markers. Divisions all share one home building (v1),
        // matching the move command's single-source model.
        Building* home = nullptr;
        std::vector<int> ids;
        const auto& markers = owner->divisionMapOverlay != nullptr
            ? owner->divisionMapOverlay->markers
            : divisionMapWidget.markers;
        for (const auto& marker : markers)
        {
            if (marker.homeBuilding == nullptr)
                continue;
            if (marker.screenPos.x < x0 || marker.screenPos.x > x1 ||
                marker.screenPos.y < y0 || marker.screenPos.y > y1)
                continue;
            if (home == nullptr)
                home = marker.homeBuilding;
            if (marker.homeBuilding == home)
                for (int id : marker.divisionIds)
                    ids.push_back(id);
        }

        if (home != nullptr && !ids.empty())
        {
            militaryDivisionBarWidget.building = home;
            militaryDivisionBarWidget.selectedDivisionIds = ids;
            isDivisionOnlyMode = true;
            isBuildingSelected = false;
            Log::Msg("[Input]", "Box-selected ", ids.size(), " divisions");
        }
        else
        {
            militaryDivisionBarWidget.selectedDivisionIds.clear();
            isDivisionOnlyMode = false;
        }
    }
    else
    {
        // Plain click on open ground clears the division selection.
        militaryDivisionBarWidget.selectedDivisionIds.clear();
        isDivisionOnlyMode = false;
    }

    pendingBox = false;
    boxActive = false;
    moveTargetWidget.drawBox = false;
}

// Initializes BasicMapViewSystem::RmbPressed.
void BasicMapViewSystem::RmbPressed()
{
    GuiPanel* activePanel = researchPanel.HasBuilding()
        ? static_cast<GuiPanel*>(&researchPanel)
        : &buildingInfoPanel;
    auto mousePos = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mousePos.x), static_cast<int>(mousePos.y)};
    if (isBuildingSelected && activePanel->ContainsPoint(screenPos))
    {
        cameraMovement.isMoving = false;
        return;
    }

    // Division attack: RMB on an enemy counter moves selected divisions adjacent
    // to it; field combat starts once they arrive.
    {
        const auto& divIds = militaryDivisionBarWidget.selectedDivisionIds;
        Building* home = militaryDivisionBarWidget.building;
        if (!divIds.empty() && home != nullptr &&
            !militaryDivisionBarWidget.ContainsPoint(screenPos))
        {
            Player* localPlayer = LocalPlayer(scene);
            const DivisionMapMarker* hit = owner->divisionMapOverlay != nullptr
                ? owner->divisionMapOverlay->HitTest(screenPos)
                : nullptr;
            if (hit != nullptr && hit->owner != nullptr && hit->owner != localPlayer && hit->tile.x >= 0)
            {
                int targetTileId = scene->game->tilemap.GetIdFromCoords(hit->tile);
                for (int divId : divIds)
                {
                    scene->SubmitLocalCommand(GameCommand::AttackTile(
                        scene->game->GetLocalPlayerId(), home->positionId, divId, targetTileId));
                    Log::Msg("[Input]", "division #", divId, " attack order against enemy division at ",
                             hit->tile.x, ",", hit->tile.y);
                }
                cameraMovement.isMoving = false;
                return;
            }

            // Division group move: RMB on owned ground spreads them one-per-tile
            // inside the clicked 2x2 sector. RMB on a building falls through to
            // order/receiver logic below.
            Vec2i tilePos = ScreenToTile(scene, mousePos);
            if (tilePos.x >= 0 && tilePos.y >= 0 &&
                scene->game->tilemap.GetBuilding(tilePos) == nullptr)
            {
                int tileId = scene->game->tilemap.GetIdFromCoords(tilePos);
                for (int divId : divIds)
                {
                    scene->SubmitLocalCommand(GameCommand::MoveDivision(
                        scene->game->GetLocalPlayerId(), home->positionId, divId, tileId));
                    Log::Msg("[Input]", "division #", divId, " ordered to sector at ",
                             tilePos.x, ",", tilePos.y);
                }
                cameraMovement.isMoving = false;
                return;
            }
        }
    }

    if (isBuildingSelected && activePanel->HasBuilding())
    {
        Vec2i tilePos = ScreenToTile(scene, mousePos);
        if (tilePos.x >= 0 && tilePos.y >= 0)
        {
            auto* selected = activePanel->GetBuilding();
            auto* receiver = scene->game->tilemap.GetBuilding(tilePos);
            if (selected != nullptr && receiver != nullptr && selected != receiver)
            {
                auto* selectedGarrison = selected->GetComponent<GarrisonComponent>();
                bool receiverHasTerritory = receiver->HasComponent<TerritoryComponent>();
                bool receiverHasGarrison = receiver->HasComponent<GarrisonComponent>();
                const auto& divIds = militaryDivisionBarWidget.selectedDivisionIds;
                if (selectedGarrison != nullptr && receiverHasTerritory && !divIds.empty())
                {
                    if (selected->owner != receiver->owner)
                    {
                        for (int divId : divIds)
                        {
                            scene->SubmitLocalCommand(GameCommand::IssueMilitaryOrder(scene->game->GetLocalPlayerId(), MilitaryOrderType::Attack, selected->positionId, receiver->positionId, divId));
                            Log::Msg("[Input]", selected->name, " division #", divId, " attack order against ", receiver->name);
                        }
                        return;
                    }

                    if (selected->owner == receiver->owner && receiverHasGarrison)
                    {
                        for (int divId : divIds)
                        {
                            scene->SubmitLocalCommand(GameCommand::IssueMilitaryOrder(scene->game->GetLocalPlayerId(), MilitaryOrderType::Support, selected->positionId, receiver->positionId, divId));
                            Log::Msg("[Input]", selected->name, " division #", divId, " support order for ", receiver->name);
                        }
                        return;
                    }
                }

                // No division selected — only allow logistics receiver assignment
                bool alternativeReceiver = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
                scene->SubmitLocalCommand(GameCommand::SetReceiver(scene->game->GetLocalPlayerId(), selected->positionId, receiver->positionId, alternativeReceiver));
                Log::Msg("[Input]", receiver->name, alternativeReceiver ? " set as alternative receiver for " : " set as receiver for ", selected->name);
                return;
            }
        }
    }

    // Camera panning lives on the middle mouse button now (see mmbp/mmbr), so the
    // right button is free for movement and combat orders.
}

// Initializes BasicMapViewSystem::RmbReleased.
void BasicMapViewSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

// Initializes BasicMapViewSystem::Scroll.
void BasicMapViewSystem::Scroll()
{
    auto mouse = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
    GuiPanel* activePanel = researchPanel.HasBuilding()
        ? static_cast<GuiPanel*>(&researchPanel)
        : &buildingInfoPanel;
    if (isBuildingSelected && activePanel->ContainsPoint(screenPos))
    {
        if (researchPanel.HasBuilding() && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)))
        {
            researchPanel.treePanOffset.y += GetMouseWheelMove() * 64.0f;
            return;
        }
        if (researchPanel.HasBuilding())
        {
            researchPanel.AdjustTreeZoom(screenPos, GetMouseWheelMove());
            return;
        }
        activePanel->ScrollContent(GetMouseWheelMove());
        return;
    }

    ZoomCamera(scene);
}

// Submits this command to the simulation.
void BasicMapViewSystem::SubmitRecruitCommand(Building* building, MilitaryUnitType unitType)
{
    if (scene == nullptr || scene->game == nullptr || building == nullptr)
        return;

    scene->SubmitLocalCommand(GameCommand::RecruitUnit(scene->game->GetLocalPlayerId(), building->positionId, unitType));
}

// Advances this object's state for one frame.
void SelectedBuildingWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr || building == nullptr)
        return;

    for (const auto& supplier : building->GetSupplierViews())
    {
        if (supplier.building == nullptr)
            continue;

        Rectangle supplierDest = BuildingScreenRect(scene, supplier.building);
        DrawRectangleRounded(supplierDest, 0.04f, 8, Color{73, 146, 236, 48});
        DrawRectangleRoundedLines(supplierDest, 0.04f, 8, 1.0f, Color{96, 174, 255, 190});
    }

    Rectangle dest = BuildingScreenRect(scene, building);
    DrawRectangleRounded(dest, 0.04f, 8, Color{88, 196, 124, 55});
    DrawRectangleRoundedLines(dest, 0.04f, 8, 1.0f, Color{112, 230, 150, 185});
}

// Advances this object's state for one frame.
void ProductionWarningWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    auto localPlayerIt = scene->game->playerHandler.players.find(scene->game->GetLocalPlayerId());
    Player* localPlayer = localPlayerIt != scene->game->playerHandler.players.end() ? localPlayerIt->second.get() : nullptr;
    if (localPlayer == nullptr)
        return;

    for (auto* building : localPlayer->GetTrackedBuildings())
    {
        if (building == nullptr || !building->IsProductionStalled())
            continue;

        Rectangle dest = BuildingScreenRect(scene, building);
        DrawRectangleRounded(dest, 0.04f, 8, Color{236, 184, 62, 42});
        DrawRectangleRoundedLines(dest, 0.04f, 8, 1.0f, Color{255, 211, 84, 210});
    }
}

// Advances this object's state for one frame.
void MilitaryOrderWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    auto localPlayerIt = scene->game->playerHandler.players.find(scene->game->GetLocalPlayerId());
    Player* localPlayer = localPlayerIt != scene->game->playerHandler.players.end() ? localPlayerIt->second.get() : nullptr;
    if (localPlayer == nullptr)
        return;

    for (auto* building : localPlayer->GetTrackedBuildingsWithComponent<GarrisonComponent>())
    {
        auto* garrison = building != nullptr ? building->GetComponent<GarrisonComponent>() : nullptr;
        if (garrison == nullptr || building->HasComponent<RecruitmentComponent>())
            continue;

        if (garrison->currentOrder != MilitaryOrderType::None)
        {
            Building* target = scene->game->tilemap.GetBuilding(garrison->orderTargetId);
            if (target != nullptr)
                DrawOrderArrow(BuildingScreenCenter(scene, building), BuildingScreenCenter(scene, target), MilitaryOrderColor(garrison->currentOrder));
        }

        for (const auto& division : garrison->divisions)
        {
            if (division.currentOrder == MilitaryOrderType::None)
                continue;

            Building* target = scene->game->tilemap.GetBuilding(division.orderTargetPositionId);
            if (target == nullptr)
                continue;

            // Arrow origin: division's current world position (if in transit, else building center)
            Vector2 divScreen;
            if (division.inTransit && division.worldPos.x >= 0.0f)
            {
                Vec2f s = scene->render.WorldToScreen(division.worldPos);
                divScreen = {s.x, s.y};
            }
            else
            {
                divScreen = BuildingScreenCenter(scene, building);
            }
            DrawOrderArrow(divScreen, BuildingScreenCenter(scene, target), MilitaryOrderColor(division.currentOrder));
        }
    }

    // Draw battle indicator circles at midpoints of active attack lines
    for (const auto& battle : scene->game->battles.GetBattles())
    {
        if (battle.IsOver()) continue;
        Building* atk = scene->game->tilemap.GetBuilding(battle.attackerTileId);
        Building* def = scene->game->tilemap.GetBuilding(battle.defenderTileId);
        if (atk == nullptr || def == nullptr) continue;

        Vector2 atkPos = BuildingScreenCenter(scene, atk);
        Vector2 defPos = BuildingScreenCenter(scene, def);
        Vector2 mid{(atkPos.x + defPos.x) * 0.5f, (atkPos.y + defPos.y) * 0.5f};

        DrawCircle((int)mid.x, (int)mid.y, 14.0f, Color{30, 16, 16, 200});
        DrawCircleLines((int)mid.x, (int)mid.y, 14.0f, Color{220, 80, 50, 255});
        DrawText("VS", (int)(mid.x - 9), (int)(mid.y - 7), 13, Color{255, 210, 90, 255});
    }

    // Field battles: a circle between every pair of engaged enemy divisions.
    battleMarkers.clear();
    struct EngagedUnit { int playerId; int divId; Vec2i tile; Vec2f world; };
    std::vector<EngagedUnit> engaged;
    for (auto& [pid, player] : scene->game->playerHandler.players)
    {
        if (player == nullptr) continue;
        for (Building* b : player->GetTrackedBuildingsWithComponent<GarrisonComponent>())
        {
            auto* g = b != nullptr ? b->GetComponent<GarrisonComponent>() : nullptr;
            if (g == nullptr) continue;
            for (const auto& d : g->divisions)
                if (d.engaged && d.occupiedTile.x >= 0 && d.worldPos.x >= 0.0f)
                    engaged.push_back({pid, d.id, d.occupiedTile, d.worldPos});
        }
    }
    for (size_t i = 0; i < engaged.size(); i++)
        for (size_t j = i + 1; j < engaged.size(); j++)
        {
            if (engaged[i].playerId == engaged[j].playerId) continue;
            if (std::abs(engaged[i].tile.x - engaged[j].tile.x) > 1 ||
                std::abs(engaged[i].tile.y - engaged[j].tile.y) > 1) continue;

            Vec2f a = scene->render.WorldToScreen(engaged[i].world);
            Vec2f b = scene->render.WorldToScreen(engaged[j].world);
            Vector2 mid{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
            float pulse = 13.0f + 2.0f * std::sin(static_cast<float>(GetTime()) * 6.0f);
            DrawCircle((int)mid.x, (int)mid.y, pulse + 2.0f, Color{30, 16, 16, 210});
            DrawCircleLines((int)mid.x, (int)mid.y, pulse, Color{235, 90, 55, 255});
            // Crossed-swords icon (two diagonal strokes).
            Color blade{255, 215, 110, 255};
            DrawLineEx({mid.x - 6, mid.y - 6}, {mid.x + 6, mid.y + 6}, 2.0f, blade);
            DrawLineEx({mid.x + 6, mid.y - 6}, {mid.x - 6, mid.y + 6}, 2.0f, blade);

            battleMarkers.push_back({mid, engaged[i].playerId, engaged[i].divId,
                                     engaged[j].playerId, engaged[j].divId});
        }

    // Battle details panel (opened by clicking a circle).
    if (detailsOpen)
    {
        auto find = [&](int playerId, int divId, Player*& ownerOut) -> const SoldierDivision*
        {
            auto pit = scene->game->playerHandler.players.find(playerId);
            Player* p = pit != scene->game->playerHandler.players.end() ? pit->second.get() : nullptr;
            if (p == nullptr) return nullptr;
            for (Building* bb : p->GetTrackedBuildingsWithComponent<GarrisonComponent>())
            {
                auto* g = bb != nullptr ? bb->GetComponent<GarrisonComponent>() : nullptr;
                if (g == nullptr) continue;
                for (const auto& d : g->divisions)
                    if (d.id == divId) { ownerOut = p; return &d; }
            }
            return nullptr;
        };

        Player* ownerA = nullptr;
        Player* ownerB = nullptr;
        const SoldierDivision* dA = find(selPlayerA, selDivA, ownerA);
        const SoldierDivision* dB = find(selPlayerB, selDivB, ownerB);
        if (dA == nullptr || dB == nullptr)
        {
            detailsOpen = false;  // a participant is gone — battle over
        }
        else
        {
            float w = 340.0f, h = 132.0f;
            Rectangle panel{GetScreenWidth() * 0.5f - w * 0.5f, GetScreenHeight() * 0.15f, w, h};
            DrawRectangleRounded(panel, 0.06f, 8, Color{18, 22, 30, 240});
            DrawRectangleRoundedLines(panel, 0.06f, 8, 1.0f, Color{170, 100, 80, 235});
            DrawText("FIELD BATTLE", (int)(panel.x + 12), (int)(panel.y + 8), 16, Color{240, 160, 120, 255});
            DrawText("VS", (int)(panel.x + w * 0.5f - 9), (int)(panel.y + 58), 16, Color{240, 160, 120, 255});

            auto drawSide = [&](const SoldierDivision* d, Player* owner, float x)
            {
                Color col = owner != nullptr ? owner->color : WHITE;
                std::string name = "#" + std::to_string(d->id) + " " + MilitaryUnitLabel(d->type);
                DrawText(name.c_str(), (int)x, (int)(panel.y + 34), 14, col);
                float ratio = d->maxHealth > 0 ? std::clamp(d->health / (float)d->maxHealth, 0.0f, 1.0f) : 0.0f;
                Rectangle bar{x, panel.y + 56, w * 0.38f, 8};
                DrawRectangleRec(bar, Color{20, 24, 30, 255});
                Rectangle fill = bar; fill.width *= ratio;
                DrawRectangleRec(fill, Color{89, 197, 121, 255});
                DrawText(("HP " + std::to_string(std::max(0, d->health)) + "/" + std::to_string(d->maxHealth)).c_str(),
                         (int)x, (int)(panel.y + 70), 12, RAYWHITE);
                DivisionCombatStats cs = ComputeDivisionCombatStats(*d, owner != nullptr ? &owner->balanceModifiers : nullptr);
                DrawText(("Atk " + std::to_string((int)std::lround(cs.lightAttack)) +
                          "  Def " + std::to_string((int)std::lround(cs.defense))).c_str(),
                         (int)x, (int)(panel.y + 88), 12, Color{205, 212, 224, 255});
            };
            drawSide(dA, ownerA, panel.x + 14.0f);
            drawSide(dB, ownerB, panel.x + w * 0.5f + 10.0f);
        }
    }
}

const FieldBattleMarker* MilitaryOrderWidget::HitTest(Vec2i point) const
{
    for (const auto& m : battleMarkers)
    {
        float dx = point.x - m.screenPos.x;
        float dy = point.y - m.screenPos.y;
        if (dx * dx + dy * dy <= 18.0f * 18.0f)
            return &m;
    }
    return nullptr;
}

// Returns true if a division is in the current selection group.
bool MilitaryDivisionBarWidget::IsSelected(int divId) const
{
    for (int id : selectedDivisionIds)
        if (id == divId) return true;
    return false;
}

// Selects or group-toggles a division card in the bottom military strip.
// Ctrl+LMB adds/removes from group; plain LMB replaces selection.
bool MilitaryDivisionBarWidget::HandleClick(Vec2i point)
{
    auto* garrison = building != nullptr ? building->GetComponent<GarrisonComponent>() : nullptr;
    if (building == nullptr || garrison == nullptr || garrison->divisions.empty() || !ContainsPoint(point))
        return false;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    const float stripH = 28.0f;
    const float gap = 4.0f;
    float y = bounds.y + 36.0f;
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    for (const auto& division : garrison->divisions)
    {
        Rectangle strip{bounds.x + 8.0f, y, bounds.width - 16.0f, stripH};
        if (CheckCollisionPointRec(Vector2{static_cast<float>(point.x), static_cast<float>(point.y)}, strip))
        {
            if (ctrl)
            {
                auto it = std::find(selectedDivisionIds.begin(), selectedDivisionIds.end(), division.id);
                if (it != selectedDivisionIds.end())
                    selectedDivisionIds.erase(it);
                else
                    selectedDivisionIds.push_back(division.id);
            }
            else
            {
                selectedDivisionIds = {division.id};
            }
            return true;
        }
        y += stripH + gap;
        if (y + stripH > bounds.y + bounds.height - 8.0f)
            break;
    }

    return false;
}

// Draws stationed divisions for the selected military building.
void MilitaryDivisionBarWidget::Update(double dt)
{
    if (building == nullptr)
        return;

    auto* garrison = building->GetComponent<GarrisonComponent>();
    if (garrison == nullptr)
        return;

    // Clear selection when building changes
    if (building != prevBuilding)
    {
        selectedDivisionIds.clear();
        prevBuilding = building;
    }

    // Remove ids for divisions that no longer exist
    selectedDivisionIds.erase(
        std::remove_if(selectedDivisionIds.begin(), selectedDivisionIds.end(),
            [&garrison](int id) {
                for (const auto& div : garrison->divisions)
                    if (div.id == id) return false;
                return true;
            }),
        selectedDivisionIds.end());

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    DrawRectangleRounded(bounds, 0.04f, 8, Color{20, 24, 30, 232});
    DrawRectangleRoundedLines(bounds, 0.04f, 8, 1.0f, Color{86, 98, 116, 235});
    UiText::DrawFit("Divisions — " + building->name,
        Rectangle{bounds.x + 12.0f, bounds.y + 8.0f, bounds.width - 24.0f, 22.0f}, 18, RAYWHITE);

    if (garrison->divisions.empty())
    {
        UiText::DrawFit("No divisions",
            Rectangle{bounds.x + 12.0f, bounds.y + 40.0f, bounds.width - 24.0f, 24.0f}, 16, Color{190, 198, 208, 255});
        return;
    }

    // HOI4-style vertical list: one thin strip per division.
    Vector2 mouse = GetMousePosition();
    const MilitaryDivision* hovered = nullptr;
    const float stripH = 28.0f;
    const float gap = 4.0f;
    float y = bounds.y + 36.0f;
    for (const auto& division : garrison->divisions)
    {
        if (y + stripH > bounds.y + bounds.height - 8.0f)
            break;

        Rectangle strip{bounds.x + 8.0f, y, bounds.width - 16.0f, stripH};
        bool isHovered = CheckCollisionPointRec(mouse, strip);
        bool isSelected = IsSelected(division.id);
        if (isHovered)
            hovered = &division;

        Color accent = DivisionColor(division.type);
        DrawRectangleRounded(strip, 0.25f, 6, isSelected ? Color{46, 58, 74, 250} : Color{30, 36, 45, 242});
        DrawRectangleRoundedLines(strip, 0.25f, 6, 1.0f,
                                  isHovered || isSelected ? accent : Color{70, 80, 96, 230});
        DrawRectangleRec({strip.x, strip.y + 3.0f, 4.0f, strip.height - 6.0f}, accent);  // unit-type tab

        std::string label = "#" + std::to_string(division.id) + " " + MilitaryUnitLabel(division.type);
        UiText::DrawFit(label, Rectangle{strip.x + 12.0f, strip.y + 3.0f, strip.width * 0.52f, 22.0f}, 15, RAYWHITE);

        // March / order badge.
        if (division.inTransit)
            UiText::DrawFit("MARCH", Rectangle{strip.x + strip.width * 0.52f, strip.y + 6.0f, 46.0f, 14.0f},
                            12, Color{130, 200, 255, 255});
        else if (division.currentOrder != MilitaryOrderType::None)
            UiText::DrawFit("ORDER", Rectangle{strip.x + strip.width * 0.52f, strip.y + 6.0f, 46.0f, 14.0f},
                            12, MilitaryOrderColor(division.currentOrder));

        // HP + supply mini-bars on the right.
        float barW = strip.width * 0.24f;
        float barX = strip.x + strip.width - barW - 8.0f;
        Rectangle hpBar{barX, strip.y + 5.0f, barW, 6.0f};
        Rectangle supBar{barX, strip.y + 15.0f, barW, 6.0f};
        DrawRectangleRounded(hpBar, 0.4f, 4, Color{18, 22, 28, 255});
        DrawRectangleRounded(supBar, 0.4f, 4, Color{18, 22, 28, 255});
        Rectangle hpFill = hpBar;
        hpFill.width *= division.HealthRatio();
        DrawRectangleRounded(hpFill, 0.4f, 4, Color{89, 197, 121, 255});
        Rectangle supFill = supBar;
        supFill.width *= division.weaponSupplyCapacity > 0
            ? std::clamp(division.weaponSupply / static_cast<float>(division.weaponSupplyCapacity), 0.0f, 1.0f) : 0.0f;
        DrawRectangleRounded(supFill, 0.4f, 4, Color{126, 142, 162, 255});

        y += stripH + gap;
    }

    if (hovered != nullptr)
    {
        Tooltip::Draw(std::string(MilitaryUnitLabel(hovered->type)) + " division #" + std::to_string(hovered->id), {
            "Health: " + std::to_string(hovered->health) + "/" + std::to_string(hovered->maxHealth),
            "Strength: " + std::to_string(hovered->strength),
            "Endurance: " + std::to_string(hovered->endurance),
            "Morale: " + std::to_string(hovered->morale),
            "Experience: " + std::to_string(hovered->experience),
            "Speed: " + FormatOneDecimal(hovered->speedTilesPerMinute) + " tiles/min",
            "Food: " + std::to_string(hovered->foodSupply) + "/" + std::to_string(hovered->foodSupplyCapacity),
            "Weapon supply: " + std::to_string(hovered->weaponSupply) + "/" + std::to_string(hovered->weaponSupplyCapacity),
            "Order: " + std::string(MilitaryOrderLabel(hovered->currentOrder)),
            "Weapon: " + EquipmentLabel(hovered->equipment.weapon),
            "Armor: " + EquipmentLabel(hovered->equipment.armor),
            "Ranged: " + EquipmentLabel(hovered->equipment.rangedWeapon),
            "Ammo: " + EquipmentLabel(hovered->equipment.ammo)
        }, 310.0f);
    }
}

// ─── ArmyBarWidget ────────────────────────────────────────────────────────────

void ArmyBarWidget::Update(double dt)
{
    cardRects.clear();
    plusRect = {0, 0, 0, 0};
    contentBounds = {0, 0, 0, 0};

    Player* localPlayer = LocalPlayer(scene);
    if (localPlayer == nullptr)
        return;

    // No fixed background panel — a screen-centered HBox of floating cards that
    // grows symmetrically as armies are added, plus a trailing "+".
    Vector2 mouse = GetMousePosition();

    const float cardW = 132.0f;
    const float cardH = static_cast<float>(size.y) - 8.0f;
    const float gap = 8.0f;
    const float plusW = 44.0f;
    const float y = static_cast<float>(pos.y) + 4.0f;

    const auto& armies = localPlayer->armyGroups.GetArmies();
    float screenW = static_cast<float>(GetScreenWidth());
    int maxCards = std::max(0, static_cast<int>((screenW - 80.0f - plusW) / (cardW + gap)));
    int shown = std::min(static_cast<int>(armies.size()), maxCards);

    float contentW = shown * (cardW + gap) + plusW;
    float x = screenW * 0.5f - contentW * 0.5f;
    contentBounds = {x - 6.0f, y - 4.0f, contentW + 12.0f, cardH + 8.0f};

    for (int i = 0; i < shown; i++)
    {
        const ArmyGroup& army = armies[i];
        Rectangle card{x, y, cardW, cardH};
        bool hovered = CheckCollisionPointRec(mouse, card);
        DrawRectangleRounded(card, 0.18f, 6, hovered ? Color{46, 56, 70, 252} : Color{28, 34, 43, 244});
        DrawRectangleRoundedLines(card, 0.18f, 6, 1.0f, Color{182, 160, 100, 220});
        UiText::DrawFit(army.name, Rectangle{card.x + 8.0f, card.y + 5.0f, card.width - 16.0f, 18.0f}, 15, RAYWHITE);
        UiText::DrawFit(std::to_string(army.divisions.size()) + " divisions",
                        Rectangle{card.x + 8.0f, card.y + 25.0f, card.width - 16.0f, 16.0f}, 13,
                        Color{190, 198, 208, 255});
        cardRects.emplace_back(army.id, card);
        x += cardW + gap;
    }

    // Trailing "+" — always present so more armies can be created.
    bool canForm = bar != nullptr && bar->building != nullptr && !bar->selectedDivisionIds.empty();
    plusRect = {x, y, plusW, cardH};
    bool hovered = CheckCollisionPointRec(mouse, plusRect);
    DrawRectangleRounded(plusRect, 0.2f, 6, hovered ? Color{44, 54, 68, 252} : Color{26, 32, 41, 240});
    DrawRectangleRoundedLines(plusRect, 0.2f, 6, 1.0f,
                              canForm ? Color{120, 220, 150, 235} : Color{96, 108, 124, 190});
    UiText::DrawFit("+", plusRect, 26, canForm ? Color{150, 235, 175, 255} : Color{130, 140, 152, 255});
}

bool ArmyBarWidget::IsOverContent(Vec2i point) const
{
    Vector2 p{static_cast<float>(point.x), static_cast<float>(point.y)};
    return CheckCollisionPointRec(p, contentBounds);
}

bool ArmyBarWidget::HandleClick(Vec2i point)
{
    Player* localPlayer = LocalPlayer(scene);
    if (localPlayer == nullptr)
        return false;

    Vector2 click{static_cast<float>(point.x), static_cast<float>(point.y)};

    // Clicks anywhere on the strip are consumed (so they don't deselect via the
    // map underneath); only cards / "+" trigger an action.
    if (!CheckCollisionPointRec(click, contentBounds))
        return false;

    // "+" groups the current selection into a new army.
    if (CheckCollisionPointRec(click, plusRect))
    {
        if (bar != nullptr && bar->building != nullptr && !bar->selectedDivisionIds.empty())
        {
            scene->SubmitLocalCommand(GameCommand::FormArmy(
                scene->game->GetLocalPlayerId(), bar->building->positionId, bar->selectedDivisionIds));
            Log::Msg("[Input]", "Form-army requested for ", bar->selectedDivisionIds.size(), " divisions");
        }
        return true;
    }

    // Clicking an army card selects its divisions (single-home v1).
    for (const auto& [armyId, rect] : cardRects)
    {
        if (!CheckCollisionPointRec(click, rect))
            continue;
        const ArmyGroup* army = localPlayer->armyGroups.FindArmy(armyId);
        if (army != nullptr && bar != nullptr && !army->divisions.empty())
        {
            Building* home = scene->game->tilemap.GetBuilding(army->divisions.front().homeTileId);
            if (home != nullptr)
            {
                bar->building = home;
                bar->selectedDivisionIds.clear();
                for (const auto& ref : army->divisions)
                    if (ref.homeTileId == home->positionId)
                        bar->selectedDivisionIds.push_back(ref.divisionId);
            }
        }
        return true;
    }

    // Clicked the strip but not a card/"+" — still consume so it doesn't deselect.
    return true;
}

// Advances this object's state for one frame.
void StrategicResourceHudWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* player = LocalPlayer(scene);
    if (player == nullptr)
        return;

    PlayerStatsSnapshot stats = BuildPlayerStatsSnapshot(player);
    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    DrawRectangle(0, 0, GetScreenWidth(), static_cast<int>(bounds.height), Color{21, 25, 31, 238});
    DrawRectangleGradientV(0, 0, GetScreenWidth(), static_cast<int>(bounds.height), Color{38, 46, 57, 230}, Color{18, 22, 28, 238});
    DrawRectangle(0, static_cast<int>(bounds.height - 2.0f), GetScreenWidth(), 2, Color{95, 109, 129, 235});

    float iconSize = std::max(26.0f, bounds.height - 20.0f);
    float y = bounds.y + (bounds.height - iconSize) * 0.5f;
    Rectangle manpowerIcon{bounds.x + 18.0f, y, iconSize, iconSize};
    Rectangle foodIcon{bounds.x + 150.0f, y, iconSize, iconSize};

    auto drawManpowerIcon = [&](Rectangle icon)
    {
        DrawCircle(static_cast<int>(icon.x + icon.width * 0.5f), static_cast<int>(icon.y + icon.height * 0.28f), icon.width * 0.16f, Color{214, 226, 238, 255});
        DrawRectangleRounded(Rectangle{icon.x + icon.width * 0.28f, icon.y + icon.height * 0.46f, icon.width * 0.44f, icon.height * 0.38f}, 0.30f, 8, Color{214, 226, 238, 255});
    };

    auto drawStatChip = [&](Rectangle icon, const std::string& text, Color accent, std::function<void(Rectangle)> drawIcon)
    {
        Rectangle chip{icon.x - 6.0f, icon.y - 4.0f, 120.0f, icon.height + 8.0f};
        DrawRectangleRounded(chip, 0.16f, 8, Color{34, 40, 49, 232});
        DrawRectangleRoundedLines(chip, 0.16f, 8, 1.0f, Color{accent.r, accent.g, accent.b, 170});
        DrawRectangleRounded(icon, 0.16f, 8, Color{42, 49, 60, 238});
        drawIcon(icon);
        UiText::DrawFit(text, Rectangle{icon.x + icon.width + 8.0f, chip.y + 5.0f, chip.width - icon.width - 14.0f, chip.height - 10.0f}, 22, RAYWHITE);
    };

    auto drawResourceChip = [&](ResourceType type, float x)
    {
        Rectangle chip{x, y - 4.0f, 116.0f, iconSize + 8.0f};
        Rectangle icon{chip.x + 6.0f, y, iconSize, iconSize};
        int amount = 0;
        auto it = stats.storedResources.find(type);
        if (it != stats.storedResources.end())
            amount = it->second;

        DrawRectangleRounded(chip, 0.16f, 8, Color{33, 39, 48, 228});
        DrawRectangleRoundedLines(chip, 0.16f, 8, 1.0f, Color{83, 95, 114, 220});
        GuiPanel::DrawResourceIcon(type, Rectangle{icon.x + 3.0f, icon.y + 3.0f, icon.width - 6.0f, icon.height - 6.0f});
        UiText::DrawFit(std::to_string(amount), Rectangle{icon.x + icon.width + 8.0f, chip.y + 5.0f, chip.width - icon.width - 16.0f, chip.height - 10.0f}, 21, Color{229, 235, 242, 255});
    };

    drawStatChip(manpowerIcon, std::to_string(stats.freeManpower), Color{157, 190, 225, 255}, drawManpowerIcon);

    drawStatChip(foodIcon, std::to_string(stats.foodSupplyPercent) + "%", Color{145, 198, 118, 255}, [&](Rectangle icon)
    {
        GuiPanel::DrawResourceIcon(ResourceType::MEAT, Rectangle{icon.x + 4.0f, icon.y + 4.0f, icon.width - 8.0f, icon.height - 8.0f});
    });

    Rectangle statsButton = StatsHudButtonRect(*this);
    Rectangle focusButton = FocusHudButtonRect(*this);
    Rectangle destroyButton = DestroyHudButtonRect(*this);
    Rectangle roadButton = RoadHudButtonRect(*this);
    Rectangle buildButton = BuildHudButtonRect(*this);

    float resourceX = foodIcon.x + 130.0f;
    float resourceRightLimit = buildButton.x - 12.0f;
    bool showWood = resourceX + 116.0f <= resourceRightLimit;
    bool showStone = resourceX + 242.0f <= resourceRightLimit;
    bool showPlanks = resourceX + 368.0f <= resourceRightLimit;
    if (showWood)
        drawResourceChip(ResourceType::WOOD, resourceX);
    if (showStone)
        drawResourceChip(ResourceType::STONE, resourceX + 126.0f);
    if (showPlanks)
        drawResourceChip(ResourceType::PLANKS, resourceX + 252.0f);

    bool statsHovered = CheckCollisionPointRec(GetMousePosition(), statsButton);
    bool focusHovered = CheckCollisionPointRec(GetMousePosition(), focusButton);
    bool destroyHovered = CheckCollisionPointRec(GetMousePosition(), destroyButton);
    bool roadHovered = CheckCollisionPointRec(GetMousePosition(), roadButton);
    bool buildHovered = CheckCollisionPointRec(GetMousePosition(), buildButton);
    bool focusAvailable = player->focuses.GetActiveFocusId().empty();
    float focusProgress = static_cast<float>(player->focuses.GetActiveFocusProgress());
    float pulse = focusAvailable ? (0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 4.0f)) : 0.0f;

    auto drawHudButton = [&](Rectangle rect, const std::string& label, bool hovered, Color base, Color line)
    {
        DrawRectangleRounded(rect, 0.14f, 8, hovered ? Color{
            static_cast<unsigned char>(std::min(255, base.r + 24)),
            static_cast<unsigned char>(std::min(255, base.g + 24)),
            static_cast<unsigned char>(std::min(255, base.b + 24)),
            246} : base);
        DrawRectangleRoundedLines(rect, 0.14f, 8, 1.1f, hovered ? Color{
            static_cast<unsigned char>(std::min(255, line.r + 30)),
            static_cast<unsigned char>(std::min(255, line.g + 30)),
            static_cast<unsigned char>(std::min(255, line.b + 30)),
            248} : line);
        UiText::DrawFit(label, Rectangle{rect.x + 10.0f, rect.y + 4.0f, rect.width - 20.0f, rect.height - 8.0f}, 20, RAYWHITE);
    };

    drawHudButton(buildButton, "Build", buildHovered, Color{43, 60, 52, 238}, Color{92, 151, 118, 230});
    drawHudButton(roadButton, "Road", roadHovered, Color{44, 55, 68, 238}, Color{94, 134, 174, 230});
    drawHudButton(destroyButton, "Destroy", destroyHovered, Color{62, 45, 48, 238}, Color{157, 92, 100, 230});

    Color focusFill = focusAvailable
        ? Color{static_cast<unsigned char>(88 + pulse * 34.0f), static_cast<unsigned char>(68 + pulse * 36.0f), static_cast<unsigned char>(134 + pulse * 52.0f), 246}
        : Color{61, 48, 91, 238};
    Color focusLine = focusAvailable
        ? Color{210, 170, 255, 255}
        : Color{147, 120, 205, 238};
    if (focusHovered)
        focusFill = Color{112, 86, 166, 250};
    DrawRectangleRounded(focusButton, 0.14f, 8, focusFill);
    DrawRectangleRoundedLines(focusButton, 0.14f, 8, 1.5f, focusLine);
    if (focusAvailable)
        DrawRectangleRounded(Rectangle{focusButton.x + 4.0f, focusButton.y + 4.0f, focusButton.width - 8.0f, focusButton.height - 8.0f}, 0.14f, 8, Color{255, 221, 120, static_cast<unsigned char>(22 + pulse * 48.0f)});
    UiText::DrawFit("Focus Tree", Rectangle{focusButton.x + 14.0f, focusButton.y + 4.0f, focusButton.width - 28.0f, focusButton.height - 14.0f}, 21, RAYWHITE);
    Rectangle focusBar{focusButton.x + 12.0f, focusButton.y + focusButton.height - 9.0f, focusButton.width - 24.0f, 4.0f};
    DrawRectangleRounded(focusBar, 0.6f, 6, Color{26, 22, 34, 170});
    Rectangle focusFillBar = focusBar;
    focusFillBar.width *= std::clamp(focusProgress, 0.0f, 1.0f);
    if (focusFillBar.width >= 1.0f)
        DrawRectangleRounded(focusFillBar, 0.6f, 6, focusAvailable ? Color{255, 220, 116, 210} : Color{178, 140, 248, 235});

    DrawRectangleRounded(statsButton, 0.14f, 8, statsHovered ? Color{69, 83, 103, 245} : Color{43, 51, 64, 238});
    DrawRectangleRoundedLines(statsButton, 0.14f, 8, 1.2f, statsHovered ? Color{139, 166, 202, 245} : Color{88, 103, 124, 230});
    UiText::DrawFit("Resources", Rectangle{statsButton.x + 12.0f, statsButton.y + 4.0f, statsButton.width - 24.0f, statsButton.height - 8.0f}, 21, RAYWHITE);

    Vector2 mouse = GetMousePosition();
    if (CheckCollisionPointRec(mouse, Rectangle{manpowerIcon.x, bounds.y, 125.0f, bounds.height}))
    {
        Tooltip::Draw("Manpower", {
            "Free: " + std::to_string(stats.freeManpower),
            "Workers: " + std::to_string(stats.workers),
            "Soldiers: " + std::to_string(stats.soldiers),
            "Militia: " + std::to_string(stats.army.militia),
            "Swordsmen: " + std::to_string(stats.army.swordsmen),
            "Archers: " + std::to_string(stats.army.archers),
            "Training queue: " + std::to_string(stats.army.TotalQueued()),
            "Total: " + std::to_string(stats.totalPopulation) + "/" + std::to_string(stats.populationCap),
            "Gain: +" + FormatOneDecimal(stats.manpowerGainPerMinute) + " / min"
        }, 280.0f);
    }
    else if (CheckCollisionPointRec(mouse, Rectangle{foodIcon.x, bounds.y, 155.0f, bounds.height}))
    {
        double totalConsumption = stats.villageFoodConsumptionPerMinute + stats.militaryFoodConsumptionPerMinute;
        Tooltip::Draw("Food Supply", {
            "Supply: " + std::to_string(stats.foodSupplyPercent) + "%",
            "Village consumption: " + FormatOneDecimal(stats.villageFoodConsumptionPerMinute) + " / min",
            "Military consumption: " + FormatOneDecimal(stats.militaryFoodConsumptionPerMinute) + " / min",
            "Total consumption: " + FormatOneDecimal(totalConsumption) + " / min"
        }, 290.0f);
    }
    else if (showWood && CheckCollisionPointRec(mouse, Rectangle{resourceX, bounds.y, 116.0f, bounds.height}))
    {
        Tooltip::Draw("Wood", {"Stored: " + std::to_string(stats.storedResources[ResourceType::WOOD])}, 180.0f);
    }
    else if (showStone && CheckCollisionPointRec(mouse, Rectangle{resourceX + 126.0f, bounds.y, 116.0f, bounds.height}))
    {
        Tooltip::Draw("Stone", {"Stored: " + std::to_string(stats.storedResources[ResourceType::STONE])}, 180.0f);
    }
    else if (showPlanks && CheckCollisionPointRec(mouse, Rectangle{resourceX + 252.0f, bounds.y, 116.0f, bounds.height}))
    {
        Tooltip::Draw("Planks", {"Stored: " + std::to_string(stats.storedResources[ResourceType::PLANKS])}, 180.0f);
    }
    else if (buildHovered)
    {
        Tooltip::Draw("Build", {"[Q] Open build menu"}, 210.0f);
    }
    else if (roadHovered)
    {
        Tooltip::Draw("Road", {"[R] Open road building"}, 220.0f);
    }
    else if (destroyHovered)
    {
        Tooltip::Draw("Destroy", {"[D] Open destroy mode"}, 220.0f);
    }
    else if (statsHovered)
    {
        Tooltip::Draw("Resources", {"[S] Open economy overview"}, 240.0f);
    }
    else if (focusHovered)
    {
        Tooltip::Draw("Focus Tree", {
            "[F] Open political focus tree",
            focusAvailable ? "No active focus selected" : "Focus is already in progress",
            "Progress: " + std::to_string(static_cast<int>(std::round(focusProgress * 100.0f))) + "%"
        }, 240.0f);
    }
}

void StrategicResourceHudWidget::UpdateSize(Vec2i windowSize)
{
    UpdateStrategicHudLayout(*this, windowSize);
}

// Draws the player-wide economy and strategic statistics panel.
void StatsPanelWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* player = LocalPlayer(scene);
    if (player == nullptr)
        return;

    PlayerStatsSnapshot stats = BuildPlayerStatsSnapshot(player);
    const bool showingConsumption = selectedFlowMode == 1;
    const auto& currentRates = showingConsumption ? stats.consumptionRatesPerMinute : stats.productionRatesPerMinute;
    const auto& flowHistory = player->economyTelemetry.history;
    double historyTime = player->economyTelemetry.elapsedTime;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    DrawRectangleRounded(bounds, 0.025f, 8, Color{20, 24, 30, 244});
    DrawRectangleRoundedLines(bounds, 0.025f, 8, 1.0f, Color{100, 114, 136, 255});

    Rectangle title{bounds.x, bounds.y, bounds.width, 54.0f};
    DrawRectangleRounded(title, 0.025f, 8, Color{38, 48, 62, 255});
    UiText::DrawFit("Statistics", Rectangle{title.x + 20.0f, title.y + 10.0f, title.width * 0.45f, 34.0f}, 30, RAYWHITE);
    DrawCloseButton(bounds);

    constexpr std::array<int, 3> windows{15, 60, 300};
    const char* windowLabels[] = {"15 sec", "1 min", "5 min"};
    // Right edge is at title.width - 54, giving a 10px gap before the close button (at title.width - 44).
    Rectangle spin{title.x + title.width - 244.0f, title.y + 10.0f, 190.0f, 34.0f};
    DrawRectangleRounded(spin, 0.12f, 8, Color{24, 30, 38, 255});
    DrawRectangleRoundedLines(spin, 0.12f, 8, 1.0f, Color{90, 106, 128, 255});
    UiText::DrawFit("<", Rectangle{spin.x + 8.0f, spin.y + 4.0f, 24.0f, 24.0f}, 24, RAYWHITE);
    UiText::DrawFit(windowLabels[selectedWindowIndex], Rectangle{spin.x + 42.0f, spin.y + 5.0f, spin.width - 84.0f, 24.0f}, 22, RAYWHITE);
    UiText::DrawFit(">", Rectangle{spin.x + spin.width - 32.0f, spin.y + 4.0f, 24.0f, 24.0f}, 24, RAYWHITE);

    Rectangle modeToggle{title.x + title.width - 500.0f, title.y + 10.0f, 220.0f, 34.0f};
    const char* modeLabels[] = {"Production", "Consumption"};
    for (int i = 0; i < 2; i++)
    {
        Rectangle half{modeToggle.x + i * modeToggle.width * 0.5f, modeToggle.y, modeToggle.width * 0.5f, modeToggle.height};
        bool selected = selectedFlowMode == i;
        DrawRectangleRounded(half, 0.10f, 8, selected ? Color{54, 72, 92, 245} : Color{24, 30, 38, 235});
        DrawRectangleRoundedLines(half, 0.10f, 8, 1.0f, selected ? Color{130, 166, 210, 245} : Color{76, 88, 106, 220});
        UiText::DrawFit(modeLabels[i], Rectangle{half.x + 8.0f, half.y + 6.0f, half.width - 16.0f, 22.0f}, 18, RAYWHITE);
    }

    float colGap = 22.0f;
    float top = bounds.y + 78.0f;
    float bottom = bounds.y + bounds.height - 22.0f;
    Rectangle left{bounds.x + 24.0f, top, bounds.width * 0.31f, bottom - top};
    Rectangle chart{left.x + left.width + colGap, top, bounds.x + bounds.width - (left.x + left.width + colGap) - 24.0f, bottom - top};

    auto drawColumn = [](Rectangle col, const std::string& titleText)
    {
        DrawRectangleRounded(col, 0.035f, 8, Color{29, 34, 42, 232});
        DrawRectangleRoundedLines(col, 0.035f, 8, 1.0f, Color{74, 86, 104, 255});
        UiText::DrawFit(titleText, Rectangle{col.x + 14.0f, col.y + 12.0f, col.width - 28.0f, 26.0f}, 23, RAYWHITE);
    };

    auto drawRow = [](Rectangle col, int index, const std::string& label, const std::string& value, Color valueColor = RAYWHITE)
    {
        float y = col.y + 52.0f + index * 30.0f;
        UiText::DrawFit(label, Rectangle{col.x + 14.0f, y, col.width * 0.58f, 22.0f}, 20, Color{190, 201, 216, 255});
        UiText::DrawFit(value, Rectangle{col.x + col.width * 0.58f, y, col.width * 0.38f - 12.0f, 22.0f}, 20, valueColor);
    };

    drawColumn(left, "Population & army");
    drawRow(left, 0, "Free manpower", std::to_string(stats.freeManpower));
    drawRow(left, 1, "Workers", std::to_string(stats.workers));
    drawRow(left, 2, "Total / cap", std::to_string(stats.totalPopulation) + " / " + std::to_string(stats.populationCap));
    drawRow(left, 3, "Growth", "+" + FormatOneDecimal(stats.manpowerGainPerMinute) + " / min", Color{154, 238, 166, 255});
    drawRow(left, 4, "Food supply", std::to_string(stats.foodSupplyPercent) + "%", stats.foodSupplyPercent < 60 ? Color{248, 126, 126, 255} : Color{154, 238, 166, 255});
    drawRow(left, 5, "Food use", FormatOneDecimal(stats.villageFoodConsumptionPerMinute + stats.militaryFoodConsumptionPerMinute) + " / min");
    drawRow(left, 7, "Militia", std::to_string(stats.army.militia));
    drawRow(left, 8, "Swordsmen", std::to_string(stats.army.swordsmen));
    drawRow(left, 9, "Archers", std::to_string(stats.army.archers));
    drawRow(left, 10, "Training queue", std::to_string(stats.army.TotalQueued()));
    drawRow(left, 11, "Army strength", std::to_string(stats.army.strength));
    drawRow(left, 12, "Army supply", std::to_string(stats.army.supply) + " / " + std::to_string(stats.army.supplyCapacity));
    drawRow(left, 14, "Buildings", std::to_string(stats.buildingCount));
    drawRow(left, 15, "Roads", std::to_string(stats.roadCount));
    drawRow(left, 16, "Military buildings", std::to_string(
        player->GetTrackedBuildingCount(BuildingType::Headquarters, true) +
        player->GetTrackedBuildingCount(BuildingType::GuardTower, true) +
        player->GetTrackedBuildingCount(BuildingType::Fortress, true) +
        player->GetTrackedBuildingCount(BuildingType::Castle, true) +
        player->GetTrackedBuildingCount(BuildingType::Barracks, true)));
    drawRow(left, 17, "Build commands", std::to_string(player->GetAcceptedCommandCount(GameCommandType::BuildBuilding)));

    drawColumn(chart, showingConsumption ? "Consumption graph" : "Production graph");
    Rectangle plot{chart.x + 46.0f, chart.y + 64.0f, chart.width - 72.0f, chart.height - 142.0f};
    DrawRectangleRounded(plot, 0.02f, 8, Color{16, 20, 26, 255});
    for (int i = 1; i <= 4; i++)
    {
        float y = plot.y + plot.height * i / 5.0f;
        DrawLineEx(Vector2{plot.x, y}, Vector2{plot.x + plot.width, y}, 1.0f, Color{50, 58, 70, 180});
    }

    double windowSeconds = static_cast<double>(windows[selectedWindowIndex]);
    double startTime = std::max(0.0, historyTime - windowSeconds);
    double tickSeconds = selectedWindowIndex == 0 ? 1.0 : (selectedWindowIndex == 1 ? 5.0 : 15.0);
    double firstTick = std::floor(startTime / tickSeconds) * tickSeconds;
    for (double tick = firstTick; tick <= historyTime; tick += tickSeconds)
    {
        float age = static_cast<float>((historyTime - tick) / windowSeconds);
        float x = plot.x + plot.width - std::clamp(age, 0.0f, 1.0f) * plot.width;
        Color tickColor = std::fmod(tick, tickSeconds * 5.0) < 0.001 ? Color{76, 88, 106, 185} : Color{42, 50, 62, 150};
        DrawLineEx(Vector2{x, plot.y}, Vector2{x, plot.y + plot.height}, 1.0f, tickColor);
    }

    int maxObservedRate = 1;
    std::vector<ResourceType> activeResources;
    for (const auto& [type, rate] : currentRates)
    {
        if (rate > 0)
        {
            maxObservedRate = std::max(maxObservedRate, rate);
            activeResources.push_back(type);
        }
    }
    for (ResourceType type : resourceTypes)
    {
        bool visible = false;
        for (const auto& sample : flowHistory)
        {
            if (sample.time < startTime)
                continue;
            const auto& sampleRates = showingConsumption ? sample.consumptionRatesPerMinute : sample.productionRatesPerMinute;
            auto it = sampleRates.find(type);
            if (it != sampleRates.end() && it->second > 0)
            {
                maxObservedRate = std::max(maxObservedRate, it->second);
                visible = true;
            }
        }
        if (visible && std::find(activeResources.begin(), activeResources.end(), type) == activeResources.end())
            activeResources.push_back(type);
    }
    filterResources.assign(std::begin(resourceTypes), std::end(resourceTypes));

    std::vector<ResourceType> visibleResources;
    for (ResourceType type : filterResources)
    {
        bool active = std::find(activeResources.begin(), activeResources.end(), type) != activeResources.end();
        if ((selectedResources.empty() && active) || selectedResources.contains(type))
            visibleResources.push_back(type);
    }

    int maxRate = std::max(1, static_cast<int>(std::ceil(maxObservedRate * 1.18)));
    struct SeriesEndpoint
    {
        ResourceType type{ResourceType::Null};
        Vector2 point{};
        int rate{0};
        Color color{};
    };
    std::vector<SeriesEndpoint> endpoints;

    for (ResourceType type : visibleResources)
    {
        bool hasPrevious = false;
        Vector2 previous{};
        Vector2 lastPoint{};
        int lastRate = 0;
        Color color = ResourceChartColor(type);
        std::vector<ResourceFlowSnapshot> samples(flowHistory.begin(), flowHistory.end());
        samples.push_back(player->economyTelemetry.current);
        for (const auto& sample : samples)
        {
            if (sample.time < startTime)
                continue;
            int rate = 0;
            const auto& sampleRates = showingConsumption ? sample.consumptionRatesPerMinute : sample.productionRatesPerMinute;
            auto it = sampleRates.find(type);
            if (it != sampleRates.end())
                rate = it->second;
            float age = static_cast<float>((historyTime - sample.time) / windowSeconds);
            float x = plot.x + plot.width - std::clamp(age, 0.0f, 1.0f) * plot.width;
            float y = plot.y + plot.height - static_cast<float>(rate) / static_cast<float>(maxRate) * plot.height;
            Vector2 point{x, y};
            if (hasPrevious)
                DrawLineEx(previous, point, 2.0f, color);
            DrawCircleV(point, rate > 0 ? 3.0f : 2.0f, rate > 0 ? color : Color{72, 82, 96, 190});
            previous = point;
            lastPoint = point;
            lastRate = rate;
            hasPrevious = true;
        }
        if (hasPrevious && lastRate > 0)
            endpoints.push_back({type, lastPoint, lastRate, color});
    }

    UiText::DrawFit("0", Rectangle{plot.x - 28.0f, plot.y + plot.height - 16.0f, 24.0f, 16.0f}, 16, Color{160, 174, 190, 255});
    UiText::DrawFit(std::to_string(maxObservedRate) + "/m", Rectangle{plot.x - 42.0f, plot.y - 4.0f, 40.0f, 18.0f}, 16, Color{160, 174, 190, 255});
    UiText::DrawFit("-" + std::string(windowLabels[selectedWindowIndex]), Rectangle{plot.x, plot.y + plot.height + 6.0f, 54.0f, 18.0f}, 16, Color{160, 174, 190, 255});
    UiText::DrawFit("now", Rectangle{plot.x + plot.width - 36.0f, plot.y + plot.height + 6.0f, 36.0f, 18.0f}, 16, Color{160, 174, 190, 255});

    std::sort(endpoints.begin(), endpoints.end(), [](const SeriesEndpoint& a, const SeriesEndpoint& b)
    {
        return a.point.y < b.point.y;
    });
    float lastLabelY = plot.y - 18.0f;
    int endpointLabels = 0;
    for (const auto& endpoint : endpoints)
    {
        if (endpointLabels >= 8)
            break;
        float labelY = std::clamp(endpoint.point.y - 10.0f, plot.y + 4.0f, plot.y + plot.height - 22.0f);
        if (labelY < lastLabelY + 22.0f)
            labelY = std::min(plot.y + plot.height - 22.0f, lastLabelY + 22.0f);
        lastLabelY = labelY;

        std::string label = std::string(ResourceChipShortName(endpoint.type)) + " " +
            (showingConsumption ? "-" : "+") + std::to_string(endpoint.rate) + "/m";
        float labelWidth = std::min(104.0f, std::max(58.0f, static_cast<float>(MeasureText(label.c_str(), 13) + 18)));
        Rectangle chip{plot.x + plot.width - labelWidth - 6.0f, labelY, labelWidth, 18.0f};
        DrawLineEx(endpoint.point, Vector2{chip.x, chip.y + chip.height * 0.5f}, 1.0f, Color{endpoint.color.r, endpoint.color.g, endpoint.color.b, 180});
        DrawRectangleRounded(chip, 0.16f, 8, Color{18, 23, 30, 228});
        DrawRectangleRoundedLines(chip, 0.16f, 8, 1.0f, endpoint.color);
        DrawRectangleRounded(Rectangle{chip.x + 5.0f, chip.y + 5.0f, 7.0f, 7.0f}, 0.35f, 4, endpoint.color);
        UiText::DrawFit(label, Rectangle{chip.x + 15.0f, chip.y + 2.0f, chip.width - 19.0f, 14.0f}, 13, RAYWHITE);
        endpointLabels++;
    }

    Rectangle allButton = GetAllFilterButtonRect(chart);
    bool allActive = selectedResources.empty();
    DrawRectangleRounded(allButton, 0.16f, 8, allActive ? Color{55, 80, 64, 245} : Color{31, 37, 46, 240});
    DrawRectangleRoundedLines(allButton, 0.16f, 8, 1.0f, allActive ? Color{118, 226, 150, 230} : Color{78, 92, 112, 230});
    UiText::DrawFit("All", Rectangle{allButton.x + 4.0f, allButton.y + 6.0f, allButton.width - 8.0f, 18.0f}, 18, RAYWHITE);

    int shown = 0;
    for (ResourceType type : filterResources)
    {
        if (shown >= 18)
            break;
        Rectangle slot = GetFilterButtonRect(chart, shown);
        Color color = ResourceChartColor(type);
        bool active = std::find(activeResources.begin(), activeResources.end(), type) != activeResources.end();
        bool selected = selectedResources.empty() || selectedResources.contains(type);
        DrawRectangleRounded(slot, 0.12f, 8, selected && active ? Color{42, 52, 64, 245} : Color{24, 29, 36, 210});
        DrawRectangleRoundedLines(slot, 0.12f, 8, 1.0f, selected && active ? color : Color{70, 78, 90, 210});
        GuiPanel::DrawResourceIcon(type, Rectangle{slot.x + 5.0f, slot.y + 5.0f, 24.0f, 24.0f});
        DrawRectangleRounded(Rectangle{slot.x + 4.0f, slot.y + slot.height - 6.0f, slot.width - 8.0f, 3.0f}, 0.4f, 4, active ? color : Color{70, 78, 90, 210});
        if (CheckCollisionPointRec(GetMousePosition(), slot))
            Tooltip::Draw(rt2s(type), {
                active ? (showingConsumption ? "Consumed in selected window" : "Produced in selected window")
                       : (showingConsumption ? "No consumption in selected window" : "No production in selected window"),
                selected ? "Visible on graph" : "Hidden from graph"
            }, 250.0f);
        shown++;
    }
    if (visibleResources.empty())
        UiText::DrawFit(showingConsumption ? "No consumption in selected window" : "No production in selected window", Rectangle{plot.x + 20.0f, plot.y + plot.height * 0.45f, plot.width - 40.0f, 24.0f}, 22, Color{190, 201, 216, 255});

}

void FocusPanelWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* player = LocalPlayer(scene);
    if (player == nullptr)
        return;

    Vector2 mouse = GetMousePosition();
    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    DrawRectangleRounded(bounds, 0.025f, 8, Color{26, 30, 37, 244});
    DrawRectangleRoundedLines(bounds, 0.025f, 8, 1.0f, Color{92, 102, 118, 255});
    Rectangle title{bounds.x, bounds.y, bounds.width, 52.0f};
    DrawRectangleRounded(title, 0.025f, 8, Color{42, 50, 62, 255});
    UiText::DrawFit("Political Focus Tree", Rectangle{title.x + 18.0f, title.y + 10.0f, title.width - 86.0f, 30.0f}, 28, RAYWHITE);
    DrawCloseButton(bounds);

    auto nodes = ResearchCatalog::BuildFocusView(*player);
    std::map<std::string, ResearchNodeView*> byId;
    for (auto& node : nodes)
        byId[node.id] = &node;

    auto visibleTags = CollectVisibleTags(nodes);
    Rectangle tagBar{bounds.x + 24.0f, bounds.y + 62.0f, bounds.width - 48.0f, 30.0f};
    DrawTagFilterBar(tagBar, visibleTags, selectedTagFilter);

    float top = bounds.y + 104.0f;
    float bottom = bounds.y + bounds.height - 20.0f;
    float sideW = std::min(260.0f, std::max(218.0f, bounds.width * 0.22f));
    float sideGap = 18.0f;
    Rectangle treeArea{bounds.x + 24.0f, top, bounds.width - sideW - sideGap - 48.0f, bottom - top};
    Rectangle statePanel{treeArea.x + treeArea.width + sideGap, top, sideW, bottom - top};
    if (CheckCollisionPointRec(mouse, treeArea) && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
    {
        panning = true;
        lastPanMouse = {mouse.x, mouse.y};
    }
    if (panning && IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    {
        panOffset.x += mouse.x - lastPanMouse.x;
        panOffset.y += mouse.y - lastPanMouse.y;
        lastPanMouse = {mouse.x, mouse.y};
    }
    if (panning && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
        panning = false;

    auto laneRank = [](const std::string& lane)
    {
        if (lane == "PRODUCTION") return 0;
        if (lane == "MILITARY" || lane == "WARFARE") return 1;
        if (lane == "SOCIAL") return 2;
        if (lane == "POLITICS") return 3;
        return 10;
    };

    float nodeW = 128.0f * zoom;
    float nodeH = 122.0f * zoom;
    float colGap = 118.0f * zoom;
    float laneGap = 250.0f * zoom;
    float rowGap = 176.0f * zoom;
    float laneHeaderH = 38.0f * zoom;
    std::map<std::string, Rectangle> nodeRects;
    std::map<std::string, int> depthById;
    std::map<std::string, std::map<int, std::vector<ResearchNodeView*>>> nodesByLaneDepth;
    std::vector<std::pair<std::string, Rectangle>> laneHeaders;
    std::vector<std::string> lanes;

    auto preferredDepth = [](const ResearchNodeView& node)
    {
        return node.layoutOrder >= 1000 ? node.layoutOrder / 1000 - 1 : 0;
    };

    std::function<int(const ResearchNodeView&)> depthOf = [&](const ResearchNodeView& node)
    {
        auto cached = depthById.find(node.id);
        if (cached != depthById.end())
            return cached->second;
        int depth = preferredDepth(node);
        for (const auto& prerequisite : node.prerequisites)
        {
            auto it = byId.find(prerequisite);
            if (it != byId.end())
                depth = std::max(depth, depthOf(*it->second) + 1);
        }
        depthById[node.id] = depth;
        return depth;
    };

    int maxDepth = 0;
    for (auto& node : nodes)
    {
        int depth = depthOf(node);
        maxDepth = std::max(maxDepth, depth);
        std::string lane = node.layoutLane.empty() ? node.category : node.layoutLane;
        nodesByLaneDepth[lane][depth].push_back(&node);
        if (std::find(lanes.begin(), lanes.end(), lane) == lanes.end())
            lanes.push_back(lane);
    }

    std::sort(lanes.begin(), lanes.end(), [&](const std::string& a, const std::string& b)
    {
        int rankA = laneRank(a);
        int rankB = laneRank(b);
        if (rankA != rankB)
            return rankA < rankB;
        return a < b;
    });

    float laneX = treeArea.x + 28.0f + panOffset.x;
    for (const auto& lane : lanes)
    {
        auto& rows = nodesByLaneDepth[lane];
        size_t maxColumns = 1;
        for (const auto& [depth, rowNodes] : rows)
            maxColumns = std::max(maxColumns, rowNodes.size());

        float laneWidth = std::max(680.0f * zoom, maxColumns * nodeW + (maxColumns - 1) * colGap + 360.0f * zoom);
        Rectangle laneHeader{laneX, treeArea.y + panOffset.y - scrollOffset, laneWidth, laneHeaderH};
        laneHeaders.push_back({lane, laneHeader});

        for (auto& [depth, rowNodes] : rows)
        {
            std::stable_sort(rowNodes.begin(), rowNodes.end(), [&](const ResearchNodeView* a, const ResearchNodeView* b)
            {
                auto parentOrder = [&](const ResearchNodeView* node)
                {
                    int order = node->layoutOrder;
                    for (const auto& prerequisite : node->prerequisites)
                    {
                        auto it = byId.find(prerequisite);
                        if (it != byId.end())
                            order = std::min(order, it->second->layoutOrder);
                    }
                    return order;
                };

                int parentA = parentOrder(a);
                int parentB = parentOrder(b);
                if (parentA != parentB)
                    return parentA < parentB;
                if (a->layoutOrder != b->layoutOrder)
                    return a->layoutOrder < b->layoutOrder;
                return a->definitionIndex < b->definitionIndex;
            });

            std::vector<float> desiredX(rowNodes.size(), laneX);
            float laneCenter = laneX + laneWidth * 0.5f;
            for (size_t i = 0; i < rowNodes.size(); i++)
            {
                float parentCenterSum = 0.0f;
                int parentCount = 0;
                for (const auto& prerequisite : rowNodes[i]->prerequisites)
                {
                    auto parentIt = nodeRects.find(prerequisite);
                    if (parentIt == nodeRects.end())
                        continue;
                    parentCenterSum += parentIt->second.x + parentIt->second.width * 0.5f;
                    parentCount++;
                }
                float rowOffset = (static_cast<float>(i) - (static_cast<float>(rowNodes.size()) - 1.0f) * 0.5f) * (nodeW + colGap * 1.35f);
                int orderWithinLayer = ((rowNodes[i]->layoutOrder % 1000) + 1000) % 1000;
                float orderNorm = static_cast<float>(orderWithinLayer) / 999.0f;
                float laneMargin = std::min(laneWidth * 0.28f, 230.0f * zoom);
                float orderTarget = laneX + laneMargin + orderNorm * std::max(0.0f, laneWidth - laneMargin * 2.0f - nodeW) + nodeW * 0.5f;
                if (parentCount > 0)
                {
                    float parentCenter = parentCenterSum / parentCount;
                    float orderInfluence = orderWithinLayer <= 80 || orderWithinLayer >= 920 ? 0.62f : 0.42f;
                    float spreadTarget = laneCenter + rowOffset;
                    float center = parentCenter * (1.0f - orderInfluence) + orderTarget * orderInfluence;
                    center = center * 0.72f + spreadTarget * 0.28f;
                    desiredX[i] = center - nodeW * 0.5f;
                }
                else
                {
                    desiredX[i] = laneCenter + rowOffset - nodeW * 0.5f;
                }
            }

            std::vector<size_t> order(rowNodes.size());
            for (size_t i = 0; i < order.size(); i++)
                order[i] = i;
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
            {
                return desiredX[a] < desiredX[b];
            });

            std::vector<float> placed(order.size(), laneX);
            for (size_t orderIndex = 0; orderIndex < order.size(); orderIndex++)
            {
                size_t i = order[orderIndex];
                placed[orderIndex] = std::clamp(desiredX[i], laneX, laneX + laneWidth - nodeW);
            }
            float minStep = nodeW + colGap;
            for (int pass = 0; pass < 2; pass++)
            {
                for (size_t i = 1; i < placed.size(); i++)
                    placed[i] = std::max(placed[i], placed[i - 1] + minStep);
                for (int i = static_cast<int>(placed.size()) - 2; i >= 0; i--)
                    placed[i] = std::min(placed[i], placed[i + 1] - minStep);
            }
            if (!placed.empty())
            {
                float rowMin = placed.front();
                float rowMax = placed.back();
                float rowCenter = (rowMin + rowMax + nodeW) * 0.5f;
                float targetCenter = laneCenter;
                for (float x : desiredX)
                    targetCenter += x + nodeW * 0.5f;
                targetCenter /= static_cast<float>(desiredX.size() + 1);
                float shift = std::clamp(targetCenter - rowCenter, laneX - rowMin, laneX + laneWidth - nodeW - rowMax);
                for (auto& x : placed)
                    x += shift;
            }

            for (size_t orderIndex = 0; orderIndex < order.size(); orderIndex++)
            {
                size_t i = order[orderIndex];
                Rectangle rect{
                    placed[orderIndex],
                    treeArea.y + panOffset.y + laneHeaderH + 28.0f + depth * (nodeH + rowGap) - scrollOffset,
                    nodeW,
                    nodeH};
                nodeRects[rowNodes[i]->id] = rect;
            }
        }
        laneX += laneWidth + laneGap;
    }

    float contentBottom = treeArea.y;
    for (const auto& [id, rect] : nodeRects)
        contentBottom = std::max(contentBottom, rect.y + rect.height + scrollOffset);
    maxScrollOffset = std::max(0.0f, contentBottom - (treeArea.y + treeArea.height));
    scrollOffset = std::clamp(scrollOffset, 0.0f, maxScrollOffset);

    const ResearchNodeView* hovered = nullptr;
    for (const auto& node : nodes)
    {
        auto it = nodeRects.find(node.id);
        if (it != nodeRects.end() && CheckCollisionPointRec(mouse, it->second))
            hovered = &node;
    }

    std::set<std::string> highlightedPath;
    std::function<void(const std::string&)> collectParents = [&](const std::string& id)
    {
        if (!highlightedPath.insert(id).second)
            return;
        auto it = byId.find(id);
        if (it == byId.end())
            return;
        for (const auto& prerequisite : it->second->prerequisites)
            collectParents(prerequisite);
    };
    if (hovered != nullptr)
        collectParents(hovered->id);

    BeginScissorMode(static_cast<int>(treeArea.x), static_cast<int>(treeArea.y), static_cast<int>(treeArea.width), static_cast<int>(treeArea.height));
    for (const auto& [lane, header] : laneHeaders)
    {
        DrawRectangleRounded(header, 0.14f, 8, Color{36, 43, 54, 215});
        UiText::DrawFit(lane, Rectangle{header.x + 12.0f, header.y + 4.0f, header.width - 24.0f, header.height - 8.0f}, std::max(20, static_cast<int>(27 * zoom)), Color{208, 220, 238, 255});
    }

    for (const auto& node : nodes)
    {
        auto child = nodeRects[node.id];
        Vector2 childAnchor{child.x + child.width * 0.5f, child.y};
        for (const auto& prerequisite : node.prerequisites)
        {
            auto parentIt = nodeRects.find(prerequisite);
            if (parentIt == nodeRects.end())
                continue;
            Rectangle parent = parentIt->second;
            Vector2 parentAnchor{parent.x + parent.width * 0.5f, parent.y + parent.height};
            bool highlighted = highlightedPath.contains(node.id) && highlightedPath.contains(prerequisite);
            Color edgeColor = highlighted ? Color{232, 202, 104, 255} : Color{74, 86, 104, 150};
            float edgeWidth = highlighted ? 4.0f : 1.5f;
            Vector2 midA{parentAnchor.x, parentAnchor.y + rowGap * 0.34f};
            Vector2 midB{childAnchor.x, childAnchor.y - rowGap * 0.34f};
            DrawLineEx(parentAnchor, midA, edgeWidth, edgeColor);
            DrawLineEx(midA, midB, edgeWidth, edgeColor);
            DrawLineEx(midB, childAnchor, edgeWidth, edgeColor);
        }
    }

    for (const auto& node : nodes)
    {
        Rectangle rect = nodeRects[node.id];
        bool hover = CheckCollisionPointRec(GetMousePosition(), rect);
        bool tagMatched = HasNodeTag(node, selectedTagFilter);
        const StateDevelopmentDefinition* unlockedState = StateDevelopment::FindDefinition(node.governmentId);
        Color fill = node.researched ? Color{45, 86, 63, 245}
                   : node.active ? Color{70, 65, 38, 245}
                   : node.available ? Color{47, 66, 88, 245}
                   : Color{34, 38, 46, 230};
        Color line = node.researched ? Color{87, 176, 113, 255}
                   : node.active ? Color{214, 178, 84, 255}
                   : node.available ? Color{94, 134, 188, 255}
                   : Color{78, 86, 100, 220};
        if (!selectedTagFilter.empty() && !tagMatched)
        {
            fill.a = 110;
            line.a = 120;
        }
        DrawRectangleRounded(rect, 0.06f, 8, fill);
        DrawRectangleRoundedLines(rect, 0.06f, 8, 1.0f, tagMatched && !selectedTagFilter.empty() ? Color{112, 208, 172, 255} : highlightedPath.contains(node.id) ? Color{232, 202, 104, 255} : (hover ? Color{190, 215, 255, 255} : line));
        DrawUiTextWrappedCentered(node.name, Rectangle{rect.x + 10.0f * zoom, rect.y + 7.0f * zoom, rect.width - 20.0f * zoom, 45.0f * zoom}, std::max(15, static_cast<int>(24 * zoom)), RAYWHITE, 2);
        UiText::DrawFit(node.stateText, Rectangle{rect.x + 12.0f * zoom, rect.y + 53.0f * zoom, rect.width - 24.0f * zoom, 21.0f * zoom}, std::max(11, static_cast<int>(18 * zoom)),
            node.researched ? Color{145, 230, 160, 255} : node.available ? Color{190, 215, 255, 255} : Color{180, 186, 196, 255});
        if (unlockedState != nullptr)
        {
            Color governmentFill = unlockedState->color;
            governmentFill.a = 210;
            DrawRectangleRounded(Rectangle{rect.x + 12.0f * zoom, rect.y + 76.0f * zoom, rect.width - 24.0f * zoom, 22.0f * zoom}, 0.16f, 6, governmentFill);
            UiText::DrawFit("State class: " + unlockedState->name, Rectangle{rect.x + 20.0f * zoom, rect.y + 78.0f * zoom, rect.width - 40.0f * zoom, 18.0f * zoom}, std::max(10, static_cast<int>(16 * zoom)), Color{205, 224, 255, 255});
        }
        std::string timeText = node.active ? FormatDuration(node.remainingTime) + " left" : FormatDuration(node.researchTime);
        UiText::DrawFit(timeText, Rectangle{rect.x + 12.0f * zoom, rect.y + 94.0f * zoom, rect.width - 24.0f * zoom, 16.0f * zoom}, std::max(10, static_cast<int>(15 * zoom)), Color{180, 190, 205, 255});
        if (node.active || node.researched)
        {
            Rectangle progress{rect.x + 12.0f, rect.y + rect.height - 11.0f, rect.width - 24.0f, 5.0f};
            DrawRectangleRounded(progress, 0.5f, 4, Color{17, 20, 25, 230});
            Rectangle fillBar = progress;
            fillBar.width *= static_cast<float>(std::clamp(node.progress, 0.0, 1.0));
            DrawRectangleRounded(fillBar, 0.5f, 4, node.researched ? Color{95, 190, 116, 255} : Color{214, 178, 84, 255});
        }
        if (hover && node.available && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && scene != nullptr && scene->game != nullptr)
            scene->SubmitLocalCommand(GameCommand::StartFocus(scene->game->GetLocalPlayerId(), node.id));
    }
    EndScissorMode();

    if (maxScrollOffset > 0.0f)
    {
        Rectangle track{treeArea.x + treeArea.width + 6.0f, treeArea.y, 5.0f, treeArea.height};
        DrawRectangleRounded(track, 0.5f, 4, Color{18, 22, 28, 190});
        float thumbH = std::max(32.0f, track.height * (track.height / (track.height + maxScrollOffset)));
        float thumbY = track.y + (track.height - thumbH) * (scrollOffset / maxScrollOffset);
        DrawRectangleRounded(Rectangle{track.x, thumbY, track.width, thumbH}, 0.5f, 4, Color{116, 132, 154, 230});
    }

    DrawRectangleRounded(statePanel, 0.04f, 8, Color{31, 36, 44, 236});
    DrawRectangleRoundedLines(statePanel, 0.04f, 8, 1.0f, Color{88, 100, 118, 235});
    UiText::DrawFit("State Overview", Rectangle{statePanel.x + 14.0f, statePanel.y + 14.0f, statePanel.width - 28.0f, 26.0f}, 23, RAYWHITE);
    const auto& stateDefinition = player->stateDevelopment.GetDefinition();
    std::vector<std::pair<std::string, std::string>> placeholders{
        {"Class", stateDefinition.name},
        {"Administration", "TBD"},
        {"Stability", "TBD"},
        {"Legitimacy", "TBD"},
        {"War Support", "TBD"},
        {"Treasury", "TBD"}};
    float rowY = statePanel.y + 54.0f;
    Rectangle stateClassRow{};
    for (const auto& [label, value] : placeholders)
    {
        Rectangle row{statePanel.x + 14.0f, rowY, statePanel.width - 28.0f, 30.0f};
        Color rowFill = Color{24, 29, 36, 225};
        if (label == "Class")
        {
            rowFill = stateDefinition.color;
            rowFill.a = 210;
        }
        DrawRectangleRounded(row, 0.08f, 6, rowFill);
        if (label == "Class")
        {
            stateClassRow = row;
            UiText::DrawFit(label, Rectangle{row.x + 10.0f, row.y + 5.0f, 52.0f, 20.0f}, 17, Color{184, 196, 214, 255});
            UiText::DrawFit(value, Rectangle{row.x + 70.0f, row.y + 5.0f, row.width - 80.0f, 20.0f}, 17, RAYWHITE);
        }
        else
        {
            UiText::DrawFit(label, Rectangle{row.x + 10.0f, row.y + 5.0f, row.width * 0.62f, 20.0f}, 17, Color{184, 196, 214, 255});
            UiText::DrawFit(value, Rectangle{row.x + row.width - 62.0f, row.y + 5.0f, 52.0f, 20.0f}, 17, Color{138, 151, 170, 255});
        }
        rowY += 38.0f;
    }
    Rectangle stateDescription{
        statePanel.x + 14.0f,
        rowY + 4.0f,
        statePanel.width - 28.0f,
        66.0f};
    DrawRectangleRounded(stateDescription, 0.08f, 6, Color{24, 29, 36, 205});
    UiText::DrawFit(stateDefinition.description,
        Rectangle{stateDescription.x + 10.0f, stateDescription.y + 8.0f, stateDescription.width - 20.0f, stateDescription.height - 16.0f},
        16,
        Color{172, 184, 202, 255});
    const std::string& activeId = player->focuses.GetActiveFocusId();
    const TechnologyDefinition* activeFocus = activeId.empty() ? nullptr : FindFocusDefinition(activeId);
    Rectangle activeBox{statePanel.x + 14.0f, statePanel.y + statePanel.height - 86.0f, statePanel.width - 28.0f, 68.0f};
    DrawRectangleRounded(activeBox, 0.08f, 6, Color{25, 31, 39, 230});
    UiText::DrawFit("Active Focus", Rectangle{activeBox.x + 10.0f, activeBox.y + 8.0f, activeBox.width - 20.0f, 20.0f}, 17, Color{184, 196, 214, 255});
    UiText::DrawFit(activeFocus != nullptr ? activeFocus->name : "None", Rectangle{activeBox.x + 10.0f, activeBox.y + 32.0f, activeBox.width - 20.0f, 24.0f}, 20, activeFocus != nullptr ? RAYWHITE : Color{138, 151, 170, 255});

    if (CheckCollisionPointRec(mouse, stateClassRow))
    {
        std::vector<std::string> lines{stateDefinition.description};
        if (stateDefinition.modifiers.empty())
        {
            lines.push_back(TooltipSeparatorLine());
            lines.push_back("No fixed effects yet");
        }
        else
        {
            lines.push_back(TooltipSeparatorLine());
            for (const auto& modifier : stateDefinition.modifiers)
                lines.push_back(FormatModifierForFocusTooltip(modifier));
        }
        Tooltip::Draw(stateDefinition.name, lines, 460.0f);
    }

    if (hovered != nullptr)
    {
        std::vector<std::string> lines{hovered->description};
        lines.push_back(TooltipSeparatorLine());
        lines.push_back("Time: " + FormatDuration(hovered->researchTime) + " | " + hovered->stateText);
        if (hovered->active)
            lines.push_back("Remaining: " + FormatDuration(hovered->remainingTime));
        lines.push_back(TooltipSeparatorLine());
        if (const auto* unlockedState = StateDevelopment::FindDefinition(hovered->governmentId))
            lines.push_back("State class change: " + unlockedState->name);
        for (const auto& modifier : hovered->modifiers)
            lines.push_back(FormatModifierForFocusTooltip(modifier));
        Tooltip::Draw(hovered->name, lines, 460.0f);
    }
}

void FocusPanelWidget::AdjustTreeZoom(Vec2i point, float wheel)
{
    if (wheel == 0.0f)
        return;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    float top = bounds.y + 104.0f;
    float bottom = bounds.y + bounds.height - 20.0f;
    float sideW = std::min(260.0f, std::max(218.0f, bounds.width * 0.22f));
    float sideGap = 18.0f;
    Rectangle treeArea{bounds.x + 24.0f, top, bounds.width - sideW - sideGap - 48.0f, bottom - top};
    Vector2 mouse{static_cast<float>(point.x), static_cast<float>(point.y)};
    if (!CheckCollisionPointRec(mouse, treeArea))
        return;

    float oldZoom = zoom;
    float newZoom = std::clamp(zoom + wheel * 0.08f, 0.42f, 1.15f);
    if (std::abs(newZoom - oldZoom) < 0.001f)
        return;

    float localX = mouse.x - treeArea.x;
    float localY = mouse.y - treeArea.y;
    panOffset.x = localX - (localX - panOffset.x) * (newZoom / oldZoom);
    panOffset.y = localY - (localY - panOffset.y) * (newZoom / oldZoom);
    zoom = newZoom;
}

// Returns one resource filter slot rectangle in the production graph panel.
Rectangle StatsPanelWidget::GetFilterButtonRect(Rectangle chart, int index) const
{
    float size = 36.0f;
    float gap = 8.0f;
    int columns = std::max(1, static_cast<int>((chart.width - 116.0f) / (size + gap)));
    int col = index % columns;
    int row = index / columns;
    return Rectangle{chart.x + 104.0f + col * (size + gap), chart.y + chart.height - 70.0f + row * (size + gap), size, size};
}

// Returns the "all resources" filter reset button rectangle.
Rectangle StatsPanelWidget::GetAllFilterButtonRect(Rectangle chart) const
{
    return Rectangle{chart.x + 18.0f, chart.y + chart.height - 70.0f, 72.0f, 36.0f};
}

// Handles clicks on the stats panel controls.
bool StatsPanelWidget::HandleClick(Vec2i point)
{
    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    Rectangle title{bounds.x, bounds.y, bounds.width, 54.0f};
    Rectangle spin{title.x + title.width - 244.0f, title.y + 10.0f, 190.0f, 34.0f};
    Rectangle modeToggle{title.x + title.width - 500.0f, title.y + 10.0f, 220.0f, 34.0f};
    Vector2 mouse{static_cast<float>(point.x), static_cast<float>(point.y)};
    if (CheckCollisionPointRec(mouse, modeToggle))
    {
        selectedFlowMode = point.x < modeToggle.x + modeToggle.width * 0.5f ? 0 : 1;
        return true;
    }
    if (CheckCollisionPointRec(mouse, spin))
    {
        if (point.x < spin.x + spin.width * 0.5f)
            selectedWindowIndex = (selectedWindowIndex + 2) % 3;
        else
            selectedWindowIndex = (selectedWindowIndex + 1) % 3;
        return true;
    }

    float colGap = 22.0f;
    float top = bounds.y + 78.0f;
    float bottom = bounds.y + bounds.height - 22.0f;
    Rectangle left{bounds.x + 24.0f, top, bounds.width * 0.31f, bottom - top};
    Rectangle chart{left.x + left.width + colGap, top, bounds.x + bounds.width - (left.x + left.width + colGap) - 24.0f, bottom - top};
    if (CheckCollisionPointRec(mouse, GetAllFilterButtonRect(chart)))
    {
        selectedResources.clear();
        return true;
    }

    int index = 0;
    for (ResourceType type : filterResources)
    {
        Rectangle slot = GetFilterButtonRect(chart, index);
        if (CheckCollisionPointRec(mouse, slot))
        {
            if (selectedResources.contains(type))
                selectedResources.erase(type);
            else
                selectedResources.insert(type);
            return true;
        }
        index++;
    }
    return false;
}

// Advances this object's state for one frame.
void BuildPanelWidget::Update(double dt)
{
    if (scene == nullptr || options == nullptr)
        return;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    Vector2 mouse = GetMousePosition();
    DrawRectangleRounded(bounds, 0.025f, 8, Color{28, 32, 38, 238});
    DrawRectangleRoundedLines(bounds, 0.025f, 8, 1.0f, Color{92, 102, 118, 255});

    int margin = std::max(7, size.x / 64);
    int titleBar = std::max(30, size.y / 17);
    Rectangle titleBounds{bounds.x, bounds.y, bounds.width, static_cast<float>(titleBar)};
    Rectangle closeButton = PanelCloseButtonRect(bounds);
    DrawRectangleRounded(titleBounds, 0.025f, 8, Color{44, 52, 65, 255});
    if (CheckCollisionPointRec(mouse, titleBounds) &&
        !CheckCollisionPointRec(mouse, closeButton) &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        dragging = true;
        dragOffset = Vec2i{static_cast<int>(mouse.x) - pos.x, static_cast<int>(mouse.y) - pos.y};
    }
    if (dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        pos.x = std::clamp(static_cast<int>(mouse.x) - dragOffset.x, 0, std::max(0, GetScreenWidth() - size.x));
        pos.y = std::clamp(static_cast<int>(mouse.y) - dragOffset.y, 0, std::max(0, GetScreenHeight() - size.y));
        bounds = Rectangle{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
        titleBounds = Rectangle{bounds.x, bounds.y, bounds.width, static_cast<float>(titleBar)};
        closeButton = PanelCloseButtonRect(bounds);
    }
    if (dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        dragging = false;

    int titleFont = std::max(17, std::min(23, titleBar / 2 + 2));
    int titleWidth = UiText::Measure(title, titleFont);
    UiText::Draw(title, bounds.x + (bounds.width - titleWidth) * 0.5f, bounds.y + (titleBar - titleFont) * 0.5f, titleFont, RAYWHITE);
    DrawCloseButton(bounds);

    int columns = 3;
    float gap = 7.0f;
    float scrollbarW = 8.0f;
    float contentW = bounds.width - margin * 2 - scrollbarW - 4.0f;
    float cardW = (contentW - gap * (columns - 1)) / columns;
    float cardH = std::max(74.0f, cardW * 0.78f);
    float headerH = 22.0f;
    float categoryGap = 3.0f;
    float viewportTop = bounds.y + titleBar + margin;
    float viewportBottom = bounds.y + bounds.height - margin;
    float startY = viewportTop - scrollOffset;
    int hoveredOption = -1;

    BeginScissorMode(static_cast<int>(bounds.x + margin), static_cast<int>(viewportTop), static_cast<int>(contentW), static_cast<int>(viewportBottom - viewportTop));
    std::string currentCategory;
    float yCursor = startY;
    int col = 0;
    float contentBottom = viewportTop;
    for (size_t i = 0; i < options->size(); i++)
    {
        const auto& option = (*options)[i];
        if (option.category != currentCategory)
        {
            if (!currentCategory.empty() && col != 0)
                yCursor += cardH + gap;
            currentCategory = option.category;
            Rectangle header{
                bounds.x + margin,
                yCursor,
                contentW,
                headerH};
            if (header.y + header.height >= viewportTop && header.y <= viewportBottom)
            {
                DrawRectangleRounded(header, 0.12f, 6, Color{38, 45, 56, 215});
                int headerFont = 17;
                int headerWidth = UiText::Measure(currentCategory, headerFont);
                UiText::Draw(currentCategory,
                    header.x + (header.width - headerWidth) * 0.5f,
                    header.y + (header.height - headerFont) * 0.5f,
                    headerFont,
                    Color{196, 210, 232, 255});
            }
            contentBottom = std::max(contentBottom, header.y + header.height + scrollOffset);
            yCursor += headerH + categoryGap;
            col = 0;
        }

        Rectangle card{
            bounds.x + margin + col * (cardW + gap),
            yCursor,
            cardW,
            cardH};
        contentBottom = std::max(contentBottom, card.y + card.height + scrollOffset);
        bool visible = card.y + card.height >= viewportTop && card.y <= viewportBottom;
        if (!visible)
        {
            col++;
            if (col >= columns)
            {
                col = 0;
                yCursor += cardH + gap;
            }
            continue;
        }

        bool selected = selectedIndex < options->size() && static_cast<size_t>(i) == selectedIndex;
        auto lockReasons = BuildLockReasons(scene, option);
        bool locked = !lockReasons.empty();
        DrawRectangleRounded(card, 0.04f, 8, locked ? Color{20, 22, 26, 226} : (selected ? Color{48, 68, 58, 245} : Color{36, 41, 49, 235}));
        DrawRectangleRoundedLines(card, 0.04f, 8, 1.0f, locked ? Color{58, 64, 74, 240} : (selected ? Color{112, 230, 150, 210} : Color{88, 98, 114, 255}));

        float icon = std::min(card.width * 0.52f, card.height - 14.0f);
        Rectangle dst{card.x + 6.0f, card.y + (card.height - icon) * 0.5f, icon, icon};
        auto textureIt = scene->render.buildingTextures.find(option.buildingType);
        if (textureIt != scene->render.buildingTextures.end() && textureIt->second.id != 0)
        {
            Texture2D tex = textureIt->second;
            Rectangle src{0.0f, 0.0f, static_cast<float>(tex.width), static_cast<float>(tex.height)};
            DrawTexturePro(tex, src, dst, {0.0f, 0.0f}, 0.0f, locked ? Color{80, 84, 92, 145} : WHITE);
        }
        else
        {
            DrawRectangleRounded(dst, 0.08f, 6, locked ? Color{58, 62, 70, 220} : Color{85, 92, 106, 255});
        }

        int nameFont = 15;
        UiText::DrawFit(option.name, Rectangle{card.x + card.width * 0.52f, card.y + 8.0f, card.width * 0.46f - 5.0f, 22.0f}, nameFont, locked ? Color{126, 132, 142, 255} : RAYWHITE);

        float costX = card.x + card.width * 0.52f;
        float costY = card.y + 36.0f;
        float smallIcon = 15.0f;
        float costGap = 5.0f;
        int visibleCosts = std::min<int>(static_cast<int>(option.buildCosts.size()), 2);
        for (int costIndex = 0; costIndex < visibleCosts; costIndex++)
        {
            const auto& cost = option.buildCosts[costIndex];
            Rectangle icon{costX, costY, smallIcon, smallIcon};
            GuiPanel::DrawResourceIcon(cost.type, icon);
            std::string amount = std::to_string(cost.amount);
            bool hasResource = CountStoredResource(scene, cost.type) >= cost.amount;
            UiText::Draw(amount, costX + smallIcon + 2.0f, costY + 1.0f, 13, hasResource ? Color{176, 232, 176, 255} : Color{242, 126, 126, 255});
            costX += smallIcon + UiText::Measure(amount, 13) + costGap;
            if (costX > card.x + card.width - 24.0f)
                break;
        }
        if (option.buildCosts.empty())
            UiText::Draw("Free", card.x + card.width * 0.52f, card.y + 36.0f, 13, Color{188, 197, 208, 255});

        if (CheckCollisionPointRec(mouse, card))
            hoveredOption = static_cast<int>(i);

        col++;
        if (col >= columns)
        {
            col = 0;
            yCursor += cardH + gap;
        }
    }
    EndScissorMode();

    maxScrollOffset = std::max(0.0f, contentBottom - viewportBottom);
    scrollOffset = std::clamp(scrollOffset, 0.0f, maxScrollOffset);
    if (maxScrollOffset > 0.0f)
    {
        Rectangle track{bounds.x + bounds.width - margin - scrollbarW * 0.5f, viewportTop, scrollbarW, viewportBottom - viewportTop};
        DrawRectangleRounded(track, 0.5f, 4, Color{18, 22, 28, 190});
        float thumbH = std::max(28.0f, track.height * (track.height / (track.height + maxScrollOffset)));
        float thumbY = track.y + (track.height - thumbH) * (scrollOffset / maxScrollOffset);
        DrawRectangleRounded(Rectangle{track.x, thumbY, track.width, thumbH}, 0.5f, 4, Color{116, 132, 154, 230});
    }

    if (hoveredOption >= 0)
    {
        const auto& option = (*options)[hoveredOption];
        DrawBuildTooltip(scene, option, hoveredTile);
    }
}

// Scrolls this build list by mouse wheel steps.
void BuildPanelWidget::Scroll(float wheel)
{
    if (wheel == 0.0f)
        return;

    scrollOffset = std::clamp(scrollOffset - wheel * 42.0f, 0.0f, maxScrollOffset);
}

// Returns the build option index under a screen point.
int BuildPanelWidget::GetOptionAt(Vec2i point) const
{
    if (options == nullptr)
        return -1;

    int margin = std::max(7, size.x / 64);
    int titleBar = std::max(30, size.y / 17);
    float viewportTop = pos.y + titleBar + margin;
    float viewportBottom = pos.y + size.y - margin;
    if (point.y < viewportTop || point.y > viewportBottom)
        return -1;
    int columns = 3;
    float gap = 7.0f;
    float scrollbarW = 8.0f;
    float contentW = size.x - margin * 2 - scrollbarW - 4.0f;
    float cardW = (contentW - gap * (columns - 1)) / columns;
    float cardH = std::max(74.0f, cardW * 0.78f);
    float headerH = 22.0f;
    float categoryGap = 3.0f;
    float startY = pos.y + titleBar + margin - scrollOffset;

    std::string currentCategory;
    float yCursor = startY;
    int col = 0;
    for (size_t i = 0; i < options->size(); i++)
    {
        const auto& option = (*options)[i];
        if (option.category != currentCategory)
        {
            if (!currentCategory.empty() && col != 0)
                yCursor += cardH + gap;
            currentCategory = option.category;
            yCursor += headerH + categoryGap;
            col = 0;
        }

        Rectangle card{
            static_cast<float>(pos.x + margin) + col * (cardW + gap),
            yCursor,
            cardW,
            cardH};
        if (CheckCollisionPointRec(Vector2{static_cast<float>(point.x), static_cast<float>(point.y)}, card))
            return static_cast<int>(i);

        col++;
        if (col >= columns)
        {
            col = 0;
            yCursor += cardH + gap;
        }
    }
    return -1;
}

// Advances this object's state for one frame.
void BuildGhostWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr || selectedOption == nullptr)
        return;
    if (tilePos.x < 0 || tilePos.y < 0)
        return;

    Vec2i footprint = selectedOption->footprint;
    Vec2f worldTopLeft{
        static_cast<float>(tilePos.x * TILE_SIZE),
        static_cast<float>(tilePos.y * TILE_SIZE)};
    Vec2f worldBottomRight{
        worldTopLeft.x + footprint.x * TILE_SIZE,
        worldTopLeft.y + footprint.y * TILE_SIZE};

    Vec2f screenTopLeft = scene->render.WorldToScreen(worldTopLeft);
    Vec2f screenBottomRight = scene->render.WorldToScreen(worldBottomRight);
    Rectangle dest{
        screenTopLeft.x,
        screenTopLeft.y,
        screenBottomRight.x - screenTopLeft.x,
        screenBottomRight.y - screenTopLeft.y};

    Color tint = canBuild ? Color{88, 196, 124, 62} : Color{220, 80, 80, 70};
    DrawRectangleRounded(dest, 0.04f, 8, tint);
    DrawRectangleRoundedLines(dest, 0.04f, 8, 1.0f, canBuild ? Color{112, 230, 150, 180} : Color{240, 110, 110, 190});

    auto textureIt = scene->render.buildingTextures.find(selectedOption->buildingType);
    if (textureIt != scene->render.buildingTextures.end() && textureIt->second.id != 0)
    {
        Texture2D tex = textureIt->second;
        Rectangle src{0.0f, 0.0f, static_cast<float>(tex.width), static_cast<float>(tex.height)};
        DrawTexturePro(tex, src, dest, {0.0f, 0.0f}, 0.0f, Color{255, 255, 255, static_cast<unsigned char>(canBuild ? 170 : 120)});
    }
}

// Builds the requested map object or helper path.
BuildGuiSystem::BuildGuiSystem(GuiController* con)
    // Initializes GuiSystem.
    : GuiSystem(con)
{
    scene = owner->scene;

    actionMap["esc"]  = [this] { EscPressed(); };
    actionMap["q"]    = [this] { BuildPressed(); };
    actionMap["r"]    = [this] { RoadBuildPressed(); };
    actionMap["d"]    = [this] { DestroyPressed(); };
    actionMap["e"]    = [this] { HeadquartersPressed(); };
    actionMap["s"]    = [this] { StatsPressed(); };
    actionMap["f"]    = [this] { FocusPressed(); };
    actionMap["lmbp"] = [this] { LmbPressed(); };
    actionMap["lmbr"] = [this] { LmbReleased(); };
    actionMap["rmbp"] = [this] { RmbPressed(); };
    actionMap["rmbr"] = [this] { RmbReleased(); };
    actionMap["mmbp"] = [this] { cameraMovement.isMoving = true; };
    actionMap["mmbr"] = [this] { cameraMovement.isMoving = false; };
    actionMap["scroll"] = [this] { Scroll(); };

    buildPanel.ChangePositionAnchor({0.69f, 0.08f});
    buildPanel.ChangeSizeAnchor({0.28f, 0.82f});
    buildPanel.scene = scene;
    buildPanel.options = &options;
    buildPanel.title = "Build";
    buildPanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    strategicHudWidget.scene = scene;
    strategicHudWidget.ChangePositionAnchor({0.012f, 0.012f});
    strategicHudWidget.ChangeSizeAnchor({0.42f, 0.055f});
    strategicHudWidget.UpdateSize({GetScreenWidth(), GetScreenHeight()});

    for (BuildingType type : GetBuildableBuildingTypes())
        options.push_back(MakeBuildOption(scene, type));
    SortBuildOptions(options);

    ghostWidget.scene = scene;
}

// Advances UpdateUiWidgets for one frame or simulation tick.
void BuildGuiSystem::UpdateUiWidgets(Vec2i size)
{
    buildPanel.UpdateSize(size);
    strategicHudWidget.UpdateSize(size);
}

// Advances this object's state for one frame.
void BuildGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    RefreshGhost();
    buildPanel.hoveredTile = ghostWidget.tilePos;
    owner->AddUiWidget(&ghostWidget);
    owner->AddUiWidget(&buildPanel);
    owner->AddUiWidget(&strategicHudWidget);
}

// Initializes BuildGuiSystem::EscPressed.
void BuildGuiSystem::EscPressed()
{
    ReturnToMapView();
}

// Builds the requested map object or helper path.
void BuildGuiSystem::BuildPressed()
{
    ReturnToMapView();
}

// Initializes BuildGuiSystem::RoadBuildPressed.
void BuildGuiSystem::RoadBuildPressed()
{
    owner->ChangeSystem("road_build");
}

// Initializes BuildGuiSystem::DestroyPressed.
void BuildGuiSystem::DestroyPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("destroy");
}

// Opens the headquarters panel from build mode.
void BuildGuiSystem::HeadquartersPressed()
{
    OpenHeadquartersAndReturn();
}

// Opens the statistics panel from build mode.
void BuildGuiSystem::StatsPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stats");
}

void BuildGuiSystem::FocusPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("focus");
}

// Initializes BuildGuiSystem::LmbPressed.
void BuildGuiSystem::LmbPressed()
{
    auto mousePos = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mousePos.x), static_cast<int>(mousePos.y)};
    if (IsBuildHudButtonHovered(strategicHudWidget))
    {
        BuildPressed();
        return;
    }
    if (IsRoadHudButtonHovered(strategicHudWidget))
    {
        RoadBuildPressed();
        return;
    }
    if (IsDestroyHudButtonHovered(strategicHudWidget))
    {
        DestroyPressed();
        return;
    }
    if (IsStatsHudButtonHovered(strategicHudWidget))
    {
        StatsPressed();
        return;
    }
    if (IsFocusHudButtonHovered(strategicHudWidget))
    {
        FocusPressed();
        return;
    }
    if (buildPanel.ContainsPoint(screenPos))
    {
        Rectangle panelBounds{
            static_cast<float>(buildPanel.pos.x),
            static_cast<float>(buildPanel.pos.y),
            static_cast<float>(buildPanel.size.x),
            static_cast<float>(buildPanel.size.y)};
        if (CheckCollisionPointRec(mousePos, PanelCloseButtonRect(panelBounds)))
        {
            ReturnToMapView();
            return;
        }

        int option = buildPanel.GetOptionAt(screenPos);
        if (option >= 0)
            SelectOption(static_cast<size_t>(option));
        return;
    }

    TryPlaceSelectedAtHovered(true);
}

// Initializes BuildGuiSystem::LmbReleased.
void BuildGuiSystem::LmbReleased()
{
}

// Initializes BuildGuiSystem::RmbPressed.
void BuildGuiSystem::RmbPressed()
{
    cameraMovement.isMoving = true;
}

// Initializes BuildGuiSystem::RmbReleased.
void BuildGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

// Initializes BuildGuiSystem::Scroll.
void BuildGuiSystem::Scroll()
{
    Vector2 mouse = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
    if (buildPanel.ContainsPoint(screenPos))
    {
        buildPanel.Scroll(GetMouseWheelMove());
        return;
    }

    ZoomCamera(scene);
}

// Initializes BuildGuiSystem::ReturnToMapView.
void BuildGuiSystem::ReturnToMapView()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}

// Switches to map view and opens the headquarters panel.
void BuildGuiSystem::OpenHeadquartersAndReturn()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
    auto mapSystem = std::dynamic_pointer_cast<BasicMapViewSystem>(owner->systems["default"]);
    if (mapSystem != nullptr)
        mapSystem->OpenHeadquartersPanel();
}

// Returns the tile currently targeted by the build cursor.
Vec2i BuildGuiSystem::GetHoveredTile() const
{
    auto mousePos = GetMousePosition();
    Vec2f worldPos = scene->render.ScreenToWorld(mousePos);
    if (worldPos.x < 0.0f || worldPos.y < 0.0f)
        return {-1, -1};

    Vec2i footprint{1, 1};
    if (selectedPreview != nullptr)
        footprint = selectedPreview->GetFootprint();

    return Vec2i{
        static_cast<int>(std::floor(worldPos.x / TILE_SIZE - (footprint.x - 1) * 0.5f)),
        static_cast<int>(std::floor(worldPos.y / TILE_SIZE - (footprint.y - 1) * 0.5f))};
}

// Returns whether this condition is currently true.
bool BuildGuiSystem::CanPlaceSelected(Vec2i tilePos) const
{
    if (selectedPreview == nullptr || scene->game == nullptr)
        return false;
    if (!scene->game->tilemap.IsInsideFootprint(tilePos, selectedPreview->GetFootprint()))
        return false;

    auto playerIt = scene->game->playerHandler.players.find(scene->game->GetLocalPlayerId());
    if (playerIt == scene->game->playerHandler.players.end() || playerIt->second == nullptr)
        return false;

    auto player = playerIt->second.get();
    const auto& definition = GetBuildingDefinition(selectedPreview->buildingType);
    bool debugFreeBuild = scene->game->tilemap.params.debugMode && player->id == scene->game->GetLocalPlayerId();
    return scene->game->tilemap.CanPlaceBuilding(selectedPreview->buildingType, tilePos, selectedPreview->GetFootprint(), player) &&
           (debugFreeBuild || player->CanBuildDefinition(definition));
}

// Initializes BuildGuiSystem::SelectOption.
void BuildGuiSystem::SelectOption(size_t index)
{
    if (index >= options.size())
        return;

    selectedIndex = index;
    selectedPreview = options[selectedIndex].previewFactory();
    buildPanel.selectedIndex = selectedIndex;
}

// Initializes BuildGuiSystem::RefreshGhost.
void BuildGuiSystem::RefreshGhost()
{
    ghostWidget.selectedOption = selectedIndex < options.size() ? &options[selectedIndex] : nullptr;
    ghostWidget.tilePos = GetHoveredTile();
    ghostWidget.canBuild = CanPlaceSelected(ghostWidget.tilePos);
}

// Initializes BuildGuiSystem::TryPlaceSelectedAtHovered.
bool BuildGuiSystem::TryPlaceSelectedAtHovered(bool returnAfterBuild)
{
    Vec2i tilePos = GetHoveredTile();
    if (!CanPlaceSelected(tilePos))
        return false;

    if (selectedIndex >= options.size())
        return false;

    buildTimePlaceholder = options[selectedIndex].buildTime;
    options[selectedIndex].buildAt(tilePos);
    if (returnAfterBuild)
        ReturnToMapView();

    return true;
}

// Initializes RoadBuildSystem::RoadBuildSystem.
RoadBuildSystem::RoadBuildSystem(GuiController* con)
    // Builds the requested map object or helper path.
    : BuildGuiSystem(con)
{
    buildPanel.title = "Roads";
    options.clear();
    for (BuildingType type : GetBuildableRoadTypes())
        options.push_back(MakeBuildOption(scene, type));
    SortBuildOptions(options);

    SelectOption(0);
}

// Advances this object's state for one frame.
void RoadBuildSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    RefreshGhost();
    owner->AddUiWidget(&ghostWidget);
    owner->AddUiWidget(&strategicHudWidget);

    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        auto mousePos = GetMousePosition();
        Vec2i screenPos{static_cast<int>(mousePos.x), static_cast<int>(mousePos.y)};
        if (!IsBuildHudButtonHovered(strategicHudWidget) &&
            !IsRoadHudButtonHovered(strategicHudWidget) &&
            !IsDestroyHudButtonHovered(strategicHudWidget) &&
            !IsStatsHudButtonHovered(strategicHudWidget) &&
            !IsFocusHudButtonHovered(strategicHudWidget))
            TryPlaceRoadAtHovered();
    }
}

// Builds the requested map object or helper path.
void RoadBuildSystem::BuildPressed()
{
    lastRoadDragTile = {-9999, -9999};
    cameraMovement.isMoving = false;
    owner->ChangeSystem("build");
}

// Initializes RoadBuildSystem::RoadBuildPressed.
void RoadBuildSystem::RoadBuildPressed()
{
    ReturnToMapView();
}

// Initializes RoadBuildSystem::LmbPressed.
void RoadBuildSystem::LmbPressed()
{
    auto mousePos = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mousePos.x), static_cast<int>(mousePos.y)};
    if (IsBuildHudButtonHovered(strategicHudWidget))
    {
        BuildPressed();
        return;
    }
    if (IsRoadHudButtonHovered(strategicHudWidget))
    {
        RoadBuildPressed();
        return;
    }
    if (IsDestroyHudButtonHovered(strategicHudWidget))
    {
        DestroyPressed();
        return;
    }
    if (IsStatsHudButtonHovered(strategicHudWidget))
    {
        StatsPressed();
        return;
    }
    if (IsFocusHudButtonHovered(strategicHudWidget))
    {
        FocusPressed();
        return;
    }
    TryPlaceRoadAtHovered();
}

// Initializes RoadBuildSystem::LmbReleased.
void RoadBuildSystem::LmbReleased()
{
    lastRoadDragTile = {-9999, -9999};
}

void RoadBuildSystem::Scroll()
{
    ZoomCamera(scene);
}

// Initializes RoadBuildSystem::TryPlaceRoadAtHovered.
bool RoadBuildSystem::TryPlaceRoadAtHovered()
{
    Vec2i tilePos = GetHoveredTile();
    if (tilePos == lastRoadDragTile)
        return false;

    if (!CanPlaceSelected(tilePos))
        return false;

    if (selectedIndex >= options.size())
        return false;

    buildTimePlaceholder = options[selectedIndex].buildTime;
    options[selectedIndex].buildAt(tilePos);
    lastRoadDragTile = tilePos;
    return true;
}

// Initializes DestroyGuiSystem::DestroyGuiSystem.
DestroyGuiSystem::DestroyGuiSystem(GuiController* con)
    // Initializes GuiSystem.
    : GuiSystem(con)
{
    scene = owner->scene;

    actionMap["esc"] = [this] { EscPressed(); };
    actionMap["q"] = [this] { BuildPressed(); };
    actionMap["r"] = [this] { RoadBuildPressed(); };
    actionMap["d"] = [this] { DestroyPressed(); };
    actionMap["e"] = [this] { HeadquartersPressed(); };
    actionMap["s"] = [this] { StatsPressed(); };
    actionMap["f"] = [this] { FocusPressed(); };
    actionMap["lmbp"] = [this] { LmbPressed(); };
    actionMap["lmbr"] = [this] { LmbReleased(); };
    actionMap["rmbp"] = [this] { RmbPressed(); };
    actionMap["rmbr"] = [this] { RmbReleased(); };
    actionMap["mmbp"] = [this] { cameraMovement.isMoving = true; };
    actionMap["mmbr"] = [this] { cameraMovement.isMoving = false; };
    actionMap["scroll"] = [this] { Scroll(); };

    destroyTargetWidget.scene = scene;
    strategicHudWidget.scene = scene;
    strategicHudWidget.ChangePositionAnchor({0.012f, 0.012f});
    strategicHudWidget.ChangeSizeAnchor({0.42f, 0.055f});
    strategicHudWidget.UpdateSize({GetScreenWidth(), GetScreenHeight()});
}

// Advances UpdateUiWidgets for one frame or simulation tick.
void DestroyGuiSystem::UpdateUiWidgets(Vec2i size)
{
    strategicHudWidget.UpdateSize(size);
}

// Advances this object's state for one frame.
void DestroyGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);

    Vec2i tilePos = ScreenToTile(scene, GetMousePosition());
    hoveredBuilding = tilePos.x >= 0 && tilePos.y >= 0
        ? scene->game->tilemap.GetBuilding(tilePos)
        : nullptr;
    auto localPlayerIt = scene->game->playerHandler.players.find(scene->game->GetLocalPlayerId());
    Player* localPlayer = localPlayerIt != scene->game->playerHandler.players.end() ? localPlayerIt->second.get() : nullptr;
    if (hoveredBuilding != nullptr && hoveredBuilding->owner != localPlayer)
        hoveredBuilding = nullptr;

    destroyTargetWidget.building = hoveredBuilding;
    if (hoveredBuilding != nullptr)
        owner->AddUiWidget(&destroyTargetWidget);
    owner->AddUiWidget(&strategicHudWidget);
}

// Initializes DestroyGuiSystem::EscPressed.
void DestroyGuiSystem::EscPressed()
{
    ReturnToMapView();
}

// Builds the requested map object or helper path.
void DestroyGuiSystem::BuildPressed()
{
    owner->ChangeSystem("build");
}

// Initializes DestroyGuiSystem::RoadBuildPressed.
void DestroyGuiSystem::RoadBuildPressed()
{
    owner->ChangeSystem("road_build");
}

// Initializes DestroyGuiSystem::DestroyPressed.
void DestroyGuiSystem::DestroyPressed()
{
    ReturnToMapView();
}

// Opens the headquarters panel from destroy mode.
void DestroyGuiSystem::HeadquartersPressed()
{
    ReturnToMapView();
    auto mapSystem = std::dynamic_pointer_cast<BasicMapViewSystem>(owner->systems["default"]);
    if (mapSystem != nullptr)
        mapSystem->OpenHeadquartersPanel();
}

// Opens the statistics panel from destroy mode.
void DestroyGuiSystem::StatsPressed()
{
    cameraMovement.isMoving = false;
    hoveredBuilding = nullptr;
    destroyTargetWidget.building = nullptr;
    owner->ChangeSystem("stats");
}

void DestroyGuiSystem::FocusPressed()
{
    cameraMovement.isMoving = false;
    hoveredBuilding = nullptr;
    destroyTargetWidget.building = nullptr;
    owner->ChangeSystem("focus");
}

// Initializes DestroyGuiSystem::LmbPressed.
void DestroyGuiSystem::LmbPressed()
{
    if (IsBuildHudButtonHovered(strategicHudWidget))
    {
        BuildPressed();
        return;
    }
    if (IsRoadHudButtonHovered(strategicHudWidget))
    {
        RoadBuildPressed();
        return;
    }
    if (IsDestroyHudButtonHovered(strategicHudWidget))
    {
        DestroyPressed();
        return;
    }
    if (IsStatsHudButtonHovered(strategicHudWidget))
    {
        StatsPressed();
        return;
    }
    if (IsFocusHudButtonHovered(strategicHudWidget))
    {
        FocusPressed();
        return;
    }

    if (scene->game == nullptr || hoveredBuilding == nullptr)
        return;
    if (!hoveredBuilding->CanBeManuallyDestroyed())
        return;

    int positionId = hoveredBuilding->positionId;
    hoveredBuilding = nullptr;
    destroyTargetWidget.building = nullptr;
    scene->SubmitLocalCommand(GameCommand::DestroyBuilding(scene->game->GetLocalPlayerId(), positionId));
    ReturnToMapView();
}

// Initializes DestroyGuiSystem::LmbReleased.
void DestroyGuiSystem::LmbReleased()
{
}

// Initializes DestroyGuiSystem::RmbPressed.
void DestroyGuiSystem::RmbPressed()
{
    cameraMovement.isMoving = true;
}

// Initializes DestroyGuiSystem::RmbReleased.
void DestroyGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

// Initializes DestroyGuiSystem::Scroll.
void DestroyGuiSystem::Scroll()
{
    ZoomCamera(scene);
}

// Initializes DestroyGuiSystem::ReturnToMapView.
void DestroyGuiSystem::ReturnToMapView()
{
    cameraMovement.isMoving = false;
    hoveredBuilding = nullptr;
    destroyTargetWidget.building = nullptr;
    owner->ChangeSystem("default");
}

// Initializes the full-screen statistics interaction mode.
StatsGuiSystem::StatsGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    scene = owner->scene;

    actionMap["esc"] = [this] { EscPressed(); };
    actionMap["q"] = [this] { BuildPressed(); };
    actionMap["r"] = [this] { RoadBuildPressed(); };
    actionMap["d"] = [this] { DestroyPressed(); };
    actionMap["e"] = [this] { HeadquartersPressed(); };
    actionMap["s"] = [this] { StatsPressed(); };
    actionMap["f"] = [this] { FocusPressed(); };
    actionMap["lmbp"] = [this] { LmbPressed(); };
    actionMap["lmbr"] = [this] { LmbReleased(); };
    actionMap["rmbp"] = [this] { RmbPressed(); };
    actionMap["rmbr"] = [this] { RmbReleased(); };
    actionMap["mmbp"] = [this] { cameraMovement.isMoving = true; };
    actionMap["mmbr"] = [this] { cameraMovement.isMoving = false; };
    actionMap["scroll"] = [this] { Scroll(); };

    statsPanel.scene = scene;
    statsPanel.ChangePositionAnchor({0.06f, 0.10f});
    statsPanel.ChangeSizeAnchor({0.88f, 0.82f});
    statsPanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    strategicHudWidget.scene = scene;
    strategicHudWidget.ChangePositionAnchor({0.012f, 0.012f});
    strategicHudWidget.ChangeSizeAnchor({0.42f, 0.055f});
    strategicHudWidget.UpdateSize({GetScreenWidth(), GetScreenHeight()});
}

// Rebuilds statistics layout after window size changes.
void StatsGuiSystem::UpdateUiWidgets(Vec2i size)
{
    statsPanel.UpdateSize(size);
    strategicHudWidget.UpdateSize(size);
}

// Draws the statistics overlay and top HUD.
void StatsGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    owner->AddUiWidget(&statsPanel);
    owner->AddUiWidget(&strategicHudWidget);
}

// Closes the statistics overlay.
void StatsGuiSystem::EscPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}

// Switches from statistics to build mode.
void StatsGuiSystem::BuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("build");
}

// Switches from statistics to road build mode.
void StatsGuiSystem::RoadBuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("road_build");
}

// Switches from statistics to destroy mode.
void StatsGuiSystem::DestroyPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("destroy");
}

// Opens headquarters from statistics mode.
void StatsGuiSystem::HeadquartersPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
    auto mapSystem = std::dynamic_pointer_cast<BasicMapViewSystem>(owner->systems["default"]);
    if (mapSystem != nullptr)
        mapSystem->OpenHeadquartersPanel();
}

// Toggles the statistics overlay.
void StatsGuiSystem::StatsPressed()
{
    EscPressed();
}

void StatsGuiSystem::FocusPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("focus");
}

// Handles clicks on the statistics overlay.
void StatsGuiSystem::LmbPressed()
{
    if (IsBuildHudButtonHovered(strategicHudWidget))
    {
        BuildPressed();
        return;
    }
    if (IsRoadHudButtonHovered(strategicHudWidget))
    {
        RoadBuildPressed();
        return;
    }
    if (IsDestroyHudButtonHovered(strategicHudWidget))
    {
        DestroyPressed();
        return;
    }
    if (IsStatsHudButtonHovered(strategicHudWidget))
    {
        StatsPressed();
        return;
    }
    if (IsFocusHudButtonHovered(strategicHudWidget))
    {
        FocusPressed();
        return;
    }

    Vector2 mouse = GetMousePosition();
    Rectangle panelBounds{
        static_cast<float>(statsPanel.pos.x),
        static_cast<float>(statsPanel.pos.y),
        static_cast<float>(statsPanel.size.x),
        static_cast<float>(statsPanel.size.y)};
    if (CheckCollisionPointRec(mouse, PanelCloseButtonRect(panelBounds)))
    {
        EscPressed();
        return;
    }

    statsPanel.HandleClick(Vec2i{static_cast<int>(mouse.x), static_cast<int>(mouse.y)});
}

// Stops left-button interaction.
void StatsGuiSystem::LmbReleased()
{
}

// Starts camera drag behind the statistics overlay.
void StatsGuiSystem::RmbPressed()
{
    cameraMovement.isMoving = true;
}

// Stops camera drag.
void StatsGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

// Zooms camera behind the statistics overlay.
void StatsGuiSystem::Scroll()
{
    ZoomCamera(scene);
}

FocusGuiSystem::FocusGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    scene = owner->scene;

    actionMap["esc"] = [this] { EscPressed(); };
    actionMap["q"] = [this] { BuildPressed(); };
    actionMap["r"] = [this] { RoadBuildPressed(); };
    actionMap["d"] = [this] { DestroyPressed(); };
    actionMap["e"] = [this] { HeadquartersPressed(); };
    actionMap["s"] = [this] { StatsPressed(); };
    actionMap["f"] = [this] { FocusPressed(); };
    actionMap["lmbp"] = [this] { LmbPressed(); };
    actionMap["lmbr"] = [this] { LmbReleased(); };
    actionMap["rmbp"] = [this] { RmbPressed(); };
    actionMap["rmbr"] = [this] { RmbReleased(); };
    actionMap["mmbp"] = [this] { cameraMovement.isMoving = true; };
    actionMap["mmbr"] = [this] { cameraMovement.isMoving = false; };
    actionMap["scroll"] = [this] { Scroll(); };

    focusPanel.scene = scene;
    focusPanel.ChangePositionAnchor({0.06f, 0.10f});
    focusPanel.ChangeSizeAnchor({0.88f, 0.82f});
    focusPanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    strategicHudWidget.scene = scene;
    strategicHudWidget.ChangePositionAnchor({0.012f, 0.012f});
    strategicHudWidget.ChangeSizeAnchor({0.42f, 0.055f});
    strategicHudWidget.UpdateSize({GetScreenWidth(), GetScreenHeight()});
}

void FocusGuiSystem::UpdateUiWidgets(Vec2i size)
{
    focusPanel.UpdateSize(size);
    strategicHudWidget.UpdateSize(size);
}

void FocusGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    owner->AddUiWidget(&focusPanel);
    owner->AddUiWidget(&strategicHudWidget);
}

void FocusGuiSystem::EscPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}

void FocusGuiSystem::BuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("build");
}

void FocusGuiSystem::RoadBuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("road_build");
}

void FocusGuiSystem::DestroyPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("destroy");
}

void FocusGuiSystem::HeadquartersPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
    auto mapSystem = std::dynamic_pointer_cast<BasicMapViewSystem>(owner->systems["default"]);
    if (mapSystem != nullptr)
        mapSystem->OpenHeadquartersPanel();
}

void FocusGuiSystem::StatsPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stats");
}

void FocusGuiSystem::FocusPressed()
{
    EscPressed();
}

void FocusGuiSystem::LmbPressed()
{
    if (IsBuildHudButtonHovered(strategicHudWidget))
    {
        BuildPressed();
        return;
    }
    if (IsRoadHudButtonHovered(strategicHudWidget))
    {
        RoadBuildPressed();
        return;
    }
    if (IsDestroyHudButtonHovered(strategicHudWidget))
    {
        DestroyPressed();
        return;
    }
    if (IsStatsHudButtonHovered(strategicHudWidget))
    {
        StatsPressed();
        return;
    }
    if (IsFocusHudButtonHovered(strategicHudWidget))
    {
        FocusPressed();
        return;
    }

    Vector2 mouse = GetMousePosition();
    Rectangle panelBounds{
        static_cast<float>(focusPanel.pos.x),
        static_cast<float>(focusPanel.pos.y),
        static_cast<float>(focusPanel.size.x),
        static_cast<float>(focusPanel.size.y)};
    if (CheckCollisionPointRec(mouse, PanelCloseButtonRect(panelBounds)))
    {
        EscPressed();
        return;
    }
}

void FocusGuiSystem::LmbReleased()
{
}

void FocusGuiSystem::RmbPressed()
{
    Vector2 mouse = GetMousePosition();
    if (focusPanel.ContainsPoint(Vec2i{static_cast<int>(mouse.x), static_cast<int>(mouse.y)}))
    {
        cameraMovement.isMoving = false;
        return;
    }
    cameraMovement.isMoving = true;
}

void FocusGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

void FocusGuiSystem::Scroll()
{
    Vector2 mouse = GetMousePosition();
    Vec2i point{static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
    if (focusPanel.ContainsPoint(point))
    {
        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
        {
            focusPanel.scrollOffset = std::clamp(focusPanel.scrollOffset - GetMouseWheelMove() * 48.0f, 0.0f, focusPanel.maxScrollOffset);
            return;
        }
        focusPanel.AdjustTreeZoom(point, GetMouseWheelMove());
        return;
    }
    ZoomCamera(scene);
}

// Propagates window resize to the battle info panel.
void BattleInfoPanel::UpdateSize(Vec2i windowSize)
{
    UiWidget::UpdateSize(windowSize);
}

// Renders battle parameters for both sides plus battle log.
void BattleInfoPanel::Update(double dt)
{
    if (!HasBattle() || scene == nullptr || scene->game == nullptr)
        return;

    const BattleInstance* battle = scene->game->battles.FindBattle(activeBattleId);
    if (battle == nullptr)
    {
        activeBattleId = -1;
        return;
    }

    Rectangle panel{(float)pos.x, (float)pos.y, (float)size.x, (float)size.y};

    DrawRectangleRec(panel, Color{18, 22, 30, 245});
    DrawRectangleLinesEx(panel, 2.0f, Color{180, 70, 50, 255});

    // Title row
    DrawText("BATTLE", (int)(panel.x + 12), (int)(panel.y + 10), 20, Color{220, 110, 70, 255});

    const char* stateLabel = "Ongoing";
    Color stateColor = Color{100, 220, 120, 255};
    switch (battle->state)
    {
        case BattleState::AttackerWon: stateLabel = "Attacker Won"; stateColor = Color{220, 100, 80, 255}; break;
        case BattleState::DefenderWon: stateLabel = "Defender Won"; stateColor = Color{80, 120, 220, 255}; break;
        case BattleState::Withdrawn:   stateLabel = "Withdrawn";    stateColor = Color{180, 180, 80, 255}; break;
        default: break;
    }
    std::string timeStr = std::to_string((int)battle->elapsedTime) + "s";
    DrawText(timeStr.c_str(), (int)(panel.x + panel.width / 2.0f - 15.0f), (int)(panel.y + 12), 15, Color{160, 160, 160, 255});
    DrawText(stateLabel, (int)(panel.x + panel.width - MeasureText(stateLabel, 15) - 10), (int)(panel.y + 12), 15, stateColor);

    float sepY = panel.y + 38.0f;
    DrawLineEx({panel.x, sepY}, {panel.x + panel.width, sepY}, 1.0f, Color{55, 60, 72, 255});

    float colW = panel.width / 2.0f - 8.0f;
    float leftX = panel.x + 6.0f;
    float rightX = panel.x + panel.width / 2.0f + 4.0f;
    float colY = panel.y + 46.0f;

    auto DrawSide = [&](int tileId, const std::vector<int>& supportIds, float colX, const char* sideLabel, Color sideColor)
    {
        DrawText(sideLabel, (int)colX, (int)colY, 13, sideColor);

        Building* b = scene->game->tilemap.GetBuilding(tileId);
        if (b == nullptr)
        {
            DrawText("(destroyed)", (int)colX, (int)(colY + 18), 13, Color{160, 80, 80, 255});
            return;
        }

        DrawText(b->name.c_str(), (int)colX, (int)(colY + 18), 15, RAYWHITE);

        auto* territory = b->GetComponent<TerritoryComponent>();
        if (territory != nullptr)
        {
            int hp = territory->hp;
            int maxHp = territory->GetMaxHp(*b);
            std::string hpStr = "HP: " + std::to_string(hp) + "/" + std::to_string(maxHp);
            DrawText(hpStr.c_str(), (int)colX, (int)(colY + 38), 13, Color{190, 200, 210, 255});

            Rectangle barBg{colX, colY + 56.0f, colW - 4.0f, 9.0f};
            Rectangle barFill = barBg;
            float ratio = maxHp > 0 ? std::clamp(hp / (float)maxHp, 0.0f, 1.0f) : 0.0f;
            barFill.width *= ratio;
            DrawRectangleRec(barBg, Color{35, 38, 45, 220});
            DrawRectangleRec(barFill, Color{75, 185, 100, 230});
            DrawRectangleLinesEx(barBg, 1.0f, Color{60, 65, 75, 200});
        }

        auto* garrison = b->GetComponent<GarrisonComponent>();
        if (garrison != nullptr)
        {
            int troops = garrison->GetTotalTroops();
            int divCount = (int)garrison->divisions.size();
            std::string troopsStr = "Div: " + std::to_string(divCount) + "  Troops: " + std::to_string(troops);
            DrawText(troopsStr.c_str(), (int)colX, (int)(colY + 70), 12, Color{170, 185, 205, 255});

            int strength = garrison->GetEffectiveStrength(*b);
            std::string strStr = "Strength: " + std::to_string(strength);
            DrawText(strStr.c_str(), (int)colX, (int)(colY + 86), 12, Color{210, 170, 100, 255});
        }

        if (!supportIds.empty())
        {
            DrawText("Support:", (int)colX, (int)(colY + 104), 12, Color{110, 170, 235, 255});
            float sy = colY + 120.0f;
            for (int sId : supportIds)
            {
                Building* sb = scene->game->tilemap.GetBuilding(sId);
                if (sb != nullptr)
                {
                    DrawText(sb->name.c_str(), (int)(colX + 8), (int)sy, 12, Color{150, 195, 240, 220});
                    sy += 15.0f;
                    if (sy > colY + 170.0f) break;
                }
            }
        }
    };

    DrawSide(battle->attackerTileId, battle->attackerSupportTileIds, leftX, "ATTACKER", Color{220, 90, 70, 255});
    DrawSide(battle->defenderTileId, battle->defenderSupportTileIds, rightX, "DEFENDER", Color{70, 110, 220, 255});

    float midLineX = panel.x + panel.width / 2.0f;
    DrawLineEx({midLineX, panel.y + 42.0f}, {midLineX, panel.y + 220.0f}, 1.0f, Color{55, 60, 72, 255});

    // Battle log
    float logSepY = panel.y + 224.0f;
    DrawLineEx({panel.x, logSepY}, {panel.x + panel.width, logSepY}, 1.0f, Color{55, 60, 72, 255});
    DrawText("BATTLE LOG", (int)(panel.x + 10), (int)(logSepY + 6), 12, Color{140, 145, 165, 255});

    float logY = logSepY + 24.0f;
    int logStart = std::max(0, (int)battle->log.size() - 10);
    for (int i = logStart; i < (int)battle->log.size(); i++)
    {
        if (logY > panel.y + panel.height - 8.0f) break;
        const auto& entry = battle->log[i];
        std::string tStr = "[" + std::to_string((int)entry.time) + "s] ";
        int tStrW = MeasureText(tStr.c_str(), 12);
        DrawText(tStr.c_str(), (int)(panel.x + 6), (int)logY, 12, Color{110, 130, 150, 255});
        DrawText(entry.message.c_str(), (int)(panel.x + 6 + tStrW), (int)logY, 12, Color{195, 200, 215, 255});
        logY += 16.0f;
    }
}

// ─── DivisionMapWidget ────────────────────────────────────────────────────────

static Color DivisionMarkerColor(MilitaryUnitType type)
{
    switch (type)
    {
        case MilitaryUnitType::Militia:   return Color{220, 200, 110, 255};
        case MilitaryUnitType::Swordsman: return Color{ 88, 178, 238, 255};
        case MilitaryUnitType::Archer:    return Color{118, 210, 130, 255};
        default:                          return Color{200, 200, 200, 255};
    }
}

// World-space position a division is drawn at: where it stands when deployed,
// otherwise just above its home building (still garrisoned).
static Vec2f DivisionRenderWorldPos(GameScene* scene, Building* building, const SoldierDivision& div)
{
    if (div.worldPos.x >= 0.0f)
        return div.worldPos;
    Vec2i c = scene->game->tilemap.GetCoordsFromId(building->positionId);
    Vec2i fp = building->GetFootprint();
    return {(c.x + fp.x * 0.5f) * TILE_SIZE,
            (c.y + fp.y * 0.5f) * TILE_SIZE - TILE_SIZE * 0.8f};
}

// Builds the marker list for division counters drawn by GameWorld::DrawMap.
void DivisionMapWidget::Update(double dt)
{
    markers.clear();
    if (scene == nullptr || scene->game == nullptr) return;

    struct Stack
    {
        Building* home{nullptr};
        Player* owner{nullptr};
        Vec2f world{0.0f, 0.0f};
        Vec2i tile{-1, -1};
        std::vector<int> ids;
        MilitaryUnitType type{MilitaryUnitType::Militia};
        bool moving{false};
    };

    for (auto& [playerId, player] : scene->game->playerHandler.players)
    {
        if (player == nullptr) continue;
        for (auto* building : player->GetTrackedBuildingsWithComponent<GarrisonComponent>())
        {
            if (building == nullptr) continue;
            auto* garrison = building->GetComponent<GarrisonComponent>();
            if (garrison == nullptr) continue;

            std::map<long long, Stack> stacks;
            for (const auto& div : garrison->divisions)
            {
                bool deployed = div.occupiedTile.x >= 0;
                Vec2f world = DivisionRenderWorldPos(scene, building, div);
                long long key = -(static_cast<long long>(building->positionId) + 1);
                if (deployed)
                {
                    DivisionSector sector = ResolveDivisionSector(scene->game->tilemap, div.occupiedTile, player.get());
                    if (sector.IsValid() && !div.inTransit)
                    {
                        Vec2f center = sector.CenterTile();
                        world = {center.x * TILE_SIZE, center.y * TILE_SIZE};
                    }
                    key = div.inTransit
                        ? (1000000000000LL + static_cast<long long>(div.sectorCell.x) * 100000 + div.sectorCell.y)
                        : (static_cast<long long>(div.sectorCell.x) * 100000 + div.sectorCell.y);
                }

                Stack& stack = stacks[key];
                if (stack.ids.empty())
                {
                    stack.home = building;
                    stack.owner = player.get();
                    stack.world = world;
                    stack.tile = div.occupiedTile;
                    stack.type = div.type;
                }
                else if (div.inTransit)
                {
                    float n = static_cast<float>(stack.ids.size());
                    stack.world = {
                        (stack.world.x * n + world.x) / (n + 1.0f),
                        (stack.world.y * n + world.y) / (n + 1.0f)};
                }
                stack.ids.push_back(div.id);
                if (div.inTransit)
                    stack.moving = true;
            }

            for (auto& [key, stack] : stacks)
            {
                Vec2f screen = scene->render.WorldToScreen(stack.world);
                Color color = DivisionMarkerColor(stack.type);
                markers.push_back({stack.home, stack.owner, stack.ids, stack.tile, {screen.x, screen.y}, color});

                Rectangle rect{screen.x - kMarkerHalfW, screen.y - kMarkerHalfH,
                               kMarkerHalfW * 2.0f, kMarkerHalfH * 2.0f};
                Color ownerColor = stack.owner != nullptr ? stack.owner->color : Color{220, 220, 220, 255};
                DrawRectangleRec(rect, Color{18, 22, 30, 225});
                DrawRectangleRec({rect.x, rect.y, 4.0f, rect.height}, color);
                DrawRectangleLinesEx(rect, stack.moving ? 2.0f : 1.0f,
                                     stack.moving ? Color{255, 255, 255, 210} : ownerColor);
                UiText::DrawFit(std::to_string(stack.ids.size()),
                                Rectangle{rect.x + 4.0f, rect.y + 2.0f, rect.width - 6.0f, rect.height - 4.0f},
                                14, RAYWHITE);
            }
        }
    }
}

// Returns the marker stack at a screen point, or nullptr.
const DivisionMapMarker* DivisionMapWidget::HitTest(Vec2i screenPoint) const
{
    for (const auto& m : markers)
    {
        if (std::abs(screenPoint.x - m.screenPos.x) <= kMarkerHalfW + 3.0f &&
            std::abs(screenPoint.y - m.screenPos.y) <= kMarkerHalfH + 3.0f)
            return &m;
    }
    return nullptr;
}

// Highlights the quadrant under the cursor (move destination) and rings the
// currently selected divisions.
void MoveTargetWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr || bar == nullptr)
        return;

    // Drag-selection rectangle.
    if (drawBox)
    {
        DrawRectangleRec(boxRect, Color{120, 200, 255, 40});
        DrawRectangleLinesEx(boxRect, 1.5f, Color{150, 215, 255, 220});
    }

    if (bar->building == nullptr || bar->selectedDivisionIds.empty())
        return;

    auto* garrison = bar->building->GetComponent<GarrisonComponent>();
    if (garrison == nullptr)
        return;

    Player* localPlayer = LocalPlayer(scene);

    // Draws a subtle outline (+ optional tint) around a single map tile.
    auto drawTileOutline = [&](Vec2i pos, Color lineCol, Color fillCol, float thick)
    {
        if (!scene->game->tilemap.IsInside(pos))
            return;
        Vec2f sTL = scene->render.WorldToScreen({pos.x * static_cast<float>(TILE_SIZE),
                                                 pos.y * static_cast<float>(TILE_SIZE)});
        Vec2f sBR = scene->render.WorldToScreen({(pos.x + 1) * static_cast<float>(TILE_SIZE),
                                                 (pos.y + 1) * static_cast<float>(TILE_SIZE)});
        Rectangle rect{sTL.x, sTL.y, sBR.x - sTL.x, sBR.y - sTL.y};
        if (fillCol.a > 0)
            DrawRectangleRec(rect, fillCol);
        DrawRectangleLinesEx(rect, thick, lineCol);
    };

    auto drawSectorOutline = [&](const DivisionSector& sector, Color lineCol, Color fillCol, float thick)
    {
        if (!sector.IsValid())
            return;
        for (int tileId : sector.TileIds(scene->game->tilemap))
            drawTileOutline(scene->game->tilemap.GetCoordsFromId(tileId), lineCol, fillCol, thick);
    };

    // (3) Subtly outline the tile each selected division currently holds.
    for (int divId : bar->selectedDivisionIds)
    {
        for (const auto& div : garrison->divisions)
        {
            if (div.id != divId)
                continue;
            if (div.occupiedTile.x >= 0)
            {
                drawSectorOutline(ResolveDivisionSector(scene->game->tilemap, div.occupiedTile, localPlayer),
                                  Color{135, 228, 158, 165}, Color{0, 0, 0, 0}, 1.5f);
            }
            else
            {
                Vec2f sc = scene->render.WorldToScreen(DivisionRenderWorldPos(scene, bar->building, div));
                DrawRectangleLinesEx({sc.x - 17.0f, sc.y - 12.0f, 34.0f, 24.0f}, 1.5f,
                                     Color{135, 228, 158, 170});
            }
            break;
        }
    }

    // (4) Move target under the cursor — single tile, kept subtle.
    Vector2 mouse = GetMousePosition();
    Vec2i mouseScreen{static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
    if (bar->ContainsPoint(mouseScreen) ||
        (armyBar != nullptr && armyBar->IsOverContent(mouseScreen)))
        return;
    Vec2i tile = ScreenToTile(scene, mouse);
    if (tile.x < 0 || tile.y < 0)
        return;

    DivisionSector targetSector = ResolveDivisionSector(scene->game->tilemap, tile, localPlayer);
    bool valid = targetSector.IsValid();
    bool hasFree = false;
    if (valid && localPlayer != nullptr)
    {
        for (int tileId : targetSector.TileIds(scene->game->tilemap))
        {
            Vec2i sectorTile = scene->game->tilemap.GetCoordsFromId(tileId);
            if (IsTileFree(*localPlayer, sectorTile, -1))
            {
                hasFree = true;
                break;
            }
        }
    }

    Color fill = !valid ? Color{220, 70, 60, 16}
               : hasFree ? Color{90, 220, 120, 20}
                         : Color{232, 172, 72, 22};
    Color line = !valid ? Color{235, 110, 100, 150}
               : hasFree ? Color{140, 235, 165, 165}
                         : Color{240, 200, 120, 160};

    if (valid)
        drawSectorOutline(targetSector, line, fill, 1.5f);
    else
        drawTileOutline(tile, line, fill, 1.5f);
}
