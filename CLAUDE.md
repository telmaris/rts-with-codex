# CLAUDE.md — rts-with-codex

Projekt hobbystyczny: gra RTS w C++20 na raylib, hybryd Factorio i Hearts of Iron 4.
Topdown 2D, losowo generowana mapa, ekspansja przez budynki militarne, logistyka dróg i produkcja.
Aktualnie: grywalny prototyp z lokalnym MP (lobby v0.1).

## Kontekst gry

### Wizja i gatunek

Hybryd Factorio i Hearts of Iron 4. Gracz buduje sieć produkcji i logistyki (Factorio), a jednocześnie zarządza ekspansją militarną i rozwojem państwa na poziomie makro (HoI4). Perspektywa topdown 2D, losowo generowana mapa, cel: podbicie wszystkich przeciwników.

### Pętla rozgrywki

1. **Ekonomia** — wycinaj lasy, wydobywaj węgiel i rudę, przetwarzaj surowce w łańcuchach produkcji. Zasoby transportowane są fizycznie po drogach między budynkami.
2. **Logistyka** — budujesz drogi łączące producentów z magazynami i odbiorcami. Drogi mają pojemność i prędkość; wąskie gardła faktycznie blokują produkcję.
3. **Populacja** — wioski generują manpower w tempie zależnym od zaopatrzenia w żywność. Manpower zasila wojsko i siłę roboczą budynków.
4. **Militaria** — Guard Towers / Fortresses / Castles projkują terytorium. Barracks szkolą jednostki (Militia / Swordsman / Archer). Jednostki grupowane są w dywizje z mortalem, doświadczeniem i zaopatrzeniem.
5. **Technologia i focusy** — University odblokowuje technologie (modyfikatory do produkcji, wojska, logistyki). Focusy to oddzielne drzewo decyzji strategicznych (rząd, specjalizacja).
6. **Rozwój państwa** — progresja przez poziomy: Tribal Society → Chiefdom → Kingdom → Aristocratic State, każdy daje globalne bonusy.

### Zasoby — łańcuchy produkcji

```
WOOD (teren)  →  Woodcutter  →  WOOD  →  LumberMill  →  PLANKS
IRON_ORE (teren) → Mine → IRON_ORE → Foundry → IRON
COAL (teren) → Mine → COAL (potrzebny do Foundry)
WHEAT (teren) → WheatFarm → WHEAT → Windmill → FLOUR → Bakery → BREAD
HuntersHut → MEAT / LEATHER
Well → WATER → Inn → BEER
Paperworks → PAPER
Smith → TOOLS / bronie (COPPER_SWORD, IRON_SWORD, BOW, ARROWS)
```

Żywność (BREAD, MEAT, BEER) → wioska → manpower → rekrutacja.
FOOD_PROVISIONS i WEAPON_SUPPLY to logistyczne zasoby wojskowe (pakiety dostarczane do budynków militarnych).

### Terytorium i walka

- Budynki militarne projektują terytorium promieniowo (`territoryRadius`). Tylko w swoim terytorium można budować.
- Atak: gracz wybiera budynek militarny jako źródło i cel — dywizje maszerują, zadają obrażenia (`hitPoints`), można je odzyskać.
- Ekspansja: stawiasz Guard Tower na skraju terytorium, rozszerzasz zasięg, budujesz za nim infrastrukturę.

### AI

Cztery poziomy trudności (Primitive / Easy / Normal / Hard), jeden model implementacji: `PrimitiveAIModel`.
Pipeline AI: ocena sytuacji (pressures 0-1 na 8 osiach) → wybór planu strategicznego → generowanie komend.
Osie: Resources, Logistics, Military, Expansion, InternalDevelopment, Technology, Diplomacy, Risk.
Plany: RecoverEconomy, FixLogistics, BuildArmy, DefendBorder, PrepareOffensive, ExpandForResources, DevelopPopulation, ResearchSpecialization, ConsolidateTerritory.
Każde AI ma `AIPersonality` (11 cech float) wpływającą na progi i utility.

Szczegółowy design AI w `docs/strategic_ai_design.md`.

### Balans i modyfikatory

Parametry budynków data-driven z `assets/data/buildings.rtsdata`.
Technologie i focusy z `assets/data/technologies.rtsdata` i `assets/data/focuses.rtsdata`.
Modyfikatory balance przez `BalanceModifierSet` na `Player` — addytywne + multiplikatywne, z zakresem Global / Building / Area / Territory. Formula: `(base + additive) * multiplier`. Szczegóły w `docs/balance_audit.md`.

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
  ├─ Player          ← ekonomia, wojsko, technologie, focusy, telemetria
  │    └─ Building*  ← hierarchia: Building > ProductionBuilding/StorageBuilding/MilitaryBuilding
  └─ IController     ← LocalController | AIController | RemoteController

