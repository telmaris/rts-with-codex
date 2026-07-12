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

- [x] **`GameWorld::SaveToFile`/`LoadFromFile` było de facto złamane dla KAŻDEGO budynku
  produkcyjnego** (`src/core/GameWorld.Persistence.cpp`). Odkryte 2026-07-12 (audyt TD etap-4,
  pierwszy w historii projektu test faktycznie robiący pełny round-trip save→load na świecie
  z aktywnym budynkiem produkcyjnym — dlatego nikt tego wcześniej nie złapał). Dwa niezależne bugi
  w tym samym bloku "PROD":
  1. Blok wymagał `ResearchComponent != nullptr` dla KAŻDEGO budynku z `ProductionComponent`,
     a ResearchComponent ma tylko `University`. `SaveToFile` zwracał `false` (ciche niepowodzenie
     zapisu) dla dowolnej gry z Woodcutterem/LumberMillem/itd. Naprawione: `research` jest teraz
     opcjonalny, przy `nullptr` zapisywane/wczytywane są wartości domyślne (kształt wire'a bez zmian).
  2. Po naprawie #1 wyszedł drugi, głębszy bug: sekcja `WORKERS` przy odczycie robiła
     `in >> tag >> count;` (generyczny "peek" łapiący PIERWSZĄ wartość payloadu WORKERS do `count`),
     a potem WEWNĄTRZ brancha `if (tag == "WORKERS")` robiła `in >> workers->capacity >> workers->assigned;`
     — czyli czytała DWIE NOWE wartości zamiast użyć już wczytanego `count` jako capacity i doczytać
     tylko `assigned`. To przesuwało cały strumień o jeden token dla KAŻDEGO budynku produkcyjnego,
     psując parsowanie RECIPE/RESEARCH/INGREDIENTS w kaskadzie. Naprawione: `workers->capacity = count;`
     + `in >> workers->assigned;`.
  **Wniosek:** save/load nigdy nie miało realnego pokrycia testowego dla świata z produkcją —
  wszystkie dotychczasowe testy save/load (ETAP 2/3/4) budowały światy bez żadnego budynku
  produkcyjnego (tylko HQ/Village/Road/Barracks). Warto rozważyć jeden stały test integracyjny
  save/load na "pełnej" ekonomii (co najmniej jeden Woodcutter) jako regresję na przyszłość —
  niezrobione teraz, poza zakresem etap-4.

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
  **Odkryte 2026-07-12 (weryfikacja TD etap-5):** ta sama luka teraz obejmuje też walkę drogową —
  `CombatResolver::ResolveDamage` (`src/warfare/CombatPipeline.cpp`) seeduje RNG z
  `(worldSeed, simulationTick, sourceUnitInstanceId)`, więc odtwarza się identycznie TYLKO jeśli
  host i klient mają dokładnie ten sam `simulationTick` w momencie rozwiązania ataku — co lockstep
  faktycznie gwarantuje (zweryfikowano ręcznym testem: 45 s realnej walki przez prawdziwy
  `LocalhostGameTransport`, checksumy identyczne). Ale gdyby kiedyś DOSZŁO do prawdziwego desyncu
  w trakcie walki, ten sam brakujący `LoadFromSnapshot` oznacza, że klient nie ma jak się realnie
  zresynchronizować mid-fight — rozjedzie się na stałe, tak samo jak dla stanu ekonomicznego.
  Nie blokuje etap-5 (determinizm w normalnych warunkach potwierdzony), ale podnosi priorytet
  naprawy `LoadFromSnapshot`, gdy walka drogowa stanie się głównym trybem rozgrywki MP.

- [ ] **Przejęte budynki (TD etap-6.3) nie są rejestrowane w sieci dróg zwycięzcy**
  (`src/core/GameWorld.Elimination.cpp`, sekcja "Production buildings change hands"). `EliminatePlayer`
  przepina `building->owner` i wywołuje `RegisterBuilding`/`UnregisterBuilding` (dataTracker + rejestry
  strategiczne), ale NIE dotyka `Player::roadNetwork`/`NavigationMap` — ani usunięcia budynku z
  `roadNetwork` pokonanego, ani dodania do `roadNetwork` zwycięzcy. Skutek: budynek dalej działa
  (własna produkcja, istniejące połączenia supplier/receiver to surowe wskaźniki, obojętne na
  ownera) i istniejące połączenia z INNYMI przejętymi budynkami też działają — ale zwycięzca NIE
  MOŻE poprowadzić NOWEJ drogi surowcowej do/z przejętego budynku dopóki nie zostanie ręcznie
  zarejestrowany. Świadomie odłożone (poza zakresem etap-6, wymaga `NavigationMap` removal API,
  którego dziś nie ma — tylko `UpdateNavMap` do dodawania/aktualizacji). Naprawić przy okazji
  ETAP 7/8 jeśli gracze faktycznie zaczną łączyć przejęte budynki nowymi drogami.

- [ ] **"Ostatnia jednostka oblegająca" (TD etap-6.2, propozycja planu) uproszczona do "najniższe
  instanceId"** (`src/warfare/UnitCombatSystem.cpp`, `FindBesiegerOpponent`). Plan proponuje, żeby
  świeży obrońca walczył z OSTATNIĄ (najnowszą) jednostką oblegającą HQ; zaimplementowano zamiast
  tego deterministyczny, ale PROSTSZY wybór: najniższe `instanceId` wśród oblegających (ten sam
  tie-break co reszta `UnitCombatSystem`). Świadoma decyzja — dokładna tożsamość "które oblężenie"
  nie ma dziś żadnego znaczenia dla gracza (brak GUI pokazującego pojedynki), a próba odtworzenia
  DOKŁADNIE "ostatniej" wymagałaby osobnego tie-breaka bez oczywistej korzyści. Do rewizji, jeśli
  ETAP 8 (GUI) kiedykolwiek zacznie wizualizować konkretne pojedynki 1v1 przy bramie.

- [ ] **Wielu oblegających renderuje się dokładnie w tym samym pikselu.** `UnitMarchSystem::
  ComputeWorldPosition` zwraca identyczną pozycję dla KAŻDEJ jednostki na ostatnim kaflu (świadomie
  poluzowany single-file z etap-6.2, żeby dało się grupować atak na HQ) — placeholder-prostokąty
  wszystkich oblegających nakładają się wizualnie. Nieistotne dopóki grafika to płaski prostokąt
  (obecny placeholder), ale do rozwiązania przy prawdziwych sprite'ach (ETAP 9): rozrzucić pozycje
  w małym okręgu/gridzie wokół bramy zamiast dokładnie nakładać.

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
- [x] **`ResourcePool::GetResource` mógł crashować przy wyczerpaniu puli** (`src/data/Resource.cpp`).
  Odkryte 2026-07-12 (audyt TD etap-3): `static ResourcePool resourcePool;` to jeden globalny
  singleton na CAŁY proces (nie per-`GameWorld`), z `std::array<Resource, N>` o stałym rozmiarze
  per `ResourceType`. `GetResource` wołało `.front()`/`.pop_front()` na wewnętrznym
  `std::deque<Resource*>` bez sprawdzenia `empty()` — przy wyczerpaniu puli dla danego typu
  crash (`Assertion failed: front() called on empty deque`). W normalnej rozgrywce (jedna
  `GameWorld` na proces) rozmiar 10000/typ praktycznie nigdy się nie wyczerpywał, ale
  **cały test suite dzieli jeden proces** — ~100 testów tworzących świeże `GameWorld` (każdy
  z w pełni zaopatrzonym HQ) nigdy nie zwraca wygenerowanych zasobów do puli przy zniszczeniu
  obiektów testowych, więc pula stopniowo się wyczerpuje w trakcie całego przebiegu i **którykolwiek**
  test pod koniec przebiegu może trafić na pusty deque — nie tylko testy używające nowych
  budynków/jednostek. Naprawione: `GetResource` zwraca teraz `nullptr` przy wyczerpaniu (spójne
  z konwencją sentinela `ResourceType::Null` już używaną w kodzie), `ResourceBuffer::GenerateResource`
  po prostu nic nie robi gdy `nullptr`. Rozmiar puli podniesiony 10000→50000/typ jako zapas na
  rosnący test suite (`inc/data/Resource.h`) — to łata objaw, nie źródło: pula wciąż jest jednym
  globalnym singletonem bez resetu między testami, więc przy dalszym wzroście liczby testów
  wyczerpanie może wrócić. Docelowa naprawa (nie zrobiona, poza zakresem etap-3): albo
  `ResourcePool::Reset()` wołany w test fixture między testami, albo pula per-`GameWorld`
  zamiast globalnego singletona.
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

- [ ] **`BattleUnitState::Dying` nigdy nie jest realnie obserwowany** (`src/warfare/UnitCombatSystem.cpp`,
  Pass 2). Plan (`docs/tower_defense_rework_plan.md` 5.2) zakładał stan `Dying` z fade'em po stronie
  renderu; TD etap-5 v1 zamiast tego usuwa martwą jednostkę z `deployedUnits` natychmiast w tym samym
  ticku, w którym HP spada ≤0 — świadome uproszczenie, udokumentowane komentarzem w kodzie, bo bez
  prawdziwego sprite'a (placeholder-prostokąt, decyzja użytkownika z etap-4/5) fade nie miał czego
  animować. Kiedy pojawią się realne tekstury jednostek (plan 4.3 / ETAP 9), rozważyć przywrócenie
  krótkiego okna `Dying` (np. 0.3–0.5 s zanikania alfa) zamiast natychmiastowego usunięcia — obecnie
  brak takiego okna nie psuje żadnej mechaniki (wygrywająca jednostka i tak przechodzi do
  `Marching` dopiero po usunięciu przeciwnika w Pass 2).
- [ ] **CI uruchamia tylko `--gtest_filter=GameCommandTests.*`** — reszta testów (BuildingDomain,
  RoadNetwork, TileMap...) kompiluje się, ale nie jest uruchamiana.
- [ ] **Brak cache vcpkg w CI** → raylib reinstalowany co przebieg.
- [ ] **Brak buildu Linux/macOS** mimo ścieżek UNIX w CMake.
- [ ] **Zduplikowane skrypty** (`*.bat` + `*.ps1`) — `.bat` delegują do `.ps1`, akceptowalne.

- [ ] **TD(etap-7) — pociski wież są "homing", nie ballistyczne z wyprzedzeniem.** Plan
  (`docs/tower_defense_rework_plan.md` 7.2) opisuje pocisk jako "pozycja, prędkość, kierunek" —
  sugerując pojedynczy strzał po ustalonym torze, wymagający obliczenia punktu przechwycenia
  poruszającego się celu. Zaimplementowano zamiast tego (`src/warfare/TowerAttackSystem.cpp`)
  pocisk "homing": co tick koryguje kierunek w stronę AKTUALNEJ pozycji celu — deterministyczne
  (skoro ruch celu jest deterministyczny) i znacznie prostsze niż prawdziwa balistyka z
  wyprzedzeniem, kosztem realizmu wizualnego (strzała "skręca" w locie zamiast lecieć po prostej).
  Świadome uproszczenie odpowiednie dla placeholderowej grafiki; do rewizji, gdy pojawią się
  prawdziwe animacje pocisków.
- [ ] **TD(etap-7) — kolizja pocisku to `CircleShape`, nie `RectShape`/segment jak sugerował plan.**
  Ten sam plik. Plan proponował kształt prostokąt/segment dla strzały; że pocisk jest "homing"
  (zawsze leci wprost na cel), okrąg daje identyczny efekt funkcjonalny bez potrzeby dodawania
  orientacji/rotacji do `ICollisionShape` (którego dziś brak). Do rewizji tylko jeśli pojawi się
  realny powód dla kształtu zależnego od kierunku (np. przyszła zdolność "przebicia" - pierce).
- [ ] **`DefenseTower` to jeden `BuildingType` z jedną wbudowaną `TowerDefinition`, nie osobny
  string-keyed katalog jak `UnitDefinition`.** Plan 7.1 sugerował "typy wież w buildings.rtsdata"
  (wieloliczbowo), co przy jednej klasie C++ implikowałoby raczej mechanizm podobny do
  `unitDefId`/`UnitCatalog`. Zaimplementowano prościej: `TowerDefinition` wbudowana w
  `BuildingDefinition` (wzorem `HqDefinition`/`VillageDefinition`) — wystarczające dla JEDNEGO
  typu wieży (etap-7 nie wymaga więcej, kryteria testowe też nie). Dodanie DRUGIEGO tworu wieży
  później wymaga własnego `BuildingType` + wpisu w `buildings.rtsdata` (analogicznie do
  Woodcutter/Mine) — akceptowalne przy 2-3 typach, ale przy prawdziwym drzewku wież (jak
  jednostek) warto wtedy przejść na string-keyed katalog zamiast mnożyć enum values.
- [ ] **Wieże nie mają HP i nie są niszczalne przez wroga.** Plan 7.1 sam zostawił to pytajnikiem
  ("koszt, HP?, ammoResource..."), a kryteria wyjścia etap-7 nie testują zniszczenia wieży w
  walce — więc zaimplementowano wieże jako statyczne, niezniszczalne przez combat (można je
  usunąć tylko ręczną komendą `DestroyBuilding`, jak każdy inny budynek). Jeśli gameplay ma
  wymagać, by maszerująca kolumna mogła zniszczyć wieżę po drodze, to osobny, nie-trywialny
  dodatek (wieża potrzebowałaby własnego HP/hardDefense jak HQ, i mechanizmu ataku jednostek NA
  budynki poza HQ) — poza zakresem etap-7.
- [ ] **`ARROWS` nie ma dziś żadnego producenta.** Zasób istnieje (magazyny HQ/StorageBuilding/
  DefenseTower go przechowują), ale żaden budynek produkcyjny go nie wytwarza — Smith (który po
  staremu systemie robił BOW/ARROWS) dziś produkuje tylko miecze/narzędzia. Testy integracyjne
  wież (`tests/TowerAttackSystemTests.cpp`) obchodzą to, zasilając magazyn ręcznie
  (`SetStoredAmount`) zamiast przez prawdziwy łańcuch produkcji. Do uzupełnienia w ETAP 9
  (dane/balans) — dodać recipe produkujące ARROWS (Smith? nowy Fletcher?) żeby wieże miały
  realną ścieżkę zaopatrzenia w rozgrywce.

---

## Rekomendowana kolejność spłaty

1. Higiena repo + `.gitignore` — **zrobione 2026-06-27**.
2. Rozszerzyć CI o pełny suite testów (tania, duża wartość).
3. Zdecydować o kompozycji `Building` zanim dojdą kolejne typy budynków (drożeje wykładniczo).
4. Ujednolicić serializację, gdy zaczną dochodzić nowe pola/typy zasobów.
5. Determinizm (fixed-point) i snapshoty — później; działają na prototyp.
