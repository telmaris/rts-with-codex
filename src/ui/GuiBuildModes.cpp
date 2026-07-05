// Build, road and destroy interaction modes plus the build panel, tooltip and
// ghost-preview widgets they share.

#include "GuiInternal.h"

#include "scenes/Scenes.h"
#include "economy/Player.h"
#include "economy/BuildingConfig.h"
#include "economy/ProductionBuildings.h"
#include "warfare/DivisionSector.h"
#include "warfare/ArmyOrder.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Counts a resource across owned storage-like buildings.
    int CountStoredResource(GameScene* scene, ResourceType type)
    {
        Player* player = GuiLocalPlayer(scene);
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
        Player* player = GuiLocalPlayer(scene);
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

    // Creates a build option for one concrete building class.
    template <typename T>
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

    // Creates a build option for a building type from its data definition.
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
            case BuildingType::Mint: return MakeBuildOption<Mint>(scene, definition);
            case BuildingType::Glassworks: return MakeBuildOption<Glassworks>(scene, definition);
            case BuildingType::Powderworks: return MakeBuildOption<Powderworks>(scene, definition);
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
}

// ─── BuildPanelWidget ────────────────────────────────────────────────────────

// Draws available build options grouped by category.
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
        InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        dragging = true;
        dragOffset = Vec2i{static_cast<int>(mouse.x) - pos.x, static_cast<int>(mouse.y) - pos.y};
    }
    if (dragging && InputManager::IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        pos.x = std::clamp(static_cast<int>(mouse.x) - dragOffset.x, 0, std::max(0, GetScreenWidth() - size.x));
        pos.y = std::clamp(static_cast<int>(mouse.y) - dragOffset.y, 0, std::max(0, GetScreenHeight() - size.y));
        bounds = Rectangle{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
        titleBounds = Rectangle{bounds.x, bounds.y, bounds.width, static_cast<float>(titleBar)};
        closeButton = PanelCloseButtonRect(bounds);
    }
    if (dragging && InputManager::IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
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

// ─── BuildGhostWidget ────────────────────────────────────────────────────────

// Draws the placement preview and validity tint under the cursor.
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

// ─── BuildGuiSystem ──────────────────────────────────────────────────────────

BuildGuiSystem::BuildGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    scene = owner->scene;

    WireCommonSystemActions(*this, cameraMovement);

    buildPanel.ChangePositionAnchor({0.69f, 0.08f});
    buildPanel.ChangeSizeAnchor({0.28f, 0.82f});
    buildPanel.scene = scene;
    buildPanel.options = &options;
    buildPanel.title = "Build";
    buildPanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    SetupStrategicHud(strategicHudWidget, scene);

    for (BuildingType type : GetBuildableBuildingTypes())
        options.push_back(MakeBuildOption(scene, type));
    SortBuildOptions(options);

    ghostWidget.scene = scene;
}

// Applies window size changes to build-mode widgets.
void BuildGuiSystem::UpdateUiWidgets(Vec2i size)
{
    buildPanel.UpdateSize(size);
    strategicHudWidget.UpdateSize(size);
}

// Updates camera drag, build panel and ghost preview.
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

// Cancels build mode and returns to map view.
void BuildGuiSystem::EscPressed()
{
    ReturnToMapView();
}

// Toggles build mode off.
void BuildGuiSystem::BuildPressed()
{
    ReturnToMapView();
}

// Switches to road build mode.
void BuildGuiSystem::RoadBuildPressed()
{
    owner->ChangeSystem("road_build");
}

// Switches to destroy mode.
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

void BuildGuiSystem::TechPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("tech");
}

// Selects a build option or places the selected building.
void BuildGuiSystem::LmbPressed()
{
    auto mousePos = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mousePos.x), static_cast<int>(mousePos.y)};
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

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

void BuildGuiSystem::LmbReleased()
{
}

// Starts camera drag.
void BuildGuiSystem::RmbPressed()
{
    cameraMovement.isMoving = true;
}

// Stops camera drag.
void BuildGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

// Scrolls the build panel or zooms the camera.
void BuildGuiSystem::Scroll()
{
    Vector2 mouse = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
    if (buildPanel.ContainsPoint(screenPos))
    {
        buildPanel.Scroll(InputManager::GetMouseWheelMove());
        return;
    }

    ZoomCamera(scene);
}

