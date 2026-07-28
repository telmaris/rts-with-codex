# Settlement Supply & Population Scaling

## Status and intent

Implemented pilot specification for the economy reset. This is also the
handoff note for later balancing and asset replacement; it is not a request
to keep the old gold/coin/gunpowder chains alive.

The settlement system is the permanent demand engine of the economy:

- a player may spam cheap `Village` buildings for fast, vulnerable manpower;
- or invest in fewer, much larger `Town` and `City` settlements whose supply
  chains are deep and whose logistics are worth defending.

Food remains mandatory at every level. Higher levels add *packages*, rather
than making a settlement request a long list of loose materials. A package is
an ordinary, physically transported resource produced by a dedicated workshop.
This keeps the receiver interface stable and puts the Factorio-style chain on
the producer side.

## Settlement levels

| Level | Population cap | Required upkeep packages | Intended role |
|---|---:|---|---|
| Village | 140 | Food Rations | Cheap frontier manpower and aggressive expansion. |
| Town | 350 | Food Rations, Household Goods | Compact productive settlement; opens specialist economy. |
| City | 1200 | Food Rations, Household Goods, Urban Goods | Expensive late-game population and specialist hub. |

The exact cap values are deliberate. A Town is not merely a cosmetic upgrade:
it replaces 2.5 Villages. A City replaces roughly 8.5 Villages before global
population-cap modifiers apply. Percentage bonuses therefore reward the
high-investment city route strongly, while the Village route remains the fast
way to field an early army.

### Upgrade behaviour

- `Village -> Town` and `Town -> City` upgrade the same building in place. The
  road connection, owner, position, existing food buffer and construction
  state remain intact.
- The upgrade itself needs construction materials; the ongoing packages are
  separate from those one-time costs.
- A level must have all of its required packages to generate manpower at its
  normal rate.
- Missing Food Rations pauses manpower growth first. Missing a higher-tier
  package removes that level's bonus growth/capacity benefit; it must not
  instantly destroy population or downgrade the settlement.
- Apply a grace reserve (for example two upkeep intervals) before penalties.
  This protects the player from a single delayed transport and avoids a
  logistics death spiral.

## Supply packages

### 1. Food Rations (`FOOD_PROVISIONS`)

Already present. This remains the only Village requirement.

```text
BREAD + MEAT + WATER -> FOOD_RATIONS
```

Food Rations represent staple calories and clean drinking water. They should
stay intentionally simple: the early player must be able to expand before
building consumer-goods industry.

### 2. Household Goods (`HOUSEHOLD_GOODS`)

Town requirement. This is the first civilian-standard package, not a luxury
crate. It represents a household being equipped and maintained with furniture,
clothes, footwear, storage and basic cooking vessels.

Final first recipe:

```text
PLANKS + CLOTHING + POTTERY -> HOUSEHOLD_GOODS
```

| Ingredient | In-world meaning | Why it belongs here |
|---|---|---|
| Planks | furniture, house repairs, chests | keeps forestry relevant after early construction |
| Clothing | ordinary garments and bedding | creates a permanent textile sink |
| Pottery | cooking/storage vessels | gives clay and kilns a broad non-military purpose |

`CLOTHING` deliberately has alternative raw-material routes. The first pilot
recipes may be `LEATHER -> CLOTHING` and `FLAX/HEMP -> FIBRE -> CLOTH ->
CLOTHING`. Leather is a simple early route; plant fibres become the scalable
route once agriculture expands. This avoids requiring livestock and textiles
to be implemented in the same first patch.

Town should request *one* `HOUSEHOLD_GOODS` type, never the three ingredients
directly. A Household Workshop requests the three inputs and delivers the finished
package through the normal road system.

### 3. Urban Goods (`URBAN_GOODS`)

City requirement. Do not call this package `Luxury Goods` internally unless it
is meant to be purely optional: soap and candles are urban necessities, not
luxuries. `Urban Goods` describes the higher level of hygiene, literacy and
domestic equipment expected by a dense city.

Final first recipe:

```text
SOAP + BOOKS + COPPERWARE -> URBAN_GOODS
```

