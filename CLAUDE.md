# CLAUDE.md — rts-with-codex

Projekt hobbystyczny: gra RTS w C++20 na raylib, hybryd Factorio i Tower Defense.
Topdown 2D, losowo generowana mapa, ekonomia + logistyka dróg jak w Factorio, a starcia
militarne rozgrywane jako tower defense na immutable drodze wojskowej łączącej graczy.
Aktualnie: grywalny prototyp z lokalnym MP (lobby v0.1).

## Kontekst gry

### Wizja i gatunek

Hybryd Factorio i Tower Defense. Gracz buduje sieć produkcji i logistyki (Factorio) i broni się
przed / atakuje przeciwników wysyłając jednostki na jedną, stałą drogę wojskową łączącą HQ
graczy — jednostki maszerują nią automatycznie do wrogiego HQ, walcząc po drodze z jednostkami
przeciwnika i ostrzeliwane przez wieże obronne. Perspektywa topdown 2D, losowo generowana mapa,
cel: zniszczenie HQ wszystkich przeciwników. (Do 2026-07 gra była hybrydą Factorio×HoI4 z
dywizjami/terytorium/frontami — ten system został całkowicie usunięty i zastąpiony powyższym;
zobacz `docs/tower_defense_design.md` i `docs/tower_defense_rework_plan.md`.)

### Pętla rozgrywki

1. **Ekonomia** — wycinaj lasy, wydobywaj węgiel i rudę, przetwarzaj surowce w łańcuchach produkcji. Zasoby transportowane są fizycznie po drogach między budynkami.
2. **Logistyka** — budujesz drogi łączące producentów z magazynami i odbiorcami. Drogi mają pojemność i prędkość; wąskie gardła faktycznie blokują produkcję.
3. **Populacja** — wioski generują manpower w tempie zależnym od zaopatrzenia w żywność. Manpower zasila rekrutację jednostek i obsadę budynków (produkcyjnych i wież).
4. **Militaria** — `Barracks` rekrutuje jednostki (`assets/data/units.rtsdata`) do rosteru gracza. Gracz grupuje jednostki i deployuje je na własnym końcu drogi wojskowej (`GameCommand::DeployUnits`) — maszerują pojedynczo w kolumnie do wrogiego HQ, walcząc z napotkanymi jednostkami wroga. `DefenseTower` (footprint 2×2, amunicja = zwykły zasób transportowany drogami surowcowymi) automatycznie ostrzeliwuje wrogie jednostki w zasięgu. HQ ma HP, twardą obronę i okresowe "thorns" (obrażenia obszarowe dla oblegających); spadek HP do 0 = eliminacja gracza, budynki produkcyjne przechodzą na zwycięzcę z rampem produktywności 30%→100%.
5. **Technologia i focusy** — University odblokowuje technologie (modyfikatory do produkcji, jednostek, wież, HQ, logistyki). Focusy to oddzielne drzewo decyzji strategicznych (`assets/data/focuses.rtsdata` — obecnie płaska "ściągawka" statów czekająca na przeprojektowanie, zobacz `docs/tower_defense_design.md`).
6. **Rozwój państwa** — progresja przez poziomy: Tribal Society → Chiefdom → Kingdom → Aristocratic State, każdy daje globalne bonusy.

### Zasoby — łańcuchy produkcji

```
WOOD (teren)  →  Woodcutter  →  WOOD  →  LumberMill  →  PLANKS
IRON_ORE (teren) → Mine → IRON_ORE → Foundry → IRON
COAL (teren) → Mine → COAL (potrzebny do Foundry)
WHEAT (teren) → WheatFarm → WHEAT → Windmill → FLOUR → Bakery → BREAD
HuntersHut → MEAT / LEATHER
Well → WATER
BREAD + MEAT + WATER → Inn → FOOD_PROVISIONS
Paperworks → PAPER
Smith → TOOLS / bronie (COPPER_SWORD, IRON_SWORD, BRONZE_SWORD, STEEL_SWORD) / ARROWS (amunicja wież)
Mint → COINS, Glassworks → GLASS, Powderworks → GUNPOWDER
```

