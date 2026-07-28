#include "Inspector.h"

#include "TreeModel.h"
#include "TreeSerializer.h"

#include "ui/UiText.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace
{
    constexpr float rowH = 24.0f;
    constexpr float gap = 6.0f;
    // Smaller than the game's chrome: this panel is a dense form, and it draws
    // with the Plain face (set by main.cpp) which stays legible well below the
    // display font's comfortable size.
    constexpr int labelFont = 12;
    constexpr int valueFont = 14;
    constexpr int headerFont = 14;

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

    void DrawLabel(const std::string& text, float x, float y)
    {
        UiText::Draw(text, x, y, labelFont, UiTheme::ParchmentDim);
    }

    void DrawSectionHeader(const std::string& text, Rectangle bounds, float& y)
    {
        y += 6.0f;
        DrawLineEx({bounds.x, y}, {bounds.x + bounds.width, y}, 1.0f, Fade(UiTheme::Bronze, 0.6f));
        y += 6.0f;
        UiText::Draw(text, bounds.x, y, headerFont, UiTheme::AmberBright);
        y += 21.0f;
    }

    // Small square button used for +/x controls next to list rows.
    bool DrawMiniButton(Rectangle rect, const std::string& label, Color accent)
    {
        Vector2 mouse = GetMousePosition();
        bool hover = CheckCollisionPointRec(mouse, rect);
        DrawRectangleRounded(rect, 0.25f, 6, hover ? UiTheme::Timber : UiTheme::Oak);
        DrawRectangleRoundedLines(rect, 0.25f, 6, 1.0f, hover ? accent : UiTheme::Bronze);
        UiText::DrawFit(label, rect, valueFont, hover ? accent : UiTheme::ParchmentDim);
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
        row.multiplier.text = Number(modifier.multiplier);
        row.multiplier.numericOnly = true;
    }
}