| Ingredient | In-world meaning | Chain opened |
|---|---|---|
| Soap | hygiene, washing, disease prevention | wood + tallow |
| Books | records, schooling, civic administration | paper + ink |
| Copperware | pots, fixtures, durable household equipment | copper -> sheet -> finished goods |

This is the intended first persistent sink for copper. Copper does not compete
with iron for swords; it supplies corrosion-resistant, formable and
heat-conducting crafted goods. `COPPERWARE` should later also be used by
brewery/distillery/apothecary upgrades and by city water fittings.

### Future luxury layer (optional, not City baseline)

Once a City already has a stable Urban Goods chain, add a fourth, optional
package `CULTURAL_GOODS`. It should provide a bonus to a City rather than be a
hard prerequisite for basic manpower.

Possible future ingredients:

```text
DYED_CLOTH + GLASSWARE + BRASS_ACCESSORIES + BOOKS -> CULTURAL_GOODS
```

Potential rewards: research speed, focus progress, specialist capacity or
prestige. Do not use gold, silver or jewelry in the first economy reset; they
can return only with a complete trade/diplomacy loop.

## Supporting production chains

Only add a resource together with a consumer. The minimal set needed to make
the first three packages meaningful is below.

## Basic map resources added by this design

| Map resource | First consumer | Immediate purpose | Planned later purpose |
|---|---|---|---|
| Clay | Kiln | Pottery for Household Goods | Bricks, roof tiles, drainage pipes |
| Sand | Kiln | Glass for the City upgrade | Mortar, casting moulds |
| Copper Ore | Copperworks | Copperware for Urban Goods | vessels, pipes, mechanical parts, brass, wire |

`CLAY` is a required new terrain deposit in the first implementation. It must
be common enough that a Town upgrade is never gated by a rare-map lottery.
`SAND` and existing `COPPER_ORE` also exist on the map. The pilot Kiln can
select a Glass recipe, and Glass is part of the one-time City upgrade cost.

### Pilot buildings

| Building | Inputs | Output | Notes |
|---|---|---|---|
| Inn / Rations Kitchen | Meat, Bread, Water | Food Rations | Existing Food Provisions producer; rename is optional. |
| Animal Farm | Wheat, Water | Cattle | Stable, high-throughput livestock production; longer and more resource-intensive than hunting. |
| Butcher | Cattle | Meat, Raw Hide, Tallow | Splits one transported animal into the three useful products. |
| Tannery | Raw Hide, Water, Tannin | Leather | Tannin may be simplified away in the first code slice, but is the desired final recipe. |
| Tailor | Leather **or** Cloth | Clothing | Two selectable recipes; leather route is early, cloth route scales later. |
| Armorer | Leather, Iron | Leather Armor | Military Leather consumer; recipe and unit usage must arrive together. |
| Horse Stable | Wheat, Water | Horse | Slow, reliable horse production for cavalry and logistics. |
| Kiln | Clay or Sand, Wood | Pottery, Bricks, Glass | Shared fired-material workshop with selectable recipes. |
| Household Workshop | Planks, Clothing, Pottery | Household Goods | Town package assembler. |
| Soapworks | Wood, Tallow | Soap | Deliberately simplified historical formula for the game. |
| Inkworks | Charcoal, Water | Ink | Compact pilot recipe; refine only when a broader dye/chemistry branch exists. |
| Scriptorium | Paper, Ink | Books | First Books producer; University remains a later direct consumer. |
| Copperworks | Copper, Wood/Charcoal | Copperware, Copper Vessel, Copper Pipe, Mechanical Parts | One building with selectable recipes in the pilot; each finished good has different building consumers. |
| Urban Workshop | Soap, Books, Copperware | Urban Goods | City package assembler. |

### Clothing

```text
LEATHER -> CLOTHING

FLAX or HEMP -> FIBRE -> CLOTH -> CLOTHING
```

`CLOTHING` is a Town input and later a construction/maintenance input for
military and logistics content. Wool can join this branch later; it is not
needed to validate the first Town supply loop.

### Cattle, meat, hides and tallow

```text
WHEAT + WATER -> CATTLE

CATTLE -> MEAT + RAW_HIDE + TALLOW

RAW_HIDE + WATER + TANNIN -> LEATHER

LEATHER -> CLOTHING
LEATHER + IRON -> LEATHER_ARMOR
```