`FOOD_PROVISIONS` (produkowane przez Inn z BREAD+MEAT+WATER) → wioska → manpower → rekrutacja
jednostek + obsada budynków/wież; to jedyny zasób, który `PopulationComponent` faktycznie przyjmuje.
`ResourceType::BEER` to nieużywany placeholder w enumie — żaden budynek go dziś nie produkuje ani
nie konsumuje; nie usuwać bez świadomej decyzji i bumpa save/wire (patrz `docs/tech_debt.md`).
Stary `WEAPON_SUPPLY`/system pakietów zaopatrzeniowych został usunięty w ramach pivotu — amunicja
wież to dziś zwykły zasób (`ARROWS`) płynący normalną siecią dróg surowcowych.

### Wojna: tower defense (drogi wojskowe, jednostki, HQ, wieże)

- **Droga wojskowa** — `MilitaryRoadNetwork` generowana raz przy tworzeniu mapy, immutable
  (bez pojemności, nie do przebudowy). Gracze w pierścieniu (sortowani po kącie HQ), każdy
  połączony ze swoimi dwoma sąsiadami. `PathingService::FindMilitaryPath`/`AreHqsConnected`
  (statyczne) obsługują też wielohopowe BFS przez pierścień PRZEZ wyeliminowanych graczy.
- **Jednostki** — `BattleUnit` (instancja) + `UnitDefinition` (katalog, data-driven z
  `assets/data/units.rtsdata`). Rekrutowane w `Barracks`, trafiają do `Player::roster`,
  deployowane grupą (`GameCommand::DeployUnits`) na drogę wojskową — maszerują pojedynczo,
  zawsze do wrogiego HQ, bez rally/hold. Napotkanie wrogiej kolumny = walka szpica-na-szpicę
  (`UnitCombatSystem`); dotarcie do bramy = grupowanie bez limitu i atak na HQ
  (`HqCombatSystem`).
- **Wspólny pipeline ataku** — KAŻDY atak (jednostka-jednostka, jednostka-HQ, thorns HQ,
  pocisk wieży) przez jedną infrastrukturę: `AttackEmission` + `ICollisionShape`
  (`CircleShape`/`RectShape`) + `CombatResolver::ResolveDamage` (`inc|src/warfare/CombatPipeline.*`).
- **HQ** — `HqComponent` (HP, hardDefense, thorns). Eliminacja gracza (`GameWorld::EliminatePlayer`)
  → budynki produkcyjne przechodzą na zwycięzcę pod `ConqueredEconomy` (ramp 30%→100%),
  magazyny drenowane do ~20% dla zwycięzcy, pokonany = obserwator z wolną kamerą.
- **Wieże** — `TowerCombatComponent` na `DefenseTower` (jedyny typ dziś; dodanie kolejnego =
  nowy `BuildingType` + klasa wzorem `Woodcutter`/`Mine`, zobacz `docs/tower_defense_design.md`).
  Amunicja to zwykły bufor karmiony przez `LogisticsComponent`/sieć dróg surowcowych.

Pełny opis architektury, uproszczeń i punktów rozszerzalności: `docs/tower_defense_design.md`.
Reguła budowania: zakaz stawiania budynku w promieniu 3 kratek (Chebyshev) od wrogiej struktury
(`TileMap::IsWithinEnemyProximity` w `CanBuildFootprint`) — zastępuje starą mechanikę terytorium.

### AI

Jeden deterministyczny model utility pod tower defense: `UtilityAIModel` (`inc/ai/AIModel.h`,
`src/ai/AIModel.cpp`), wpięty przez `AIController` (`inc/ai/Controller.h`). Stary 3-tier
goal→milestone→action (osie priorytetów, `AIPersonality`, `PrimitiveAIModel`) został całkowicie
usunięty w AI-rework 2026-07-16 (etap 1 "czystka") — osie nie pasowały do pętli TD.

**Cykl decyzyjny (co ~1.5 s sim):**
1. **Sensing** — `AISituation` (throttlowany snapshot, wyłącznie odczyty): siła jednostek
   moich/nadchodzących na torze, HP HQ, roster per typ, wieże + amunicja, manpower, telemetria
   produkcja-vs-konsumpcja, deficyty zasobów, budynki niepodłączone do dróg
   (`LogisticsComponent::IsConnectedToRoadNetwork`).
2. **Utility potrzeb** (`AINeed`): Defense / RecruitDeploy / EconomySustain / LogisticsRepair /
   Research — score [0,1] z prostych wzorów (np. `Threat()` = siła wroga na moim torze vs
   deployed+wieże). RecruitDeploy jest trwale wysoki — cel nadrzędny AI to jednostki na torze.