IGameSession         ← abstrakcja nad pętlą symulacji
  ├─ HostSession       ← autorytatywny host (SP + MP); background thread startuje w konstruktorze
  └─ ClientSession     ← klient MP (mirror + resync przez snapshot); transport abstrakcyjny (Localhost/TCP)

Sceny (Scenes.h/cpp)
  └─ GameScene       ← aktywna sesja + GuiController
       └─ GuiController → GuiSystem (BasicMapView / Build / Road / Destroy / Stats / Focus)
```

### Kluczowe wzorce

**Lockstep deterministyczny:** cała mutacja stanu gry wyłącznie przez `GameCommand` → `SubmitCommand` → `ProcessCommands` na fixed-tick 100 Hz. Nigdy nie mutuj stanu symulacji bezpośrednio z UI.

**Fixed-tick clock:** `FixedSimulationClock::FixedDt = 1/100s`, max 12 ticków na ramkę. Akumulacja desyncu wykrywana checksumem co 1 s; klient wysyła `RESYNC_REQUEST`, host odpowiada snapshotem.

**ResourcePool:** fixed-size pool zamiast heap allocations per resource. `ResourceType::Null = 255` — sprawdzaj ten sentinel przed użyciem pointera.

**`std::map` w ścieżce symulacji** — świadomy wybór dla deterministycznej kolejności iteracji. Nie zamieniać na `unordered_map` w kodzie wpływającym na logikę gry.

## Struktura katalogów

```
inc/            ← nagłówki (prawie cały interfejs publiczny)
src/            ← implementacje; GameWorld rozbity na partial TU:
                   GameWorld.cpp / .Commands / .Init / .Persistence / .Render / .TileMap / .Checksum