`CATTLE` is an ordinary transportable resource, exactly like the planned
`HORSE`: Animal Farm produces it and Butcher consumes it. The longer chain is
deliberate. It consumes more Wheat and Water, needs more road capacity and
more buildings than hunting, but gives a predictable, scalable supply of all
three outputs at once.

| Output | Immediate consumer | Role |
|---|---|---|
| `MEAT` | Rations Kitchen | One of the three Food Rations inputs. |
| `RAW_HIDE` | Tannery | Unprocessed skin; do not let Tailor consume it directly. |
| `TALLOW` | Soapworks | Prevents animal farming from creating a dead by-product. |

`RAW_HIDE` is the recommended technical resource name. UI text may call it
"Raw Leather" if desired, but `Leather` must remain the processed product of
the Tannery. The pilot may use `RAW_HIDE + WATER -> LEATHER`; add `TANNIN`
from bark once wood by-products have their own robust logistics/storage path.

Leather has two immediate branches:

```text
LEATHER -> CLOTHING                 (Tailor)
LEATHER + IRON -> LEATHER_ARMOR  (Armorer)
```

The first supports Town upkeep; the second supports military recruitment.
`TALLOW` immediately feeds Soap. A later candle branch is optional; do not add
it until it has a real consumer.

### Horse breeding and mounted logistics

```text
WHEAT + WATER -> HORSE
```

Horse Stable should have a long cycle and modest output: a horse is a capital
asset, not a quick food conversion. It deliberately competes with Animal Farm
for Wheat and Water.

| Horse use | Pilot implementation | Later extension |
|---|---|---|
| Cavalry | Recruitment consumes `HORSE` alongside manpower, food and equipment. | Add Harness/Saddle as a second requirement. |
| Logistics | Upgrade a **road hub or route**, spending a Horse to add horse-drawn transport capacity/speed. | `HORSE + HARNESS + PLANKS -> WAGON`; wagons become reusable fleet assets. |
| Agriculture | Optional Farm upgrade consumes a Horse and Tools for higher output. | Draft teams, ploughing and fertiliser loops. |

The pilot charges one Horse at road level 3 and another Horse plus Mechanical
Parts at level 4 because the current upgrade model is per tile. Treat this as
a temporary balance hook. Once route/hub upgrades exist, migrate that cost to
the connected route or Logistics Hub so long roads do not become
disproportionately expensive.

### Soap

```text
WOOD + TALLOW -> SOAP
```

This is an intentionally compact game recipe derived from the historical
wood-ash/animal-fat process. Do not add Ash, Lye or Potash to the pilot until
their by-products have multiple consumers.

### Ceramics

```text
CLAY + CHARCOAL -> POTTERY
```

Later extensions:

```text
CLAY + CHARCOAL -> BRICKS
CLAY + CHARCOAL -> ROOF_TILES
```

`BRICKS` should be a primary one-time building-upgrade material for Town,
City, fortifications and industrial workshops. `ROOF_TILES` are optional until
weather/fire mechanics or a dedicated City construction demand exists. Pottery
enters Household Goods immediately, so Clay has a stable civilian sink from
day one.

### Paper and books

```text
PAPER + INK -> BOOKS
```

Books remain a natural University/research input, so City demand does not make
paper a one-purpose resource. Ink can initially be a simple wood/charcoal
derived craft good; refine it later only when dyes and chemistry have their
own consumers.

### Copper goods

```text
COPPER_ORE -> COPPER
                  -> COPPERWARE
                  -> COPPER_VESSEL
                  -> COPPER_PIPE
                  -> MECHANICAL_PARTS
```

This is intentionally a crafted-goods branch. Do not make Copper Sword the
default military tier while Iron is universally available. Do not merge the
four outputs into one generic `COPPER_GOODS`: they need distinct destinations
in the construction and upgrade system.