3. **Egzekucja** — potrzeby próbowane w kolejności (score desc, przy remisie stała kolejność
   enumu = priorytet) aż któraś wykona realny `GameCommand` przez aktuatory z `ai/AIActions.h`
   (mechaniczne wykonawstwo przeniesione 1:1 ze starego modelu, z fixami determinizmu sort-by-id).

Kompozycja rosteru: pod atakiem defensywna (staying power per koszt), w ofensywie mix 2:1
lane-fighter:siege (`UtilityAIModel::RankUnitChoices`, pure + unit-tested). Poziomy trudności
(0 Primitive … 3 Hard, `MapParameters::aiDifficulty`): wyższy = większy grant startowy
(zasoby+manpower do HQ przy inicie, `GrantDifficultyStartingBonus`), niższy = większy seedowany
szum decyzyjny (swing utility ±30/20/10/0% + skip cyklu 15/10/5/0%) — jeden model, zero różnic
w logice. Determinizm: tylko odczyty + `SubmitCommand`, RNG wyłącznie `mt19937` seedowany z
(seed mapy, id gracza) — lockstep-safe, testowane checksumami dwóch światów przy aktywnym szumie.

Pełny opis modelu: `docs/td_ai_design.md`. Historyczny design osiowy: `docs/strategic_ai_design.md`
(superseded).

### Balans i modyfikatory

Parametry budynków/jednostek data-driven z `assets/data/buildings.rtsdata` i `assets/data/units.rtsdata`.
Technologie i focusy z `assets/data/technologies.rtsdata` i `assets/data/focuses.rtsdata`
(focuses.rtsdata jest dziś płaską "ściągawką" statów, nie prawdziwym drzewem — zobacz
`docs/tower_defense_design.md`).
Modyfikatory balance przez `BalanceModifierSet` na `Player` — addytywne + multiplikatywne, z zakresem
Global / Building / Area (scope Territory usunięty razem z systemem terytorium), opcjonalnie filtrowane
po `buildingType`/`resourceType`/`resourceCategory`/`unitDefId`. Formula: `(base + additive) * multiplier`.
Pełna lista statów w `inc/economy/BalanceStats.h`.

### Multiplayer

Eksperymentalny LAN (TCP). Do 2 graczy. Lokalna sesja host+klient (`LocalhostHostSession` + `LocalhostClientSession`) lub prawdziwy TCP (`TcpGameTransport`). Desync wykrywany checksumem co sekundę, recovery przez pełen snapshot. Lobby v0.1 — brak matchmakingu.

## Szybki start

```powershell
.\build_and_run.ps1          # bump patch, configure, build Release, uruchom
.\run_tests.ps1              # build + uruchom pełen suite testów (domyślnie Debug)
.\run_tests.ps1 -List        # wylistuj testy bez uruchamiania
.\measure_coverage.ps1       # build z pokryciem + HTML raport (wymaga OpenCppCoverage)
```

Skrypty `.bat` delegują do `.ps1` — można używać obu zamiennie.

## Wersjonowanie

Źródło prawdy: plik `VERSION` (format `MAJOR.MINOR.PATCH`).
- `PATCH` — auto-inkrementowany przez `scripts/bump_version.ps1` przy każdym lokalnym buildzie.
- `MINOR` / `MAJOR` — bumped ręcznie przez edycję `VERSION`.
- CI tworzy GitHub Release i tag `vX.Y.Z` na push do main, tylko jeśli tag dla danej wersji jeszcze nie istnieje.
- Nagłówek `build/generated/Version.h` generowany przez CMake z `VERSION` — dostępny jako `RTS_VERSION_STRING` itp.

## Zależności

| Biblioteka | Wersja | Lokalizacja |
|---|---|---|
| raylib | 5.0 (built from source) | `deps/raylib/` (gitignored) |
| raygui | 4.0 (header-only) | `deps/raygui/raygui.h` (committed) |

Źródła raylib: `deps/raylib-src/` (gitignored, sklonowane z tag `5.0`).
Żeby odbudować raylib od zera:
```powershell
cmake -S deps/raylib-src -B deps/raylib-src/build -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$PWD\deps\raylib" -DBUILD_EXAMPLES=OFF
cmake --build deps/raylib-src/build --config Release --parallel
cmake --install deps/raylib-src/build --config Release
```

CMake odbiera raylib przez `-Draylib_INCLUDE_DIR` + `-Draylib_LIBRARY` (nie przez `raylib_DIR` — config z raylib to Find-style, nie install config).

