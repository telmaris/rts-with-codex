# Audyt architektury i dług technologiczny

- **Data:** 2026-06-27
- **Wersja:** v0.1.2
- **Zakres:** `inc/`, `src/`, skrypty build, GitHub Actions

Dokument żywy — aktualizować przy spłacie pozycji lub pojawieniu się nowego długu.

---

## Ocena ogólna

Szkielet znacznie powyżej poziomu typowego hobbystycznego RTS. **Rdzeń symulacji ma właściwą
architekturę pod multiplayer** (deterministyczny lockstep sterowany komendami) — najtrudniejszą
rzecz do naprawienia później. Dług koncentruje się w warstwie prezentacji i serializacji, nie
w fundamentach.

---

## Mocne strony (nie ruszać)

1. **Deterministyczna symulacja sterowana komendami.** Mutacja stanu wyłącznie przez
   `GameCommand` → `SubmitCommand` → `ProcessCommands` na stałym ticku (`inc/GameWorld.h`).
   Kanoniczny wzorzec lockstep dla RTS MP.
2. **Abstrakcja sesji** (`inc/GameSession.h`): `IGameSession` z dwoma impl. `HostSession` + `ClientSession`.
   Single player = HostSession bez transportu; to sama ścieżka co host MP.
   Eliminuje klasę bugów "działa w SP, psuje się w MP". Background thread w `HostSession` (nie dekorator).
3. **Fixed-tick z akumulatorem** (100 Hz) + walidacja checksumem co sekundę + resync.
4. **GameWorld rozbity na partial translation units** (`.Commands`, `.Persistence`, `.Render`,
   `.TileMap`, `.Checksum`) — dobre dla czasów kompilacji i nawigacji.
5. **`PlayerDataTracker`** (`inc/Player.h`) — indeksy budynków aktualizowane zdarzeniowo zamiast
   skanowania mapy.

---

## Dług technologiczny (priorytetyzowany)

### 🔴 Wysokie

- [ ] **`Building` to "fat interface" — 40+ metod wirtualnych w bazie** (`inc/Building.h:168-240`).
  `Road`/`Village` nadpisują połowę metod pustymi ciałami. Produkcja, logistyka, militaria,
  konstrukcja i metadane renderowania w jednej hierarchii. Dziedziczenie nie skaluje się przy
  kombinatoryce cech — krytyczne dla hybrydy Factorio+HoI4, gdzie liczba typów bytów eksploduje.
  → **Kierunek: kompozycja zamiast dziedziczenia** (budynek *ma* `ProductionComponent` /
  `LogisticsComponent` / `MilitaryComponent`). Im później, tym drożej.

- [ ] **Ręczna serializacja tekstowa zduplikowana w 5+ miejscach.** `GameCommand`,
  `GameCommandResult`, `GameServerFrame`, `GameSnapshot` + `GameWorld.Persistence.cpp` (31 KB) —
  każde robi pozycyjny `stream <<`/`>>` z ręcznym `WireVersion`. Zmiana kolejności pola psuje
  wire *i* save naraz; format zapisu i sieciowy pisane osobno mimo identycznych danych.
  → Jedna warstwa serializacji; save i wire dzielą definicję.

- [ ] **Pełne snapshoty mapy przez TCP** (`inc/GameSession.h:327-347`), chunki 12 KB. Komentarz
  w kodzie sam ostrzega: "Do not stream full maps over TCP". Recovery desyncu = pełny resync.
  Nie skaluje się dla map w stylu Factorio. Lockstep nie powinien potrzebować snapshotów poza
  join-in-progress — jeśli potrzebuje, determinizm przecieka (patrz niżej).
  → **Odkryte 2026-07-12 (audyt TD etap-1):** `GameSnapshot` (`inc/core/GameSnapshot.h`) jest
  **czysto wizualny** — trzyma tylko `terrainTextureId`/`ownerColor`/`buildingType`+footprint per
  tile, żadnego stanu ekonomicznego (bufory zasobów, postęp produkcji, worker count, tech/focus
  state). `HostSession::SendCorrectionSnapshot()` wysyła go po wykryciu desyncu, ale
  `GameWorld` **nie ma metody `LoadFromSnapshot`** — po stronie klienta payload ląduje tylko
  w `ClientSession::latestNetworkSnapshot`/`ConsumeLatestSnapshot()`, które w `GameScene.cpp`
  służą wyłącznie jako fallback do rysowania mapy (`DrawSnapshot`), nigdy nie są aplikowane z
  powrotem do żywego `observedWorld`. **Realny recovery-after-desync obecnie nie istnieje** —
  gdyby stan hosta i klienta kiedykolwiek faktycznie się rozjechały (nie tylko checksum-noise,
  patrz niżej), klient zostaje rozjechany na stałe. Naprawa = nowy serializowalny format
  ekonomicznego stanu gry (analogiczny do formatu save) + `GameWorld::LoadFromSnapshot(...)` +
  podpięcie w `GameScene`/`ClientSession` + nowy wire/save version — realny, osobny feature,
  nie "brakujące wywołanie". Nie w zakresie ETAP 1 (`docs/tower_defense_rework_plan.md`).
  Sam checksum (`GameWorld::BuildChecksum()`) miał osobny, już naprawiony bug: hashował
  `PlayerDataTracker::buildings` (`std::set<Building*>`, porządek wg adresu wskaźnika) w
  kolejności zależnej od układu sterty procesu — dawało to fałszywe alarmy desyncu między
  niezależnie zaalokowanymi światami host/client nawet przy identycznym stanie gry. Naprawione
  2026-07-12: sortowanie po `building->id` przed hashowaniem (`src/core/GameWorld.Checksum.cpp`).

