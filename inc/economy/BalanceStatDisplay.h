#ifndef BALANCE_STAT_DISPLAY_H
#define BALANCE_STAT_DISPLAY_H

// Presentation rules for balance stats and modifiers: what a stat is called,
// which direction counts as an improvement, and whether a given modifier is a
// bonus or a penalty.
//
// Shared rather than copied because getting this wrong is silent and visible:
// if "which way is better" drifts between the game's research panel and
// tools/tech-tree-editor, a nerf renders green in one and red in the other.
// It already drifted once (WorkerCapacity, 2026-07-26) across three copies.
//
// Pure functions over BalanceStat / BalanceModifier — no Player, no GameScene —
// so the standalone tools can link this directly.

#include "economy/BalanceModifiers.h"
#include "economy/BalanceStats.h"

#include <string>

// Human-readable stat name, e.g. "Production cycle time".
const char* BalanceStatLabel(BalanceStat stat);

// True when a SMALLER number is the improvement. Build timers, costs and
// worker headcount all read this way: the design wants as few people tied up
// in buildings as possible, so raising WorkerCapacity is a nerf.
bool LowerValueIsBetter(BalanceStat stat);

// For stats where a shrinking duration is better, the natural phrasing flips to
// a rate: "Build time x0.9" reads as "Build speed +11%".
const char* ImprovedRateLabel(BalanceStat stat);

// Whether the modifier helps the player, accounting for LowerValueIsBetter.
// Drives the green/red split in tooltips and the bonus summary.
bool IsPositiveModifier(const BalanceModifier& modifier);

// Display name for a modifier's building filter.
const char* BalanceBuildingLabel(BuildingType type);

#endif
