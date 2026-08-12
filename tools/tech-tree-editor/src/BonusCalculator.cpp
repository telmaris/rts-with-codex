#include "BonusCalculator.h"

#include "EditorTheme.h"
#include "TreeModel.h"
#include "TreeSerializer.h"

#include "economy/BalanceStatDisplay.h"
#include "data/Resource.h"
#include "ui/UiText.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace
{

    // Full filter signature: two modifiers may only be aggregated when the game
    // would apply both to exactly the same set of contexts.
    struct GroupKey
    {
        BalanceStat stat;
        std::string building;
        std::string resource;
        std::string category;
        std::string unit;

        bool operator<(const GroupKey& other) const
        {
            if (stat != other.stat) return stat < other.stat;
            if (building != other.building) return building < other.building;
            if (resource != other.resource) return resource < other.resource;
            if (category != other.category) return category < other.category;
            return unit < other.unit;
        }
    };

    std::string StatLabel(BalanceStat stat)
    {
        const bool isRate = stat == BalanceStat::BuildTime ||
                            stat == BalanceStat::ProductionCycleTime ||
                            stat == BalanceStat::TransportTime;
        return isRate ? ImprovedRateLabel(stat) : BalanceStatLabel(stat);
    }

    bool UsesRateDisplay(BalanceStat stat)
    {
        return stat == BalanceStat::BuildTime ||
               stat == BalanceStat::ProductionCycleTime ||
               stat == BalanceStat::TransportTime;
    }

    std::string BuildLabel(const GroupKey& key)
    {
        std::string label = StatLabel(key.stat);
        std::string filters;
        auto append = [&](const std::string& text)
        {
            filters += filters.empty() ? text : ", " + text;
        };
        if (!key.building.empty()) append(key.building);
        if (!key.resource.empty()) append(key.resource);
        if (!key.category.empty()) append(key.category + " category");
        if (!key.unit.empty()) append("unit " + key.unit);
        return filters.empty() ? label + "  (global)" : label + "  [" + filters + "]";
    }

    // Trims from the END (unlike the widgets' FitTail): the stat name leads the
    // label and matters more than the filter list that follows it.
    std::string FitWidth(const std::string& text, float maxWidth, int fontSize)
    {
        if (UiText::Measure(text, fontSize) <= maxWidth)
            return text;
        std::string trimmed = text;
        while (!trimmed.empty() && UiText::Measure(trimmed + "...", fontSize) > maxWidth)
            trimmed.pop_back();
        return trimmed + "...";
    }

    std::string FormatNumber(double value, int decimals = 2)
    {
        std::ostringstream stream;
        stream.setf(std::ios::fixed);
        stream.precision(decimals);
        stream << value;
        std::string text = stream.str();
        // Trim trailing zeros so 1.10 reads as 1.1 and 2.00 as 2.
        if (text.find('.') != std::string::npos)
        {
            while (!text.empty() && text.back() == '0')
                text.pop_back();
            if (!text.empty() && text.back() == '.')
                text.pop_back();
        }
        return text;
    }
}

std::vector<BonusGroup> AggregateTakenBonuses(const TreeDocument& document)
{
    std::map<GroupKey, BonusGroup> groups;
    std::map<std::string, BonusGroup> unlocks;

    for (const auto& definition : document.GetDefinitions())
    {
        if (!document.IsTaken(definition.id))
            continue;

        for (const auto& modifier : definition.modifiers)
        {
            GroupKey key;
            key.stat = modifier.stat;
            if (modifier.buildingType.has_value())
                key.building = RtsDataNames::NameOf(modifier.buildingType.value());
            if (modifier.resourceType.has_value())
                key.resource = RtsDataNames::NameOf(modifier.resourceType.value());
            if (modifier.resourceCategory.has_value())
                key.category = RtsDataNames::NameOf(modifier.resourceCategory.value());
            key.unit = modifier.unitDefId.value_or("");

            auto& group = groups[key];
            group.stat = key.stat;
            group.additive += modifier.additive;
            group.multiplier *= modifier.multiplier;
            group.sourceCount++;
        }

        for (const auto& building : document.GetUnlockedBuildings(definition.id))
        {
            auto& group = unlocks[building];
            group.kind = BonusGroupKind::BuildingUnlock;
            group.label = "Unlock " + building;
            group.sourceCount++;
            group.positive = true;
        }
    }

    std::vector<BonusGroup> result;
    result.reserve(groups.size());
    for (auto& [key, group] : groups)
    {
        group.label = BuildLabel(key);

        bool lowerIsBetter = LowerValueIsBetter(key.stat);
        if (std::abs(group.additive) > 0.001)
            group.positive = lowerIsBetter ? group.additive < 0.0 : group.additive > 0.0;
        else if (std::abs(group.multiplier - 1.0) > 0.001)
            group.positive = lowerIsBetter ? group.multiplier < 1.0 : group.multiplier > 1.0;
        else
            group.positive = true;

        result.push_back(group);
    }
    for (auto& [building, group] : unlocks)
        result.push_back(group);

    std::sort(result.begin(), result.end(), [](const BonusGroup& a, const BonusGroup& b)
    {
        return a.label < b.label;
    });
    return result;
}

