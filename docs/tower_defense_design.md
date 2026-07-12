# Tower Defense Design — Final Architecture

This document is the settled architecture reference for the Factorio×Tower-Defense pivot
(`docs/tower_defense_rework_plan.md`, ETAP 0–9, commits `08c8962`..`19fa980` and the ETAP-9
data/balance/docs pass after it). It replaces the plan document as the place to look once a
system is built — the plan describes intent and open questions; this describes what actually
shipped and how to extend it. Historical implementation notes, bugs found and fixed, and
user decisions per stage are in memory (`project_tower_defense_pivot.md`) and `docs/tech_debt.md`.

## Game loop

1. **Economy** — unchanged from before the pivot: resource chains, road-based logistics,
   villages/manpower, tech/focus modifiers.
2. **Recruitment** — `Barracks` (any building with `RecruitmentComponent`) queues a unit from
   `assets/data/units.rtsdata` against `UnitDefinition::recruitBuilding`. Resources + manpower
   are deducted up front; the unit enters the player's roster (`Player::roster`,
   `UnitRoster`) after `recruitTime` (tech-modified, see "Balance stats" below).
3. **Deploy** — `GameCommand::DeployUnits(playerId, targetPlayerId, unitInstanceIds)` moves an
   ordered group of roster units onto the player's own end of the military road toward
   `targetPlayerId`. No rally points, no hold: units always march toward the enemy HQ.
4. **March & road combat** — `UnitMarchSystem` moves units single-file along the immutable
   military road; `UnitCombatSystem` resolves unit-vs-unit combat when two opposing columns
   meet (front unit fights front unit; the loser is removed, the next unit in line takes its
   place; ties broken by lowest `instanceId`).
5. **Siege** — a unit that reaches the enemy HQ gate enters `AttackingHq` and groups on the
   final tile with no cap; `HqCombatSystem` resolves damage against the HQ (with the HQ's own
   periodic "thorns" AoE retaliation) and eventual elimination.
6. **Towers** — `DefenseTower` (or any building with `TowerCombatComponent`) auto-targets the
   nearest-to-goal enemy unit in range and fires ammo-consuming projectiles resolved through
   the same combat pipeline as unit/HQ combat.
7. **Victory** — a defeated player's HQ hits 0 HP: their deployed units vanish, production
   buildings are transferred to the winner (`ConqueredEconomy` ramps productivity 30%→100%),
   storage is drained to ~20% for the winner, and the loser becomes a free-camera observer.

## Map & military roads

`MilitaryRoadNetwork` (`inc/simulation/MilitaryRoadNetwork.h`) is generated once at map
creation and is immutable afterward — no capacity, no player modification. Players are
arranged in a ring (sorted by HQ angle around the map center); each player connects to their
two ring neighbors. Tiles are flagged `Tile::isMilitaryRoad`, mutually exclusive with regular
roads and buildings (`TileMap::CanBuildFootprint`).

`PathingService::FindMilitaryPath` / `AreHqsConnected` are `static` (no instance state) and
support multi-hop BFS through *eliminated* players' ring segments — so conquering a player's
HQ opens a transit route to whoever was beyond them, without granting transit through a
still-active player.

## Units

- **Definition**: `assets/data/units.rtsdata`, parsed into `UnitDefinition`
  (`inc/warfare/UnitDefinition.h`) — same `.rtsdata` pattern as buildings. Current pilot
  roster: `militia` (cheap/weak), `swordsman` (melee core), `knight` (slow/armored),
  `ram` (siege specialist — high `siege_attack`, weak `road_attack`, needs an escort).
  Values are unbalanced placeholders, not playtested.
- **Instance**: `BattleUnit` (`inc/warfare/BattleUnit.h`) — one instance per deployed/rostered
  unit, `instanceId = playerId*100000 + per-player counter`. Every effective stat
  (`GetEffectiveMaxHp`, `GetEffectiveRoadAttack`, `GetEffectiveSiegeAttack`, `GetEffectiveArmor`,
  `GetEffectiveMoveSpeed`, `GetEffectiveAttackSpeed`) is computed on demand through
  `Player::ModifyBalanceForUnit`, never cached — a tech unlocked after recruitment still
  applies to already-recruited units.
