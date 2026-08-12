#include "Inspector.h"

#include "EditorTheme.h"
#include "TreeModel.h"
#include "TreeSerializer.h"

#include "ui/UiText.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace
{
    constexpr float rowH = 30.0f;
    constexpr float gap = 10.0f;
    constexpr float labelH = 19.0f;
    // This is an authoring panel, not a replica of the game HUD. Give labels
    // and values enough size to scan comfortably at normal desktop distance.
    constexpr int labelFont = 14;
    constexpr int valueFont = 16;
    constexpr int headerFont = 17;

    // "(none)" is offered wherever a modifier filter is optional, so a filter
    // can be cleared without deleting and re-adding the whole modifier.
    const std::string noneOption = "(none)";

    std::vector<std::string> WithNone(const std::vector<std::string>& values)
    {
        std::vector<std::string> options{noneOption};
        options.insert(options.end(), values.begin(), values.end());
        return options;
    }

    std::string Number(double value)
    {
        std::ostringstream stream;
        stream << std::defaultfloat << value;
        return stream.str();
    }

    double ToDouble(const std::string& text, double fallback)
    {
        try
        {
            size_t consumed = 0;
            double parsed = std::stod(text, &consumed);
            return consumed == 0 ? fallback : parsed;
        }
        catch (...)
        {
            return fallback;
        }
    }

    int ToInt(const std::string& text, int fallback)
    {
        try
        {
            size_t consumed = 0;
            int parsed = std::stoi(text, &consumed);
            return consumed == 0 ? fallback : parsed;
        }
        catch (...)
        {
            return fallback;
        }
    }

    // Match the game's tooltip vocabulary: these time-based stats are shown
    // as a speed/rate, while their serialized multiplier scales duration.
    bool UsesRateDisplay(BalanceStat stat)
    {
        return stat == BalanceStat::BuildTime ||
               stat == BalanceStat::ProductionCycleTime ||
               stat == BalanceStat::TransportTime;
    }

    double EffectPercent(const BalanceModifier& modifier)
    {
        if (UsesRateDisplay(modifier.stat) && modifier.multiplier > 0.0)
            return (1.0 / modifier.multiplier - 1.0) * 100.0;
        return (modifier.multiplier - 1.0) * 100.0;
    }

    double MultiplierFromEffectPercent(BalanceStat stat, double effectPercent)
    {
        if (UsesRateDisplay(stat))
        {
            // A -100% rate would divide by zero. Keep the persisted value
            // valid while still allowing an extreme slowdown to be authored.
            return 1.0 / std::max(0.01, 1.0 + effectPercent / 100.0);
        }
        return std::max(0.0, 1.0 + effectPercent / 100.0);
    }

    void DrawLabel(const std::string& text, float x, float y)
    {
        UiText::Draw(text, x, y, labelFont, EditorTheme::TextMuted);
    }

    void DrawSectionHeader(const std::string& text, Rectangle bounds, float& y)
    {
        y += 10.0f;
        DrawLineEx({bounds.x, y}, {bounds.x + bounds.width, y}, 1.0f, EditorTheme::Divider);
        y += 8.0f;
        UiText::Draw(text, bounds.x, y, headerFont, EditorTheme::Accent);
        y += 25.0f;
    }

    // Small square button used for +/x controls next to list rows.
    bool DrawMiniButton(Rectangle rect, const std::string& label, Color accent)
    {
        Vector2 mouse = GetMousePosition();
        bool hover = CheckCollisionPointRec(mouse, rect);
        DrawRectangleRounded(rect, 0.25f, 6, hover ? EditorTheme::SurfaceHover : EditorTheme::Surface);
        DrawRectangleRoundedLines(rect, 0.25f, 6, 1.0f, hover ? accent : EditorTheme::Border);
        UiText::DrawFit(label, rect, valueFont, hover ? accent : EditorTheme::TextMuted);
        return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    }
}