## Architektura — przegląd

```
GameWorld            ← symulacja: TileMap + PlayerHandler + Commands
  ├─ TileMap         ← siatka kafelków, każdy tile może mieć Building*
  ├─ Player          ← ekonomia, roster jednostek, technologie, focusy, telemetria
  │    └─ Building*  ← Building + komponenty (IBuildingComponent — kompozycja, nie dziedziczenie
  │                    per-cecha); konkretne klasy (Woodcutter, Barracks, DefenseTower, Headquarters...)
  │                    rejestrują tylko te komponenty, których potrzebują
  └─ IController     ← LocalController | AIController | RemoteController

IGameSession         ← abstrakcja nad pętlą symulacji
  ├─ HostSession       ← autorytatywny host (SP + MP); background thread startuje w konstruktorze
  └─ ClientSession     ← klient MP (mirror + resync przez snapshot); transport abstrakcyjny (Localhost/TCP)

Sceny (scenes/Scenes.h/cpp)
  └─ GameScene       ← aktywna sesja + GuiController
       └─ GuiController → GuiSystem (BasicMapView / Build / Road / Destroy / Stats / Focus / Tech / Roster)
```

### Kluczowe wzorce

**Lockstep deterministyczny:** cała mutacja stanu gry wyłącznie przez `GameCommand` → `SubmitCommand` → `ProcessCommands` na fixed-tick 100 Hz. Nigdy nie mutuj stanu symulacji bezpośrednio z UI.

**Fixed-tick clock:** `FixedSimulationClock::FixedDt = 1/100s`, max 12 ticków na ramkę. Akumulacja desyncu wykrywana checksumem co 1 s; klient wysyła `RESYNC_REQUEST`, host odpowiada snapshotem.

**Kompozycja budynków (`IBuildingComponent`):** `Building` nie jest fat-interface hierarchią —
każda cecha (produkcja, logistyka, workers, przepisy, storage, populacja, road, rekrutacja, HQ,
walka wieży) to osobny komponent (`inc/economy/BuildingComponents.h`, `BuildingCapability` enum
+ `GetComponent<T>()`). Konkretna klasa budynku (np. `DefenseTower`) tylko rejestruje komponenty,
których potrzebuje.

**ResourcePool:** fixed-size pool zamiast heap allocations per resource. `ResourceType::Null = 255` — sprawdzaj ten sentinel przed użyciem pointera.

**`std::map` w ścieżce symulacji** — świadomy wybór dla deterministycznej kolejności iteracji. Nie zamieniać na `unordered_map` w kodzie wpływającym na logikę gry.

**Wspólny pipeline ataku (`CombatPipeline`):** jedna infrastruktura collider+resolver dla
KAŻDEGO rodzaju ataku (jednostka-jednostka, jednostka-HQ, thorns, pociski wież) — nie duplikować
logiki obrażeń przy nowych typach walki, rozszerzać `CombatPipeline.h/.cpp`.

## Struktura katalogów

```
inc/            ← nagłówki, per-domena podkatalogi:
                   core/ (GameWorld, GameSession, GameCommand, Stat...)
                   economy/ (Building, BuildingComponents, Player, ConqueredEconomy...)
                   warfare/ (BattleUnit, UnitDefinition, CombatPipeline, UnitMarchSystem,
                             UnitCombatSystem, HqCombatSystem, TowerAttackSystem)
                   simulation/ (MapGenerator, RoadNetwork, MilitaryRoadNetwork, PathingService)
                   ai/ (Controller, AIModel, AIActions)
                   research/ (Technology, ResearchCatalog, StateDevelopment)
                   data/ (Resource, Equipment, RtsDataFile, StrategicResource)
                   ui/ (Gui, GuiController, Input, InputManager, Renderer, AudioSystem)
                   scenes/ (Game, GameWindow, Scenes, SceneUtils)
                   multiplayer/ (TcpGameTransport)
src/            ← implementacje, sama struktura podkatalogów co inc/;
                   GameWorld rozbity na partial TU w core/:
                   GameWorld.cpp / .Commands / .Init / .Persistence / .Render / .TileMap /
                   .Checksum / .Units / .Elimination
                   GUI rozbite na TU w ui/: Gui, GuiController, GuiCommon, GuiMapWidgets,
                   GuiHudPanels, GuiResearchTree, GuiBuildModes, GuiRoster
tests/          ← Google Test — pełny suite uruchamiany w CI (patrz sekcja CI)
deps/           ← zależności (raylib-src/, raylib/ gitignored; raygui/ committed)
cmake/          ← Version.h.in (szablon nagłówka wersji)
scripts/        ← bump_version.ps1
docs/           ← tech_debt.md (audyt długu), post_pivot_audit_2026-07-12.md (audyt +
                   wykonane bugfixy po pivocie), work_plan_2026-07-13.md (plan pracy;
                   Blok C superseded przez AI-rework), td_ai_design.md (aktualny model AI),
                   tower_defense_design.md (finalna architektura pivotu),
                   tower_defense_rework_plan.md (historyczny plan pivotu),
                   strategic_ai_design.md (superseded), resource_world_design.md
assets/         ← tekstury, fonty, dane (.rtsdata)
```