// Switches controller back to default map view.
void BuildGuiSystem::ReturnToMapView()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}

// Switches to map view and opens the headquarters panel.
void BuildGuiSystem::OpenHeadquartersAndReturn()
{
    cameraMovement.isMoving = false;
    SwitchToMapViewAndOpenHeadquarters(owner);
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

// Returns true when the selected building can be placed at a tile.
bool BuildGuiSystem::CanPlaceSelected(Vec2i tilePos) const
{
    if (selectedPreview == nullptr || scene->game == nullptr)
        return false;
    if (!scene->game->tilemap.IsInsideFootprint(tilePos, selectedPreview->GetFootprint()))
        return false;

    Player* player = GuiLocalPlayer(scene);
    if (player == nullptr)
        return false;

    const auto& definition = GetBuildingDefinition(selectedPreview->buildingType);
    bool debugFreeBuild = scene->game->tilemap.params.debugMode;
    return scene->game->tilemap.CanPlaceBuilding(selectedPreview->buildingType, tilePos, selectedPreview->GetFootprint(), player) &&
           (debugFreeBuild || player->CanBuildDefinition(definition));
}

// Selects a build option by index and refreshes the preview.
void BuildGuiSystem::SelectOption(size_t index)
{
    if (index >= options.size())
        return;

    selectedIndex = index;
    selectedPreview = options[selectedIndex].previewFactory();
    buildPanel.selectedIndex = selectedIndex;
}

// Rebuilds the ghost preview for the selected option.
void BuildGuiSystem::RefreshGhost()
{
    ghostWidget.selectedOption = selectedIndex < options.size() ? &options[selectedIndex] : nullptr;
    ghostWidget.tilePos = GetHoveredTile();
    ghostWidget.canBuild = CanPlaceSelected(ghostWidget.tilePos);
}

// Places the selected option under the cursor when placement is valid.
bool BuildGuiSystem::TryPlaceSelectedAtHovered(bool returnAfterBuild)
{
    Vec2i tilePos = GetHoveredTile();
    if (!CanPlaceSelected(tilePos))
        return false;

    if (selectedIndex >= options.size())
        return false;

    options[selectedIndex].buildAt(tilePos);
    if (returnAfterBuild)
        ReturnToMapView();

    return true;
}

// ─── RoadBuildSystem ─────────────────────────────────────────────────────────

RoadBuildSystem::RoadBuildSystem(GuiController* con)
    : BuildGuiSystem(con)
{
    buildPanel.title = "Roads";
    options.clear();
    for (BuildingType type : GetBuildableRoadTypes())
        options.push_back(MakeBuildOption(scene, type));
    SortBuildOptions(options);

    SelectOption(0);
}

// Updates camera drag, ghost preview and drag placement.
void RoadBuildSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    RefreshGhost();
    owner->AddUiWidget(&ghostWidget);
    owner->AddUiWidget(&strategicHudWidget);

    // Drag placement: keep painting roads while LMB is held, but never under
    // the strategic HUD buttons.
    if (InputManager::IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsAnyHudButtonHovered(strategicHudWidget))
        TryPlaceRoadAtHovered();
}

// Switches back to building placement mode.
void RoadBuildSystem::BuildPressed()
{
    lastRoadDragTile = {-9999, -9999};
    cameraMovement.isMoving = false;
    owner->ChangeSystem("build");
}

// Toggles road placement mode off.
void RoadBuildSystem::RoadBuildPressed()
{
    ReturnToMapView();
}

// Handles HUD buttons or starts placing roads.
void RoadBuildSystem::LmbPressed()
{
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    TryPlaceRoadAtHovered();
}

// Ends road drag placement.
void RoadBuildSystem::LmbReleased()
{
    lastRoadDragTile = {-9999, -9999};
}

void RoadBuildSystem::Scroll()
{
    ZoomCamera(scene);
}

