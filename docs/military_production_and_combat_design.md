# Military Production & Combat Design

## Scope

Design and implementation note for the first complete pre-gunpowder army. The
pilot roster, equipment chains, range, cavalry counter and area damage are now
data-driven. Explicit airborne pathing/target filtering and richer front/rear
formation controls remain later combat work.

The rule is the same as in the civilian economy: an equipment resource exists
only if it has a producer, at least one real consumer and a tactical reason to
choose it.

## Combat vocabulary

| Stat | Meaning |
|---|---|
| Soft Attack | Damage against living units. Primary stat for line infantry, cavalry, archers and ballistae. |
| Hard Attack | Damage against buildings, towers and siege targets. Primary stat for rams and catapults. |
| Armor | Reduces incoming Soft Attack; some weapons/units can have armor penetration later. |
| Range | Number of combat rows/tiles a unit can attack across. Melee is range 1. |
| Splash | One attack can damage nearby targets; reserved for catapults and later explosives. |
| Anti-Air | Can target flying units. Initially archers and later dedicated anti-air weapons. |
| Move Speed | Determines strategic march speed and tactical ability to close distance. |

`Soft Attack` and `Hard Attack` should replace/clarify the current vague
road-attack/siege-attack presentation. One stat model must be used both in UI
and simulation.

## Line combat

When hostile armies meet, they form an engagement with a front line and a rear
line. This is deliberately a compact model, not a separate tactical minigame.

```text
attacker rear:    Archers / Ballista / Catapult
attacker front:   Spearmen / Infantry / Cavalry

defender front:   Spearmen / Infantry / Cavalry
defender rear:    Archers / Ballista / Catapult
```

- Melee units attack the opposing front line. They cannot hit rear-line ground
  units until the front is broken.
- Ranged units fire over their own front line according to Range. Their default
  target is the enemy front; target-priority rules may later let them aim at
  specific vulnerable roles.
- Ranged units are weak when exposed: once an enemy reaches their line they
  have poor melee damage and low armor.
- The front has limited frontage. Extra melee units wait in reserve, then fill
  empty slots when a frontline unit dies or retreats.
- Cavalry may have a charge bonus when entering an engagement, but not while
  attacking braced Spearmen.
- Siege units prefer buildings/towers when a target is in range. They should
  be very vulnerable to a direct melee breakthrough.

This gives the player a clear army-composition puzzle: a cheap infantry wall
protects damage dealers; Spearmen deny cavalry; cavalry hunts vulnerable rear
units; artillery defeats static defenses.

## Flying units (later infrastructure, not first roster)

Flying units ignore ground frontage and may continue toward their strategic
target instead of stopping to fight ground armies. They can be attacked only
by:

- flying units;
- ground units with `Anti-Air` (initially Archers); and
- later dedicated anti-air towers or weapons.

First candidates are balloons and gyrocopters after advanced engineering.
They are explicitly out of scope until line combat, range targeting and
Anti-Air exist and are tested.

## First roster

| Unit | Role | Soft | Hard | Armor / speed | Counterplay |
|---|---|---|---|---|---|
| Light Infantry | Cheap, fast early raider; rushes exposed economy. | Medium | Low | Low armor, high speed | Loses to armored line troops and defended ranged formations. |
| Swordsman | Standard mid-game line infantry in leather armor. | High | Low | Medium armor/speed | Loses efficiency to Heavy Infantry; must reach ranged units. |
| Heavy Infantry | Slow, expensive line anchor. | High | Low-medium | High armor, low speed | Flanked, kited or bypassed by air; costly equipment. |
| Archer | Basic ranged Soft Attack. | Medium at range | Very low | Low armor | Dies quickly if cavalry/light infantry reaches it. |
| Heavy Archer | Longer range and higher Soft Attack. | High at range | Very low | Low armor, slow | Expensive bow/ammunition; still vulnerable in melee. |
| Light Cavalry | Fast flanker and raider. | High on charge | Low | Medium armor, very high speed | Braced Spearmen; expensive Horse supply. |
| Heavy Cavalry | Armored breakthrough unit. | Very high on charge | Low | High armor, high speed | Spearmen and poor value against structures. |
| Spearman | Defensive frontline and cavalry counter. | Medium | Low | Medium armor, low-medium speed | Weak offensive pressure; bonus only when braced. |
| Ballista | Long-range anti-personnel siege support. | Very high | Low | Very low armor, very slow | Needs a screen; poor against buildings. |
| Battering Ram | Durable melee building breaker. | Very low | High | Medium-high armor, very slow | Requires escort; poor in unit combat. |
| Catapult | Ultimate pre-gunpowder siege unit. | Medium / splash | Very high | Very low armor, very slow | Vulnerable, expensive and needs protected firing position. |

Every unit in this roster now has a live equipment producer. Heavy variants,
Ballista and Catapult are included in the pilot so their balance can be tested
against the ranged/front-line combat implementation.

## Physical equipment chains

### Baseline melee equipment

```text
IRON + WOOD -> LIGHT_WEAPON          (Smith)
IRON + WOOD -> SWORD                 (Smith)
IRON + WOOD -> SPEAR                 (Spear Workshop)

LEATHER + IRON -> LEATHER_ARMOR      (Armorer)
IRON + LEATHER -> HEAVY_ARMOR        (Armorer, pilot)
```