## Typy budynków (BuildingType)

Produkcja: `Woodcutter`, `HuntersHut`, `LumberMill`, `Mine`, `Foundry`, `Well`, `WheatFarm`, `Windmill`, `Bakery`, `Inn`, `Paperworks`, `Smith`, `Mint`, `Glassworks`, `Powderworks`, `University`
Militarne: `DefenseTower`, `Barracks` (rekrutacja)
Inne: `Headquarters`, `Village`, `StorageBuilding`, `Road`

`GuardTower`/`Fortress`/`Castle`/`MilitaryBuilding` (stary system terytorium) — usunięte w ETAP 1
pivotu; nie przywracać bez świadomej decyzji projektowej.

## Zasoby (ResourceType)

Surowce: `WOOD`, `COAL`, `IRON_ORE`, `STONE` (typy terenu też)
Przetworzone: `PLANKS`, `IRON`, `FLOUR`, `BREAD`, `MEAT`, `WATER`, `BEER`, `PAPER`, `TOOLS`, `COINS`
Militarne: `FOOD_PROVISIONS` (→ manpower), `COPPER_SWORD`/`IRON_SWORD`/`BRONZE_SWORD`/`STEEL_SWORD`
(koszt rekrutacji jednostek), `ARROWS` (amunicja `DefenseTower`, produkowana przez Smith)

`WEAPON_SUPPLY` i cała kategoria pakietów zaopatrzeniowych (`SupplyPackage`/`SupplyHub`) —
usunięte w ramach pivotu.

## Serializacja

- **Sieciowa:** `GameCommand::Serialize/TryDeserialize` — `WireVersion = 12` (osobne mniejsze
  `WireVersion` też na `GameSnapshot` i innych strukturach — sprawdź `inc/core/GameCommand.h`),
  pozycyjna.
- **Zapis:** `GameWorld::SaveToFile/LoadFromFile` (`src/core/GameWorld.Persistence.cpp`) — tekstowy format `RTS_SAVE`, aktualny save version = 27 (26→27: usunięty martwy `DiplomaticState`).
- **Snapshot MP:** `GameSnapshot::Serialize/TryDeserialize` — chunki 12 KB przez TCP przy join.

Przy zmianie formatu serializacji: inkrementuj odpowiedni `WireVersion` / save version.

## CI (GitHub Actions)

`.github/workflows/windows-release.yml`:
- Triggeruje na każdy push i PR.
- Instaluje raylib przez vcpkg (`x64-windows`, z cache), używa `deps/raygui/raygui.h` z repo.
- Uruchamia PEŁEN suite testów (`rts_tests.exe` bez `--gtest_filter`) — zielony (2026-07-13: usunięto
  8 pre-existing testów "forestry"/`technologies.rtsdata` mismatch, patrz `docs/tech_debt.md`;
  drzewo tech/focus i tak czeka na ręczne przeprojektowanie od podstaw).
- Release tworzony tylko gdy `VERSION` zawiera wersję bez istniejącego tagu.

## Mapa ficzerów → pliki

Ściąga: gdzie szukać zanim zaczniesz skanować. Format `inc/` = deklaracja/interfejs, `src/` = implementacja.