bool Inspector::IsEditing()
{
    return TextFieldWidget::IsAnyFocused();
}

void Inspector::SyncBuffers(TreeDocument& document, const std::string& id)
{
    boundId = id;
    const auto* definition = document.Find(id);
    if (definition == nullptr)
        return;

    for (auto* field : {&idField, &nameField, &descriptionField, &laneField,
                        &researchTimeField, &layerField, &orderField, &newPrerequisiteField})
        field->fontSize = valueFont;

    idField.text = definition->id;
    nameField.text = definition->name;
    descriptionField.text = definition->description;
    laneField.text = definition->layoutLane;
    researchTimeField.text = Number(definition->researchTime);
    researchTimeField.numericOnly = true;

    // layout_order is split into the two numbers it actually encodes, because
    // "2300" is not a value anyone reasons about — "layer 2, order 300" is.
    int layoutOrder = definition->layoutOrder;
    bool hasOrder = layoutOrder != std::numeric_limits<int>::max();
    int layer = hasOrder && layoutOrder >= 1000 ? layoutOrder / 1000 : 1;
    int order = hasOrder ? ((layoutOrder % 1000) + 1000) % 1000 : 500;
    layerField.text = std::to_string(layer);
    layerField.numericOnly = true;
    orderField.text = std::to_string(order);
    orderField.numericOnly = true;

    categoryDropdown.fontSize = valueFont;
    categoryDropdown.SetOptions(RtsDataNames::Categories());
    categoryDropdown.SelectByText(definition->category);

    tagDropdowns.assign(definition->tags.size(), DropdownWidget{});
    for (size_t i = 0; i < definition->tags.size(); i++)
    {
        tagDropdowns[i].fontSize = valueFont;
        tagDropdowns[i].SetOptions(RtsDataNames::Tags());
        tagDropdowns[i].SelectByText(definition->tags[i]);
    }

    costRows.assign(definition->costs.size(), CostRow{});
    for (size_t i = 0; i < definition->costs.size(); i++)
    {
        costRows[i].resource.fontSize = valueFont;
        costRows[i].amount.fontSize = valueFont;
        costRows[i].resource.SetOptions(RtsDataNames::ResourceTypes());
        costRows[i].resource.SelectByText(RtsDataNames::NameOf(definition->costs[i].type));
        costRows[i].amount.text = std::to_string(definition->costs[i].amount);
        costRows[i].amount.numericOnly = true;
    }

    buildingUnlockDropdowns.clear();
    const auto unlockedIds = document.GetUnlockedBuildingIds(definition->id);
    const auto options = document.GetBuildingUnlockOptions(definition->id);
    buildingUnlockDropdowns.assign(unlockedIds.size(), DropdownWidget{});
    for (size_t i = 0; i < unlockedIds.size(); i++)
    {
        buildingUnlockDropdowns[i].fontSize = valueFont;
        buildingUnlockDropdowns[i].SetOptions(options);
        buildingUnlockDropdowns[i].SelectByText(document.GetBuildingUnlockLabel(unlockedIds[i]));
    }

    modifierRows.assign(definition->modifiers.size(), ModifierRow{});
    for (size_t i = 0; i < definition->modifiers.size(); i++)
    {
        const auto& modifier = definition->modifiers[i];
        auto& row = modifierRows[i];
        for (auto* d : {&row.stat, &row.building, &row.resource, &row.category})
            d->fontSize = valueFont;
        for (auto* f : {&row.unit, &row.additive, &row.multiplier})
            f->fontSize = valueFont;
        row.stat.SetOptions(RtsDataNames::BalanceStats());
        row.stat.SelectByText(RtsDataNames::NameOf(modifier.stat));
        row.building.SetOptions(WithNone(RtsDataNames::BuildingTypes()));
        row.building.SelectByText(modifier.buildingType.has_value()
            ? RtsDataNames::NameOf(modifier.buildingType.value()) : noneOption);
        row.resource.SetOptions(WithNone(RtsDataNames::ResourceTypes()));
        row.resource.SelectByText(modifier.resourceType.has_value()
            ? RtsDataNames::NameOf(modifier.resourceType.value()) : noneOption);
        row.category.SetOptions(WithNone(RtsDataNames::ResourceCategories()));
        row.category.SelectByText(modifier.resourceCategory.has_value()
            ? RtsDataNames::NameOf(modifier.resourceCategory.value()) : noneOption);
        row.unit.text = modifier.unitDefId.value_or("");
        row.additive.text = Number(modifier.additive);
        row.additive.numericOnly = true;
        row.multiplier.text = Number(EffectPercent(modifier));
        row.multiplier.numericOnly = true;
    }
}