std::string Inspector::Draw(Rectangle bounds, TreeDocument& document, const std::string& selectedId)
{
    DrawRectangleRounded(bounds, 0.02f, 8, Color{30, 22, 16, 250});
    DrawRectangleRoundedLines(bounds, 0.02f, 8, 1.0f, UiTheme::Bronze);
    Rectangle title{bounds.x, bounds.y, bounds.width, 40.0f};
    DrawRectangleRounded(title, 0.05f, 8, UiTheme::Oak);
    UiText::DrawFit("Node Inspector", title, 17, UiTheme::Parchment);

    if (selectedId.empty() || document.Find(selectedId) == nullptr)
    {
        UiText::DrawFit("Select a node (left click).",
            Rectangle{bounds.x + 16.0f, bounds.y + 58.0f, bounds.width - 32.0f, 20.0f}, valueFont, UiTheme::ParchmentFaint);
        UiText::DrawFit("Right click a node adds a child.",
            Rectangle{bounds.x + 16.0f, bounds.y + 80.0f, bounds.width - 32.0f, 20.0f}, valueFont, UiTheme::ParchmentFaint);
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
    // Right inset leaves room for the scrollbar so no field runs under it.
    Rectangle content{bounds.x + 16.0f, bounds.y + 46.0f, bounds.width - 38.0f, bounds.height - 58.0f};

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
    DrawLabel("id", fieldX, y);
    y += 15.0f;
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

    DrawLabel("name", fieldX, y);
    y += 15.0f;
    if (nameField.Draw({fieldX, y, fieldW, rowH}, "Display name"))
    {
        definition->name = nameField.text;
        document.MarkDirty();
    }
    y += rowH + gap;

    DrawLabel("description", fieldX, y);
    y += 15.0f;
    if (descriptionField.Draw({fieldX, y, fieldW, rowH}, "Tooltip text"))
    {
        definition->description = descriptionField.text;
        document.MarkDirty();
    }
    y += rowH + gap;

    // --- layout --------------------------------------------------------------
    DrawSectionHeader("Layout", content, y);

    DrawLabel("category", fieldX, y);
    DrawLabel("layout_lane", fieldX + half + gap, y);
    y += 15.0f;
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

    DrawLabel("layer", fieldX, y);
    DrawLabel("order (0-999)", fieldX + third + gap, y);
    DrawLabel("research_time", fieldX + (third + gap) * 2.0f, y);
    y += 15.0f;
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
                 fieldX, y, labelFont, UiTheme::ParchmentFaint);
    y += 20.0f;

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
        if (DrawMiniButton({fieldX + half + gap, y, rowH, rowH}, "x", UiTheme::RustBright))
        {
            definition->tags.erase(definition->tags.begin() + i);
            document.MarkDirty();
            SyncBuffers(document, selectedId);
            break;
        }
        y += rowH + 4.0f;
    }
    if (DrawMiniButton({fieldX, y, 110.0f, rowH}, "+ add tag", UiTheme::SageBright))
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
        DrawRectangleRounded(row, 0.18f, 6, UiTheme::Bark);
        DrawRectangleRoundedLines(row, 0.18f, 6, 1.0f, missing ? UiTheme::Rust : UiTheme::Bronze);
        UiText::Draw(definition->prerequisites[i] + (missing ? "  (missing!)" : ""),
                     row.x + 8.0f, row.y + 5.0f, valueFont, missing ? UiTheme::RustBright : UiTheme::Parchment);
        if (DrawMiniButton({fieldX + fieldW - rowH, y, rowH, rowH}, "x", UiTheme::RustBright))
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
    if (DrawMiniButton({fieldX + fieldW - rowH, y, rowH, rowH}, "+", UiTheme::SageBright) ||
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
        if (DrawMiniButton({fieldX + fieldW - rowH, y, rowH, rowH}, "x", UiTheme::RustBright))
        {
            definition->costs.erase(definition->costs.begin() + i);
            document.MarkDirty();
            SyncBuffers(document, selectedId);
            break;
        }
        y += rowH + 4.0f;
    }
    if (DrawMiniButton({fieldX, y, 110.0f, rowH}, "+ add cost", UiTheme::SageBright))
    {
        definition->costs.push_back({RtsDataNames::ToResourceType("PAPER"), 10});
        document.MarkDirty();
        SyncBuffers(document, selectedId);
    }
    y += rowH + gap;

    // --- modifiers -----------------------------------------------------------
    DrawSectionHeader("Modifiers", content, y);
    for (size_t i = 0; i < modifierRows.size() && i < definition->modifiers.size(); i++)
    {
        auto& row = modifierRows[i];
        auto& modifier = definition->modifiers[i];

        DrawRectangleRounded({fieldX - 4.0f, y - 4.0f, fieldW + 8.0f, rowH * 3.0f + 20.0f}, 0.05f, 6,
                             Color{38, 28, 20, 200});

        row.stat.Draw({fieldX, y, fieldW - rowH - gap, rowH}, "stat");
        if (row.stat.ConsumeChanged())
        {
            modifier.stat = RtsDataNames::ToBalanceStat(row.stat.SelectedText());
            document.MarkDirty();
        }
        if (DrawMiniButton({fieldX + fieldW - rowH, y, rowH, rowH}, "x", UiTheme::RustBright))
        {
            definition->modifiers.erase(definition->modifiers.begin() + i);
            document.MarkDirty();
            SyncBuffers(document, selectedId);
            break;
        }
        y += rowH + 4.0f;

        if (row.additive.Draw({fieldX, y, half, rowH}, "additive"))
        {
            modifier.additive = ToDouble(row.additive.text, modifier.additive);
            document.MarkDirty();
        }
        if (row.multiplier.Draw({fieldX + half + gap, y, half, rowH}, "multiplier"))
        {
            modifier.multiplier = ToDouble(row.multiplier.text, modifier.multiplier);
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

        UiText::Draw(SerializeModifier(modifier), fieldX, y, labelFont, UiTheme::ParchmentFaint);
        y += 22.0f;
    }
    if (DrawMiniButton({fieldX, y, 130.0f, rowH}, "+ add modifier", UiTheme::SageBright))
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
    if (DrawMiniButton({fieldX, y, 150.0f, rowH}, "Delete node", UiTheme::RustBright))
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
        Rectangle track{content.x + content.width + 4.0f, content.y, 4.0f, content.height};
        DrawRectangleRounded(track, 0.5f, 4, Color{20, 15, 11, 190});
        float thumbH = std::max(28.0f, track.height * (track.height / (track.height + maxScroll)));
        float thumbY = track.y + (track.height - thumbH) * (scroll / maxScroll);
        DrawRectangleRounded({track.x, thumbY, track.width, thumbH}, 0.5f, 4, UiTheme::Bronze);
    }

    return nextSelection;
}