### Symulacja i pętla gry
| Ficer | Gdzie szukać |
|---|---|
| Fixed-tick, akumulator, UpdateSimulation | `inc/core/GameSession.h` — `FixedSimulationClock`, `HostSession` (background thread) |
| Główna pętla Update (kamera, render, sim) | `src/core/GameWorld.Render.cpp` — `GameWorld::Update`, `GameWorld::UpdateSimulation` |
| Inicjalizacja świata, starting base | `src/core/GameWorld.Init.cpp` — `InitWorld`, `CreateStartingBase` |
| Kolejkowanie i egzekucja komend | `src/core/GameWorld.Commands.cpp` — `SubmitCommand`, `ProcessCommands`, `ExecuteCommand` |
| Update jednostek/walki (kolejność: march→combat→hq→tower) | `src/core/GameWorld.Units.cpp` — `GameWorld::UpdateUnits` |
| Eliminacja gracza, przejęcie ekonomii | `src/core/GameWorld.Elimination.cpp` — `GameWorld::EliminatePlayer` |
| Zapis/wczytywanie | `src/core/GameWorld.Persistence.cpp` |
| Checksum deterministyczny | `src/core/GameWorld.Checksum.cpp` |

### Budynki i produkcja
| Ficer | Gdzie szukać |
|---|---|
| Klasa bazowa `Building` + kompozycja komponentów | `inc/economy/Building.h`, `inc/economy/BuildingComponents.h` — `BuildingCapability`, `IBuildingComponent`, `GetComponent<T>()` |
| Konkretne klasy produkcyjne | `inc/economy/ProductionBuildings.h` — `Woodcutter`, `Mine`, `Foundry`, `Smith`, `University`... |
| Budynki militarne (Barracks, DefenseTower, Headquarters) | `inc/economy/Building.h` |
| Logika produkcji (cykl, bufory, przepis) | `src/economy/ProductionComponent.cpp` |
| Logistyka (transport, żądania) | `src/economy/LogisticsComponent.cpp` |
| Rekrutacja jednostek | `src/economy/RecruitmentComponent.cpp` — `RecruitmentComponent::QueueRecruitment` |
| HQ (HP, thorns, hard defense) | `src/economy/HqComponent.cpp` |
| Walka wieży (cel, amunicja) | `src/economy/TowerCombatComponent.cpp` |
| Przejęta ekonomia (ramp produktywności) | `inc/economy/ConqueredEconomy.h`, `src/economy/ConqueredEconomy.cpp` |
| Konfiguracja budynków z pliku .rtsdata | `src/economy/BuildingConfig.cpp`, `inc/economy/BuildingConfig.h` |
| Factory do tworzenia budynków z save | `inc/core/GameWorldInternal.h` — `CreateBuildingFromType` |
| Stawianie budynku na mapie (walidacja, tile, reguła bliskości) | `src/core/GameWorld.TileMap.cpp` — `TileMap::CanPlaceBuilding`, `IsWithinEnemyProximity` |

### Wojsko (jednostki, walka, HQ, wieże)
| Ficer | Gdzie szukać |
|---|---|
| Katalog jednostek (data-driven) | `assets/data/units.rtsdata`, `inc/warfare/UnitDefinition.h` |
| Instancja jednostki, roster gracza | `inc/warfare/BattleUnit.h`, `src/warfare/BattleUnit.cpp` — `BattleUnit`, `UnitRoster` |
| Marsz kolumny po drodze wojskowej | `inc/warfare/UnitMarchSystem.h`, `src/warfare/UnitMarchSystem.cpp` |
| Walka jednostka-jednostka | `inc/warfare/UnitCombatSystem.h`, `src/warfare/UnitCombatSystem.cpp` |
| Walka na HQ (siege, thorns, eliminacja) | `inc/warfare/HqCombatSystem.h`, `src/warfare/HqCombatSystem.cpp` |
| Wieże (cel, pocisk, amunicja) | `inc/warfare/TowerAttackSystem.h`, `src/warfare/TowerAttackSystem.cpp` |
| Wspólny pipeline ataku (collidery, resolver) | `inc/warfare/CombatPipeline.h`, `src/warfare/CombatPipeline.cpp` |
| Droga wojskowa (generator, sieć) | `inc/simulation/MilitaryRoadNetwork.h`, `src/simulation/MilitaryRoadNetwork.cpp` |
| Pathfinding (HQ connectivity, marsz) | `inc/simulation/PathingService.h`, `src/simulation/PathingService.cpp` — statyczne `FindMilitaryPath`/`AreHqsConnected` |