### 🟡 Średnie

- [ ] **`GameWorld` rozpierdol — audyt i segregacja odpowiedzialności**. Obecnie rozbity na:
  - `GameWorld.cpp` (główny plik)
  - `GameWorld.Commands.cpp` — przetwarzanie komend
  - `GameWorld.Init.cpp` — inicjalizacja
  - `GameWorld.Persistence.cpp` — serializacja (31 KB)
  - `GameWorld.Render.cpp` — PROBLEM: zawiera `Update()`, `UpdateSimulation()`, `ResupplyDeployedDivisions()` (logika symulacji, nie rendering)
  - `GameWorld.TileMap.cpp` — operacje na mapie
  - `GameWorld.Checksum.cpp` — walidacja
  
  **Problem:** `.Render.cpp` zawiera czystą logikę symulacji zamiast renderingu. Powinna być:
  - `GameWorld.SimulationLogic.cpp` (nowy) — `Update()`, tick dispatch, logika podsystemów
  - `GameWorld.Render.cpp` — TYLKO `DrawMap()` i rendering
  
  → Wymaga pełnego audytu: co jest gdzie, dlaczego, czy dział odpowiedzialności są jasne.

- [ ] **`Player` staje się god-objectem** — 769 linii nagłówka, ~150 składowych, 13 includów
  (`inc/Player.h`). Akumuluje ekonomię, armię, technologie, focusy, telemetrię, rejestr budynków.
- [ ] **Brak enkapsulacji stanu symulacji.** `GameWorld` wystawia `tilemap`/`playerHandler` jako
  `public`; `Building` ma wszystkie pola publiczne. Ryzyko: UI może mutować stan poza ścieżką
  komend, łamiąc determinizm.
- [ ] **Duplikacja w hierarchii `GuiSystem`** (`inc/GuiController.h:262-477`). Każdy system
  redeklaruje ten sam zestaw handlerów (`EscPressed`, `BuildPressed`, ...) — 5× boilerplate.
  → Domyślne no-op w bazie.
- [ ] **Determinizm na `double`/`float`.** Akumulacja floatów różni się między
  platformami/kompilatorami — stąd checksum+resync. Centralne ryzyko lockstepu dla cross-platform
  MP. → Rozważyć fixed-point dla wielkości wpływających na symulację. Trzymać się `std::map`
  (uporządkowany) zamiast `unordered_map` w ścieżce symulacji.

### 🟢 Niskie / higiena

- [x] **`.gitignore` nie pokrywał `build-tests/`, `build-tests-coverage/`** (wygenerowany HTML
  pokrycia leżał w drzewie). Naprawione 2026-06-27.
- [ ] **CI uruchamia tylko `--gtest_filter=GameCommandTests.*`** — reszta testów (BuildingDomain,
  RoadNetwork, TileMap...) kompiluje się, ale nie jest uruchamiana.
- [ ] **Brak cache vcpkg w CI** → raylib reinstalowany co przebieg.
- [ ] **Brak buildu Linux/macOS** mimo ścieżek UNIX w CMake.
- [ ] **Zduplikowane skrypty** (`*.bat` + `*.ps1`) — `.bat` delegują do `.ps1`, akceptowalne.

---

## Rekomendowana kolejność spłaty

1. Higiena repo + `.gitignore` — **zrobione 2026-06-27**.
2. Rozszerzyć CI o pełny suite testów (tania, duża wartość).
3. Zdecydować o kompozycji `Building` zanim dojdą kolejne typy budynków (drożeje wykładniczo).
4. Ujednolicić serializację, gdy zaczną dochodzić nowe pola/typy zasobów.
5. Determinizm (fixed-point) i snapshoty — później; działają na prototyp.