- **Roster**: `UnitRoster` (map of `instanceId → BattleUnit`) lives on `Player`. Units in
  `BattleUnitState::InRoster` are available to deploy; `RosterGuiSystem`/`RosterPanelWidget`
  (`src/ui/GuiRoster.cpp`) is the GUI surface for grouping and deploying them.

## Combat pipeline

One shared pipeline resolves every kind of attack — unit-vs-unit, unit-vs-HQ, HQ thorns, and
tower projectiles — through `inc|src/warfare/CombatPipeline.*`:

- `ICollisionShape` / `CircleShape` / `RectShape` — collider shapes for hit-testing.
- `AttackEmission` — an emitted attack: a collider, a damage payload, and (for projectiles)
  `targetUnitInstanceId`/`speed` for homing movement.
- `CombatResolver::ResolveDamage` — the *only* place with the damage formula:
  `max(1, attack - armor) × resistance × variance(0.9–1.1)`, with the RNG seeded from
  `(worldSeed, tick, sourceUnitInstanceId)` — no persistent RNG state, fully deterministic
  for lockstep replay.
- `ComputeBuildingCenter` — shared helper for HQ thorns' AoE center and tower range/targeting.

Known simplifications (documented in `docs/tech_debt.md`, not required by the plan's exit
criteria): tower projectiles are homing (correct heading every tick toward the target's
*current* position) rather than ballistic with lead calculation; projectile collision is a
circle rather than a directional rect/segment. Both are functionally equivalent for a
projectile that always flies straight at its target and has no pierce mechanic yet.

## HQ

`HqComponent` (`BuildingCapability::Hq`) holds `maxHp`/`hardDefense`/`thornsDamage` as `Stat<>`
(tech/focus-modifiable) plus `thornsInterval`/`captureStockFraction`/`conquestRampDuration` as
plain data fields, driven by the `hq ...` line in `buildings.rtsdata`. `HqCombatSystem`
(`inc|src/warfare/HqCombatSystem.*`) applies siege damage from every `AttackingHq` unit in
parallel (no front-line cap — a deliberate v1 decision, see plan §2.6) and periodic thorns AoE
against besiegers. HP reaching 0 triggers `GameWorld::EliminatePlayer`
(`src/core/GameWorld.Elimination.cpp`): flags the player defeated, clears their
units/roster/spawn queues, hands production buildings to the winner under `ConqueredEconomy`
(`inc/economy/ConqueredEconomy.h`) which ramps productivity 30%→100% over
`conquestRampDuration`, and drains the loser's storage to `floor(total × captureStockFraction)`
for the winner.

## Towers

`TowerCombatComponent` (`BuildingCapability::TowerCombat`) + `DefenseTower` class
(`StorageComponent` for ammo + `LogisticsComponent` + `WorkerComponent` for crew +
`TowerCombatComponent` itself). Ammo is a normal resource delivered over the *regular*
(resource) road network like any production input — not a separate supply system. Crew is a
normal manpower-backed `WorkerComponent`; manpower returns for free through the existing
generic destruction path when the tower is removed.

`TowerAttackSystem` (`inc|src/warfare/TowerAttackSystem.*`, runs after `HqCombatSystem`) picks
the in-range enemy unit closest to reaching *its own* march goal (same rule as road-combat
front-unit selection), consumes one ammo unit, and emits a homing `AttackEmission`.

Only one tower type ships today (`BuildingType::DefenseTower`, `TowerDefinition` embedded in
`BuildingDefinition`, footprint 2×2, `guard_tower.png` placeholder texture). Towers have no HP
and cannot be destroyed in combat — only via the normal `DestroyBuilding` command. Adding a
second tower type means: a new `BuildingType` enum value, a new concrete class mirroring
`DefenseTower` (same four components, its own `BuildingType` baked into the constructor — the
same pattern every building subclass already follows, e.g. `Woodcutter`/`Mine`), a factory
entry in `CreateBuildingFromType` (`inc/core/GameWorldInternal.h`), a build-mode entry in
`GuiBuildModes.cpp`, and a `building <NewType> ... tower ... end` block in `buildings.rtsdata`.
If a third+ tower type follows, switch to a string-keyed catalog like `UnitDefinition` instead
of continuing to add enum values (see `docs/tech_debt.md`).

## Balance stats & modifiers

Every numeric combat parameter is a `BalanceStat` (`inc/economy/BalanceStats.h`) resolved
through the existing `BalanceModifierSet` (`(base + additive) × multiplier`, scoped
Global/Building/Area/Territory, optionally filtered by `buildingType`, `resourceType`,
`resourceCategory`, or — new in ETAP 9 — `unitDefId`):

