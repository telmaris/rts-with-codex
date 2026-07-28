#include "BonusCalculator.h"

#include "TreeModel.h"
#include "TreeSerializer.h"

#include "economy/BalanceStatDisplay.h"
#include "data/Resource.h"
#include "ui/UiText.h"
#include "ui/UiTheme.h"

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
        return RtsDataNames::NameOf(stat);
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
            group.additive += modifier.additive;
            group.multiplier *= modifier.multiplier;
            group.sourceCount++;
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

    std::sort(result.begin(), result.end(), [](const BonusGroup& a, const BonusGroup& b)
    {
        return a.label < b.label;
    });
    return result;
}

void BonusCalculatorPanel::Draw(Rectangle bounds, const TreeDocument& document)
{
    DrawRectangleRounded(bounds, 0.02f, 8, Color{30, 22, 16, 250});
    DrawRectangleRoundedLines(bounds, 0.02f, 8, 1.0f, UiTheme::Bronze);
    Rectangle title{bounds.x, bounds.y, bounds.width, 40.0f};
    DrawRectangleRounded(title, 0.05f, 8, UiTheme::Oak);
    UiText::DrawFit("Bonus Total", title, 17, UiTheme::Parchment);

    auto groups = AggregateTakenBonuses(document);

    int takenCount = 0;
    for (const auto& definition : document.GetDefinitions())
        if (document.IsTaken(definition.id))
            takenCount++;

    std::string summary = std::to_string(takenCount) + (takenCount == 1 ? " node selected, " : " nodes selected, ") +
                          std::to_string(groups.size()) + (groups.size() == 1 ? " stat group" : " stat groups");
    UiText::Draw(summary, bounds.x + 16.0f, bounds.y + 46.0f, 12, UiTheme::ParchmentDim);

    Rectangle content{bounds.x + 16.0f, bounds.y + 66.0f, bounds.width - 32.0f, bounds.height - 78.0f};

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
        UiText::Draw("Left click nodes in the tree to add them.", content.x + 2.0f, y + 4.0f, 13, UiTheme::ParchmentFaint);
        y += 26.0f;
    }

    for (const auto& group : groups)
    {
        Rectangle row{content.x, y, content.width, 38.0f};
        DrawRectangleRounded(row, 0.08f, 6, Color{38, 28, 20, 210});

        UiText::Draw(FitWidth(group.label, row.width - 42.0f, 13), row.x + 8.0f, row.y + 4.0f, 13, UiTheme::Parchment);

        std::string effect;
        if (std::abs(group.additive) > 0.001)
            effect += (group.additive > 0.0 ? "+" : "") + FormatNumber(group.additive);
        if (std::abs(group.multiplier - 1.0) > 0.001)
        {
            if (!effect.empty())
                effect += "  ";
            double percent = (group.multiplier - 1.0) * 100.0;
            effect += "x" + FormatNumber(group.multiplier, 4) +
                      " (" + (percent > 0.0 ? "+" : "") + FormatNumber(percent, 1) + "%)";
        }
        if (effect.empty())
            effect = "no numeric effect";

        UiText::Draw(effect, row.x + 8.0f, row.y + 20.0f, 14,
                     group.positive ? UiTheme::SageBright : UiTheme::RustBright);

        std::string sources = std::to_string(group.sourceCount) + "x";
        UiText::Draw(sources, row.x + row.width - UiText::Measure(sources, 12) - 8.0f, row.y + 21.0f, 12,
                     UiTheme::ParchmentFaint);
        y += 42.0f;
    }

    // The exact formula the game uses, spelled out so the numbers above are
    // interpretable without opening BalanceModifiers.h.
    if (!groups.empty())
    {
        y += 4.0f;
        UiText::Draw("applied as: (base + additive) * multiplier", content.x + 2.0f, y, 12, UiTheme::ParchmentFaint);
        y += 22.0f;
    }

    EndScissorMode();

    maxScroll = std::max(0.0f, (y + scroll) - (content.y + content.height));
    scroll = std::clamp(scroll, 0.0f, maxScroll);
}
