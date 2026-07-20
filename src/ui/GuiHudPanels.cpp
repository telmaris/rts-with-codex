// Strategic top HUD, the full-screen statistics panel and its interaction mode.

#include "GuiInternal.h"

#include "scenes/Scenes.h"
#include "economy/Player.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    // Player-wide strategic stats aggregated for the HUD and statistics panel.
    struct PlayerStatsSnapshot
    {
        int freeManpower{0};
        int workers{0};
        int totalPopulation{0};
        int populationCap{0};
        double manpowerGainPerMinute{0.0};
        int foodSupplyPercent{100};
        int workerProductivityPercent{100};
        double villageFoodConsumptionPerMinute{0.0};
        std::map<ResourceType, int> storedResources;
        std::map<ResourceType, int> productionRatesPerMinute;
        std::map<ResourceType, int> consumptionRatesPerMinute;
        int buildingCount{0};
        int roadCount{0};
        int totalProduced{0};
    };

    PlayerStatsSnapshot BuildPlayerStatsSnapshot(Player* player)
    {
        PlayerStatsSnapshot stats;
        if (player == nullptr)
            return stats;

        stats.freeManpower = static_cast<int>(std::floor(player->strategicResources.Get(StrategicResourceType::Manpower)));
        stats.workers = static_cast<int>(std::floor(player->strategicResources.Get(StrategicResourceType::Workers)));
        stats.totalPopulation = static_cast<int>(std::floor(player->GetTotalPopulation()));
        stats.populationCap = player->GetPopulationCap();
        // T6 (docs/post_pivot_audit_2026-07-12.md): the HUD chip must show the
        // raw food supply ratio, not GetFoodProductivity()'s floored (0.3+)
        // worker-productivity average — the village panel already shows the
        // real supply ratio, and the two disagreed (e.g. "30%" vs "0%").
        stats.foodSupplyPercent = static_cast<int>(std::round(player->GetFoodSupplyRatio() * 100.0));
        stats.workerProductivityPercent = static_cast<int>(std::round(player->GetFoodProductivity() * 100.0));
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

    // Short resource label for endpoint chips on the flow chart.
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
            case ResourceType::COPPER_SWORD: return "CuSw";
            case ResourceType::IRON_SWORD: return "FeSw";
            case ResourceType::STEEL_SWORD: return "StSw";
            case ResourceType::BOW: return "Bow";
            case ResourceType::ARROWS: return "Arr";
            default: return "Res";
        }
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
}

// ─── StrategicResourceHudWidget ──────────────────────────────────────────────

void StrategicResourceHudWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* player = GuiLocalPlayer(scene);
    if (player == nullptr)
        return;

    PlayerStatsSnapshot stats = BuildPlayerStatsSnapshot(player);
    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    DrawRectangle(0, 0, GetScreenWidth(), static_cast<int>(bounds.height), Color{26, 19, 14, 238});
    DrawRectangleGradientV(0, 0, GetScreenWidth(), static_cast<int>(bounds.height), Color{52, 40, 28, 230}, Color{22, 16, 12, 238});
    DrawRectangle(0, static_cast<int>(bounds.height - 2.0f), GetScreenWidth(), 2, Color{150, 108, 58, 235});

    float iconSize = std::max(26.0f, bounds.height - 20.0f);
    float y = bounds.y + (bounds.height - iconSize) * 0.5f;
    Rectangle manpowerIcon{bounds.x + 18.0f, y, iconSize, iconSize};
    Rectangle foodIcon{bounds.x + 150.0f, y, iconSize, iconSize};

    auto drawManpowerIcon = [&](Rectangle icon)
    {
        DrawCircle(static_cast<int>(icon.x + icon.width * 0.5f), static_cast<int>(icon.y + icon.height * 0.28f), icon.width * 0.16f, Color{224, 208, 178, 255});
        DrawRectangleRounded(Rectangle{icon.x + icon.width * 0.28f, icon.y + icon.height * 0.46f, icon.width * 0.44f, icon.height * 0.38f}, 0.30f, 8, Color{224, 208, 178, 255});
    };

    auto drawStatChip = [&](Rectangle icon, const std::string& text, Color accent, std::function<void(Rectangle)> drawIcon)
    {
        Rectangle chip{icon.x - 6.0f, icon.y - 4.0f, 120.0f, icon.height + 8.0f};
        DrawRectangleRounded(chip, 0.16f, 8, Color{44, 33, 23, 232});
        DrawRectangleRoundedLines(chip, 0.16f, 8, 1.0f, Color{accent.r, accent.g, accent.b, 170});
        DrawRectangleRounded(icon, 0.16f, 8, Color{50, 38, 27, 238});
        drawIcon(icon);
        UiText::DrawFit(text, Rectangle{icon.x + icon.width + 8.0f, chip.y + 5.0f, chip.width - icon.width - 14.0f, chip.height - 10.0f}, 22, UiTheme::Parchment);
    };

    auto drawResourceChip = [&](ResourceType type, float x)
    {
        Rectangle chip{x, y - 4.0f, 116.0f, iconSize + 8.0f};
        Rectangle icon{chip.x + 6.0f, y, iconSize, iconSize};
        int amount = 0;
        auto it = stats.storedResources.find(type);
        if (it != stats.storedResources.end())
            amount = it->second;

        DrawRectangleRounded(chip, 0.16f, 8, Color{42, 31, 22, 228});
        DrawRectangleRoundedLines(chip, 0.16f, 8, 1.0f, Color{120, 92, 58, 220});
        GuiPanel::DrawResourceIcon(type, Rectangle{icon.x + 3.0f, icon.y + 3.0f, icon.width - 6.0f, icon.height - 6.0f});
        UiText::DrawFit(std::to_string(amount), Rectangle{icon.x + icon.width + 8.0f, chip.y + 5.0f, chip.width - icon.width - 16.0f, chip.height - 10.0f}, 21, Color{228, 210, 180, 255});
    };

    drawStatChip(manpowerIcon, std::to_string(stats.freeManpower), Color{188, 150, 96, 255}, drawManpowerIcon);

    drawStatChip(foodIcon, std::to_string(stats.foodSupplyPercent) + "%", Color{145, 198, 118, 255}, [&](Rectangle icon)
    {
        GuiPanel::DrawResourceIcon(ResourceType::MEAT, Rectangle{icon.x + 4.0f, icon.y + 4.0f, icon.width - 8.0f, icon.height - 8.0f});
    });

    // Builders chip: free / total construction builders available to the player.
    Rectangle buildersIcon{foodIcon.x + 132.0f, y, iconSize, iconSize};
    int totalBuilders = player->construction.EffectiveBuilders(*player);
    int freeBuilders = std::max(0, totalBuilders - player->construction.ActiveCount());
    drawStatChip(buildersIcon, std::to_string(freeBuilders) + "/" + std::to_string(totalBuilders),
                 Color{201, 174, 122, 255}, [&](Rectangle icon)
    {
        float cx = icon.x + icon.width * 0.5f;
        DrawRectangleRounded(Rectangle{icon.x + icon.width * 0.28f, icon.y + icon.height * 0.22f, icon.width * 0.46f, icon.height * 0.20f}, 0.35f, 6, Color{224, 208, 178, 255});
        DrawRectangleRounded(Rectangle{cx - icon.width * 0.06f, icon.y + icon.height * 0.36f, icon.width * 0.12f, icon.height * 0.44f}, 0.40f, 6, Color{201, 174, 122, 255});
    });

    Rectangle statsButton = StatsHudButtonRect(*this);
    Rectangle focusButton = FocusHudButtonRect(*this);
    Rectangle techButton = TechHudButtonRect(*this);
    Rectangle destroyButton = DestroyHudButtonRect(*this);
    Rectangle roadButton = RoadHudButtonRect(*this);
    Rectangle buildButton = BuildHudButtonRect(*this);
    Rectangle rosterButton = RosterHudButtonRect(*this);

    float resourceX = buildersIcon.x + 130.0f;
    float resourceRightLimit = rosterButton.x - 12.0f;
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
    bool techUnlocked = HasUniversity(scene);
    bool techHovered = CheckCollisionPointRec(GetMousePosition(), techButton);
    bool destroyHovered = CheckCollisionPointRec(GetMousePosition(), destroyButton);
    bool roadHovered = CheckCollisionPointRec(GetMousePosition(), roadButton);
    bool buildHovered = CheckCollisionPointRec(GetMousePosition(), buildButton);
    bool focusAvailable = player->focuses.GetActiveFocusId().empty();
    float focusProgress = static_cast<float>(player->focuses.GetActiveFocusProgress());
    float pulse = focusAvailable ? (0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 4.0f)) : 0.0f;

    // TD(etap-8.5): roster summary + "HQ under attack" warning. Derived
    // live from simulation state rather than a stored flag — no besieging
    // AttackingHq unit targeting this player means no warning, so it clears
    // itself the instant the siege actually ends.
    bool rosterHovered = CheckCollisionPointRec(GetMousePosition(), rosterButton);
    int rosterCount = static_cast<int>(player->roster.units.size());
    bool hqUnderAttack = false;
    for (const auto& [unitId, unit] : scene->game->GetDeployedUnits())
    {
        if (unit.state == BattleUnitState::AttackingHq && unit.routeToPlayerId == player->id)
        {
            hqUnderAttack = true;
            break;
        }
    }
    float warningPulse = hqUnderAttack ? (0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 6.0f)) : 0.0f;

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
        UiText::DrawFit(label, Rectangle{rect.x + 10.0f, rect.y + 4.0f, rect.width - 20.0f, rect.height - 8.0f}, 20, UiTheme::Parchment);
    };

    drawHudButton(buildButton, "Build", buildHovered, Color{43, 60, 52, 238}, Color{92, 151, 118, 230});
    drawHudButton(roadButton, "Road", roadHovered, Color{54, 46, 34, 238}, Color{140, 112, 74, 230});
    drawHudButton(destroyButton, "Destroy", destroyHovered, Color{62, 45, 48, 238}, Color{157, 92, 100, 230});

    // Roster button — pulses red border when this player's HQ is under siege.
    Color rosterBase = hqUnderAttack
        ? Color{static_cast<unsigned char>(90 + warningPulse * 60.0f), 45, 48, 238}
        : Color{46, 34, 24, 238};
    Color rosterLine = hqUnderAttack ? Color{244, 132, 142, 255} : Color{128, 108, 80, 230};
    drawHudButton(rosterButton, "Roster (" + std::to_string(rosterCount) + ")", rosterHovered, rosterBase, rosterLine);
    if (hqUnderAttack)
    {
        UiText::DrawFit("HQ UNDER ATTACK", Rectangle{rosterButton.x, rosterButton.y - 18.0f, rosterButton.width, 15.0f},
                        13, Color{255, 140, 140, 255});
    }

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
    UiText::DrawFit("Focus Tree", Rectangle{focusButton.x + 14.0f, focusButton.y + 4.0f, focusButton.width - 28.0f, focusButton.height - 14.0f}, 21, UiTheme::Parchment);
    Rectangle focusBar{focusButton.x + 12.0f, focusButton.y + focusButton.height - 9.0f, focusButton.width - 24.0f, 4.0f};
    DrawRectangleRounded(focusBar, 0.6f, 6, Color{26, 22, 34, 170});
    Rectangle focusFillBar = focusBar;
    focusFillBar.width *= std::clamp(focusProgress, 0.0f, 1.0f);
    if (focusFillBar.width >= 1.0f)
        DrawRectangleRounded(focusFillBar, 0.6f, 6, focusAvailable ? Color{255, 220, 116, 210} : Color{178, 140, 248, 235});

    if (techUnlocked)
        drawHudButton(techButton, "Technology", techHovered, Color{46, 40, 30, 238}, Color{130, 100, 66, 230});
    else
    {
        DrawRectangleRounded(techButton, 0.14f, 8, Color{30, 24, 19, 220});
        DrawRectangleRoundedLines(techButton, 0.14f, 8, 1.1f, Color{96, 82, 66, 200});
        UiText::DrawFit("Technology", Rectangle{techButton.x + 10.0f, techButton.y + 4.0f, techButton.width - 20.0f, techButton.height - 8.0f}, 20, Color{150, 132, 108, 230});
    }

    DrawRectangleRounded(statsButton, 0.14f, 8, statsHovered ? Color{66, 50, 36, 245} : Color{46, 34, 24, 238});
    DrawRectangleRoundedLines(statsButton, 0.14f, 8, 1.2f, statsHovered ? Color{176, 138, 82, 245} : Color{112, 88, 58, 230});
    UiText::DrawFit("Resources", Rectangle{statsButton.x + 12.0f, statsButton.y + 4.0f, statsButton.width - 24.0f, statsButton.height - 8.0f}, 21, UiTheme::Parchment);

    Vector2 mouse = GetMousePosition();
    if (CheckCollisionPointRec(mouse, Rectangle{manpowerIcon.x, bounds.y, 125.0f, bounds.height}))
    {
        Tooltip::Draw("Manpower", {
            "Free: " + std::to_string(stats.freeManpower),
            "Workers: " + std::to_string(stats.workers),
            "Total: " + std::to_string(stats.totalPopulation) + "/" + std::to_string(stats.populationCap),
            "Gain: +" + FormatOneDecimal(stats.manpowerGainPerMinute) + " / min"
        }, 280.0f);
    }
    else if (CheckCollisionPointRec(mouse, Rectangle{foodIcon.x, bounds.y, 155.0f, bounds.height}))
    {
        Tooltip::Draw("Food Supply", {
            "Supply: " + std::to_string(stats.foodSupplyPercent) + "%",
            "Worker productivity: " + std::to_string(stats.workerProductivityPercent) + "%",
            "Village consumption: " + FormatOneDecimal(stats.villageFoodConsumptionPerMinute) + " / min"
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
    else if (techHovered)
    {
        if (techUnlocked)
            Tooltip::Draw("Technology", {"[T] Open technology research tree"}, 270.0f);
        else
            Tooltip::Draw("Technology", {"Requires a completed University"}, 270.0f);
    }
    else if (rosterHovered)
    {
        Tooltip::Draw("Roster", {
            "[U] Open roster and deploy",
            "Ready to deploy: " + std::to_string(rosterCount),
            hqUnderAttack ? "Your HQ is under attack!" : "No units are currently besieging your HQ"
        }, 260.0f);
    }
}

void StrategicResourceHudWidget::UpdateSize(Vec2i windowSize)
{
    UpdateStrategicHudLayout(*this, windowSize);
}

// ─── StatsPanelWidget ────────────────────────────────────────────────────────

// Time-window spinner in the title bar.
Rectangle StatsPanelWidget::GetWindowSpinnerRect() const
{
    return Rectangle{pos.x + size.x - 244.0f, pos.y + 10.0f, 190.0f, 34.0f};
}

// Production/consumption toggle in the title bar.
Rectangle StatsPanelWidget::GetFlowModeToggleRect() const
{
    return Rectangle{pos.x + size.x - 500.0f, pos.y + 10.0f, 220.0f, 34.0f};
}

// Chart column (right of the population/army column).
Rectangle StatsPanelWidget::GetChartRect() const
{
    float colGap = 22.0f;
    float top = pos.y + 78.0f;
    float bottom = pos.y + size.y - 22.0f;
    float leftWidth = size.x * 0.31f;
    float chartX = pos.x + 24.0f + leftWidth + colGap;
    return Rectangle{chartX, top, pos.x + size.x - chartX - 24.0f, bottom - top};
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

// Draws the player-wide economy and strategic statistics panel.
void StatsPanelWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* player = GuiLocalPlayer(scene);
    if (player == nullptr)
        return;

    PlayerStatsSnapshot stats = BuildPlayerStatsSnapshot(player);
    const bool showingConsumption = selectedFlowMode == 1;
    const auto& currentRates = showingConsumption ? stats.consumptionRatesPerMinute : stats.productionRatesPerMinute;
    const auto& flowHistory = player->economyTelemetry.history;
    double historyTime = player->economyTelemetry.elapsedTime;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    DrawRectangleRounded(bounds, 0.025f, 8, Color{30, 22, 16, 244});
    DrawRectangleRoundedLines(bounds, 0.025f, 8, 1.0f, Color{150, 108, 58, 255});

    Rectangle title{bounds.x, bounds.y, bounds.width, 54.0f};
    DrawRectangleRounded(title, 0.025f, 8, Color{50, 38, 27, 255});
    UiText::DrawTitleBar(title, "Statistics", PanelTitleCloseReserve(bounds));
    DrawCloseButton(bounds);

    constexpr std::array<int, 3> windows{15, 60, 300};
    const char* windowLabels[] = {"15 sec", "1 min", "5 min"};
    // Right edge is at title.width - 54, giving a 10px gap before the close button (at title.width - 44).
    Rectangle spin = GetWindowSpinnerRect();
    DrawRectangleRounded(spin, 0.12f, 8, Color{30, 22, 16, 255});
    DrawRectangleRoundedLines(spin, 0.12f, 8, 1.0f, Color{118, 92, 62, 255});
    UiText::DrawFit("<", Rectangle{spin.x + 8.0f, spin.y + 4.0f, 24.0f, 24.0f}, 24, UiTheme::Parchment);
    UiText::DrawFit(windowLabels[selectedWindowIndex], Rectangle{spin.x + 42.0f, spin.y + 5.0f, spin.width - 84.0f, 24.0f}, 22, UiTheme::Parchment);
    UiText::DrawFit(">", Rectangle{spin.x + spin.width - 32.0f, spin.y + 4.0f, 24.0f, 24.0f}, 24, UiTheme::Parchment);

    Rectangle modeToggle = GetFlowModeToggleRect();
    const char* modeLabels[] = {"Production", "Consumption"};
    for (int i = 0; i < 2; i++)
    {
        Rectangle half{modeToggle.x + i * modeToggle.width * 0.5f, modeToggle.y, modeToggle.width * 0.5f, modeToggle.height};
        bool selected = selectedFlowMode == i;
        DrawRectangleRounded(half, 0.10f, 8, selected ? Color{66, 60, 32, 245} : Color{30, 22, 16, 235});
        DrawRectangleRoundedLines(half, 0.10f, 8, 1.0f, selected ? UiTheme::Gold : Color{100, 84, 64, 220});
        UiText::DrawFit(modeLabels[i], Rectangle{half.x + 8.0f, half.y + 6.0f, half.width - 16.0f, 22.0f}, 18, UiTheme::Parchment);
    }

    float top = bounds.y + 78.0f;
    float bottom = bounds.y + bounds.height - 22.0f;
    Rectangle left{bounds.x + 24.0f, top, bounds.width * 0.31f, bottom - top};
    Rectangle chart = GetChartRect();

    auto drawColumn = [](Rectangle col, const std::string& titleText)
    {
        DrawRectangleRounded(col, 0.035f, 8, Color{38, 28, 20, 232});
        DrawRectangleRoundedLines(col, 0.035f, 8, 1.0f, Color{120, 92, 58, 255});
        UiText::DrawFit(titleText, Rectangle{col.x + 14.0f, col.y + 12.0f, col.width - 28.0f, 26.0f}, 23, UiTheme::Parchment);
    };

    auto drawRow = [](Rectangle col, int index, const std::string& label, const std::string& value, Color valueColor = UiTheme::Parchment)
    {
        float y = col.y + 52.0f + index * 30.0f;
        UiText::DrawFit(label, Rectangle{col.x + 14.0f, y, col.width * 0.58f, 22.0f}, 20, UiTheme::ParchmentDim);
        UiText::DrawFit(value, Rectangle{col.x + col.width * 0.58f, y, col.width * 0.38f - 12.0f, 22.0f}, 20, valueColor);
    };

    drawColumn(left, "Population & economy");
    drawRow(left, 0, "Free manpower", std::to_string(stats.freeManpower));
    drawRow(left, 1, "Workers", std::to_string(stats.workers));
    drawRow(left, 2, "Total / cap", std::to_string(stats.totalPopulation) + " / " + std::to_string(stats.populationCap));
    drawRow(left, 3, "Growth", "+" + FormatOneDecimal(stats.manpowerGainPerMinute) + " / min", UiTheme::SageBright);
    drawRow(left, 4, "Food supply", std::to_string(stats.foodSupplyPercent) + "%", stats.foodSupplyPercent < 60 ? UiTheme::RustBright : UiTheme::SageBright);
    drawRow(left, 5, "Food use", FormatOneDecimal(stats.villageFoodConsumptionPerMinute) + " / min");
    drawRow(left, 6, "Worker productivity", std::to_string(stats.workerProductivityPercent) + "%");
    drawRow(left, 7, "Buildings", std::to_string(stats.buildingCount));
    drawRow(left, 8, "Roads", std::to_string(stats.roadCount));
    drawRow(left, 9, "Recruitment buildings", std::to_string(player->GetTrackedBuildingCount(BuildingType::Barracks, true)));
    drawRow(left, 10, "Build commands", std::to_string(player->GetAcceptedCommandCount(GameCommandType::BuildBuilding)));

    drawColumn(chart, showingConsumption ? "Consumption graph" : "Production graph");
    Rectangle plot{chart.x + 46.0f, chart.y + 64.0f, chart.width - 72.0f, chart.height - 142.0f};
    DrawRectangleRounded(plot, 0.02f, 8, Color{22, 16, 12, 255});
    for (int i = 1; i <= 4; i++)
    {
        float y = plot.y + plot.height * i / 5.0f;
        DrawLineEx(Vector2{plot.x, y}, Vector2{plot.x + plot.width, y}, 1.0f, Color{74, 58, 42, 180});
    }

    double windowSeconds = static_cast<double>(windows[selectedWindowIndex]);
    double startTime = std::max(0.0, historyTime - windowSeconds);
    double tickSeconds = selectedWindowIndex == 0 ? 1.0 : (selectedWindowIndex == 1 ? 5.0 : 15.0);
    double firstTick = std::floor(startTime / tickSeconds) * tickSeconds;
    for (double tick = firstTick; tick <= historyTime; tick += tickSeconds)
    {
        float age = static_cast<float>((historyTime - tick) / windowSeconds);
        float x = plot.x + plot.width - std::clamp(age, 0.0f, 1.0f) * plot.width;
        Color tickColor = std::fmod(tick, tickSeconds * 5.0) < 0.001 ? Color{120, 92, 58, 185} : Color{52, 40, 30, 150};
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
            DrawCircleV(point, rate > 0 ? 3.0f : 2.0f, rate > 0 ? color : Color{110, 92, 70, 190});
            previous = point;
            lastPoint = point;
            lastRate = rate;
            hasPrevious = true;
        }
        if (hasPrevious && lastRate > 0)
            endpoints.push_back({type, lastPoint, lastRate, color});
    }

    UiText::DrawFit("0", Rectangle{plot.x - 28.0f, plot.y + plot.height - 16.0f, 24.0f, 16.0f}, 16, Color{190, 172, 140, 255});
    UiText::DrawFit(std::to_string(maxObservedRate) + "/m", Rectangle{plot.x - 42.0f, plot.y - 4.0f, 40.0f, 18.0f}, 16, Color{190, 172, 140, 255});
    UiText::DrawFit("-" + std::string(windowLabels[selectedWindowIndex]), Rectangle{plot.x, plot.y + plot.height + 6.0f, 54.0f, 18.0f}, 16, Color{190, 172, 140, 255});
    UiText::DrawFit("now", Rectangle{plot.x + plot.width - 36.0f, plot.y + plot.height + 6.0f, 36.0f, 18.0f}, 16, Color{190, 172, 140, 255});

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
        DrawRectangleRounded(chip, 0.16f, 8, Color{26, 19, 14, 228});
        DrawRectangleRoundedLines(chip, 0.16f, 8, 1.0f, endpoint.color);
        DrawRectangleRounded(Rectangle{chip.x + 5.0f, chip.y + 5.0f, 7.0f, 7.0f}, 0.35f, 4, endpoint.color);
        UiText::DrawFit(label, Rectangle{chip.x + 15.0f, chip.y + 2.0f, chip.width - 19.0f, 14.0f}, 13, UiTheme::Parchment);
        endpointLabels++;
    }

    Rectangle allButton = GetAllFilterButtonRect(chart);
    bool allActive = selectedResources.empty();
    DrawRectangleRounded(allButton, 0.16f, 8, allActive ? Color{55, 80, 64, 245} : Color{40, 29, 21, 240});
    DrawRectangleRoundedLines(allButton, 0.16f, 8, 1.0f, allActive ? Color{118, 226, 150, 230} : Color{100, 84, 64, 230});
    UiText::DrawFit("All", Rectangle{allButton.x + 4.0f, allButton.y + 6.0f, allButton.width - 8.0f, 18.0f}, 18, UiTheme::Parchment);

    int shown = 0;
    for (ResourceType type : filterResources)
    {
        if (shown >= 18)
            break;
        Rectangle slot = GetFilterButtonRect(chart, shown);
        Color color = ResourceChartColor(type);
        bool active = std::find(activeResources.begin(), activeResources.end(), type) != activeResources.end();
        bool selected = selectedResources.empty() || selectedResources.contains(type);
        DrawRectangleRounded(slot, 0.12f, 8, selected && active ? Color{54, 44, 28, 245} : Color{30, 22, 16, 210});
        DrawRectangleRoundedLines(slot, 0.12f, 8, 1.0f, selected && active ? color : Color{100, 84, 64, 210});
        GuiPanel::DrawResourceIcon(type, Rectangle{slot.x + 5.0f, slot.y + 5.0f, 24.0f, 24.0f});
        DrawRectangleRounded(Rectangle{slot.x + 4.0f, slot.y + slot.height - 6.0f, slot.width - 8.0f, 3.0f}, 0.4f, 4, active ? color : Color{100, 84, 64, 210});
        if (CheckCollisionPointRec(GetMousePosition(), slot))
            Tooltip::Draw(rt2s(type), {
                active ? (showingConsumption ? "Consumed in selected window" : "Produced in selected window")
                       : (showingConsumption ? "No consumption in selected window" : "No production in selected window"),
                selected ? "Visible on graph" : "Hidden from graph"
            }, 250.0f);
        shown++;
    }
    if (visibleResources.empty())
        UiText::DrawFit(showingConsumption ? "No consumption in selected window" : "No production in selected window", Rectangle{plot.x + 20.0f, plot.y + plot.height * 0.45f, plot.width - 40.0f, 24.0f}, 22, UiTheme::ParchmentDim);
}