void BonusCalculatorPanel::Draw(Rectangle bounds, const TreeDocument& document)
{
    DrawRectangleRounded(bounds, 0.02f, 8, EditorTheme::Panel);
    DrawRectangleRoundedLines(bounds, 0.02f, 8, 1.0f, EditorTheme::Border);
    Rectangle title{bounds.x, bounds.y, bounds.width, 44.0f};
    DrawRectangleRounded(title, 0.05f, 8, EditorTheme::PanelHeader);
    UiText::DrawFit("Bonus Total", title, 19, EditorTheme::Text);

    auto groups = AggregateTakenBonuses(document);

    int takenCount = 0;
    for (const auto& definition : document.GetDefinitions())
        if (document.IsTaken(definition.id))
            takenCount++;

    std::string summary = std::to_string(takenCount) + (takenCount == 1 ? " node selected, " : " nodes selected, ") +
                          std::to_string(groups.size()) + (groups.size() == 1 ? " effect" : " effects");
    UiText::Draw(summary, bounds.x + 24.0f, bounds.y + 52.0f, 14, EditorTheme::TextMuted);

    Rectangle content{bounds.x + 24.0f, bounds.y + 76.0f, bounds.width - 48.0f, bounds.height - 90.0f};

    Vector2 mouse = GetMousePosition();
    if (CheckCollisionPointRec(mouse, content))
    {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
            scroll = std::clamp(scroll - wheel * 40.0f, 0.0f, maxScroll);
    }

    BeginScissorMode(static_cast<int>(content.x), static_cast<int>(content.y),
                     static_cast<int>(content.width), static_cast<int>(content.height));

    float y = content.y - scroll;
    if (groups.empty())
    {
        UiText::Draw("Left click nodes in the tree to add them.", content.x + 2.0f, y + 4.0f, 15, EditorTheme::TextFaint);
        y += 30.0f;
    }

    for (const auto& group : groups)
    {
        Rectangle row{content.x, y, content.width, 46.0f};
        DrawRectangleRounded(row, 0.08f, 6, EditorTheme::Surface);

        UiText::Draw(FitWidth(group.label, row.width - 50.0f, 15), row.x + 10.0f, row.y + 6.0f, 15, EditorTheme::Text);

        std::string effect = group.kind == BonusGroupKind::BuildingUnlock ? "Building available" : "";
        if (group.kind == BonusGroupKind::Modifier && std::abs(group.additive) > 0.001)
            effect += (group.additive > 0.0 ? "+" : "") + FormatNumber(group.additive);
        if (group.kind == BonusGroupKind::Modifier && std::abs(group.multiplier - 1.0) > 0.001)
        {
            if (!effect.empty())
                effect += "  ";
            double percent = UsesRateDisplay(group.stat)
                ? (1.0 / group.multiplier - 1.0) * 100.0
                : (group.multiplier - 1.0) * 100.0;
            effect += (percent > 0.0 ? "+" : "") + FormatNumber(percent, 1) + "%";
        }
        if (group.kind == BonusGroupKind::Modifier && effect.empty())
            effect = "no numeric effect";

        UiText::Draw(effect, row.x + 10.0f, row.y + 25.0f, 16,
                     group.positive ? EditorTheme::Positive : EditorTheme::Negative);

        std::string sources = std::to_string(group.sourceCount) + "x";
        UiText::Draw(sources, row.x + row.width - UiText::Measure(sources, 13) - 10.0f, row.y + 27.0f, 13,
                     EditorTheme::TextFaint);
        y += 52.0f;
    }

    // Time-based stats are intentionally converted to speed/rate here, just
    // like the game tooltip and the inspector's percentage input.
    const bool hasModifiers = std::any_of(groups.begin(), groups.end(), [](const BonusGroup& group)
    {
        return group.kind == BonusGroupKind::Modifier;
    });
    if (hasModifiers)
    {
        y += 4.0f;
        UiText::Draw("Displayed in the same units as the tooltip.", content.x + 2.0f, y, 14, EditorTheme::TextFaint);
        y += 26.0f;
    }

    EndScissorMode();

    maxScroll = std::max(0.0f, (y + scroll) - (content.y + content.height));
    scroll = std::clamp(scroll, 0.0f, maxScroll);
}
