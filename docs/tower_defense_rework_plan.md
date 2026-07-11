# Plan reworku: Factorio × Tower Defense

> **Dokument wykonawczy dla agenta.** Źródło wymagań: `TODO.md` (sekcja "TRANSFORMACJA W GRE TOWER DEFENSE") + decyzje użytkownika z sesji planowania (2026-07-11), wpisane niżej jako [DECYZJA].
> W razie **jakiejkolwiek** wątpliwości interpretacyjnej — zapytaj użytkownika. Podejście maksymalnie konserwatywne: architektura bazowa ma być zaprojektowana solidnie, bo na niej stanie cała gra.

---

## 0. Zasady nadrzędne (obowiązują w każdym etapie)

1. **Lockstep / multiplayer-first.** Każda mutacja stanu symulacji WYŁĄCZNIE przez `GameCommand` → `SubmitCommand` → `ProcessCommands` (fixed-tick 100 Hz). Deploy jednostek, rekrutacja, budowa wież — wszystko command-based. [DECYZJA: MP jest modelem domyślnym.]
2. **Determinizm.** `std::map` (nie `unordered_map`) w ścieżce symulacji; iteracje po kontenerach w deterministycznej kolejności; ID instancji z liczników per-świat. RNG bojowy: `std::mt19937` seedowany z `(world seed ^ tick ^ id jednostki)` — nigdy `rand()`, nigdy stan RNG poza symulacją. Nowy stan symulacji (jednostki, pociski, HP HQ, ramp produktywności) wchodzi do `GameWorld.Checksum.cpp` i do `GameSnapshot`.
3. **Serializacja.** Każdy etap zmieniający format: bump `GameCommand::WireVersion` (obecnie 9), save version w `GameWorld.Persistence.cpp` (obecnie 13), wersja snapshotu. Stare save'y NIE muszą być kompatybilne (breaking change jest OK), ale loader ma czysto odrzucić starą wersję, nie crashować.
4. **Data-driven.** Typy jednostek, wież, parametry HQ — z plików `.rtsdata` (`assets/data/`), wzorem `buildings.rtsdata`. Zero hardkodowanych statystyk w C++.
5. **Modyfikatory.** Każdy nowy parametr liczbowy (staty jednostek, wież, HQ) przechodzi przez `BalanceModifierSet` / `Stat` (`inc/economy/BalanceModifiers.h`, `BalanceStats.h`, `inc/core/Stat.h`), żeby tech tree / focusy / buffy lokalne mogły je modyfikować. Formuła jak dotąd: `(base + additive) * multiplier`.
6. **OOP, modern C++20.** Interfejsy + kompozycja, strategy pattern tam gdzie wskazano. Zakaz `goto`, zakaz proceduralnych "sprytnych" skrótów. Rozszerzalność projektowana jawnie (patrz sekcje "szwy rozszerzalności") — myślimy o przyszłym DLC.
7. **Testy.** Po każdym etapie: `.\run_tests.ps1` zielone + build przechodzi (`.\build_and_run.ps1` jako smoke test). Nowe systemy dostają testy GTest równolegle z kodem (wzorzec harness z `WarSystemTests` — observer + trace — był skuteczny, powielić dla nowej walki). GUI nie ma pokrycia automatycznego — zmiany GUI weryfikować ręcznie i zachowawczo.
8. **Commity per etap**, prefix `TD(etap-N)`, np. `TD(etap-3): BattleUnit architecture + unit catalog`.
9. **Encoding:** przy edycji plików przez PowerShell pinować UTF-8 bez BOM (znany problem manglingu — patrz pamięć projektu).

---

## 1. Decyzje użytkownika już podjęte [DECYZJA]

| Temat | Decyzja |
|---|---|
| Deploy | Jednostki wchodzą na drogę wojskową **przy własnym HQ**. Gracz wybiera trasę (którego sąsiada atakuje) — wybór targetu z poziomu GUI grup ataku (możliwy równoległy atak na 2 graczy). W 1vs1 target automatyczny (jedna trasa). |
| Zachowanie po deployu | Jednostki **zawsze maszerują** na wrogie HQ. Brak rally pointów / hold. Obrona = przechwycenie kolumny wroga na drodze + wieże. |
| Amunicja wież | Zwykły zasób transportowany po **istniejących drogach surowcowych** (wieża = normalny odbiorca, podpięta do sieci dróg). Zużycie: **1 szt. amunicji / atak**. Brak amunicji = wieża nie strzela. |
| System supply | `SupplyPackage`/`SupplyHub`/`WEAPON_SUPPLY` — **usunąć**. `FOOD_PROVISIONS` **zostaje** (zasila Village → manpower). |
| Manpower | System zostaje: obsadza budynki produkcyjne, wieże obronne i rekrutację (jednostka kosztuje surowce + manpower). Manpower NIE jest transportowany — odejmowany z globalnej puli. |
| AI | Ekonomia gra dalej; osie/plany militarne **stub** (kompiluje się, nie crashuje, nic nie robi militarnie). Pełny rework AI = osobny projekt poza tym planem. |
| Walka | Model uproszczony: HP, atak, armor, attack speed + szczypta deterministycznego RNG do rozstrzygania równych starć. Pilotażowa formuła obrażeń: `max(1, atak - armor)` (floor 1, do balansu później). |
| Wieże | Footprint **2×2**, placeholder: tekstura Guard Tower. |
| Koniec gry | HP HQ = 0 → gracz odpada. Zwycięzca przejmuje budynki **produkcyjne** z rampem produktywności od ~30% do 100% w czasie (anty-snowball, wzór: compliance z HOI4). |
| Los jednostek pokonanego | Zdeployowane jednostki pokonanego gracza **znikają** (symulacyjnie natychmiast; graficznie fade-out). |
| Przejęcie zasobów | **Bufory przejętych budynków zostają nienaruszone.** Stan magazynowy (magazyny/HQ) pokonanego gracza jest prawie czyszczony — zwycięzca przejmuje **~20% materiałów** (parametr w danych), reszta przepada. Anty-snowball. |
| Spójna mechanika ataku | Atak jednostek na jednostki ORAZ na HQ realizowany przez **emisję colliderów** — ta sama infrastruktura co pociski wież. Jeden pipeline: emisja (collider + payload obrażeń) → kolizja → `CombatResolver`. Otwiera przyszłe jednostki obszarowe i dystansowe. |