`LIGHT_WEAPON` may be represented as a simple iron axe/mace resource, or Light
Infantry can initially use only Food Rations and Manpower. Keep the first
version lean; its gameplay job is speed and low cost, not a separate perfect
equipment taxonomy.

### Rope, bows and arrows

```text
HEMP -> FIBRE -> ROPE                (Ropeworks)

WOOD + ROPE -> BOW                   (Bowyer)
PLANKS + ROPE -> HEAVY_BOW           (Bowyer)

WOOD + IRON -> ARROWS                (Fletchery)
```

Hemp is a new crop, grown at a Hemp Farm. Rope is intentionally generic and
must gain multiple consumers: bows, siege engines, later wells/hoists and
possibly logistics. Do not add Rope only for Bows.

Arrow production is separate from Bow production so that a sustained archer
army has a visible ammunition demand. At the first implementation arrows can
be consumed on recruitment; later, ranged attacks may consume or reserve
ammunition only after the logistics model can support that without constant
micro-stalls.

### Cavalry

```text
WHEAT + WATER -> HORSE               (Horse Stable)
LEATHER + IRON -> HARNESS            (Harness Workshop, later)

HORSE + equipment + FOOD_RATIONS
  -> CAVALRY RECRUITMENT
```

Light Cavalry needs Horse plus light weapon/Leather Armor. Heavy Cavalry adds
Heavy Armor and a better weapon. Both should have weak Hard Attack: their
function is to win mobile field battles, not to demolish towers.

### Siege engines

```text
PLANKS + ROPE + IRON -> BALLISTA     (Siege Workshop)
PLANKS + IRON        -> BATTERING_RAM (Siege Workshop)
PLANKS + ROPE + IRON -> CATAPULT     (Siege Workshop)
```

The precise recipe amounts are a balance task. The critical design point is
that Rope, Planks and Iron all have civilian/military alternatives, so siege
production creates a visible logistic commitment instead of a detached timer.

## Production buildings

| Building | Inputs | Output | Primary consumers |
|---|---|---|---|
| Smith | Iron, Wood | Light Weapon, Sword | Light Infantry, Swordsman |
| Spear Workshop | Iron, Wood | Spear | Spearman |
| Armorer | Leather, Iron | Leather Armor | Swordsman, Light Cavalry |
| Ropeworks | Hemp/Fibre | Rope | Bowyer, Siege Workshop, later Well/hoist |
| Bowyer | Wood/Planks, Rope | Bow, Heavy Bow | Archer, Heavy Archer |
| Fletchery | Wood, Iron | Arrows | Archer, Heavy Archer |
| Horse Stable | Wheat, Water | Horse | Cavalry, mounted logistics |
| Siege Workshop | Planks, Rope, Iron | Ballista, Ram, Catapult | Siege recruitment/deployment |

Do not make Barracks manufacture equipment. Barracks requests finished goods,
holds a short local reserve and converts them plus manpower into units. This is
what makes a broken road, a missing Ropeworks or a stalled Armorer matter.

## Gunpowder: later, but with no saltpeter deposit

Saltpeter must not be another base terrain deposit. When firearms are ready,
make nitrate a processed result of the agricultural/wood-chemistry economy:

```text
Animal husbandry -> MANURE
WOOD -> CHARCOAL + ASH

MANURE + ASH + WATER -> NITRE        (Nitre Works)
NITRE + SULFUR + CHARCOAL -> GUNPOWDER (Powderworks)
```

`SULFUR` can be a rare mineral deposit; `NITRE` is explicitly not. This gives
late gunpowder a long, connected chain without placing a mandatory saltpeter
mine on every map. Manure must first have an immediate sink such as Fertilizer,
otherwise it should not be introduced as a blocking Animal Farm output.

Gunpowder unlocks only after the pre-gunpowder roster and siege roles work:

```text
GUNPOWDER + IRON + WOOD -> MUSKET
GUNPOWDER + PAPER -> CARTRIDGE
GUNPOWDER + IRON + PLANKS -> CANNON
```

Muskets are ranged line units; cannons are later area/hard-attack artillery.
They must not be implemented merely as more damage on the existing units.

## Implementation order

1. Add the combat primitives: front/rear rows, range, Soft/Hard Attack, Armor
   and explicit building/tower targets.
2. Add Light Infantry, Swordsman, Spearman and Archer with working counters.
3. Add Hemp Farm, Ropeworks, Bowyer and Fletchery; make equipment delivery to
   Barracks visible in the UI and AI diagnostics.
4. Add Horse Stable and Light Cavalry; then bracing and the cavalry counter.
5. Add Heavy Infantry, Heavy Archer and Heavy Cavalry only after their basic
   versions have clear battlefield roles.
6. Add Siege Workshop, Ram, Ballista and Catapult; implement range/splash and
   structure targeting in the same slice.
7. Add flying units only after Anti-Air target filtering is stable.
8. Add Manure/Nitre/Gunpowder, muskets and cannons as a separate late-game
   expansion.

## Non-goals

- No resource-free unit upgrades that bypass physical equipment production.
- No per-shot Arrow/Cartridge consumption until the transport model can supply
  it reliably.
- No mandatory saltpeter map deposit.
- No balloons, gyrocopters, bombs, rockets or magic before the core role and
  targeting system is proven in playtests.