| Finished good | Pilot consumers | Long-term consumers | Design role |
|---|---|---|---|
| `COPPERWARE` | Urban Goods | Household upgrades, City amenities | Domestic pots, fixtures and durable useful objects; the repeatable City copper sink. |
| `COPPER_VESSEL` | Soapworks and City upgrade | fluid-storage building, distillery, boiler/steam devices | Tanks, vats, stills and heat-resistant process vessels; normally a one-time construction or upgrade cost. |
| `COPPER_PIPE` | Urban Workshop and City upgrade | pumps, breweries, fluid network, steam infrastructure | Pipes and fittings for hydraulic systems; kept out of the starting Well cost to avoid an early-game dependency loop. |
| `MECHANICAL_PARTS` | level-4 Road and siege equipment | pumps, powered workshops, wagons, precision machines | Small gears, bearings, pins and valves; the generic machinery construction input. |

All four recipes may initially be direct `COPPER + fuel -> finished good`
recipes at Copperworks. The recipe choice itself gives Copperworks meaningful
capacity pressure. Add `COPPER_SHEET` only when at least three outputs need it
and the extra logistics stop makes play more interesting rather than merely
longer.

`MECHANICAL_PARTS` are deliberately copper in the pilot. When Calamine/Zinc
and Brass are introduced later, keep the same finished resource and add a
better `BRASS -> MECHANICAL_PARTS` recipe (higher output, shorter cycle or
premium parts). This lets brass enrich the system without invalidating every
existing consumer.

## Initial upkeep targets

These are balance starting points per 60-second upkeep interval at full supply,
not final numbers. Food scales approximately with population cap; advanced
packages rise more slowly so that a City is viable but clearly industrially
expensive.

| Level | Food Rations / min | Household Goods / min | Urban Goods / min |
|---|---:|---:|---:|
| Village | 1 | - | - |
| Town | 3 | 1 | - |
| City | 10 | 3 | 1 |

The City route therefore needs dense, reliable throughput but earns a large
population cap per defended settlement. Tune only after a full road-network
playtest; do not tune these values from production rates in isolation.

## One-time upgrade costs (design targets)

| Upgrade | Recommended materials |
|---|---|
| Village -> Town | Planks, Stone, Tools, Cloth |
| Town -> City | Bricks, Planks, Tools, Copperware, Glass, Copper Pipe, Copper Vessel |

No luxury resource should gate Town. City should not require rare map deposits
until the game has a robust trading or substitution system.

## Gameplay and AI rules

- Villages are cheap, fast and weakly supplied. They are the correct choice
  for early aggression and frontier expansion.
- Towns are the first economic commitment. The AI may upgrade only after it
  has active production and reserve stock of every Household Goods input.
- Cities are a strategic commitment, not an automatic upgrade. Build them
  near robust road hubs, warehouses and the relevant workshops.
- The AI must treat every required package as a top-priority shortage only for
  settlements it has already upgraded. It must never chase Urban Goods before
  it owns a City.
- Store at least two upkeep intervals of each required package locally or in
  reachable stockpiles before upgrading.
- Resource telemetry, HUD and settlement UI must show each package's
  production/min, consumption/min, stored amount and next upkeep deadline.

## Suggested implementation order

1. Add settlement level to the population component and make `Village` render
   / present its current level in UI. Preserve current Level 1 behavior.
2. Add generic multi-resource settlement requests and buffers; retain
   `FOOD_PROVISIONS` compatibility while migrating to package definitions.
3. Implement `Village -> Town`, `HOUSEHOLD_GOODS`, Clothing, Clay/Pottery and
   the Tailor/Kiln/Household Workshop vertical slice.
4. Add livestock by-products, Leather, Tallow and Soapworks.
5. Implement `Town -> City`, Ink/Books, Copperware and `URBAN_GOODS`.
6. Update save/snapshot/checksum formats, UI telemetry, AI supply diagnosis and
   tests at every new persisted field or resource type.

## Explicit non-goals for this phase

- Gold, silver, coins, trade and jewelry.
- Gunpowder, muskets, cartridges and saltpeter.
- Coke, industrial acids, batteries and electric power.
- Mandatory cultural/luxury goods beyond the City Urban Goods package.

Those chains are allowed back only when they have at least two consumers, one
of them repeatable, and a reason to exist beyond a single research cost.
Their old numeric enum values and parsers remain reserved for save/data
compatibility, but they have no generated deposits, active production recipes
or build-menu buildings. Existing research costs were redirected to active
goods so the technology tree does not contain unobtainable-resource gates.
