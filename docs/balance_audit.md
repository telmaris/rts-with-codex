# Balance parameter audit

This file tracks gameplay parameters that should be data-driven and modifier-aware.

## Data-driven building parameters

Defined in `assets/data/buildings.rtsdata`:

- Build: `build_cost`, `build_time`, `footprint`
- Production: `workers`, `cycle_time`, `input`, `output`, `input_buffer`, `output_buffer`
- Terrain production: `terrain_production`, tile type, richness consumption through map tiles
- Storage: `storage` capacity and optional initial amount
- Village: `manpower_rate`, `population_cap`, `upkeep_interval`, `food_package_upkeep`
- Military: `territory_radius`, `hit_points`, `strength`, `garrison_capacity`, `supply`, `supply_capacity`
- Road/logistics: `transport_time`, `upgrade_level`, `max_capacity`, `speed_modifier`

## Runtime-only parameters

Currently hardcoded and now routed through `BalanceModifierSet` where practical:

- Recruitment time by unit type
- Recruitment manpower cost by unit type
- Military attack damage formula
- Unit strength weights for militia, swordsmen and archers
- Food productivity penalty when villages are not supplied
- Road reservation capacity checks
- Transport timing on road/building hops

## Modifier-aware stats

Implemented modifier keys in `BalanceStat`:

- `BuildTime`
- `ProductionCycleTime`
- `ProductionOutputAmount`
- `WorkerCapacity`
- `TransportTime`
- `RoadCapacity`
- `RoadSpeed`
- `MilitaryStrength`
- `AttackDamage`
- `HitPoints`
- `TerritoryRadius`
- `GarrisonCapacity`
- `SupplyCapacity`
- `SupplyConsumption`
- `ManpowerRate`
- `PopulationCap`
- `RecruitmentTime`
- `RecruitmentManpowerCost`

## Current modifier entry point

Each `Player` owns a `BalanceModifierSet`:

```cpp
player->balanceModifiers.AddModifier({
    BalanceStat::ProductionOutputAmount,
    0.0,
    1.20,
    BalanceModifierScope::Global(),
    BuildingType::Woodcutter,
    ResourceType::WOOD,
    std::nullopt,
    "tech:better_axes"
});
```

The modifier works as `(base + additive) * multiplier`.

The optional filters are:

- `BuildingType`
- `ResourceType`
- `MilitaryUnitType`

## Modifier scopes

Modifiers can now be global or local:

```cpp
// Global technology bonus.
player->balanceModifiers.AddModifier({
    BalanceStat::ProductionOutputAmount,
    0.0,
    1.20,
    BalanceModifierScope::Global(),
    BuildingType::Woodcutter,
    ResourceType::WOOD,
    std::nullopt,
    "tech:better_axes"
});

// Local aura: buildings inside an 8-tile radius produce faster.
player->balanceModifiers.AddModifier({
    BalanceStat::ProductionCycleTime,
    0.0,
    0.85,
    BalanceModifierScope::Area({120, 96}, 8),
    BuildingType::Bakery,
    ResourceType::Null,
    std::nullopt,
    "building:aura:market_42"
});
```

Available scopes:

- `GlobalPlayer`: affects every matching stat owned by the player.
- `Building`: affects one building id or position id.
- `Area`: affects matching stat queries whose map position falls within the radius.
- `Territory`: affects matching stat queries inside the player's territory.

Production, transport, road capacity/speed, manpower, recruitment and military combat now evaluate modifiers with building position context.

## Next audit targets

- Move recruitment base values to data files.
- Move unit strength weights to data files.
- Decide whether `HitPoints`, `TerritoryRadius`, `GarrisonCapacity` and `SupplyCapacity` should update live, require recalculation, or apply only on construction.
- Add save/load support for permanent tech modifiers once the tech tree exists.
- Add a data file for techs that emits `BalanceModifier` entries through `IBalanceModifierSource`.