// Handles clicks on the stats panel controls.
bool StatsPanelWidget::HandleClick(Vec2i point)
{
    Vector2 mouse{static_cast<float>(point.x), static_cast<float>(point.y)};
    Rectangle modeToggle = GetFlowModeToggleRect();
    if (CheckCollisionPointRec(mouse, modeToggle))
    {
        selectedFlowMode = point.x < modeToggle.x + modeToggle.width * 0.5f ? 0 : 1;
        return true;
    }
    Rectangle spin = GetWindowSpinnerRect();
    if (CheckCollisionPointRec(mouse, spin))
    {
        if (point.x < spin.x + spin.width * 0.5f)
            selectedWindowIndex = (selectedWindowIndex + 2) % 3;
        else
            selectedWindowIndex = (selectedWindowIndex + 1) % 3;
        return true;
    }

    Rectangle chart = GetChartRect();
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

// ─── StatsGuiSystem ──────────────────────────────────────────────────────────

// Initializes the full-screen statistics interaction mode.
StatsGuiSystem::StatsGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    // A4 (docs/work_plan_2026-07-13.md): shadows GuiSystem::scene (Scene*).
    scene = dynamic_cast<GameScene*>(owner->scene);

    WireCommonSystemActions(*this, cameraMovement);

    statsPanel.scene = scene;
    statsPanel.ChangePositionAnchor({0.06f, 0.10f});
    statsPanel.ChangeSizeAnchor({0.88f, 0.82f});
    statsPanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    SetupStrategicHud(strategicHudWidget, scene);
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
    SwitchToMapViewAndOpenHeadquarters(owner);
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

void StatsGuiSystem::TechPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("tech");
}

void StatsGuiSystem::RosterPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("roster");
}

// Handles clicks on the statistics overlay.
void StatsGuiSystem::LmbPressed()
{
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

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
