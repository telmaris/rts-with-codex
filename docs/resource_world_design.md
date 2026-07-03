# Resource & World Design

Projekt fundamentu pod surowce, biomy i handel. Cel: rozłożenie zasobów na mapie ma
znaczenie strategiczne — niektóre nacje mają łatwy dostęp do rzadkich surowców (cyna,
srebro, złoto, piasek), co ciągnie je ku handlowi. Historyczny sens + lekki steampunk.

Status: Faza 1 + 2 + 3 **zaimplementowane**. Faza 4 (handel + AIActionKind::Trade, asymetryczne
starty) zaplanowana.
- Faza 1: biomy, kopalne rudy (copper/tin/silver/gold/sand/sulfur/saltpeter).
- Faza 2: łańcuchy bronze/coke/steel/glass, Mint+COINS.
- Faza 3: Powderworks (SULFUR+SALTPETER+COKE→GUNPOWDER), broń palna MUSKET+CARTRIDGE
  wpięta w equipment taxonomy (EquipmentCategory::Firearm, EquipmentMaterial::Blackpowder q2.4),
  recepty Smith (Musket/Cartridge). SupplyHub żąda Firearm, dywizje Archer dostają muszkiety.
Wizualizacja biomów: na razie tylko placeholdery (biom steruje rozmieszczeniem, nie teksturą
gruntu — brak assetów pustyni/bagien).

## Model dwuwarstwowy terenu

`TileType` mieszał dwie rzeczy: biom (wygląd ziemi) i złoże (co kopie Mine). Rozdzielamy:

```
Tile.biome     ← region: PLAINS / FOREST / HILLS / MOUNTAINS / DESERT / WETLAND
Tile.tileType  ← złoże:  GRASS / WOOD / STONE / IRON_ORE / COAL / COPPER_ORE /
                          TIN_ORE / SILVER_ORE / GOLD_ORE / SAND / SULFUR / SALTPETER
```

`Mine`/`Woodcutter` dalej czytają `tileType` (zero zmian w produkcji). Generator zyskuje
warstwę biomów, która **bramkuje** gdzie złoże może powstać.

## Pipeline generatora

```
1. BIOME PASS    2 pola szumu (elevation, moisture) → biom per kafelek (Whittaker-style)
                 niska elewacja → PLAINS/WETLAND, średnia → HILLS, wysoka → MOUNTAINS
                 sucho+gorąco → DESERT, wilgotno → FOREST
2. DEPOSIT PASS  istniejący cellular-automata blob, ale tiles muszą trafić w zgodny biom
                 (tabela ResourcePatchParameters.allowedBiomes)
3. SCARCITY      rarity-tier → patchCount; rzadkie surowce w kilku klastrach
4. START PLACE   asymetryczne pozycje HQ → część nacji przy klastrze rzadkim (eksport),
                 reszta tylko przy pospolitych (import) → strukturalny powód do handlu
```

Las nie przetnie pustyni (SAND tylko w DESERT, WOOD tylko w FOREST). Złoto nie w trawie
(rudy tylko w HILLS/MOUNTAINS).

## Taksonomia surowców

Tier militarny (stone → copper → **bronze** → iron → steel):

| Tier | Broń | Inputy | Koszt | Klasa | Niсza |
|---|---|---|---|---|---|
| 1 | COPPER_SWORD | COPPER+WOOD | niski | niska | wczesna |
| 2 | BRONZE_SWORD | COPPER+TIN+WOOD | nisko-średni | poniżej żelaza | tania masówka jeśli masz cynę |
| 3 | IRON_SWORD | IRON+WOOD | średni | wysoka | standard |
| 4 | STEEL_SWORD | STEEL+WOOD | wysoki | najwyższa | elita |

**Cyna (TIN_ORE)** — rzadka, napędza handel: nacje z cyną robią tani brąz, reszta importuje.

Łańcuchy:
```
COPPER + TIN          → BRONZE     (Foundry)
COAL                  → COKE       (Foundry)        — fuel pod stal/parę
IRON + COKE           → STEEL      (Foundry)
SAND + COKE           → GLASS      (Glassworks)     — zalążek steampunku
SILVER / GOLD         → COINS      (Mint)           — waluta handlu
SULFUR+SALTPETER+COKE → GUNPOWDER  (Powderworks)    — Faza 3
```

## Scarcity → presja handlowa

| Tier rzadkości | Surowce | Rozmieszczenie |
|---|---|---|
| Pospolite | WOOD, STONE, COAL, IRON_ORE, WHEAT, WATER | szeroko, każda nacja ma |
| Niepospolite | COPPER_ORE | regionalnie |
| Rzadkie | TIN_ORE, SILVER_ORE, GOLD_ORE, SAND, SULFUR, SALTPETER | kilka klastrów |

## Spójność z AI

- Oś `Resources` wykrywa deficyt per surowiec → "brak cyny, mam srebro".
- Przyszła akcja `AIActionKind::Trade` (Tier 3) → milestone "zabezpiecz cynę importem".
- COINS jako medium → AI kupuje czego brak zamiast zdobywać militarnie.

## Elastyczność (jak dodać surowiec / tweakować generację)

Dodanie surowca:
1. `inc/Resource.h` — enum + `resourceTypes[]` (alokacja w ResourcePool) + `rt2s()`
2. `src/BuildingConfig.cpp` — `ParseResourceType` (nazwa→enum)
3. recepty w `assets/data/buildings.rtsdata` (data-driven)

Dodanie kopalnego złoża:
1. `inc/Building.h` — `TileType`
2. `src/BuildingConfig.cpp` — `ParseTileType`
3. `assets/data/buildings.rtsdata` — `terrain_production <TILETYPE>` w Mine
4. `MapParameters.resourcePatches` — patch + `allowedBiomes`

Tweakowanie generacji: wszystkie pokrętła w `MapParameters` (biome thresholds,
patch rarity/clustering, allowedBiomes). Generator jest deterministyczny na `seed`.

## Touch-points (mapa plików)

| Obszar | Pliki |
|---|---|
| Enumy surowców/terenu | `inc/Resource.h`, `inc/Building.h` |
| Biomy + generator | `inc/MapGenerator.h`, `src/MapGenerator.cpp` |
| Parsery danych | `src/BuildingConfig.cpp` |
| Definicje budynków/recept | `assets/data/buildings.rtsdata` |
| Nowe budynki (Mint, Glassworks) | `ProductionBuildings.h`, `Building.cpp`, `GameWorldInternal.h`, GUI |
| Serializacja | `src/GameWorld.Persistence.cpp` (save version) |