// Places a road under the cursor once per hovered tile during a drag.
bool RoadBuildSystem::TryPlaceRoadAtHovered()
{
    Vec2i tilePos = GetHoveredTile();
    if (tilePos == lastRoadDragTile)
        return false;

    if (!CanPlaceSelected(tilePos))
        return false;

    if (selectedIndex >= options.size())
        return false;

    options[selectedIndex].buildAt(tilePos);
    lastRoadDragTile = tilePos;
    return true;
}

// ─── DestroyGuiSystem ────────────────────────────────────────────────────────

DestroyGuiSystem::DestroyGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    scene = owner->scene;

    WireCommonSystemActions(*this, cameraMovement);

    destroyTargetWidget.scene = scene;
    SetupStrategicHud(strategicHudWidget, scene);
}

// Applies window size changes to destroy-mode widgets.
void DestroyGuiSystem::UpdateUiWidgets(Vec2i size)
{
    strategicHudWidget.UpdateSize(size);
}

// Tracks the hovered destroy target and updates camera drag.
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
    if (hoveredBuilding != nullptr && hoveredBuilding->owner != GuiLocalPlayer(scene))
        hoveredBuilding = nullptr;

    destroyTargetWidget.building = hoveredBuilding;
    if (hoveredBuilding != nullptr)
        owner->AddUiWidget(&destroyTargetWidget);
    owner->AddUiWidget(&strategicHudWidget);
}

// Drops the hover target before leaving destroy mode.
void DestroyGuiSystem::ClearHoverTarget()
{
    hoveredBuilding = nullptr;
    destroyTargetWidget.building = nullptr;
}

// Cancels destroy mode.
void DestroyGuiSystem::EscPressed()
{
    ReturnToMapView();
}

// Switches to build mode.
void DestroyGuiSystem::BuildPressed()
{
    owner->ChangeSystem("build");
}

// Switches to road build mode.
void DestroyGuiSystem::RoadBuildPressed()
{
    owner->ChangeSystem("road_build");
}

// Toggles destroy mode off.
void DestroyGuiSystem::DestroyPressed()
{
    ReturnToMapView();
}

// Opens the headquarters panel from destroy mode.
void DestroyGuiSystem::HeadquartersPressed()
{
    ClearHoverTarget();
    cameraMovement.isMoving = false;
    SwitchToMapViewAndOpenHeadquarters(owner);
}

// Opens the statistics panel from destroy mode.
void DestroyGuiSystem::StatsPressed()
{
    cameraMovement.isMoving = false;
    ClearHoverTarget();
    owner->ChangeSystem("stats");
}

void DestroyGuiSystem::FocusPressed()
{
    cameraMovement.isMoving = false;
    ClearHoverTarget();
    owner->ChangeSystem("focus");
}

void DestroyGuiSystem::TechPressed()
{
    cameraMovement.isMoving = false;
    ClearHoverTarget();
    owner->ChangeSystem("tech");
}

// Destroys the hovered building when allowed.
void DestroyGuiSystem::LmbPressed()
{
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    if (scene->game == nullptr || hoveredBuilding == nullptr)
        return;
    if (!hoveredBuilding->CanBeManuallyDestroyed())
        return;

    int positionId = hoveredBuilding->positionId;
    ClearHoverTarget();
    scene->SubmitLocalCommand(GameCommand::DestroyBuilding(scene->game->GetLocalPlayerId(), positionId));
    ReturnToMapView();
}

void DestroyGuiSystem::LmbReleased()
{
}

// Starts camera drag.
void DestroyGuiSystem::RmbPressed()
{
    cameraMovement.isMoving = true;
}

// Stops camera drag.
void DestroyGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

void DestroyGuiSystem::Scroll()
{
    ZoomCamera(scene);
}

// Returns to the default map view.
void DestroyGuiSystem::ReturnToMapView()
{
    cameraMovement.isMoving = false;
    ClearHoverTarget();
    owner->ChangeSystem("default");
}

// ─── BorderDeployMode ───────────────────────────────────────────────────────

BorderDeployMode::BorderDeployMode(GuiController* con) : GuiSystem(con)
{
    scene = owner->scene;
    // Manual wiring (simplified — BorderDeployMode is just for frontier selection).
    actionMap["esc"] = [this] { ReturnToMapView(); };
    actionMap["rmbp"] = [this] { RmbPressed(); };
    actionMap["rmbr"] = [this] { RmbReleased(); };
    actionMap["mmbp"] = [this] { cameraMovement.isMoving = true; };
    actionMap["mmbr"] = [this] { cameraMovement.isMoving = false; };
    actionMap["scroll"] = [this] { ZoomCamera(scene); };
}