## 2. Decyzje do potwierdzenia przed implementacją danego etapu

Agent: przy dojściu do etapu, w którym dana decyzja jest potrzebna, **zapytaj użytkownika**; w nawiasie propozycja domyślna.

1. **Nazwy statystyk** zamiast soft/hard attack (propozycja: `roadAttack` → walka jednostka-jednostka, `siegeAttack` → obrażenia zadawane HQ; po polsku w GUI: "Atak" / "Atak oblężniczy").
2. **Promień blokady budowania** przy wrogich strukturach: 2 czy 3 kratki (propozycja: 3, jako stała konfigurowalna w danych).
3. **Czy pokonany gracz zostaje obserwatorem** (propozycja: tak, kamera wolna, brak komend). (Los jego jednostek już rozstrzygnięty: znikają z fade-outem — sekcja 1.)
4. **Typ amunicji per wieża** — mapowanie zasób→wieża (propozycja: startowa wieża strzelecka zużywa `ARROWS`; kolejne typy definiowane w danych).
5. **Obsada wieży manpowerem** — ile i czy wraca po zniszczeniu wieży (propozycja: jak w budynkach produkcyjnych — pula workers; wraca).
6. **Ile jednostek może zgrupować się na kratce przy ataku na HQ** (propozycja: bez limitu w v1; limit jako parametr do balansu).
7. **Fortress/Castle** — usuwamy typy całkowicie czy zostawiamy `BuildingType` wartości zarezerwowane na przyszłe tiery wież (propozycja: usunąć klasy, wartości enum zostawić jako zarezerwowane/komentarz).

---

## 3. Mapa etapów (kolejność wykonania)

```
ETAP 0  goto-ektomia GameSession                      (mały, niezależny)
ETAP 1  Wycinka starego systemu wojny + reguła bliskości budowania
ETAP 2  Generator mapy: immutable droga wojskowa + MilitaryRoadNetwork
ETAP 3  Architektura BattleUnit + katalog jednostek + rekrutacja + roster
ETAP 4  Deploy (command) + ruch kolumn po drodze + rendering jednostek
ETAP 5  Wspólny pipeline ataku (collidery) + walka jednostka-vs-jednostka
ETAP 6  HqComponent: atak na HQ, eliminacja, przejęcie ekonomii (ramp)
ETAP 7  Wieże obronne: TowerComponent, strategie ataku, pociski, amunicja
ETAP 8  GUI: panel rosteru/deployu, panel wieży, panel HQ, sprzątanie HUD
ETAP 9  Dane, balans, dokumentacja, sprzątanie końcowe
```

Etapy wykonywać **sekwencyjnie**. Po każdym: build + testy + commit. Gra ma być uruchamialna po każdym etapie (po ETAP 1 — bez wojska w ogóle; to akceptowalny stan przejściowy).

---

## ETAP 0 — Usunięcie `goto` z `HostSession::RunSimulation`

**Plik:** `src/core/GameSession.cpp` (linie ~207 i ~264: `goto sleep_and_wait`).