std::string Inspector::Draw(Rectangle bounds, TreeDocument& document, const std::string& selectedId)
{
    DrawRectangleRounded(bounds, 0.02f, 8, EditorTheme::Panel);
    DrawRectangleRoundedLines(bounds, 0.02f, 8, 1.0f, EditorTheme::Border);
    Rectangle title{bounds.x, bounds.y, bounds.width, 44.0f};
    DrawRectangleRounded(title, 0.05f, 8, EditorTheme::PanelHeader);
    UiText::DrawFit("Node Inspector", title, 19, EditorTheme::Text);

    if (selectedId.empty() || document.Find(selectedId) == nullptr)
    {
        UiText::DrawFit("Select a node (left click).",
            Rectangle{bounds.x + 26.0f, bounds.y + 66.0f, bounds.width - 52.0f, 24.0f}, valueFont, EditorTheme::TextFaint);
        UiText::DrawFit("Right click a node adds a child.",
            Rectangle{bounds.x + 26.0f, bounds.y + 92.0f, bounds.width - 52.0f, 24.0f}, valueFont, EditorTheme::TextFaint);
        boundId.clear();
        return selectedId;
    }

    if (boundId != selectedId)
        SyncBuffers(document, selectedId);

    // Dragging a node rewrites layout_order behind the inspector's back, so
    // refresh those two fields when the model and the buffers disagree — unless
    // the user is typing in them, in which case the buffer is the newer truth.
    if (const auto* current = document.Find(selectedId))
    {
        bool editingPosition = layerField.IsFocused() || orderField.IsFocused();
        int buffered = std::max(1, ToInt(layerField.text, 1)) * 1000 +
                       std::clamp(ToInt(orderField.text, 500), 0, 999);
        if (!editingPosition && current->layoutOrder != std::numeric_limits<int>::max() &&
            current->layoutOrder != buffered)
        {
            int layer = current->layoutOrder >= 1000 ? current->layoutOrder / 1000 : 1;
            layerField.text = std::to_string(layer);
            orderField.text = std::to_string(((current->layoutOrder % 1000) + 1000) % 1000);
        }
        if (!laneField.IsFocused() && laneField.text != current->layoutLane)
            laneField.text = current->layoutLane;
    }

    std::string nextSelection = selectedId;
    // Generous insets keep the form from visually merging into the panel edge;
    // the right gutter also reserves room for the scrollbar.
    Rectangle content{bounds.x + 26.0f, bounds.y + 54.0f, bounds.width - 64.0f, bounds.height - 70.0f};

    Vector2 mouse = GetMousePosition();
    if (CheckCollisionPointRec(mouse, content) && !DropdownWidget::IsAnyOpen())
    {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
            scroll = std::clamp(scroll - wheel * 40.0f, 0.0f, maxScroll);
    }

    BeginScissorMode(static_cast<int>(content.x), static_cast<int>(content.y),
                     static_cast<int>(content.width), static_cast<int>(content.height));

    float y = content.y - scroll;
    float fieldX = content.x;
    float fieldW = content.width;
    float half = (fieldW - gap) * 0.5f;
    float third = (fieldW - gap * 2.0f) / 3.0f;

    auto* definition = document.Find(selectedId);

    // --- identity ------------------------------------------------------------
    DrawLabel("Node ID", fieldX, y);
    y += labelH;
    if (idField.Draw({fieldX, y, fieldW, rowH}, "node_id"))
    {
        // Renaming is applied only once it is unique and non-empty; typing
        // through an intermediate collision must not silently drop characters.
        if (document.RenameNode(selectedId, idField.text))
        {
            nextSelection = idField.text;
            boundId = idField.text;
        }
    }
    y += rowH + gap;

    DrawLabel("Display name", fieldX, y);
    y += labelH;
    if (nameField.Draw({fieldX, y, fieldW, rowH}, "Display name"))
    {
        definition->name = nameField.text;
        document.MarkDirty();
    }
    y += rowH + gap;

    DrawLabel("Description", fieldX, y);
    y += labelH;
    if (descriptionField.Draw({fieldX, y, fieldW, rowH}, "Tooltip text"))
    {
        definition->description = descriptionField.text;
        document.MarkDirty();
    }
    y += rowH + gap;

    // --- layout --------------------------------------------------------------
    DrawSectionHeader("Layout", content, y);

    DrawLabel("Category", fieldX, y);
    DrawLabel("Layout lane", fieldX + half + gap, y);
    y += labelH;
    categoryDropdown.Draw({fieldX, y, half, rowH}, "SCIENCE");
    if (categoryDropdown.ConsumeChanged())
    {
        definition->category = categoryDropdown.SelectedText();
        document.MarkDirty();
    }
    if (laneField.Draw({fieldX + half + gap, y, half, rowH}, "(uses category)"))
    {
        definition->layoutLane = laneField.text;
        document.MarkDirty();
    }
    y += rowH + gap;

    DrawLabel("Layer", fieldX, y);
    DrawLabel("Order (0–999)", fieldX + third + gap, y);
    DrawLabel("Research time", fieldX + (third + gap) * 2.0f, y);
    y += labelH;
    bool layerChanged = layerField.Draw({fieldX, y, third, rowH}, "1");
    bool orderChanged = orderField.Draw({fieldX + third + gap, y, third, rowH}, "500");
    if (layerChanged || orderChanged)
    {
        int layer = std::max(1, ToInt(layerField.text, 1));
        int order = std::clamp(ToInt(orderField.text, 500), 0, 999);
        definition->layoutOrder = layer * 1000 + order;
        document.MarkDirty();
    }
    if (researchTimeField.Draw({fieldX + (third + gap) * 2.0f, y, third, rowH}, "60"))
    {
        definition->researchTime = std::max(0.0, ToDouble(researchTimeField.text, definition->researchTime));
        document.MarkDirty();
    }
    y += rowH + gap;
    // Computed from the fields, not from definition->layoutOrder: a stored 500
    // and a stored 1500 mean the same layer/order pair (preferredDepth divides
    // by 1000 and subtracts one), so echoing the raw stored value here would
    // look like the numbers above disagree with the file.
    int shownLayoutOrder = std::max(1, ToInt(layerField.text, 1)) * 1000 +
                           std::clamp(ToInt(orderField.text, 500), 0, 999);
    UiText::Draw("layout_order = layer*1000 + order = " + std::to_string(shownLayoutOrder),
                 fieldX, y, labelFont, EditorTheme::TextFaint);
    y += 24.0f;

    // --- tags ----------------------------------------------------------------
    // Explicit tags are additive only: on load InferTags() filters them through
    // the allow-list and then adds more derived from the category, costs and
    // modifiers. So removing one here does not necessarily remove it in-game.
    DrawSectionHeader("Tags (filter bar)", content, y);
    for (size_t i = 0; i < tagDropdowns.size() && i < definition->tags.size(); i++)
    {
        tagDropdowns[i].Draw({fieldX, y, half, rowH}, "tag");
        if (tagDropdowns[i].ConsumeChanged())
        {
            definition->tags[i] = tagDropdowns[i].SelectedText();
            document.MarkDirty();
        }
        if (DrawMiniButton({fieldX + half + gap, y, rowH, rowH}, "x", EditorTheme::Negative))
        {
            definition->tags.erase(definition->tags.begin() + i);
            document.MarkDirty();
            SyncBuffers(document, selectedId);
            break;
        }
        y += rowH + 4.0f;
    }
    if (DrawMiniButton({fieldX, y, 132.0f, rowH}, "+ Add tag", EditorTheme::Positive))
    {
        definition->tags.push_back(RtsDataNames::Tags().front());
        document.MarkDirty();
        SyncBuffers(document, selectedId);
    }
    y += rowH + gap;

    // --- prerequisites -------------------------------------------------------
    DrawSectionHeader("Prerequisites (requires)", content, y);
    for (size_t i = 0; i < definition->prerequisites.size(); i++)
    {
        Rectangle row{fieldX, y, fieldW - rowH - gap, rowH};
        bool missing = document.Find(definition->prerequisites[i]) == nullptr;
        DrawRectangleRounded(row, 0.18f, 6, EditorTheme::Surface);
        DrawRectangleRoundedLines(row, 0.18f, 6, 1.0f, missing ? EditorTheme::Negative : EditorTheme::Border);
        UiText::Draw(definition->prerequisites[i] + (missing ? "  (missing!)" : ""),
                     row.x + 10.0f, row.y + 6.0f, valueFont, missing ? EditorTheme::Negative : EditorTheme::Text);
        if (DrawMiniButton({fieldX + fieldW - rowH, y, rowH, rowH}, "x", EditorTheme::Negative))
        {
            definition->prerequisites.erase(definition->prerequisites.begin() + i);
            document.MarkDirty();
            break;
        }
        y += rowH + 4.0f;
    }
    if (newPrerequisiteField.Draw({fieldX, y, fieldW - rowH - gap, rowH}, "add prerequisite id + Enter"))
    {
    }
    if (DrawMiniButton({fieldX + fieldW - rowH, y, rowH, rowH}, "+", EditorTheme::Positive) ||
        (newPrerequisiteField.IsFocused() && IsKeyPressed(KEY_ENTER)))
    {
        std::string candidate = newPrerequisiteField.text;
        bool alreadyThere = std::find(definition->prerequisites.begin(), definition->prerequisites.end(), candidate)
                            != definition->prerequisites.end();
        if (!candidate.empty() && candidate != definition->id && !alreadyThere)
        {
            definition->prerequisites.push_back(candidate);
            newPrerequisiteField.text.clear();
            document.MarkDirty();
        }
    }
    y += rowH + gap;

    // --- costs ---------------------------------------------------------------
    DrawSectionHeader("Costs", content, y);
    for (size_t i = 0; i < costRows.size() && i < definition->costs.size(); i++)
    {
        costRows[i].resource.Draw({fieldX, y, half, rowH}, "resource");
        if (costRows[i].resource.ConsumeChanged())
        {
            definition->costs[i].type = RtsDataNames::ToResourceType(costRows[i].resource.SelectedText());
            document.MarkDirty();
        }
        if (costRows[i].amount.Draw({fieldX + half + gap, y, half - rowH - gap, rowH}, "amount"))
        {
            definition->costs[i].amount = std::max(0, ToInt(costRows[i].amount.text, definition->costs[i].amount));
            document.MarkDirty();
        }
        if (DrawMiniButton({fieldX + fieldW - rowH, y, rowH, rowH}, "x", EditorTheme::Negative))
        {
            definition->costs.erase(definition->costs.begin() + i);
            document.MarkDirty();
            SyncBuffers(document, selectedId);
            break;
        }
        y += rowH + 4.0f;
    }
    if (DrawMiniButton({fieldX, y, 132.0f, rowH}, "+ Add cost", EditorTheme::Positive))
    {
        definition->costs.push_back({RtsDataNames::ToResourceType("PAPER"), 10});
        document.MarkDirty();
        SyncBuffers(document, selectedId);
    }
    y += rowH + gap;

    // --- building unlocks ---------------------------------------------------
    // Construction unlocks are real `requires_tech` / `requires_focus`
    // relations in buildings.rtsdata, deliberately separate from
    // BalanceModifier. This makes the editor's selection, the game build gate
    // and the tooltip use the same source of truth for both trees.
    {
        DrawSectionHeader("Building unlocks", content, y);
        auto unlockedIds = document.GetUnlockedBuildingIds(definition->id);
        const auto options = document.GetBuildingUnlockOptions(definition->id);
        for (size_t i = 0; i < buildingUnlockDropdowns.size() && i < unlockedIds.size(); i++)
        {
            buildingUnlockDropdowns[i].Draw({fieldX, y, fieldW - rowH - gap, rowH}, "Building");
            if (buildingUnlockDropdowns[i].ConsumeChanged())
            {
                std::vector<std::string> replacement = unlockedIds;
                replacement[i] = document.GetBuildingUnlockIdForLabel(buildingUnlockDropdowns[i].SelectedText());
                replacement.erase(std::remove(replacement.begin(), replacement.end(), std::string()), replacement.end());
                document.SetBuildingUnlocks(definition->id, replacement);
                SyncBuffers(document, selectedId);
                break;
            }
            if (DrawMiniButton({fieldX + fieldW - rowH, y, rowH, rowH}, "x", EditorTheme::Negative))
            {
                unlockedIds.erase(unlockedIds.begin() + i);
                document.SetBuildingUnlocks(definition->id, unlockedIds);
                SyncBuffers(document, selectedId);
                break;
            }
            y += rowH + 4.0f;
        }

        std::vector<std::string> availableIds;
        for (const auto& label : options)
        {
            const std::string id = document.GetBuildingUnlockIdForLabel(label);
            if (!id.empty() && std::find(unlockedIds.begin(), unlockedIds.end(), id) == unlockedIds.end())
                availableIds.push_back(id);
        }
        if (!availableIds.empty())
        {
            if (DrawMiniButton({fieldX, y, 176.0f, rowH}, "+ Add building unlock", EditorTheme::Positive))
            {
                unlockedIds.push_back(availableIds.front());
                document.SetBuildingUnlocks(definition->id, unlockedIds);
                SyncBuffers(document, selectedId);
            }
            y += rowH + gap;
        }
        else
        {
            UiText::Draw("All currently un-gated buildings are assigned.", fieldX, y + 4.0f, labelFont, EditorTheme::TextFaint);
            y += rowH + gap;
        }
    }

    // --- modifiers -----------------------------------------------------------
    DrawSectionHeader("Modifiers", content, y);
    for (size_t i = 0; i < modifierRows.size() && i < definition->modifiers.size(); i++)
    {
        auto& row = modifierRows[i];
        auto& modifier = definition->modifiers[i];

        DrawRectangleRounded({fieldX - 6.0f, y - 6.0f, fieldW + 12.0f, rowH * 4.0f + labelH + 53.0f}, 0.05f, 6,
                             EditorTheme::Surface);

        row.stat.Draw({fieldX, y, fieldW - rowH - gap, rowH}, "stat");
        if (row.stat.ConsumeChanged())
        {
            modifier.stat = RtsDataNames::ToBalanceStat(row.stat.SelectedText());
            row.multiplier.text = Number(EffectPercent(modifier));
            document.MarkDirty();
        }
        if (DrawMiniButton({fieldX + fieldW - rowH, y, rowH, rowH}, "x", EditorTheme::Negative))
        {
            definition->modifiers.erase(definition->modifiers.begin() + i);
            document.MarkDirty();
            SyncBuffers(document, selectedId);
            break;
        }
        y += rowH + 4.0f;

        DrawLabel("Flat value", fieldX, y);
        DrawLabel("Effect (%)", fieldX + half + gap, y);
        y += labelH;
        if (row.additive.Draw({fieldX, y, half, rowH}, "0"))
        {
            modifier.additive = ToDouble(row.additive.text, modifier.additive);
            document.MarkDirty();
        }
        if (row.multiplier.Draw({fieldX + half + gap, y, half, rowH}, "0"))
        {
            modifier.multiplier = MultiplierFromEffectPercent(
                modifier.stat, ToDouble(row.multiplier.text, EffectPercent(modifier)));
            document.MarkDirty();
        }
        y += rowH + 4.0f;

        row.building.Draw({fieldX, y, third, rowH}, "building");
        if (row.building.ConsumeChanged())
        {
            const std::string& value = row.building.SelectedText();
            if (value == noneOption)
                modifier.buildingType.reset();
            else
                modifier.buildingType = RtsDataNames::ToBuildingType(value);
            document.MarkDirty();
        }
        row.resource.Draw({fieldX + third + gap, y, third, rowH}, "resource");
        if (row.resource.ConsumeChanged())
        {
            const std::string& value = row.resource.SelectedText();
            if (value == noneOption)
                modifier.resourceType.reset();
            else
                modifier.resourceType = RtsDataNames::ToResourceType(value);
            document.MarkDirty();
        }
        row.category.Draw({fieldX + (third + gap) * 2.0f, y, third, rowH}, "category");
        if (row.category.ConsumeChanged())
        {
            const std::string& value = row.category.SelectedText();
            if (value == noneOption)
                modifier.resourceCategory.reset();
            else
                modifier.resourceCategory = RtsDataNames::ToResourceCategory(value);
            document.MarkDirty();
        }
        y += rowH + 4.0f;

        if (row.unit.Draw({fieldX, y, fieldW, rowH}, "unit id filter (optional)"))
        {
            if (row.unit.text.empty())
                modifier.unitDefId.reset();
            else
                modifier.unitDefId = row.unit.text;
            document.MarkDirty();
        }
        y += rowH + 4.0f;

        UiText::Draw(SerializeModifier(modifier), fieldX, y, labelFont, EditorTheme::TextFaint);
        y += 25.0f;
    }
    if (DrawMiniButton({fieldX, y, 156.0f, rowH}, "+ Add modifier", EditorTheme::Positive))
    {
        BalanceModifier modifier;
        modifier.stat = BalanceStat::ProductionCycleTime;
        modifier.source = "tech:" + definition->id;
        definition->modifiers.push_back(modifier);
        document.MarkDirty();
        SyncBuffers(document, selectedId);
    }
    y += rowH + gap;

    // --- danger zone ---------------------------------------------------------
    DrawSectionHeader("", content, y);
    if (DrawMiniButton({fieldX, y, 160.0f, rowH}, "Delete node", EditorTheme::Negative))
    {
        document.DeleteNode(selectedId);
        nextSelection.clear();
        boundId.clear();
    }
    y += rowH + 12.0f;

    EndScissorMode();

    maxScroll = std::max(0.0f, (y + scroll) - (content.y + content.height));
    scroll = std::clamp(scroll, 0.0f, maxScroll);

    // Without this the content just stops at the panel edge and reads as a
    // rendering bug rather than "there is more below".
    if (maxScroll > 0.0f)
    {
        Rectangle track{content.x + content.width + 8.0f, content.y, 5.0f, content.height};
        DrawRectangleRounded(track, 0.5f, 4, EditorTheme::Canvas);
        float thumbH = std::max(28.0f, track.height * (track.height / (track.height + maxScroll)));
        float thumbY = track.y + (track.height - thumbH) * (scroll / maxScroll);
        DrawRectangleRounded({track.x, thumbY, track.width, thumbH}, 0.5f, 4, EditorTheme::Accent);
    }

    return nextSelection;
}
