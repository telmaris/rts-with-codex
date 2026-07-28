#ifndef BONUS_CALCULATOR_H
#define BONUS_CALCULATOR_H

// Path-of-Building style running total for the selected (taken) nodes.
//
// Modifiers are grouped by their full filter signature — stat plus building /
// resource / category / unit — not by stat alone. That distinction is the whole
// point: a global "+10% production output" and a "+10% production output for
// Woodcutter" never stack onto the same number in the game, so showing them
// added together would be a lie. BalanceModifier::AppliesTo is what decides at
// runtime; this mirrors its filter fields.
//
// Within a group the aggregation matches BalanceModifierSet::ModifyDouble:
// additives sum, multipliers multiply, and the result is (base + Σa) * Πm.

#include "research/Technology.h"

#include "raylib.h"

#include <string>
#include <vector>

class TreeDocument;

// One aggregated row.
struct BonusGroup
{
    std::string label;       // human-readable stat + filters
    double additive{0.0};    // Σ additive
    double multiplier{1.0};  // Π multiplier
    int sourceCount{0};      // how many nodes contributed
    bool positive{true};     // whether the net effect is an improvement
};

// Aggregates the modifiers of every taken node.
std::vector<BonusGroup> AggregateTakenBonuses(const TreeDocument& document);

class BonusCalculatorPanel
{
public:
    void Draw(Rectangle bounds, const TreeDocument& document);

private:
    float scroll{0.0f};
    float maxScroll{0.0f};
};

#endif