### Mapa i kafelki
| Ficer | Gdzie szukać |
|---|---|
| Definicja TileMap i Tile | `inc/simulation/MapGenerator.h` |
| Operacje na TileMap | `src/core/GameWorld.TileMap.cpp` |
| Generacja mapy i rozmieszczenie zasobów | `src/simulation/MapGenerator.cpp`, `inc/core/GameWorldInternal.h` |
| Parametry mapy (rozmiar, seed, density) | `inc/simulation/MapGenerator.h` — `MapParameters` |

### Logistyka i drogi (surowcowe)
| Ficer | Gdzie szukać |
|---|---|
| Pathfinding, nawigacja po drogach | `inc/simulation/RoadNetwork.h` — `NavigationMap`, `RoadNetwork` |
| Transport paczek po drogach | `src/simulation/RoadNetwork.cpp` — `RoadNetwork::BeginTransport`, `Transportable::BeginTransport` |
| Pojemność drogi, rezerwacje | `src/simulation/RoadNetwork.cpp` — `CanReserveTransportPath`, `CountReservedRoadCapacity` |
| Typ transportowalny | `inc/simulation/Transport.h` — `Transportable` |

### Gracz, ekonomia
| Ficer | Gdzie szukać |
|---|---|
| Klasa Player (pełna) | `inc/economy/Player.h`, `inc/economy/PlayerEconomy.h`, `inc/economy/PlayerDataTracker.h` |
| Telemetria zasobów (flow rates) | `inc/economy/PlayerDataTracker.h` — `PlayerEconomyTelemetry`, `ResourceFlowSnapshot` |
| Build<T>() — stawianie budynku przez gracza | `inc/economy/Player.h` — template `Player::Build` |
| Modyfikatory balance (tech, state, aury) | `inc/economy/BalanceModifiers.h`, `inc/economy/BalanceStats.h` |
| Kolejka budowy ograniczona builderami | `inc/economy/ConstructionQueue.h`, `src/economy/ConstructionQueue.cpp` |

### Technologie i focusy
| Ficer | Gdzie szukać |
|---|---|
| Definicje technologii i focusów | `assets/data/technologies.rtsdata`, `assets/data/focuses.rtsdata` |
| Ładowanie, parsowanie (`ParseBalanceStat`/`ParseModifier`) | `src/research/Technology.cpp`, `inc/research/Technology.h` |
| Stan unlock gracza | `inc/research/Technology.h` — `TechnologyState`, `FocusState` |
| Poziomy rządu (Tribal→Aristocratic) | `inc/research/StateDevelopment.h` — `StateDevelopment`, `GetStateDevelopmentDefinitions` |

### AI
| Ficer | Gdzie szukać |
|---|---|
| Model decyzyjny AI (sensing → utility potrzeb → akcja) | `inc/ai/AIModel.h`, `src/ai/AIModel.cpp` — `UtilityAIModel`, `AISituation`, `AINeed` |
| Kompozycja rosteru AI (postawa, mix 2:1) | `inc/ai/AIModel.h` — `UtilityAIModel::RankUnitChoices` (public static, unit-tested) |
| Aktuatory AI (build anchor, drogi, zapytania) | `inc/ai/AIActions.h`, `src/ai/AIActions.cpp` — `FindBuildAnchor`, `SubmitRoadPath`, `TryBuildRoads`, `DiagnoseResourceNeed`, `FindProducerOptions`, `AIActionState` |
| Kontroler AI (seam IController) | `inc/ai/Controller.h`, `src/ai/Controller.cpp` — `AIController` |
| Trudność AI (grant startowy) | `src/core/GameWorld.Init.cpp` — `GrantDifficultyStartingBonus`; szum decyzyjny w `UtilityAIModel::Update` |
| Check podłączenia budynku do sieci dróg | `src/economy/LogisticsComponent.cpp` — `IsConnectedToRoadNetwork` |

### Multiplayer i sieć
| Ficer | Gdzie szukać |
|---|---|
| Interfejs transportu, localhost transport | `inc/core/GameSession.h` — `IGameTransport`, `LocalhostGameTransport` |
| Host/Client session, resync, snapshot | `inc/core/GameSession.h` — `HostSession`, `ClientSession` (transport-agnostic) |
| TCP transport (LAN) | `inc/multiplayer/TcpGameTransport.h`, `src/multiplayer/TcpGameTransport.cpp` |
| Serializacja komend i wyników | `inc/core/GameCommand.h` — `GameCommand::Serialize/TryDeserialize` |
| Snapshot (pełen stan do join) | `inc/core/GameSnapshot.h` |