tests/          ← Google Test, suite: Building, GameCommand, RoadNetwork, TileMap, Technology, Economy...
deps/           ← zależności (raylib-src/, raylib/ gitignored; raygui/ committed)
cmake/          ← Version.h.in (szablon nagłówka wersji)
scripts/        ← bump_version.ps1
docs/           ← tech_debt.md (audyt architektoniczny), balance_audit.md, strategic_ai_design.md
assets/         ← tekstury, fonty, dane (.rtsdata)
```

## Typy budynków (BuildingType)

Produkcja: `Woodcutter`, `HuntersHut`, `LumberMill`, `Mine`, `Foundry`, `Well`, `WheatFarm`, `Windmill`, `Bakery`, `Inn`, `Paperworks`, `Smith`, `University`
Militarne: `GuardTower`, `Fortress`, `Castle`, `Barracks`
Inne: `Headquarters`, `Village`, `StorageBuilding`, `Road`

## Zasoby (ResourceType)

Surowce: `WOOD`, `COAL`, `IRON_ORE`, `STONE` (typy terenu też)
Przetworzone: `PLANKS`, `IRON`, `FLOUR`, `BREAD`, `MEAT`, `WATER`, `BEER`, `PAPER`, `TOOLS`, `COINS`
Militarne: `FOOD_PROVISIONS`, `WEAPON_SUPPLY`, `COPPER_SWORD`, `IRON_SWORD`, `STEEL_SWORD`, `BOW`, `ARROWS`

## Serializacja

- **Sieciowa:** `GameCommand::Serialize/TryDeserialize` — `WireVersion = 4`, pozycyjna.
- **Zapis:** `GameWorld::SaveToFile/LoadFromFile` (`GameWorld.Persistence.cpp`) — tekstowy format `RTS_SAVE`, aktualny save version = 13.
- **Snapshot MP:** `GameSnapshot::Serialize/TryDeserialize` — chunki 12 KB przez TCP przy join.

Przy zmianie formatu serializacji: inkrementuj odpowiedni `WireVersion` / save version.

## CI (GitHub Actions)

`.github/workflows/windows-release.yml`:
- Triggeruje na każdy push i PR.
- Instaluje raylib przez vcpkg (`x64-windows`), używa `deps/raygui/raygui.h` z repo.
- Uruchamia `--gtest_filter=GameCommandTests.*` (pozostałe testy kompilują się, ale nie są uruchamiane — dług techniczny).
- Release tworzony tylko gdy `VERSION` zawiera wersję bez istniejącego tagu.

## Mapa ficzerów → pliki

Ściąga: gdzie szukać zanim zaczniesz skanować. Format `inc/` = deklaracja/interfejs, `src/` = implementacja.

### Symulacja i pętla gry
| Ficer | Gdzie szukać |
|---|---|
| Fixed-tick, akumulator, UpdateSimulation | `inc/GameSession.h` — `FixedSimulationClock`, `HostSession` (background thread) |
| Główna pętla Update (kamera, render, sim) | `src/GameWorld.Render.cpp` — `GameWorld::Update`, `GameWorld::UpdateSimulation` |
| Inicjalizacja świata, starting base | `src/GameWorld.Init.cpp` — `InitWorld`, `CreateStartingBase` |
| Kolejkowanie i egzekucja komend | `src/GameWorld.Commands.cpp` — `SubmitCommand`, `ProcessCommands`, `ExecuteCommand` |
| Zapis/wczytywanie | `src/GameWorld.Persistence.cpp` (~730 linii) |
| Checksum deterministyczny | `src/GameWorld.Checksum.cpp` |

### Budynki i produkcja
| Ficer | Gdzie szukać |
|---|---|
| Hierarchia klas Building | `inc/Building.h` — `Building`, `ProductionBuilding`, `StorageBuilding`, `MilitaryBuilding` |
| Logika produkcji (cykl, bufory, przepis) | `src/Building.cpp` — `ProductionBuilding::Update`, `Produce`, `MaintainInputRequests` |
| Logistyka (HandleTransport, RequestResource) | `src/Building.cpp` — `HandleTransport`, `ReturnOutgoingResource` |
| Konfiguracja budynków z pliku .rtsdata | `src/BuildingConfig.cpp`, `inc/BuildingConfig.h` |
| Factory do tworzenia budynków z save | `inc/GameWorldInternal.h` — `CreateBuildingFromType` |
| Budynki militarne (garnizon, HP, rozkazy) | `inc/Building.h` — `MilitaryBuilding`, `GuardTower`, `Fortress`, `Castle`, `Barracks` |
| Rekrutacja jednostek | `src/Building.cpp` — `Barracks::Update`, `QueueRecruitment` |
| Stawianie budynku na mapie (walidacja, tile) | `src/GameWorld.TileMap.cpp` — `TileMap::CanPlaceBuilding`, `BuildOnTile` |

### Mapa i kafelki
| Ficer | Gdzie szukać |
|---|---|
| Definicja TileMap i Tile | `inc/MapGenerator.h` (TileMap jest tam) |
| Operacje na TileMap | `src/GameWorld.TileMap.cpp` |
| Generacja mapy i rozmieszczenie zasobów | `src/MapGenerator.cpp`, `inc/MapGenerator.h`, `inc/GameWorldInternal.h` |
| Parametry mapy (rozmiar, seed, density) | `inc/MapGenerator.h` — `MapParameters` |

### Logistyka i drogi
| Ficer | Gdzie szukać |
|---|---|
| Pathfinding, nawigacja po drogach | `inc/RoadNetwork.h` — `NavigationMap`, `RoadNetwork` |
| Transport paczek po drogach | `src/RoadNetwork.cpp` — `RoadNetwork::BeginTransport`, `Transportable::BeginTransport` |
| Pojemność drogi, rezerwacje | `src/RoadNetwork.cpp` — `CanReserveTransportPath`, `CountReservedRoadCapacity` |
| Typ transportowalny | `inc/Transport.h` — `Transportable` |

### Gracz, ekonomia, armia
| Ficer | Gdzie szukać |
|---|---|
| Klasa Player (pełna) | `inc/Player.h` (~770 linii) — Player, HumanPlayer, PlayerDataTracker, PlayerEconomyTelemetry |
| Telemetria zasobów (flow rates) | `inc/Player.h` — `PlayerEconomyTelemetry`, `ResourceFlowSnapshot` |
| Rejestr armii (sumaryczny) | `inc/Player.h` — `ArmyRegistry` |
| Build<T>() — stawianie budynku przez gracza | `inc/Player.h` — template `Player::Build` |
| Modyfikatory balance (tech, state, aury) | `inc/BalanceModifiers.h`, `inc/BalanceStats.h` |

### Technologie i focusy
| Ficer | Gdzie szukać |
|---|---|
| Definicje technologii i focusów | `assets/data/technologies.rtsdata`, `assets/data/focuses.rtsdata` |
| Ładowanie i lookup definicji | `src/Technology.cpp`, `inc/Technology.h` |
| Stan unlock gracza | `inc/Technology.h` — `TechnologyState`, `FocusState` |
| Poziomy rządu (Tribal→Aristocratic) | `inc/StateDevelopment.h` — `StateDevelopment`, `GetStateDevelopmentDefinitions` |

### AI
| Ficer | Gdzie szukać |
|---|---|
| Pipeline AI (strategy → plan → actions) | `src/Controller.cpp` (~1800 linii), `inc/Controller.h` |
| Osobowość, sygnały, plany | `inc/Controller.h` — `AIPersonality`, `AIStrategicPlan`, `AIStrategySnapshot` |
| Diagnoza braków zasobów | `src/Controller.cpp` — `PrimitiveAIModel::DiagnoseResourceNeed` |
| Pathfinding AI do budowania dróg | `src/Controller.cpp` — `SubmitRoadPath`, `FindBuildAnchor` |

### Multiplayer i sieć
| Ficer | Gdzie szukać |
|---|---|
| Interfejs transportu, localhost transport | `inc/GameSession.h` — `IGameTransport`, `LocalhostGameTransport` |
| Host/Client session, resync, snapshot | `inc/GameSession.h` — `HostSession`, `ClientSession` (transport-agnostic) |
| TCP transport (LAN) | `inc/TcpGameTransport.h`, `src/TcpGameTransport.cpp` |
| Serializacja komend i wyników | `inc/GameCommand.h` — `GameCommand::Serialize/TryDeserialize` |
| Snapshot (pełen stan do join) | `inc/GameSnapshot.h` |

### UI i sceny
| Ficer | Gdzie szukać |
|---|---|
| Widżety bazowe (Button, VBox, Panel…) | `inc/Gui.h` — `UiWidget` i pochodne |
| Panel budynku, panel badań | `inc/Gui.h` — `BuildingInfoPanel`, `ResearchPanel` |
| Rdzeń GUI (routing inputu, BasicMapView) | `inc/GuiController.h`, `src/GuiController.cpp` |
| Tryby budowania/niszczenia (build, road, destroy) | `src/GuiBuildModes.cpp` — `BuildGuiSystem`, `RoadBuildSystem`, `DestroyGuiSystem`, `BuildPanelWidget` |
| Widgety wojskowe na mapie (dywizje, bitwy, army bar) | `src/GuiMapWidgets.cpp` — `DivisionMapWidget`, `MilitaryOrderWidget`, `ArmyBarWidget` |
| Panel statystyk, HUD zasobów | `src/GuiHudPanels.cpp` — `StatsPanelWidget`, `StrategicResourceHudWidget`, `StatsGuiSystem` |
| Drzewko focusów i technologii (wspólny widget) | `src/GuiResearchTree.cpp` — `ResearchTreePanelWidget` (`ResearchTreeKind`), `FocusGuiSystem`, `TechGuiSystem` |
| Helpery wspólne GUI (kamera, przyciski HUD) | `src/GuiInternal.h`, `src/GuiCommon.cpp` — `DispatchHudButtonClick`, `WireCommonSystemActions` |
| Sceny (MainMenu, GameScene, Multiplayer…) | `inc/Scenes.h`, `src/Scenes.cpp` (~2250 linii) |
| Sesja w GameScene, runtime loop | `src/Scenes.cpp` — `IGameRuntimeLoop`, klasy `HostRuntimeLoop` (SP+MP host), `MultiplayerClientRuntimeLoop` |
| Renderowanie mapy, tekstury, atlasy | `src/Renderer.cpp`, `inc/Renderer.h` |

### Zasoby (typy, pool, bufory)
| Ficer | Gdzie szukać |
|---|---|
| Enum ResourceType, rt2s() | `inc/Resource.h` |
| ResourcePool (fixed allocator) | `inc/Resource.h` — `ResourcePool`, `ResourceBuffer` |

## Znane długi techniczne

Pełna lista w `docs/tech_debt.md`. Najważniejsze:
1. **`Building` fat interface** — ~40 wirtualnych metod w bazie, kompozycja byłaby lepsza.
2. **Ręczna serializacja** — zduplikowana w 5+ miejscach, brak schematu.
3. **Snapshoty TCP nie skalują się** dla dużych map (używane do recovery desyncu).
4. **`Player` zbliża się do god-object** — 769 linii, 13 includów.
5. **CI nie uruchamia pełnego suite testów** (tylko GameCommandTests).

## Ważne szczegóły

- `ResourceType::GOLD` koliduje z makro raylib `GOLD` — rozwiązane przez `#undef GOLD` w `Resource.h`.
- `DrawRectangleRoundedLines` — spatchowane do API raylib 5.0 (dodany param `lineThick` przed `Color`).
- Save pliki lądują w `saves/` (gitignored).
- Logi w `logs/rts.log` (gitignored).
- `TileMap` zdefiniowany w `inc/MapGenerator.h` i `inc/Player.h` (forward declare).