```
UnitHp, UnitRoadAttack, UnitSiegeAttack, UnitArmor, UnitMoveSpeed, UnitAttackSpeed,
UnitRecruitTime, UnitRecruitManpowerCost,
HqMaxHp, HqDefense, HqThorns,
TowerDamage, TowerRange, TowerAttackSpeed, TowerAmmoEfficiency
```

`UnitRecruitTime`/`UnitRecruitManpowerCost` are the one pair kept from the old system's
"Recruitment*" stats, deliberately: queuing a frontline unit needs a non-zero time cost so
deploying an attack stays a planning decision, not an instant reaction. Wired into
`RecruitmentComponent::QueueRecruitment` (`src/economy/RecruitmentComponent.cpp`) via
`Player::ModifyBalanceForUnit`, floored at `std::max(1.0, ...)` so no multiplier can make
recruitment instant, and the recruitment GUI panel (`src/ui/Gui.cpp`) shows the *effective*
(modified) time/cost, not the raw catalog value.

### Data file syntax

A `.rtsdata` `technology`/`focus` block modifies a stat with:

```
modifier <StatName> [additive <n>] [multiplier <n>] [building <BuildingType>] [resource <ResourceType>] [category <ResourceCategory>] [unit <unitDefId>]
```

`ParseBalanceStat` (`src/research/Technology.cpp`) must recognize `<StatName>` — an unknown
name silently falls back to `BalanceStat::BuildTime` (a real bug found and fixed in ETAP 9;
see `docs/tech_debt.md` for the ~90-technology cleanup this caused). When adding a new
`BalanceStat`, always add it to `ParseBalanceStat` in the same change.

### focuses.rtsdata is currently a cheat sheet, not a tree

The pre-pivot focus tree (MILITARY/POLITICS categories, ~90 technologies) referenced stats
from the removed war system and was replaced wholesale in ETAP 9 with one flat, disconnected
placeholder technology per `BalanceStat` — titled after the stat, showing correct syntax
including the `building`/`resource`/`category`/`unit` filter keys. It is not balanced and not
meant to be researched as shipped; it exists so a real focus tree can be designed against a
working reference instead of the removed one. `technologies.rtsdata` (the SCIENCE trunk) was
*not* touched this way — it's a small (5-node), intentionally-designed tree and was fixed
in place (see `docs/tech_debt.md` for the two bugs found and fixed there).

## GUI

`RosterGuiSystem`/`RosterPanelWidget` (`src/ui/GuiRoster.cpp`) — available units, ordered
attack group (reorder/remove), target picker (auto-selects the only reachable enemy),
Deploy → `GameCommand::DeployUnits`. `GuiPanel::Update` (`src/ui/Gui.cpp`) has dedicated
branches for `HqComponent` (HP bar, defense/thorns stats, storage), `TowerCombatComponent`
(damage/range/attack-speed/crew, ammo buffer), and `RecruitmentComponent` (queue + per-unit
recruit buttons from `GetUnitCatalog()`) — all checked *before* the generic `IsStorageLike()`
branch, since all three also carry a `StorageComponent`. The strategic HUD has a "Roster (N)"
button with a live "HQ under attack" warning derived from `GetDeployedUnits()` (no extra
simulation state). All GUI-initiated mutation goes through `GameScene::SubmitLocalCommand` —
verified safe even when previewing an opponent's building, since the command layer
(`GameWorld.Commands.cpp`) independently rejects `source->owner != player`.

GUI has no automated test coverage (project-wide constraint, not new). Every GUI change in
ETAP 8/9 was verified by compiling, linking, running the full test suite, and a launch smoke
test — not by interacting with the running window. Manual playtesting (SP and MP, per the
plan's own "brak testów automatycznych GUI!" requirement) is still outstanding.

## What to read next

- `docs/tech_debt.md` — open items, simplifications, and the ETAP-9 findings (orphaned focus
  tree, `technologies.rtsdata`/test mismatch, second-tower-type steps).
- `docs/tower_defense_rework_plan.md` — original intent, stage-by-stage plan, and the design
  questions that were resolved with the user during implementation (§2).
- `project_tower_defense_pivot.md` (memory) — chronological implementation log: what was
  built per stage, bugs found and fixed, and why.