### UI i sceny
| Ficer | Gdzie szukać |
|---|---|
| Widżety bazowe (Button, Panel, GuiPanel…) | `inc/ui/Gui.h` — `UiWidget`, `GuiPanel` i pochodne |
| Rdzeń GUI (systemy, routing inputu, akcje) | `inc/ui/GuiController.h`, `src/ui/GuiController.cpp` — `GuiSystem`, `GuiController`, `WireCommonSystemActions` |
| Panel budynku (treść per typ: produkcja, storage, HQ, wieża, rekrutacja) | `src/ui/Gui.cpp` — `GuiPanel::Update` |
| Tryby budowania/niszczenia (build, road, destroy) | `src/ui/GuiBuildModes.cpp` — `BuildGuiSystem`, `RoadBuildSystem`, `DestroyGuiSystem` |
| Panel rosteru/deployu jednostek | `src/ui/GuiRoster.cpp` — `RosterGuiSystem`, `RosterPanelWidget` |
| Widgety na mapie (budynki, jednostki) | `src/ui/GuiMapWidgets.cpp` |
| Panel statystyk, HUD zasobów + roster + "HQ under attack" | `src/ui/GuiHudPanels.cpp` — `StatsPanelWidget`, `StrategicResourceHudWidget`, `StatsGuiSystem` |
| Drzewko focusów i technologii (wspólny widget) | `src/ui/GuiResearchTree.cpp` — `ResearchTreePanelWidget`, `FocusGuiSystem`, `TechGuiSystem` |
| Helpery wspólne GUI (kamera, przyciski HUD) | `src/ui/GuiInternal.h`, `src/ui/GuiCommon.cpp` — `DispatchHudButtonClick`, `WireCommonSystemActions` |
| Sceny (MainMenu, GameScene, Multiplayer…) | `inc/scenes/Scenes.h`, `src/scenes/*.cpp` |
| Sesja w GameScene, runtime loop, rejestracja GuiSystem-ów | `src/scenes/GameScene.cpp` — `controller->AddSystem<T>("name")` |
| Renderowanie mapy, tekstury, atlasy | `src/ui/Renderer.cpp`, `inc/ui/Renderer.h` |

### Zasoby (typy, pool, bufory)
| Ficer | Gdzie szukać |
|---|---|
| Enum ResourceType, rt2s() | `inc/data/Resource.h` |
| ResourcePool (fixed allocator) | `inc/data/Resource.h` — `ResourcePool`, `ResourceBuffer` |
| Kategorie zasobów (dla modifierów `category`) | `inc/data/Resource.h` — `ResourceCategory`, `ResourceCategoryOf` |

## Znane długi techniczne

Pełna lista w `docs/tech_debt.md`. Najważniejsze:
1. **Ręczna serializacja** — zduplikowana w kilku miejscach, brak schematu.
2. **Snapshoty TCP nie skalują się** dla dużych map (używane do recovery desyncu); recovery
   po desyncu w praktyce nie jest w pełni zaaplikowany do żywego świata klienta — zobacz TODO.md.
3. **`Player` zbliża się do god-object** — wiele odpowiedzialności, wiele includów.
4. **`assets/data/focuses.rtsdata`** to płaska ściągawka statów, nie prawdziwe drzewo focusów —
   czeka na przeprojektowanie (zobacz `docs/tower_defense_design.md`).
5. **GUI bez pokrycia testami automatycznymi** — każda zmiana GUI wymaga ręcznej weryfikacji.
6. **`std::set<Building*>` (`GetTrackedBuildings()`) kluczowany surowym wskaźnikiem** — bezpieczny
   dla sum/min/max, ale ŁAMIE determinizm lockstep w każdym miejscu, gdzie kolejność iteracji
   decyduje, co "wygrywa" (np. auto-connect, pierwszy pasujący budynek). Przy zamianie pełnego
   skanu tilemapy na ten rejestr — sortować po `building->id`, zobacz `docs/tech_debt.md`.

## Ważne szczegóły

- `ResourceType::GOLD` koliduje z makro raylib `GOLD` — rozwiązane przez `#undef GOLD` w `inc/data/Resource.h`.
- `DrawRectangleRoundedLines` — spatchowane do API raylib 5.0 (dodany param `lineThick` przed `Color`).
- Save pliki lądują w `saves/` (gitignored).
- Logi w `logs/rts.log` (gitignored).
- `TileMap` zdefiniowany w `inc/simulation/MapGenerator.h`.