Refaktor pętli na strukturalny przepływ: wyodrębnić ciało iteracji do prywatnej metody (np. `bool HostSession::RunSimulationTick()` — sekcje: odbiór komend transportu, gate na initial sync, update symulacji, wysyłka frame'ów), która robi wczesny `return`, a pętla `while (running)` zawsze kończy iterację wspólną sekcją sleep/wait. Zero zmian zachowania — czysty refaktor.

**Kryteria:** brak `goto` w repo (`grep -rn "goto" src inc` → 0 trafień); testy zielone; MP localhost działa (smoke: host+klient w lobby).

---

## ETAP 1 — Wycinka starego systemu wojny + reguła bliskości

Cel: usunąć WSZYSTKO związane z ruchem armii po mapie, rozkazami, bitwami, terytorium i supply (poza food). Po tym etapie projekt jest "czystą grą ekonomiczną" i kompiluje się bez trupów.

### 1.1 Inwentarz usunięć

**Moduł warfare (całe pliki):**
- `inc/warfare/`: `ArmyGroup.h`, `ArmyOrder.h`, `Battle.h`, `Division.h`, `DivisionSector.h`, `MovementPlanner.h`, `Equipment.h`, `UnitStats.h`, `Warfare.h`
- `src/warfare/`: wszystkie odpowiadające `.cpp`
- (Katalog `warfare/` zostaje — ETAP 3 zapełni go nową architekturą. `MilitaryUnitType` z `Division.h` znika; nowy katalog jednostek będzie data-driven, patrz ETAP 3.)

**Core:**
- `src/core/GameWorld.Battles.cpp` — cały plik + rejestry bitew/dywizji w `GameWorld.h`/`GameWorldInternal.h`
- `GameCommand`: usunąć typy `IssueMilitaryOrder`, `MoveDivision`, `FormArmy`, `AttackTile`, `AssignToArmy` + ich fabryki/serializację; `RecruitUnit` zostaje jako nazwa, ale ciało wymienia ETAP 3 (na razie może zostać no-op albo usunięte i przywrócone w E3 — wybrać mniejsze zło kompilacyjne). **Bump WireVersion.**
- `GameWorld.Persistence.cpp`: wyciąć serializację armii/bitew/garnizonów/supply. **Bump save version.**
- `GameWorld.Checksum.cpp`: wyciąć wkład usuwanych systemów.

**Economy:**
- `inc/economy/ArmyRegistry.h` + użycia w `Player`
- `src/economy/GarrisonComponent.cpp`, `SupplyBufferComponent.cpp`, `SupplyPackageComponent.cpp`, `TerritoryComponent.cpp` + ich deklaracje w `BuildingComponents.h`
- `inc/economy/SupplyPackage.h`, `SupplyTransport.h`
- `inc/data/StrategicResource.h` + `WEAPON_SUPPLY` z `ResourceType` (**`FOOD_PROVISIONS` zostaje** — pipeline Village/manpower nietknięty: `PopulationComponent.cpp` działa jak dziś)
- `Building.h`: klasy `GuardTower`, `Fortress`, `Castle` (stary sens — projekcja terytorium), `SupplyHub`; `Barracks` zostaje (siedziba rekrutacji, rework w E3). `MilitaryBuilding` jako kategoria — usunąć lub zdegradować (decyzja przy implementacji: nowa kategoria wież przyjdzie w E7).
- Wszystko z `territoryRadius` / projekcją terytorium: `TileMap` (ownership kafelków przez terytorium), `PathingService::Domain::Territory/TerritoryUnion` (zostawić `Global`, resztę usunąć lub zredukować).
- `RecruitmentComponent.cpp` — zostaje plik, ale treść wymienia E3; jeśli nie kompiluje się bez Division — wyciąć teraz i odtworzyć w E3.

**AI (`src/ai/Controller.cpp`, `inc/ai/Controller.h`):**
- Stub: plany militarne (BuildArmy, DefendBorder, PrepareOffensive, ConsolidateTerritory, ExpandForResources w części terytorialnej), osie Military/Expansion, generowanie komend militarnych — wyłączyć tak, żeby AI grało wyłącznie ekonomicznie i się nie wywracało. Nie przebudowywać — tylko odciąć. Oznaczyć `// TODO(td-rework): AI military overhaul`.

**GUI (`src/ui/GuiMapWidgets.cpp` ~1300 linii, `GuiHudPanels.cpp`, `Gui.cpp`):**
- Usunąć `DivisionMapWidget`, `MilitaryOrderWidget`, `ArmyBarWidget` i odwołania z HUD; usunąć elementy panelu budynku dot. garnizonu/supply. Zachowawczo: minimalna ingerencja, bez przebudowy layoutu.

**Testy:**
- Usunąć: `WarSystemTests.cpp` (3111 linii), `StrategicResourceTests.cpp`; z pozostałych suite'ów wyciąć przypadki dot. terytorium/garnizonu/supply (przejrzeć `BuildingDomainTests`, `BuildingPlacementTests`, `GameCommandTests`, `PlayerEconomyTests`).
- Zaktualizować `tests/CMakeLists.txt`.

**Dane:**
- `assets/data/buildings.rtsdata`: wpisy GuardTower/Fortress/Castle/SupplyHub — usunąć lub oznaczyć nieaktywne; `technologies.rtsdata` / `focuses.rtsdata`: usunąć modyfikatory odwołujące się do usuniętych statów (parser nie może się wywalać na sierotach).

### 1.2 Reguła bliskości budowania (zamiennik terytorium)

Terytorium znika, ale ownership budynków i dróg MUSI być pilnowany — niedopuszczalne łączenie sieci dróg z wrogiem.

- W `TileMap::CanPlaceBuilding` (`src/core/GameWorld.TileMap.cpp`): nowa walidacja — budowa (budynek lub droga) niedozwolona, jeśli w promieniu **R kratek** (patrz decyzja #2; liczony od geometrii footprintu dla budynków, od kafelka dla dróg) znajduje się **wroga** budowla lub wroga droga. Dotyczy obu stron — automatycznie zapobiega także dobudowaniu się wroga do naszej sieci.
- Droga wojskowa (E2) jest neutralna — nie blokuje niczyjego budowania, ale na jej kafelkach nie wolno budować.
- Usunąć warunek "można budować tylko we własnym terytorium" — od teraz buduje się wszędzie, gdzie nie łamie reguły bliskości (+ dotychczasowe walidacje terenu).

**Kryteria etapu:** kompilacja czysta; wszystkie pozostawione testy zielone; nowe testy reguły bliskości (placement obok wroga odrzucony, swój OK, granica promienia dokładna); gra uruchamia się, ekonomia + budowa + transport działają; AI buduje ekonomię i nie crashuje; MP localhost bez desync przez ≥5 min symulacji.

---

## ETAP 2 — Droga wojskowa: generator + `MilitaryRoadNetwork`

### 2.1 Model sieci

Nowa klasa `MilitaryRoadNetwork` (`inc/simulation/MilitaryRoadNetwork.h`, `src/simulation/`), analogiczna do `RoadNetwork`, ale:
- **bez pojemności/rezerwacji** — wyłącznie tor ruchu;
- **immutable po generacji** — żadnego API mutującego poza fazą generacji (konstrukcja przez builder/generator, potem const-owy dostęp);
- **rozłączna z siecią surowcową**: kafelek drogi wojskowej nie może być drogą surowcową ani polem budowy i odwrotnie (flaga w `Tile` — nowy typ/flaga kafelka, nie reużywać `BuildingType::Road`);
- topologia: **ring** — każdy gracz połączony trasą ze swoimi 2 sąsiadami (przy 2 graczach: 1 trasa). Trasa = sekwencja kafelków od HQ do HQ.
- Struktura danych: `MilitaryRoute { int playerA, playerB; std::vector<int> tiles; }` + lookup kafelek→trasa. Deterministyczna kolejność tras (sortowanie po id graczy).

### 2.2 Generator (`src/simulation/MapGenerator.cpp`)

- Po rozmieszczeniu HQ (2–5 graczy): wyznaczyć sąsiedztwa (ring wg kąta względem środka mapy — deterministycznie), dla każdej pary sąsiadów wygenerować trasę tym samym algorytmem kosztowym co drogi surowcowe (reużyć logikę kosztów terenu z pathfindera; unikać przecinania depozytów surowców i starting bas).
- Trasy wpisywane do `TileMap` (flagi) + do `MilitaryRoadNetwork`.
- Gwarancja spójności: po generacji walidacja, że każda para sąsiadów jest połączona — jeśli nie, poprawka/retry deterministyczny.

### 2.3 `PathingService` — adaptacja

- Nowe API: `MilitaryPath FindMilitaryPath(int fromHqTile, int toHqTile)` — trasa po kafelkach drogi wojskowej (w praktyce lookup gotowej trasy, ale przez ten interfejs, żeby E6 mógł liczyć trasy **przez** podbite HQ do kolejnych wrogów — konkatenacja tras ringu).
- Walidator startowy: `bool AreHqsConnected(playerA, playerB)`.

### 2.4 Serializacja i render

- Save/snapshot: trasy zapisywane wprost (nie regenerować z seeda — odporność na zmiany generatora). Bump wersji.
- Render (`src/ui/Renderer.cpp`): placeholder — tekstura drogi z odcieniem/tintem (user dostarczy właściwą teksturę później); rejestrowany osobny typ wizualny od początku.

**Kryteria:** testy — generacja dla 2/3/4/5 graczy daje spójny ring; determinizm (ten sam seed → identyczne trasy); zakaz budowy na kafelkach drogi wojskowej; zakaz stawiania drogi surowcowej na wojskowej; save/load zachowuje trasy.

---

## ETAP 3 — Architektura jednostki: `BattleUnit`

Serce reworku. Zaprojektować starannie, z jawnymi szwami rozszerzalności. To fundament pod DLC.

### 3.1 Definicja typu vs instancja

**`UnitDefinition`** (dane, immutable, ładowane z `assets/data/units.rtsdata` — nowy plik, format jak `buildings.rtsdata`):
- `id` (string, np. "swordsman"), `displayName`, `textureId`/`animationSet`
- staty bazowe: `maxHp`, `roadAttack`, `siegeAttack`, `armor`, `moveSpeed`, `attackSpeed`
- szwy: `attackRange` (0 = melee, default), `damageType` (enum, default `Physical`), `resistances` (mapa DamageType→float, default pusta), `movementType` (enum, default `Ground`), `colliderRadius` (koło collidera celu — używane przez pipeline ataku z E5), lista `abilities` (stringi, na razie parser akceptuje i ignoruje)
- rekrutacja: `recruitBuilding` (BuildingType, v1: Barracks), `recruitTime`, `cost` (lista ResourceType+ilość), `manpowerCost`
- `equipmentSlots` (lista kategorii, np. weapon/armor — patrz 3.4)

**`UnitCatalog`** — rejestr definicji, ładowany raz przy starcie świata, deterministyczna kolejność (std::map po id). Walidacja przy ładowaniu (błędne wpisy → log + odrzucenie, nie crash).

**`BattleUnit`** (instancja, `inc/warfare/BattleUnit.h`):
- `unitInstanceId` (z licznika per-świat), `ownerPlayerId`, wskaźnik/id definicji
- stan runtime: `currentHp`, pozycja na trasie (id trasy + indeks kafelka + progres 0..1 wewnątrz kafelka), stan (`InRoster / Marching / FightingUnit / AttackingHq / Dying`), timery ataku, kierunek (do którego HQ maszeruje)
- **staty efektywne liczone przez modyfikatory** (3.2), nie kopiowane na sztywno przy spawnie — buffy z tech tree mają działać także na już zdeployowane jednostki.

Zasada: **kompozycja ponad dziedziczenie**. Jedna klasa `BattleUnit` + dane definicji + (w przyszłości) komponenty zdolności (`IUnitAbility` — interfejs zdefiniować teraz, pusty vector w v1). ŻADNEJ hierarchii `Swordsman : BattleUnit`.

### 3.2 Modyfikatory

Nowe wpisy `BalanceStat`: `UnitHp`, `UnitRoadAttack`, `UnitSiegeAttack`, `UnitArmor`, `UnitMoveSpeed`, `UnitAttackSpeed` (+ w E6/E7: `HqDefense`, `HqThorns`, `TowerDamage`, `TowerRange`, `TowerAttackSpeed`, `TowerAmmoEfficiency` — zarezerwować od razu). Scope Global/Building/Area jak dotąd; docelowo modyfikatory per-typ-jednostki (szew: klucz modyfikatora może zawierać opcjonalny filtr unitDefId — zaprojektować interfejs, implementacja filtra może poczekać, zapytać użytkownika czy w v1).

### 3.3 Rekrutacja i roster

- **`UnitRoster`** (per gracz, w `Player` — zastępuje `ArmyRegistry`): globalna pula zrekrutowanych, niezdeployowanych jednostek. Instancje `BattleUnit` w stanie `InRoster`. Deterministyczna kolejność (std::map po instanceId).
- **`RecruitmentComponent`** (rework istniejącego pliku): kolejka rekrutacji w budynku (Barracks; przyszłe Stables/MageTower/Workshop = tylko nowe wpisy w danych + `recruitBuilding`), zużywa surowce z bufora budynku (dostarczane istniejącym transportem — koszt jednostki to fizyczne zasoby: broń ze Smitha, jedzenie itd.) + manpower z puli gracza w momencie startu rekrutacji. Po czasie `recruitTime` → jednostka trafia do rosteru.
- Komenda `RecruitUnit(playerId, buildingTileId, unitDefId)` — rework istniejącej; walidacja: budynek właściwego typu, stać go, manpower dostępny.

### 3.4 Szew pod ekwipunek (DLC-ready, NIE implementować teraz)

- Koszt jednostki wyrażony w zasobach (np. `IRON_SWORD`) już dziś daje "tiery" przez definicje (swordsman_iron vs swordsman_steel jako osobne wpisy w danych — v1).
- Zaprojektować (tylko interfejs + komentarz doktrynalny): `EquipmentInstance { itemDefId, statModifiers }` i pole `std::vector<EquipmentInstance>` w `BattleUnit` — puste w v1, serializowane jako pusta lista od pierwszej wersji formatu (żeby dodanie DLC nie łamało save'ów w przyszłości).

### 3.5 Serializacja / checksum

Roster + liczniki instancji do save/snapshot/checksum. Bump wersji.

**Kryteria:** testy — ładowanie katalogu (poprawne + błędne pliki), rekrutacja end-to-end (zasoby+manpower zużyte, jednostka w rosterze po czasie), modyfikator z tech tree zmienia staty efektywne (w tym już istniejącej instancji), determinizm id, save/load rosteru.

---

## ETAP 4 — Deploy i ruch kolumn

### 4.1 Komenda

`DeployUnits(playerId, targetPlayerId, orderedUnitInstanceIds)`:
- walidacja: jednostki w rosterze, `targetPlayerId` to żywy sąsiad na ringu (lub dalszy wróg, jeśli trasa przechodzi przez podbite HQ — E6), trasa istnieje;
- w 1vs1 GUI może auto-uzupełniać target, ale komenda ZAWSZE niesie jawny target (determinizm, MP);
- kolejność listy = kolejność w kolumnie (szpica pierwsza). To realizuje strategiczne ustawianie składu [DECYZJA].

### 4.2 System ruchu — `UnitMarchSystem`

Nowa klasa (np. `inc/warfare/UnitMarchSystem.h`, update wołany z `GameWorld::UpdateSimulation`; nowy partial `src/core/GameWorld.Units.cpp` tylko deleguje):
- jednostki spawnowane sekwencyjnie na kafelku trasy przy własnym HQ (jeśli zajęty — czekają w kolejce spawnu);
- marsz po kafelkach trasy zgodnie z `moveSpeed` (progres per tick = moveSpeed * FixedDt, deterministycznie);
- **kolejkowanie**: jednostka nie może wejść na pozycję bliżej niż `spacing` za poprzednikiem swojej kolumny; gdy szpica stoi (walka), reszta dobija i czeka;
- struktura: `std::map<unitInstanceId, BattleUnit>` jednostek zdeployowanych + indeksy per trasa, kolejność update'u po instanceId.

### 4.3 Render i animacje

- `Renderer`: sprite jednostki na pozycji interpolowanej, stany animacji `Idle/Move/Attack/Death(+fade)` — maszyna stanów animacji per jednostka (render-side, sterowana stanem symulacji; determinizm symulacji NIE zależy od animacji);
- placeholdery tekstur (user dostarczy docelowe).

### 4.4 Serializacja / checksum

Zdeployowane jednostki (pozycja, HP, stan) do save/snapshot/checksum. Bump wersji.

**Kryteria:** testy — deploy zdejmuje z rosteru i spawnuje kolumnę w zadanej kolejności; marsz deterministyczny (N ticków → identyczne pozycje dla tego samego seeda); kolejkowanie za zatrzymaną szpicą; save/load w trakcie marszu; checksum stabilny host/klient (test MP localhost ręczny).

---

## ETAP 5 — Wspólny pipeline ataku (collidery) + walka jednostka-vs-jednostka

### 5.1 Wspólna infrastruktura ataku [DECYZJA: spójna mechanika]

KAŻDY atak w grze (jednostka→jednostka, jednostka→HQ, wieża→jednostka, thorns HQ→jednostki) przechodzi przez jeden pipeline. Definiowany TUTAJ, reużywany w E6 i E7:

- **`ICollisionShape`** — kształty kolizji: v1 `CircleShape`, `RectShape` (segment/prostokąt); interfejs pod przyszłe `ConeShape`, area circular itd.;
- **`AttackEmission`** — krótkotrwały obiekt symulacyjny emitowany przez atakującego: kształt + pozycja/orientacja + payload (`damage`, `damageType`, `sourcePlayerId`, `sourceId`) + filtr celów (wrodzy / wszyscy w obszarze) + czas życia. Melee = contact-collider żyjący 1 tick na pozycji celu; pocisk wieży (E7) = emisja poruszająca się przez wiele ticków; przyszłe AoE/cone = inny kształt, zero zmian w pipeline;
- **cele** mają collidery: `BattleUnit` — koło (promień w definicji jednostki), HQ — footprint budynku (E6);
- **detekcja kolizji** w fixed-tick, deterministyczna kolejność (emisje i cele iterowane po id); trafienie → **`CombatResolver`** — JEDYNE miejsce z formułą obrażeń: v1 `max(1, atak - armor_celu)` (dla HQ: `siegeAttack - hardDefense`), + resistances przy damageType ≠ Physical, + mnożnik wariancji 0.9–1.1 z deterministycznego RNG seedowanego `(worldSeed, tick, sourceId)`;
- emisje wchodzą do checksumu i snapshotu (spójnie z decyzją o pociskach w E7); do save nie muszą (żyją krótko) — udokumentować.

### 5.2 Walka na drodze

- Dwie wrogie kolumny na tej samej trasie: gdy szpice zbliżą się na dystans styku (≤ szerokość kafelka / attackRange), obie przechodzą w stan `FightingUnit`. Walczy WYŁĄCZNIE szpica z szpicą [TODO.md pkt 9]; reszta czeka w kolejce.
- Każda walcząca jednostka ma timer ataku (`1/attackSpeed` s); przy tyknięciu **emituje `AttackEmission`** (melee contact-collider na celu) — obrażenia nalicza pipeline z 5.1, nie bezpośredni zapis HP;
- śmierć: HP≤0 → stan `Dying` (symulacyjnie martwa natychmiast: nie blokuje, nie zadaje obrażeń; fade po stronie renderu), zwycięzca wchodzi na jej miejsce i zaczyna kolejne starcie z następną jednostką wroga.
- Szew przyszłościowy: jednostka z `attackRange > 0` emituje z dystansu (lub emituje pocisk jak wieża) — architektura ma to umożliwiać bez przebudowy, w v1 nie implementować.

### 5.3 Harness testowy

Odtworzyć wzorzec z dawnych `WarSystemTests` (CombatObserver + tick trace + detekcja anomalii): nowy `tests/TowerDefenseCombatTests.cpp` — scenariusze: 1v1 równych (RNG rozstrzyga, deterministycznie powtarzalne), silniejszy wygrywa i kontynuuje marsz, kolumna 3v2, jednoczesna śmierć (edge case — zdefiniować zachowanie: obie giną), armor > atak (floor 1 działa). Plus testy jednostkowe pipeline'u 5.1: emisja trafia dokładnie raz i tylko cele pasujące do filtra, kształty koło/rect wykrywają kolizję poprawnie (w tym brak trafienia poza kształtem), CombatResolver liczy wszystkie warianty formuły.

**Kryteria:** wszystkie scenariusze harnessa zielone; identyczny wynik walki dla identycznego seeda; brak zombie (martwa jednostka nigdy nie zadaje obrażeń po śmierci — to był realny bug w starym systemie, pilnować testem).

---

## ETAP 6 — HQ: obrona, eliminacja, przejęcie ekonomii

### 6.1 `HqComponent`

Nowy komponent budynku (wzorem komponentów z `BuildingComponents.h`) na `Headquarters`:
- `maxHp/currentHp` (modyfikowalne), `hardDefense` (redukcja siegeAttack napastnika), `thornsDamage` + `thornsInterval` (okresowe obrażenia zwrotne dla atakujących) — wszystko przez `BalanceStat` (`HqDefense`, `HqThorns`...), data-driven z `buildings.rtsdata`;
- szew: lista przyszłych zdolności HQ (interfejs analogiczny do `IUnitAbility`).

### 6.2 Atak na HQ — tryb grupowy

Jednostka docierająca do kafelka styku z wrogim HQ przechodzi w `AttackingHq`; jednostki MOGĄ grupować się na kratce i atakować równolegle [TODO.md pkt 9 — trudny moment: jawne rozróżnienie trybu walki]. Implementacyjnie: stan `AttackingHq` wyłącza kolejkowanie (spacing) na ostatnich kafelkach trasy; każda jednostka niezależnie, w rytmie attackSpeed, **emituje `AttackEmission` na collider HQ** (footprint budynku jako kształt celu) — pipeline z E5.1, `CombatResolver` liczy `max(1, siegeAttack - hardDefense)`. Thorns HQ to również emisja: HQ w stałych interwałach emituje obszarowy collider (koło wokół footprintu) raniący wszystkich atakujących — pierwszy realny konsument kształtu obszarowego, waliduje generyczność pipeline'u.

**Uwaga na konflikt trybów:** jeśli w trakcie ataku na HQ nadejdzie wroga kolumna obrońcy — zdefiniować deterministyczny priorytet (propozycja: jednostki atakujące HQ NIE przerywają; świeże jednostki obrońcy atakują ogon kolumny — szpica obrońcy walczy z ostatnią jednostką oblegającą). Przetestować jawnie.

### 6.3 Eliminacja i przejęcie — `ConqueredEconomy`

Gdy `currentHp == 0`:
1. gracz pokonany: flaga eliminacji; jego zdeployowane jednostki i roster znikają [DECYZJA] — symulacyjnie usunięte natychmiast, render odpala fade-out (stan `Dying` bez zadawania obrażeń);
2. nowa klasa **`ConqueredEconomy`** ("stub gracza" [TODO.md pkt 12]): przejmuje budynki **produkcyjne** pokonanego na własność zwycięzcy. Zasoby [DECYZJA]: **bufory wewnętrzne przejętych budynków zostają nienaruszone**; stan magazynowy pokonanego (magazyny, HQ) jest prawie czyszczony — zwycięzca przejmuje `capturedStockFraction` ≈ **20%** każdego zasobu (parametr w danych, zaokrąglenie w dół, deterministycznie), reszta przepada. Ramp produktywności: mnożnik startowy ~0.3 na `ProductionCycleTime` (odwrotnie: cykl wydłużony) i przyrost manpoweru, liniowo do 1.0 w czasie T (parametr w danych). Realizacja przez istniejący system modyfikatorów (scope per-building lub dedykowany zbiór budynków w `ConqueredEconomy`), tykana w `UpdateSimulation`, serializowana;
3. trasy: `PathingService` od teraz zwraca trasę **przez** podbite HQ do kolejnego wroga (konkatenacja segmentów ringu) — deploy na dalszych wrogów możliwy;
4. zwycięstwo: został jeden żywy gracz → koniec gry (ekran końcowy minimalny, w E8 GUI).

**Kryteria:** testy — HQ ginie od siegeAttack z uwzględnieniem hardDefense (przez pipeline emisji); thorns (emisja obszarowa) zabija słabe jednostki; grupowanie na kratce (3 jednostki DPS-ują równolegle); eliminacja przenosi budynki produkcyjne z nienaruszonymi buforami, magazyn pokonanego → dokładnie `capturedStockFraction` u zwycięzcy, reszta przepada; jednostki pokonanego znikają i nie zadają już obrażeń; ramp rośnie w czasie i dochodzi do 100%; trasa przez podbite HQ istnieje; determinizm całości.

---

## ETAP 7 — Wieże obronne

### 7.1 Architektura

- Nowa kategoria budynku: **`DefenseTower`** (footprint 2×2, jedna klasa C++), typy wież w `buildings.rtsdata` (koszt, HP?, ammoResource, magazynek/bufor amunicji, workers/manpower, parametry ataku, `attackBehavior` — nazwa strategii);
- **`TowerCombatComponent`** (nowy komponent): trzyma stan bojowy (cel, timer, amunicja) i deleguje do strategii;
- **Strategy pattern — `ITowerAttackBehavior`** [TODO.md pkt 10]: `DirectProjectileBehavior` (v1: pojedynczy cel, pocisk fizyczny). Interfejs projektować pod przyszłe: AoE circular, cone, pierce (penetracja liniowa), chain — NIE implementować, tylko zostawić czyste API (strategia dostaje kontekst: pozycja wieży, lista wrogich jednostek w zasięgu — posortowana deterministycznie, fabryka pocisków);
- **Targeting**: osobna mała strategia (v1: first-in-range = najbliżej wrogiego celu trasy; deterministyczne tie-break po instanceId).

### 7.2 Pociski — fizyczne obiekty na wspólnym pipeline

- Pocisk = **ruchoma `AttackEmission`** z infrastruktury E5.1 (klasa `Projectile` jako specjalizacja/wariant emisji): pozycja, prędkość, kierunek, kształt (v1: rect/segment dla strzały), payload obrażeń, właściciel, flaga pierce (v1: false). ŻADNEJ drugiej implementacji kolizji/obrażeń — kształty, detekcja i `CombatResolver` przychodzą gotowe z E5;
- collidery jednostek już istnieją (E5.1);
- update pocisków w fixed-tick (ruch + test kolizji ze zdeployowanymi jednostkami wroga; deterministyczna kolejność), trafienie → damage przez `CombatResolver` (ścieżka tower→unit z resistances), pocisk znika (chyba że pierce);
- pociski NIE wchodzą do save (krótkożyciowe — przy load po prostu ich nie ma), ale wchodzą do checksumu i snapshotu (spójnie z regułą dla emisji z E5.1 — inaczej resync rozjedzie stan);
- render placeholder: krótkie linie/prostokąty/okręgi.

### 7.3 Amunicja i obsada

- Wieża = pełnoprawny odbiorca w istniejącej sieci dróg surowcowych (reużyć `StorageComponent`/`LogisticsComponent` pattern — bufor wejściowy na `ammoResource`, `MaintainInputRequests` jak w budynkach produkcyjnych); 1 amunicja = 1 atak; pusty bufor → wieża nie strzela (stan widoczny w GUI E8);
- manpower/workers wg decyzji #5.

**Kryteria:** testy — wieża wykrywa cel w zasięgu (i nie poza), zużywa 1 amunicji/strzał, przestaje strzelać bez amunicji, pocisk trafia poruszającą się jednostkę deterministycznie, damage przez CombatResolver, zamawianie amunicji przez sieć dróg działa (integracyjny z transportem), modyfikatory Tower* działają.

---

## ETAP 8 — GUI

Zachowawczo (brak testów automatycznych GUI!). Wzorce z `GuiInternal.h` / istniejących paneli.

1. **Panel rosteru i deployu** (nowy, otwierany z HUD): lista dostępnych jednostek (typ, staty, liczba), komponowanie grupy ataku z ustalaniem kolejności (szpica→ogon), wybór celu (żywi sąsiedzi/osiągalni wrogowie; w 1vs1 auto), przycisk Deploy → `DeployUnits`. [DECYZJA: wybór targetu z poziomu GUI grup ataku.]
2. **Panel wieży** (rozszerzenie `BuildingInfoPanel`): typ, staty (po modyfikatorach), stan amunicji, obsada.
3. **Panel HQ**: HP HQ własnego (i wroga przy kliknięciu), parametry obronne.
4. **Panel budynku rekrutacyjnego**: kolejka rekrutacji, wybór typu jednostki z katalogu, koszty.
5. HUD: wskaźnik sumaryczny rosteru (zamiast starego army bar); ostrzeżenie "HQ pod atakiem".
6. Build mode: kategoria wież w `BuildPanelWidget`.
7. Ekran końca gry (zwycięstwo/porażka) — minimalny.

**Kryteria:** ręczna checklista przeklikania każdego panelu w SP i MP (klient widzi stan hosta); wszystkie akcje GUI wyłącznie przez komendy.

---

## ETAP 9 — Dane, balans, dokumentacja, sprzątanie

1. `assets/data/units.rtsdata`: startowy zestaw 3–4 jednostek (np. militia — tania/słaba, swordsman — melee core, knight — wolny/pancerny, ram/siege — wysoki siegeAttack, niski roadAttack) — wartości pilotażowe do balansu z użytkownikiem;
2. wieże: 1–2 typy startowe w `buildings.rtsdata`;
3. `technologies.rtsdata`/`focuses.rtsdata`: podpiąć nowe BalanceStat (przykładowe techy na atak/HP/wieże);
4. przejrzeć osierocone pliki/symbole po wycince (grep za Division/Battle/Garrison/Supply/Territory — 0 trafień poza historią);
5. **zaktualizować `CLAUDE.md`** (mapa ficzerów → pliki, opis pętli gry, usunąć sekcje o dywizjach/terytorium) i `docs/` (`tech_debt.md`, nowy `docs/tower_defense_design.md` z finalnymi decyzjami architektonicznymi);
6. wyczyścić `TODO.md` z wykonanych punktów;
7. finalny pełny przebieg: build Release, pełen suite testów, partia SP vs AI-stub, partia MP localhost do zniszczenia HQ.

---

## 4. Ryzyka i uwagi dla agenta

- **Największe ryzyko: determinizm MP.** Każdy nowy system (marsz, walka, pociski, ramp) od pierwszego commita projektować pod checksum/snapshot. Desync znaleziony późno = drogie debugowanie. Po E4, E5, E7 robić ręczny test MP localhost.
- **Kolejność wycinki w E1**: usuwać od liści zależności (GUI → AI → komendy → komponenty → klasy → nagłówki), kompilując często. Duże pliki (`GarrisonComponent` 749 l., `SupplyPackageComponent` 788 l., `WarSystemTests` 3111 l.) idą w całości — nie ratować fragmentów "bo szkoda".
- **Nie przebudowywać przy okazji.** Ekonomia/transport/GUI-layout działają — nie ruszać poza wymaganym minimum. Refaktory oportunistyczne = poza planem.
- **Pytać użytkownika** przy: każdej pozycji z sekcji 2, konfliktach interpretacyjnych TODO.md, decyzjach formatu danych (`units.rtsdata` — pokazać propozycję formatu przed implementacją), nazwach widocznych w GUI.
- Working tree ma niezacommitowane zmiany (etap 11.2 starego systemu) — przed startem E0 ustalić z użytkownikiem, czy commitować/odrzucić.