void BorderDeployMode::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* localPlayer = GuiLocalPlayer(scene);
    if (localPlayer == nullptr)
        return;

    // Handle camera movement.
    MoveCamera(scene, cameraMovement);

    // Show instructions overlay.
    DrawText("Mark frontier segment with RMB drag. ESC to cancel.",
             20, static_cast<int>(GetScreenHeight()) - 40, 16,
             Color{200, 220, 240, 255});

    // Render the selected frontier tiles as highlighted quadrants.
    if (!selectedFrontierTiles.empty())
    {
        for (Vec2i tile : selectedFrontierTiles)
        {
            Vec2f wTL{tile.x * static_cast<float>(TILE_SIZE), tile.y * static_cast<float>(TILE_SIZE)};
            Vec2f wBR{(tile.x + 1) * static_cast<float>(TILE_SIZE),
                      (tile.y + 1) * static_cast<float>(TILE_SIZE)};
            Vec2f sTL = scene->render.WorldToScreen(wTL);
            Vec2f sBR = scene->render.WorldToScreen(wBR);
            Rectangle rect{sTL.x, sTL.y, sBR.x - sTL.x, sBR.y - sTL.y};

            DrawRectangleRec(rect, Color{100, 200, 150, 60});
            DrawRectangleLinesEx(rect, 2.0f, Color{100, 255, 150, 200});
        }
    }

    // Drag preview: line from dragStart to current mouse.
    if (dragging && dragStart.x >= 0)
    {
        Vector2 mouse = GetMousePosition();
        Vec2f wStart{dragStart.x * static_cast<float>(TILE_SIZE) + TILE_SIZE / 2.0f,
                     dragStart.y * static_cast<float>(TILE_SIZE) + TILE_SIZE / 2.0f};
        Vec2f sStart = scene->render.WorldToScreen(wStart);
        DrawLineEx({sStart.x, sStart.y}, mouse, 2.0f, Color{150, 255, 150, 200});
    }
}

void BorderDeployMode::RmbPressed()
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Vector2 mouse = GetMousePosition();
    dragStart = ScreenToTile(scene, mouse);
    dragEnd = dragStart;
    dragging = true;
    selectedFrontierTiles.clear();

    Player* localPlayer = GuiLocalPlayer(scene);
    if (localPlayer != nullptr && IsFrontierTile(dragStart, scene->game->tilemap, *localPlayer))
    {
        selectedFrontierTiles.push_back(dragStart);
    }
}

void BorderDeployMode::RmbReleased()
{
    if (!dragging || dragStart.x < 0)
    {
        dragging = false;
        return;
    }

    dragging = false;
    Vector2 mouse = GetMousePosition();
    dragEnd = ScreenToTile(scene, mouse);

    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* localPlayer = GuiLocalPlayer(scene);
    if (localPlayer == nullptr)
        return;

    // Collect the frontier segment from drag start to end.
    selectedFrontierTiles = CollectFrontierSegment(dragStart, dragEnd, scene->game->tilemap, *localPlayer);

    if (selectedFrontierTiles.empty())
    {
        Log::Msg("[BorderDeploy]", "No frontier tiles selected");
        return;
    }

    // Issue the border deploy order.
    std::vector<int> tileIds;
    for (Vec2i tile : selectedFrontierTiles)
    {
        if (scene->game->tilemap.IsInside(tile))
            tileIds.push_back(scene->game->tilemap.GetIdFromCoords(tile));
    }

    Log::Msg("[BorderDeploy]", "Selected ", tileIds.size(), " frontier tiles");
    // TODO: Issue GameCommand::IssueBorderDeployOrder(playerId, armyId, tileIds)

    ReturnToMapView();
}

void BorderDeployMode::ReturnToMapView()
{
    dragging = false;
    dragStart = {-1, -1};
    dragEnd = {-1, -1};
    selectedFrontierTiles.clear();
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}
