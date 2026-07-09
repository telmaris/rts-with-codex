# War System — Phase 2 Design & Task List (supply · combat · army command)

> Dokument wykonawczy dla implementatorów (Haiku/Sonnet). Każdy task jest mały,
> zamknięty i weryfikowalny testem. Implementuj **po kolei**, buduj i uruchamiaj
> `run_tests.ps1` po każdym tasku. Jeśli coś jest niejasne — **pomiń i zapytaj**,
> nie zgaduj. Priorytet: **redukcja bugów**, konserwatywne, addytywne zmiany.

Bazuje na obecnym stanie war-systemu (patrz memory `project_war_system_rework`
i `war_system_capture_bugs_fixed`). Foundation jest gotowy: dywizje są
`Player`-owned (`Player::forces`), walka per-kwadrant w `RunFieldCombat`
(`GameWorld.Render.cpp`), ruch przez `MoveDivision`/`AttackTile`, `SupplyHub`
składa paczki, `ArmyGroupRegistry` grupuje dywizje.

## Decyzje projektowe (ZATWIERDZONE przez użytkownika — nie zmieniać bez pytania)

1. **Model dywizji — DWA paski jak w HoI4:** `cohesion` (**jawne nowe pole**,
   odpowiednik *organization*) oraz `strength` (= **HP** = manpower/integralność
   ekwipunku, liczba ludzi). **Walka bije w OBA** (org szybciej, HP wolniej).
   `health`/maxHealth stają się **pochodną wyświetlania** `strength/maxStrength`
   (zostają dla UI, nie są osobnym celem walki).
2. **HP ↔ SUPPLY coupling (doprecyzowanie użytkownika):** strata HP = zapotrzebowanie
   na zaopatrzenie do odbudowy. Dywizja na 90% HP potrzebuje ~10% dostawy
   (manpower + ekwipunek + żywność) do pełni. `weaponSupplyCapacity` = **realny
   establishment ekwipunku** (np. dywizja mieczników = **40 mieczy**); ubytek HP
   niszczy ekwipunek (equipment attrition), a pakiety Weapons go odtwarzają.
3. **Skala manpower = SETKI (nie setki tysięcy).** Dywizja miecznikόw ≈ **200
   ludzi**, village daje bazowo **~1000 max**. Koszty produkcji/rekrutacji
   wojska **podbić** (patrz A5). Budynki produkcyjne przeskalujemy później.
4. **Model obrażeń = adaptacja algorytmu HoI4, DETERMINISTYCZNA** (wartości
   oczekiwane zamiast rzutów kośćmi — lockstep MP nie zniesie RNG). Pełny wzór w
   Fazie C. Attacks/defenses z ataku i obrony, hit-chance 10%/40%, kości HP(śr.
   1.5)/org(śr. 2.5, lub 3.5 dla armored-unpierced), skalowanie output przez HP,
   po walce straty ×0.7. **Zero kości/RNG.**
5. **Utrata dywizji — reguła MIĘKKA:** walka co do zasady spycha (retreat na
   legalny rear quadrant) i zjada org/HP. Zniszczenie: z braku dostaw
   (broń+żywność+manpower) **LUB** gdy dywizja **odcięta/okrążona** i `strength`→0
   w walce (kocioł HoI4).
6. **Transport paczek — TERAZ po drogach.** Paczki jadą fizycznie jako
   `Transportable` (widoczne, z opóźnieniem, blokowane wąskim gardłem drogi).

## Stan wyjściowy (fakty z kodu — punkty zaczepienia)

| Element | Lokalizacja | Uwaga |
|---|---|---|
| `SoldierDivision` (pola instancji) | `inc/Building.h` ~118–195 | health/maxHealth, strength, morale, endurance, foodSupply/Cap, weaponSupply/Cap, manpowerScale, equipment, `UnitStats stats` |
| `UnitStats` (bazy stat, Stat<float>) | `inc/UnitStats.h` | ma już `maxCohesion`, `morale`, `maxStrength`, `supplyUse`, `fatigueRate` — dziś częściowo nieużywane |
| `DivisionCombatStats` + `ResolveDivisionDuel` | `inc/UnitStats.h`, `src/UnitStats.cpp` | duel operuje na strength-loss, wynik w `health` |
| `RunFieldCombat` (fazy 1–6) | `src/GameWorld.Render.cpp` ~179–433 | per-kwadrant, order+contact driven, bije w `health`, śmierć przy `health<=0` |
| `CaptureBuilding` | `src/GameWorld.Render.cpp` ~32 | flip ownera, rehome, RecalculateTerritory |
| `SupplyPackage` + `PlanSupplyPackage` | `inc/SupplyPackage.h`, `src/Equipment.cpp` | 1 paczka: items[] + rations + soldierCapacity |
| `SupplyPackageComponent` (SupplyHub) | `inc/BuildingComponents.h` ~332, `src/BuildingComponents.cpp` ~1900 | converter: Survey→Plan→Take→readyPackages→Deliver (INSTANT) |
| `ApplyPackageToMilitary` | `src/BuildingComponents.cpp` ~1987 | rations→food buffer, weapons→weaponSupply, stamp equipment |
| `SupplyBufferComponent` | `inc/BuildingComponents.h` ~287 | FOOD_PROVISIONS budynku, `GetSupplyConsumption` sumuje manpowerScale |
| `PopulationComponent` (Village) | `inc/BuildingComponents.h` ~384 | manpowerRate{0.2}, populationCap{80} |
| `Player::forces` / `AddForce` / `RebuildGarrisonViews` | `inc/Player.h` ~332, `src/Player.cpp` | jedyny właściciel dywizji; garrison ma non-owning view |
| `ArmyGroup`/`ArmyGroupRegistry` | `inc/ArmyGroup.h` | name, commander, divisions[], modifiers |
| `Transportable` + `ReceptTransport` | `inc/Transport.h`, `src/RoadNetwork.cpp` ~5, `src/Building.cpp` ~603 | ReceptTransport dynamic_castuje TYLKO na `Resource*` |
| `GameCommandType` + `WireVersion=9` | `inc/GameCommand.h` | |
| `MilitaryOrderType {None,Attack,Support,Defend}` | `inc/Building.h` ~82 | |
| Save version = **15** | `GameWorld.Persistence.cpp` | DIVS blok per home-building |

**Gotchas (z pamięci — respektować):**
- Każdy nowy `BuildingType` MUSI trafić do `MakeBuildOption` switch w
  `src/GuiController.cpp` (default zwraca puste `std::function` → `bad_function_call`).
- Checksum (`GameWorld.Checksum.cpp`) hashuje tylko coarse building data — NIE
  hashuje ruchu/pozycji/health dywizji. Nowe pola dywizji nie wymagają zmiany
  checksumu **dopóki** ewolucja jest deterministyczna (te same operacje na
  wszystkich klientach). Nie wprowadzać RNG bez wspólnego seeda.
- MP: `DrawReadyGameplay` trzyma world-lock przez `render.DrawContent` (widgety
  czytają live `forces`), ale **zwalnia go przed `render.PresentFrame()`** — nie
  wolno trzymać locka przez `EndDrawing()` (vsync/frame-cap), bo to głodzi wątek
  symulacji 100 Hz. Nowe widgety czytające dywizje — bez własnego lockowania.
- Serializacja: przy dodaniu pól dywizji → **bump save version** i rozszerz DIVS
  blok (`GameWorld.Persistence.cpp`), z backward-compat odczytem (brak pola = default).
- Nowe komendy → **bump `GameCommand::WireVersion`**, dopisz do
  `Serialize/TryDeserialize`, `IsValidType`, factory, handler w `GameWorld.Commands.cpp`.
- Determinizm: iteruj `std::map`/posortowane wektory (jak w istniejącym
  `RunFieldCombat`), nigdy `unordered_map` w ścieżce symulacji.

---

# FAZA A — Model dywizji: cohesion, manpower, skala

Cel: wprowadzić jednoznaczny model parametrów, na którym oprze się nowa walka i
zaopatrzenie. **Sama Faza A nie zmienia jeszcze zachowania walki** (to Faza C) —
dodaje pola, skalę i helpery, utrzymując zielone testy.

### Model docelowy pól `SoldierDivision`

| Pole | Znaczenie | Źródło max |
|---|---|---|
| `strength` (int) | **HP** = aktualny manpower (liczba ludzi); cel walki (wolno) | `UnitStats.maxStrength` (per typ, setki) |
| `cohesion` (float) **NOWE** | organization = bufor bitewny 0..maxCohesion; cel walki (szybko), 0→retreat | `UnitStats.maxCohesion` |
| `morale` (int, istnieje) | wola walki 0..100; wpływa na próg retreat i regen cohesion | — |
| `health` (legacy) | **pochodna wyświetlania** = `round(100*strength/maxStrength)`; przelicz na końcu ticku, NIE cel walki | — |
| `endurance` (istnieje) | rezerwa na fatigue (opcjonalnie Faza C) | — |
| `weaponSupply` / `weaponSupplyCapacity` (istnieją) | **ekwipunek** trzymany vs establishment (np. 40 mieczy); combat niszczy, pakiety Weapons odtwarzają | establishment per typ (A5) |
| `foodSupply` / `foodSupplyCapacity` (istnieją) | pula żywności | per typ (~ maxStrength) |
| `materielSupply` / `materielSupplyCapacity` **NOWE** | pula tools&resources (drewno/deski/narzędzia) → regen cohesion | Faza B |
| `manpowerScale` (istnieje) | **ujednolicić z maxStrength** — używać maxStrength jako wagi zużycia; `manpowerScale` zostawić jako alias/usunąć w osobnym cleanup | — |

> UWAGA implementacyjna: `cohesion` jako `float` (jak `damageBuffer`), żeby drobne
> ubytki per tick nie ginęły w zaokrągleniu. `strength` pozostaje `int` (ludzie),
> z bufforem `strengthBuffer` (float) na sub-1 attrition — analogicznie do
> istniejącego `damageBuffer`.

### Tasks

**A1. Skala manpower per typ jednostki.**
- W `src/UnitStats.cpp` (`MakeDefaultUnitStats`) ustaw `maxStrength` w setkach per typ:
  Militia≈100, Swordsman≈200, Archer≈120, Spearman≈180, Cavalry≈80 (wartości
  wstępne — do strojenia w Fazie C). Ustaw `maxCohesion` (np. 30–50) i `morale`
  (np. 50–70) per typ.
- W `inc/Building.h` konstruktory konkretnych klas (`MilitiaDivision()` itd. w
  `src/UnitStats.cpp`/`Building.cpp`) — zainicjuj `strength = round(maxStrength)`,
  `manpowerScale = round(maxStrength)` (spójność z dotychczasowym zużyciem).
- **Test:** `WarSystem.UnitTypeManpowerScaleIsInHundreds` — utwórz każdą klasę,
  asercja `strength` w zakresie [50,300] i różne per typ.
- **Uwaga:** `GetBaseRecruitmentManpowerCost` (Building.cpp) musi zwracać koszt
  ~= maxStrength danego typu (rekrutacja pełnej dywizji kosztuje jej manpower).
  Zaktualizuj i sprawdź, że rekrutacja nadal działa.

**A2. Village: przeskalować pojemność i przyrost.**
- `PopulationComponent`: `populationCap` 80 → **1000** (baza), `manpowerRate`
  dostroić tak, by przy pełnym food-supply dawała ~kilka–kilkanaście ludzi/s
  (tak, by wystawienie 1–2 dywizji zajmowało dziesiątki sekund, nie godziny).
- Zaktualizuj `assets/data/buildings.rtsdata` (Village) — to plik jest źródłem
  prawdy w runtime (kod `MakeDefaultDefinitions` to tylko fallback).
- **Test:** `WarSystem.VillageGeneratesHundredsOfManpower` — village z food,
  symuluj N sekund, asercja że manpower urósł do rzędu setek i respektuje cap 1000.
- **Uwaga:** sprawdź `PlayerEconomyTelemetry`/HUD manpoweru — większe liczby nie
  mogą rozwalić formatowania. Jeśli UI się psuje → osobny mini-task, nie hack tu.

**A3. Dodać pole `cohesion` (+ bufory) do `SoldierDivision`.**
- W `inc/Building.h`: `float cohesion{0.0f}; float cohesionBuffer{0.0f};
  float strengthBuffer{0.0f};`. W konstruktorach klas: `cohesion` =
  `ResolveUnitStat(stats.maxCohesion, type, nullptr)` (pełna kohezja na starcie).
- **Serializacja:** bump save version 15→16. Rozszerz DIVS blok w
  `GameWorld.Persistence.cpp` o `cohesion` (zapis) i odczyt z backward-compat
  (stary save bez pola → `cohesion = maxCohesion`). Bufory (transient) NIE
  serializowane. Zaktualizuj też ładowanie tak, by nie wywalało starych zapisów.
- **Test:** `Persistence.DivisionCohesionRoundTrips` — zapisz świat z dywizją o
  cohesion=X, wczytaj, asercja X. Dodaj też test wczytania save'a bez pola
  (symuluj brak → default).

**A4. Helper: `DivisionMaxCohesion/MaxStrength/Morale` przez modyfikatory.**
- W `src/UnitStats.cpp`: dodaj `float ResolveDivisionMaxCohesion(const SoldierDivision&,
  const BalanceModifierSet*)` i analogicznie dla maxStrength/morale — spójne z
  `ComputeDivisionCombatStats`. `DivisionCombatStats` już ma `morale` i
  `maxStrength`; dodaj `maxCohesion` do struktury i wypełnij w
  `ComputeDivisionCombatStats`.
- **Test:** `WarSystem.MaxCohesionRespectsModifiers` — z modyfikatorem +X% na
  `UnitMaxCohesion` wynik rośnie.

**A5. Establishment ekwipunku + podbicie kosztów produkcji/rekrutacji wojska.**
- Ustaw `weaponSupplyCapacity` per typ = realny establishment ekwipunku:
  Swordsman ≈ **40** (mieczy), Spearman ≈ 40 (włóczni), Archer ≈ 40 (łuki) +
  osobna pula ammo, Cavalry ≈ 30, Militia ≈ 40 (byle jaka broń). `foodSupplyCapacity`
  ≈ maxStrength (ludzie jedzą). Ustaw w konstruktorach klas / `MakeDefaultUnitStats`.
- Podbij koszty rekrutacji (`GetBaseRecruitmentResourceCosts`, `GetBaseRecruitmentManpowerCost`
  w Building.cpp) tak, by wystawienie dywizji kosztowało ~establishment (40 mieczy
  + ~200 manpower dla mieczników). Zaktualizuj `assets/data/buildings.rtsdata`
  (Barracks recipe/koszty) jeśli koszty są data-driven.
- `SupplyPackageComponent.soldiersPerPackage` / wielkości pakietów: dostrój tak,
  by kilka pakietów Weapons realnie dozbroiło jedną dywizję (nie 1 miecz na
  paczkę). Rozważ pole „items per package" skalowane do establishment.
- **Test:** `WarSystem.SwordsmanEstablishmentIsFortyWeapons`,
  `WarSystem.RecruitingDivisionCostsFullEstablishment` (rekrutacja pobiera
  ~40 broni + manpower; przy braku — odmowa).
- **Uwaga:** przeskalowanie budynków PRODUKCYJNYCH (żeby nadążały) to **osobny,
  późniejszy task** — tu tylko koszty wojska + establishment. Zanotuj w TODO.

> Po Fazie A: pola istnieją, skala i establishment poprawne, testy zielone,
> **walka i UI działają jak wcześniej** (nadal na `health` — to zmieni Faza C).
> Commituj.

---

# FAZA B — Trzy pakiety zaopatrzenia + transport po drogach + zużycie

Cel: `SupplyHub` produkuje **trzy niezależne strumienie** paczek, które **jadą po
drogach** do budynków militarnych, są tam redystrybuowane do dywizji, a dywizje
**zużywają** zaopatrzenie per tick.

### Model kategorii paczek

```
enum class SupplyCategory : uint8_t { Food, Materiel, Weapons };
```

| Kategoria | Zawartość (ResourceType) | Pula w dywizji | Zastosowanie |
|---|---|---|---|
| `Food` | FOOD_PROVISIONS | `foodSupply` | utrzymanie ludzi; brak → attrition strength |
| `Materiel` | WOOD, PLANKS, TOOLS | `materielSupply` **(nowe)** | regen cohesion / naprawa / okopanie; brak → wolniejszy regen |
| `Weapons` | miecze/włócznie/łuki/kusze/ammo/tarcze/zbroje | `weaponSupply` + `equipment` | efektywność bojowa; brak → spadek combat stats |

### Przepływ

```
Produkcja (Smith/LumberMill/farmy) → magazyny
   → SupplyHub.Survey (per kategoria)
   → PlanSupplyPackage (per kategoria) → readyPackages[category]
   → transport po drogach (SupplyPackageTransportable) → budynek militarny
   → ApplyPackageToMilitary (rozdziela do właściwej puli dywizji, neediest-first)
   → dywizje zużywają per tick w polu (fighting/deployed)
```

### Tasks

**B1. `SupplyCategory` + kategoryzacja zawartości paczki.**
- W `inc/SupplyPackage.h`: dodaj `enum class SupplyCategory` i pole
  `SupplyCategory category{SupplyCategory::Weapons};` w `SupplyPackage`.
- Dodaj wolną funkcję `SupplyCategory CategoryOfResource(ResourceType)`:
  FOOD_PROVISIONS→Food; WOOD/PLANKS/TOOLS→Materiel; equipment (przez
  `IsEquipment`)→Weapons.
- **Test:** `Supply.CategoryOfResourceClassifiesCorrectly`.

**B2. `materielSupply` w dywizji + capacity per typ.**
- `inc/Building.h`: `int materielSupply{0}; int materielSupplyCapacity{0};`.
  Zainicjuj capacity w konstruktorach (np. proporcjonalnie do maxStrength).
- Bump save version 16→17, dopisz do DIVS (odczyt z default). *(Jeśli B robisz w
  tym samym PR co A3 — jeden wspólny bump; nie mnóż wersji.)*
- **Test:** `Persistence.DivisionMaterielRoundTrips`.

**B3. `PlanSupplyPackage` per kategoria.**
- Rozszerz/otocz `PlanSupplyPackage` tak, by można było zaplanować paczkę dla
  jednej kategorii. Najprościej: dodaj wariant
  `bool PlanCategoryPackage(const std::map<ResourceType,int>& available,
   SupplyCategory cat, int soldiers, SupplyPackage& out)` w `src/Equipment.cpp`,
  który dla Food bierze rations, dla Materiel bierze WOOD/PLANKS/TOOLS wg
  potrzeby, dla Weapons deleguje do istniejącego `PlanSupplyPackage`
  (best-per-category). Ustawia `out.category`.
- **Test:** `Supply.PlanCategoryPackageBuildsEachKind` (3 przypadki).

**B4. `SupplyHub` produkuje 3 kolejki.**
- `SupplyPackageComponent`: zamień `std::deque<SupplyPackage> readyPackages` na
  `std::array<std::deque<SupplyPackage>,3>` **albo** trzymaj jedną kolejkę z
  polem `category` i limituj `maxReadyPackages` per kategoria. Preferuj array
  (jaśniejsza kontrola limitów). `AssemblePackage` w pętli po kategoriach:
  survey → PlanCategoryPackage → TakeFromNetwork → push do kolejki kategorii,
  o ile poniżej limitu.
- Zaktualizuj info-panel SupplyHub w `src/Gui.cpp` (pokaż ready per kategoria).
- **Test:** `Supply.HubAssemblesAllThreeCategories` — hub w sieci z żywnością,
  drewnem i bronią po jednym cyklu ma po ≥1 paczce każdej kategorii.
- **Gotcha:** hub nie magazynuje — `TakeFromNetwork` pobiera dokładnie tyle, ile
  wchodzi do paczki; limit kolejki wstrzymuje pobieranie (jak dziś).

**B5. `SupplyPackageTransportable` — paczka jedzie po drodze.**
- Nowy typ `struct SupplyPackageTransportable : Transportable` (nowy plik
  `inc/SupplyTransport.h` + `src/SupplyTransport.cpp`, albo w BuildingComponents).
  Trzyma `SupplyPackage payload;`.
- **Ownership (KLUCZOWE, bug-prone):** `Building::ReceptTransport` wrzuca surowy
  `Transportable*` do `transportables` (Resource jest poolowany). Paczki NIE są
  poolowane. Rozwiązanie: `SupplyHub` trzyma
  `std::vector<std::unique_ptr<SupplyPackageTransportable>> inFlight;` i sam nimi
  zarządza; do sieci wpychasz surowy `.get()`, a po dostarczeniu/anulowaniu
  usuwasz z `inFlight`. **Alternatywa:** dedykowany pool jak `ResourcePool`.
  Wybierz jedną i opisz w komentarzu. **Nie** oddawaj `delete` w losowym miejscu.
- Rozszerz `Building::ReceptTransport` (`src/Building.cpp` ~603): po gałęzi
  `dynamic_cast<Resource*>` dodaj `else if (auto* pkg =
  dynamic_cast<SupplyPackageTransportable*>(trans))` → gdy `targetBuilding==this`,
  wywołaj `ApplyPackageToMilitary(pkg->payload, *this)` i oznacz paczkę jako
  dostarczoną (do sprzątnięcia po stronie huba). Gdy przelot przez drogę — jak dziś
  push do `transportables` (bez zmian; działa dla dowolnego Transportable).
- **Test:** `Supply.PackageTravelsOverRoadAndArrives` — hub i tower połączone
  drogą; wyślij paczkę; po odpowiednim czasie garrison dostał zaopatrzenie; przy
  braku drogi paczka nie dolatuje (cancel).
- **Gotcha:** `Transportable::Update` anuluje transport gdy tile nie należy do
  ownera lub brak budynku next — to OK, ale `cancelTransport` dynamic_castuje na
  `Resource*` (nie zwróci paczki). Dodaj obsługę cancel dla paczki (powrót do
  `inFlight` huba / usunięcie), żeby nie wyciekła. **To główne ryzyko wycieku.**

**B6. `DeliverPackages` — routing po drogach zamiast instant.**
- `SupplyPackageComponent::DeliverPackages` (`src/BuildingComponents.cpp` ~1900):
  zamiast `ApplyPackageToMilitary` natychmiast — znajdź neediest budynek
  militarny (istnieje `MilitaryWeaponDeficit` sort; dodaj analogiczne deficyty
  food/materiel), zaplanuj ścieżkę drogową (`RoadNetwork`/`NavigationMap` jak
  przy zwykłych surowcach — zobacz jak `LogisticsComponent`/`HandleTransport`
  dobiera ścieżkę), `BeginTransport` paczki. Jeśli brak trasy — zostaw paczkę w
  kolejce (spróbuje później).
- Wybór odbiorcy per kategoria: food→budynki z deficytem food, itd.
- **Test:** `Supply.HubShipsNeediestFirstOverRoads`.
- **Uwaga:** determinizm — sortuj odbiorców po (deficyt malejąco, positionId
  rosnąco).

**B7. `ApplyPackageToMilitary` obsługuje 3 kategorie.**
- Rozszerz (`src/BuildingComponents.cpp` ~1987): switch po `package.category`:
  Food→jak dziś (food buffer + `foodSupply` dywizji); Weapons→jak dziś
  (weaponSupply + equipment); Materiel→**nowa gałąź**: rozdziel do
  `materielSupply` dywizji neediest-first.
- **Test:** `Supply.ApplyMaterielFillsMaterielPool`.

**B8. Per-tick zużycie zaopatrzenia przez dywizje.**
- Nowa wolna funkcja `void ConsumeDivisionSupply(SoldierDivision&, double dt,
  bool engaged, const Player& owner)` (w `src/UnitStats.cpp` lub
  `BuildingComponents.cpp`). Zużycie ∝ `supplyUse * (strength/maxStrength) *
  (engaged ? combatMul : garrisonMul) * (1 - supplyConservation) * dt`.
  Osobno food/weapon/materiel. Odejmuj z pul (z bufforem float, jak damageBuffer),
  clamp do 0. `supplyConservation` przez `PlayerSupplyConservation(owner)`
  (patrz sekcja Supply Conservation w Fazie C — helper współdzielony).
- Wywołaj w pętli symulacji — najlepiej w `RunFieldCombat` (już iteruje
  `forces` i wie o `engaged`), po fazie walki, TYLKO dla deployed
  (`occupiedTile.x>=0`) i garnizonowanych osobno (mniejsze). Alternatywnie w
  `GarrisonComponent::Update`. Wybierz jedno miejsce, żeby nie liczyć podwójnie.
- **Test:** `Supply.DeployedDivisionConsumesSupplyOverTime`,
  `Supply.EngagedDivisionConsumesFaster`.
- **Uwaga:** to jest hot-path (100 Hz). Throttle jeśli trzeba (co N ticków ze
  skumulowanym dt), ale zachowaj determinizm.

**B9. Skutki braku zaopatrzenia (attrition + osłabienie).** *(spina się z Fazą C)*
- Gdy `foodSupply==0`: per tick ubywa `strength` (głód) z bufforem;
  `weaponSupply==0`: combat stats skalowane w dół (już `ComputeDivisionCombatStats`
  skaluje przez equipmentQuality — dodaj skalowanie przez `weaponSupply/cap`);
  `materielSupply==0`: `cohesion` nie regeneruje się poza walką.
- **Test:** `Supply.StarvingDivisionLosesStrength`,
  `Supply.UnarmedDivisionFightsWorse`.

> Po Fazie B: paczki 3 typów jeżdżą po drogach i są zużywane; brak dostaw boli.
> Walka nadal na `health` do czasu Fazy C, ale zużycie już działa. Commituj.

---

# FAZA C — Rework walki: cohesion, morale, retreat, śmierć z braku dostaw

Cel: przejść z „bicia w HP do zera” na model HoI4: walka zjada **cohesion**,
przegrany **cofa się** na legalny kwadrant w tył; `strength` (manpower) spada
wolno i jest **uzupełniany** z zaopatrzenia+manpoweru; dywizja **ginie** z braku
dostaw lub w kotle.

### Model obrażeń — DETERMINISTYCZNA adaptacja algorytmu HoI4

> HoI4 używa RNG (rzuty kośćmi, losowy hit/miss, losowa kolejność celów). W
> lockstep MP **NIE WOLNO** — używamy **wartości oczekiwanych**, które HoI4 wiki
> sama liczy w przykładach („on average… 25 hits… 4.17 org/hour"). To daje ten
> sam wynik co RNG uśredniony, ale deterministycznie. **Zero kości, zero RNG.**

Obecny `ResolveDivisionDuel` (`src/UnitStats.cpp` ~120) i `DuelOffense` do
**wymiany** na poniższy model. Rozszerz `DivisionDuelResult`:
```
struct DivisionDuelResult {
    float attackerCohesionLoss, defenderCohesionLoss;   // organization
    float attackerStrengthLoss, defenderStrengthLoss;   // HP / manpower
};
```

**Stałe (jako `BalanceStat`/const, do strojenia):**
```
kHitChanceVsDefense = 0.10   // trafienie gdy obrońca ma jeszcze defenses
kHitChanceNoDefense = 0.40   // trafienie gdy defenses wyczerpane
kHpDieAvg   = 1.5            // śr. kości HP (rozmiar 2)
kOrgDieAvg  = 2.5            // śr. kości org (rozmiar 4)
kOrgDieAvgArmored = 3.5      // śr. kości org (rozmiar 6, armored-unpierced)
kHpDamageBase  = 0.06        // mnożnik obrażeń HP
kOrgDamageBase = 0.053       // mnożnik obrażeń org
kCombatSecondsPerHour = 60.0 // 1 „godzina bojowa" HoI4 = 60 s symulacji (DOSTRÓJ)
```

**Wzór (atakujący A → obrońca B), na krok dt:**
```
h = dt / kCombatSecondsPerHour                       // ułamek „godziny bojowej"

// 1) skalowanie output przez HP atakującego (kroki co 10%, jak HoI4)
ratioA     = clamp(A.strength / A.maxStrength, 0, 1)
hpScalingA = max(0.1, floor(ratioA * 10 + 1e-6) / 10)

// 2) attacks / defenses (round(stat/10)); attack skalowany gear+weaponSupply
attackA   = A.effectiveAttack        // = (light + ½shock) * equipmentQuality * weaponRatio
attacks   = round(attackA / 10)
defenses  = round(B.defense / 10)

// 3) oczekiwane trafienia (zastępuje losowy hit/miss)
hits = min(attacks,defenses)*kHitChanceVsDefense
     + max(0, attacks-defenses)*kHitChanceNoDefense

// 4) rozmiar kości org: armored bez przebicia = większe obrażenia org
armoredUnpierced = (A.armor > B.piercing) && A_isArmored   // A_isArmored: armoredShare wysoki / IsMounted
orgDie = armoredUnpierced ? kOrgDieAvgArmored : kOrgDieAvg

// 5) obrażenia (deterministyczne, liniowe w h)
hpDamage  = hits * kHpDieAvg * kHpDamageBase  * hpScalingA * balHP  * h
orgDamage = hits * orgDie    * kOrgDamageBase * hpScalingA * balOrg * h

result.defenderStrengthLoss = hpDamage
result.defenderCohesionLoss = orgDamage
// symetrycznie dla B → A (obrona też zadaje ciosy)
```
- `balHP`/`balOrg` — współczynniki balansu (Stat<float> / `BalanceStat`), start 1.0.
- `A_isArmored`: na start użyj `armoredShare > 0.3` lub `IsMounted()`; pełny model
  hardness/soft-hard attack (blend `lightAttack`/`armoredAttack` wg `armoredShare`
  obrońcy) = **konspekt/refinement**, nie na start.
- **Test:** `Combat.Hoi4DamageMatchesExpectedValueExample` — odtwórz przykład z
  wiki (attacks vs defense, ~25 hits, ~4.17 org/h) w granicach tolerancji;
  `Combat.ArmoredUnpiercedDealsMoreOrgDamage`;
  `Combat.LowHpAttackerDealsScaledDownDamage` (hpScaling 90% przy <100% HP).

### HP ↔ supply coupling + equipment/manpower losses (×0.7)

- **Equipment attrition:** przy zadaniu `hpDamage` obrońcy, odejmij też z jego
  `weaponSupply` ubytek = `hpDamage * effEquipmentLossFactor`. `weaponSupply`
  clamp do 0.
- **Manpower loss:** `strengthLoss` = bezpośredni ubytek `strength` (ludzie giną).
- **Odtworzenie = zaopatrzenie:** dywizja na X% HP ma deficyt
  `(maxStrength - strength)` manpoweru i `(weaponSupplyCapacity - weaponSupply)`
  ekwipunku. Reinforcement (poniżej) uzupełnia OBA z puli manpoweru gracza +
  dostarczonych pakietów Weapons.
- **Test:** `Combat.HpDamageConsumesEquipment` (weaponSupply spada ~lossFactor×hpDamage),
  `Combat.RestoringHpRequiresSupplyAndManpower`.

### Supply Conservation — redukcja zapotrzebowania na zaopatrzenie (ZATWIERDZONE)

Parametr `supplyConservation ∈ [0, kMaxConservation]` (cap np. **0.8**) **redukuje
proporcję** wymaganego supply. Przy 0.0: 10% straconego HP = 10% dostawy do
odbudowy. Przy 0.5: 10% HP = **5%** dostawy. Bierze się z **focusów, tech tree i
ustroju** — czyli standardowym pipeline'em modyfikatorów.

- **Nowy `BalanceStat::SupplyConservation`** (dopisz do `inc/BalanceStats.h`,
  po `SupplyConsumption`). Baza = 0; additive z tech/focus/state daje frakcję.
- **Rozwiązywanie:** player-level (źródła są globalne dla gracza) —
  `float supplyConservation = clamp(player->ModifyBalance(BalanceStat::SupplyConservation,
   0.0, ...), 0.0, kMaxConservation);`. Helper np. `float PlayerSupplyConservation(const Player&)`.
- **Zastosowanie (mnożnik `(1 - supplyConservation)`):**
  - `effEquipmentLossFactor = kEquipmentLossFactor * (1 - supplyConservation)`
    → mniej zniszczonego ekwipunku per HP loss (mniej do odtworzenia).
  - **Zużycie per tick (B8):** `ConsumeDivisionSupply` skaluje zużycie o
    `(1 - supplyConservation)` → armia dłużej wytrzyma na tych samych dostawach.
  - **Koszt reinforcementu:** ile weaponSupply/manpoweru trzeba dostarczyć na 1
    punkt HP — również ×`(1 - supplyConservation)`.
- **Źródła danych (przykłady do dodania):**
  - `assets/data/technologies.rtsdata` — np. tech „Field Logistics" +0.15 SupplyConservation.
  - `assets/data/focuses.rtsdata` — focus logistyczny +0.10.
  - `inc/StateDevelopment.h` / definicje ustroju — wyższy ustrój (Kingdom/Aristocratic)
    +0.05..0.10 (przez modyfikatory state, tak jak inne globalne bonusy państwa).
- **Test:** `Combat.SupplyConservationHalvesRequiredSupply` (0.5 → dwukrotnie
  mniejszy koszt odbudowy/zużycia niż 0.0), `Combat.SupplyConservationIsCapped`
  (nie przekracza kMaxConservation), `Combat.SupplyConservationFromTechApplies`
  (modyfikator z tech faktycznie obniża zapotrzebowanie).
- **Uwaga:** cap `< 1.0` (nigdy darmowe zaopatrzenie). Determinizm — frakcja z
  modyfikatorów jest deterministyczna, bez zmian w checksumie.

### `RunFieldCombat` v2 (przebudowa faz 3–6)

Zachowaj fazy 1–2b (wykrywanie engagement — działają dobrze). Zmień 3–6:

- **Faza 3 (damage):** dla każdej pary walczących wywołaj nowy `ResolveDivisionDuel`
  i akumuluj `cohesionLoss`/`strengthLoss` per dywizja (mapy `divCohesionLoss`,
  `divStrengthLoss`). **Targeting (rozkład ataków):** na start — każdy atakujący
  dzieli ataki **równomiernie** po wrogach w zasięgu (`SectorsFight`), obrona
  liczona per atakujący. Engagement width / coordinated attacks / priority target
  z HoI4 są RNG-ciężkie i wymagają `combatWidth` — **konspekt/refinement** (patrz
  niżej), nie na start.
- **Faza 4 (apply):** odejmij cohesion (buffer float) i strength (buffer float);
  dodatkowo odejmij `weaponSupply` = `strengthLoss * kEquipmentLossFactor`
  (equipment attrition). Building siege — bez zmian (territory hp), ale skaluj
  obrażenia oblężnicze przez `cohesion/maxCohesion` atakującego (rozbita dywizja
  słabo oblega).
- **Faza 4b (RETREAT — nowa):** dla każdej engaged dywizji, której
  `cohesion<=0`: jeśli po drugiej stronie jest silniejszy przeciwnik (lokalny
  stosunek sił < 1), wykonaj `RetreatDivision(world, div)`:
  - znajdź legalny **rear quadrant**: własne terytorium, sąsiedni (cardinal),
    **dalej od najbliższego wroga** niż obecny, walkable, wolny (`IsSectorCellFree`/
    `IsTileFree`). Użyj `SectorGraph.h` (`SectorCardinalNeighbors`,
    `AreSectorsConnected`) + `DivisionOccupyingSector`.
  - ustaw ruch tam (`MoveDivisionTo` z home garrison — dywizja zna
    `garrisonBuildingId`), `engaged=false`, flaga `retreating=true` (nowe pole
    transient).
  - jeśli **NIE MA** legalnego rear quadrantu (okrążenie!) → dywizja jest
    *odcięta*; nie cofa się. Jeśli dodatkowo `strength<=0` → ginie (reguła
    miękka, kocioł). W przeciwnym razie walczy dalej z karą.
- **Faza 5 (disengage):** jak dziś + wyczyść `retreating` gdy dywizja bezpieczna
  (brak wroga w zasięgu i cohesion się odbudowała).
- **Faza 6 (śmierć):** dywizja ginie gdy:
  - `strength<=0` **i** (odcięta od dostaw **lub** okrążona) — reguła miękka; ORAZ
  - (attrition z Fazy B9) długotrwały brut food → strength spadł do 0.
  Usuń z `forces` (jak dziś), prune armii, RebuildGarrisonViews.
- **`health`** (ZDECYDOWANE): na końcu ticku przelicz na pochodną
  `health = round(100*strength/maxStrength)`, żeby stare UI/telemetria/paski
  działały bez zmian. NIE jest osobnym celem walki.
- **Testy:**
  - `Combat.LosingDivisionRetreatsToRearQuadrant`
  - `Combat.EncircledDivisionCannotRetreatAndCanBeDestroyed`
  - `Combat.WellSuppliedDivisionSurvivesLostBattle` (cofa się, nie ginie)
  - `Combat.CutOffStarvingDivisionDies`
  - Zaktualizuj istniejące `FieldCombat.*` (14+ testów) — dziś asertują spadek
    `health` / śmierć w walce. Przepisz na cohesion/retreat. **To duży, ostrożny
    task — rób inkrementalnie, jeden test na raz.**

### Regeneracja i uzupełnienia (reinforcement)

- **Cohesion regen:** poza walką (`!engaged`), cohesion rośnie ku max, tempo ∝
  `materielSupply>0 ? full : slow`, dodatkowo skalowane przez morale i czy
  dywizja stoi we własnym terytorium (okopanie). W `RunFieldCombat` (po fazach)
  lub `GarrisonComponent::Update`.
- **Strength reinforcement:** okresowo (throttle), zaopatrzona dywizja
  (food>0 && weapon>0) w garnizonie/na froncie odbudowuje `strength` ku
  `maxStrength`, pobierając manpower z
  `player->strategicResources[Manpower]` (`AddManpower`/`Consume`). Brak
  manpoweru → brak uzupełnień → strength stoi/spada.
- **Testy:** `Combat.SuppliedDivisionRecoversCohesionOutOfBattle`,
  `Combat.ReinforcementConsumesPlayerManpower`,
  `Combat.NoManpowerMeansNoReinforcement`.

### Gotchas Fazy C
- **Determinizm retreat:** wybór rear quadrantu MUSI być deterministyczny
  (sortuj kandydatów po dystansie do wroga, potem po id kwadrantu). Inaczej
  desync MP.
- **Brak nowego RNG.** Jeśli kiedyś potrzebny los → wspólny seed w checksumie.
- **Retreat a `MovementBlockedTiles`:** retreat celuje we własne terytorium, więc
  przechodzi przez `MovementBlockedTiles` (własne tile są passable). Sprawdź, że
  odcięta dywizja (otoczona wrogiem) faktycznie zwraca „brak trasy”.
- **CaptureBuilding + retreat:** upewnij się, że dywizje w trakcie retreat po
  utracie home-buildingu przechodzą przez `RebuildGarrisonViews`/rehome bez
  utraty rozkazu (patrz `war_system_capture_bugs_fixed` — stale orders bug).

### Konspekt (refinement po MVP walki) — pełny targeting HoI4

Do dołożenia GDY podstawowa walka działa i jest zbalansowana. Wszystko musi
zostać **deterministyczne** (bez RNG):
- **Combat width + engagement width:** dodaj `combatWidth` per typ (Stat).
  Engagement width = 2×combatWidth. Zamiast losowej kolejności celów (HoI4) —
  wybór celów **deterministyczny**: sortuj wrogów w zasięgu po (id kwadrantu, id
  dywizji), dobieraj aż suma combatWidth ≤ engagement width.
- **Coordinated/uncoordinated split:** 35% ataków to część „coordinated" trafiająca
  w **priority target**; reszta rozdzielona proporcjonalnie do combatWidth celów.
- **Priority target (deterministycznie):** wg formuły HoI4 — preferuj niską org
  (`100% − orgRatio/4`), unikaj armored gdy `armor > piercing` (rating ÷2),
  z lekkim biasem na hard-attack. Wybierz max deterministycznie (tie-break po id).
- **Hardness / soft-hard attack:** efektywny atak = blend `lightAttack`/`armoredAttack`
  wg `armoredShare` obrońcy; `A_isArmored` zależne od własnego `armoredShare`.
- Zostaw dzisiejszy równomierny podział jako fallback przy 1v1.

> Po Fazie C: walka jest „HoI4-like” (deterministyczna, org+HP, retreat, supply
> coupling). Commituj. To najbardziej ryzykowna faza — najwięcej testów
> regresyjnych, rób inkrementalnie.

---

# FAZA D — Zarządzanie armią (fundament: front + plan ataku)

Cel MVP (priorytet użytkownika): **(1) automatyczne rozstawienie armii na
odcinku granicy** i **(2) plan ataku** (armia posuwa front ku celowi). Reszta
(odwrót całą armią, trening, obrona terenu) — **konspekt + fundament**, do
późniejszej implementacji.

### Model danych

`inc/Front.h` (nowy) — persistent assignment na poziomie `Player`:
```
struct FrontAssignment {
    int armyId{-1};
    std::vector<int> frontTileIds;     // odcinek granicy do obsadzenia (frontier tiles)
    enum class Stance { Hold, Advance, Retreat } stance{Stance::Hold};
    int objectiveTileId{-1};           // cel dla Advance (kierunek natarcia)
};
```
- `Player` dostaje `std::vector<FrontAssignment> fronts;`.
- **Serializacja:** bump save version, blok FRONTS (armyId, tiles, stance,
  objective). Backward-compat: brak bloku = brak frontów.

### Warstwa auto-orders (macro → micro)

Nowa funkcja `void UpdateFronts(GameWorld&, Player&)` (np. `src/Front.cpp`),
wołana z throttlem (co ~1 s, jak inne macro), NIE co tick:
- Dla każdego `FrontAssignment`:
  - **Hold:** rozdziel dywizje armii po `frontTileIds` (reuse logiki
    `AssignBorderTiles` z GuiController — wyekstrahuj do współdzielonej wolnej
    funkcji, żeby nie duplikować). Wydaj `MoveDivision`/Defend per dywizja tylko
    gdy dywizja nie jest już na swoim przydziale (unikaj spamu komend co sekundę).
  - **Advance:** przesuń przydział o jeden „pas” kwadrantów ku `objectiveTileId`
    (frontier się przesuwa, gdy poprzednie kwadranty zdobyte). Wydaj
    Attack/Move ku kolejnym kwadrantom.
  - **Retreat:** cofnij przydział ku HQ (konspekt — patrz niżej).
- **Determinizm:** UpdateFronts generuje **GameCommand-y** (nie mutuje stanu
  bezpośrednio), przechodzą przez zwykły `SubmitCommand`/lockstep. To spójne z
  wzorcem AI (`Controller.cpp`) i bezpieczne w MP.

### Komendy

Dodaj do `GameCommandType` + factory + Serialize/TryDeserialize + IsValidType +
handler (`GameWorld.Commands.cpp`) — **bump WireVersion 9→10**:

- `AssignArmyToFront(playerId, armyId, frontAnchorTileId)` — tworzy/aktualizuje
  `FrontAssignment` (odcinek liczony z anchor + długość armii przez
  `AssignBorderTiles`). Stance=Hold.
- `SetArmyStance(playerId, armyId, stance, objectiveTileId)` — Hold/Advance/Retreat
  + cel dla Advance.

> Reużyj istniejącego pola `divisionId`/`researchId` do pakowania (jak `FormArmy`
> pakuje id przecinkami w `researchId`). Nie dodawaj nowych pól wire, jeśli da się
> zmapować — mniej ryzyka.

### GUI (MVP)

- W `ArmyBarWidget`/`GuiController`: zaznacz armię → **przeciągnij po granicy**
  (drag) → `AssignArmyToFront`. Reużyj `MoveTargetWidget` do podglądu odcinka.
- Przycisk/skrót na karcie armii: przełącz Stance (Hold/Advance), a RMB na
  wrogim terytorium przy zaznaczonej armii → `SetArmyStance(Advance, tile)`.
- GUI nietestowalne jednostkowo — oznacz do playtestu przez użytkownika.

### Tasks Fazy D

**D1.** Wyekstrahuj `AssignBorderTiles` (dziś w `GuiController.cpp`) do
współdzielonej wolnej funkcji (np. `inc/Front.h`/`src/Front.cpp`), bez zmiany
zachowania. **Test:** `Front.BorderTilesFormContiguousLine` (przenieś/istn. logikę).

**D2.** `FrontAssignment` + `Player::fronts` + serializacja (save bump).
**Test:** `Persistence.FrontAssignmentRoundTrips`.

**D3.** Komendy `AssignArmyToFront` + `SetArmyStance` (WireVersion bump, handler).
**Test:** `Commands.AssignArmyToFrontCreatesAssignment`,
`Commands.SetArmyStanceUpdatesObjective`, oraz round-trip serializacji komendy.

**D4.** `UpdateFronts` — Hold: auto-rozstawienie po granicy (generuje MoveDivision
komendy, idempotentnie). **Test:** `Front.HoldStanceDistributesArmyAlongBorder`
(bez podwójnych komend gdy już rozstawione).

**D5.** `UpdateFronts` — Advance: przesuwanie frontu ku celowi (generuje Attack).
**Test:** `Front.AdvanceStanceMovesFrontTowardObjective` (na małej mapie z
wrogim terytorium; front przesuwa się o pas po zdobyciu kwadrantu).

**D6.** Wpięcie `UpdateFronts` w pętlę symulacji (throttle ~1 s, po
`RunFieldCombat`). Sprawdź AI: `Controller.cpp` może korzystać z tych samych
komend (opcjonalnie — konspekt).

### Konspekt (fundament teraz, implementacja później)

Zaprojektuj interfejsy tak, by dało się dołożyć bez przeróbek:
- **Retreat całej armii:** `SetArmyStance(Retreat, hqTile)` → UpdateFronts cofa
  przydział ku HQ, dywizje disengage+move. Fundament: pole `stance=Retreat` już
  w modelu; brakuje tylko logiki w UpdateFronts.
- **Obrona terenu (Defend region):** wariant Hold z dodatnią „głębokością”
  (druga linia). Fundament: `FrontAssignment.frontTileIds` może trzymać 2 rzędy.
- **Trening / rebase armii:** komenda `SetArmyStance(Train)` przy własnym
  budynku militarnym → dywizje wracają do garnizonu, szybciej regen cohesion +
  gain experience. Fundament: regen cohesion już zależny od „we własnym
  terytorium/garnizonie” (Faza C).
- **Wiele frontów / priorytety zaopatrzenia:** `FrontAssignment` mógłby nieść
  `supplyPriority` wpływające na routing paczek (Faza B6 sortowanie). Fundament:
  DeliverPackages już sortuje po deficycie — dołożenie priorytetu = jedno pole.
- **Commander/experience na poziomie armii:** `ArmyCommander` już istnieje;
  front może premiować dywizje pod dowódcą (modifiery już się liczą).

---

## Kolejność pracy i punkty kontrolne

1. **Faza A** (A1→A4) — skala + cohesion, zielone testy, commit.
2. **Faza B** (B1→B9) — 3 pakiety + transport + zużycie, commit.
3. **Faza C** (duel v2 → RunFieldCombat v2 → reinforcement) — **najostrożniej**,
   przepisuj testy pojedynczo, commit po każdym zielonym zestawie.
4. **Faza D** (D1→D6) — front Hold, potem Advance; GUI do playtestu.

Po każdej fazie: `run_tests.ps1` (pełny suite), potem `build_and_run.ps1` i
poproś użytkownika o playtest części GUI (nietestowalne jednostkowo).

## Zasady dla implementatora (twarde)
- Jeden task = jeden mały, zielony commit. Nie łącz faz.
- Każda zmiana serializacji → bump wersji + backward-compat odczyt + test round-trip.
- Każda nowa komenda → WireVersion bump + round-trip test + handler + IsValidType.
- Każdy nowy BuildingType/typ → `MakeBuildOption` switch (crash guard).
- Zero RNG w symulacji bez wspólnego seeda (desync MP).
- Ownership `Transportable` paczek — jawne, bez `delete` w losowym miejscu (wyciek).
- Nie dotykaj checksumu, chyba że dodajesz stan gatingujący rozgrywkę
  niedeterministycznie.
- Wątpliwość → **zapytaj użytkownika**, nie zgaduj.
```

---

# ITERACJA 3 — bugfixy z playtestu (plan dla Soneta, 2026-07-03)

> Pięć zgłoszeń z gry. Root-cause'y ustalone z kodu (nie zgaduj — są niżej).
> Rób **jeden bug = jeden commit**, `run_tests.ps1` po każdym. Determinizm lockstep
> (MP) jest twardym wymogiem: **zero nieseedowanego RNG** w symulacji.

## BUG 1 — nie można budować dróg pod własnym wojskiem

**Root cause (potwierdzony):** `TileMap::CanBuildFootprint` (`src/GameWorld.TileMap.cpp:302`)
odrzuca budowę na kafelku z **własną** dywizją **dla każdego typu budynku**:
```cpp
if (player != nullptr && DivisionOnTile(*player, {...}, -1) >= 0) return false;
```
`CanPlaceBuilding` woła to ZANIM handler komendy (`GameWorld.Commands.cpp:459`) zdąży
pominąć `AnyDivisionInFootprint` dla dróg — więc wyjątek dla dróg jest **martwy**
(GUI `CanPlaceSelected` też woła `CanPlaceBuilding`, więc blokuje tak samo).

**Fix (task 1):**
- Przekaż typ (lub `bool allowDivisions`) do `CanBuildFootprint` / `CanPlaceBuilding`
  i pomiń check `DivisionOnTile` gdy `type == BuildingType::Road` (drogi to teren
  przechodni). Sygnatury: `CanPlaceBuilding(type, anchor, footprint, player)` już
  ma `type` — przekaż go dalej do `CanBuildFootprint(anchor, footprint, player, bool allowDivisions)`.
- Solidne budynki NADAL blokowane na dywizji (bez zmian).
- **Test** (`tests/BuildingDomainTests.cpp` lub war): `Road` da się postawić na
  kafelku z własną dywizją; `StorageBuilding` na tym samym kafelku — odrzucony.
- **Gotcha:** to naprawia zarówno komendę, jak i GUI (obie idą przez `CanPlaceBuilding`).

## BUG 2 — nie da się odbić terenu zajętego przez wroga; dywizje „utknięte"

**Częściowo zrobione:** `MovementBlockedTiles` (`GameWorld.Commands.cpp`) już
przepuszcza teren neutralny (iteracja 2), więc można wejść na grunt, który spadł
neutralny po utracie budynku. **Prawdopodobna pozostała przyczyna „utknięcia":
bitwa się nie kończy (BUG 4, funkcja 1/x)** — zaangażowane dywizje wyglądają na
zamrożone, bo walka trwa w nieskończoność, a `engaged` je trzyma. Zrób BUG 4
PRZED domykaniem BUG 2 i sprawdź ponownie.

**Fix (task 2, po BUG 4):**
- Repro: `debugMode` (outpost wroga na Twoim wschodzie) → pozwól wrogowi wejść na
  Twój teren → zaznacz dywizje → RMB na wroga. Prześledź: czy komenda przyjęta,
  czy `MoveDivisionToAttackTile` znajduje ścieżkę do kontaktu, czy `RunFieldCombat`
  je angażuje i **czy bitwa się kończy**.
- Upewnij się, że dywizja na WŁASNYM terenie sąsiadująca z wrogiem w tym samym
  kwadrancie auto-angażuje (Phase 1b) i że po śmierci/odwrocie wroga
  `ClaimTilesUnderDivisions` odzyskuje kafelki.
- **Test:** wróg z dywizją na kafelku gracza → gracz atakuje → walka **rozstrzyga
  się w skończonym czasie** → kafelek wraca do gracza (`tile.owner == gracz`).
- Jeśli po BUG 4 problem znika — to był 1/x. Jeśli nie — dołóż trace i zapytaj.

## BUG 3 — HQ/budynki nie zaopatrują dywizji walczących w pobliżu; pobór z najbliższego

**Root cause (potwierdzony):** `DeliverPackages` wysyła paczkę do budynku, a
`ApplyPackageToMilitary` rozdziela ją tylko do dywizji **homed** w tym budynku
(`garrison->divisions` = non-owning view po `garrisonBuildingId`). Dywizja
wysłana w pole i walcząca przy HQ, ale przypisana do dalekich koszar, **nie jest**
uzupełniana przez HQ. Zaopatrzenie idzie po „domu", nie po pozycji fizycznej.

**Fix — redesign dystrybucji (task 3, większy; rozbij na 3a/3b/3c):**

**3a. Budynki militarne trzymają stockpile zaopatrzenia (3 pule).** Rozszerz
`SupplyBufferComponent` (albo nowy `SupplyDepotComponent`) o pule
`weaponStock/materielStock` obok istniejącego food buffer. `ApplyPackageToMilitary`
wlewa zawartość paczki do **stockpile budynku** (nie od razu do dywizji).
Serializacja: bump save version, zapisz nowe pule (backward-compat: brak = 0).

**3b. Deployed dywizje pobierają z NAJBLIŻSZEGO przyjaznego budynku militarnego/HQ.**
Nowa funkcja (np. `ResupplyDeployedDivisions(world)` w `GameWorld.Render.cpp`,
wołana per tick po `RunFieldCombat`, throttled ~1 s): dla każdej deployed dywizji
z deficytem znajdź najbliższy własny budynek z `GarrisonComponent`+stockpile w
zasięgu (np. Manhattan/pathfind ≤ N kwadrantów) i przelej z jego stockpile do pul
dywizji (food/weapon/materiel), neediest-pool-first. Gdy dywizja się przesunie,
następny tick wybierze inny najbliższy budynek — **aktualizacja automatyczna**.
- Determinizm: sortuj kandydatów-budynki po (dystans rosnąco, positionId rosnąco).
- Zasięg zaopatrzenia jako `BalanceStat` (np. `SupplyRange`) — modyfikowalny tech/focus.

**3c. HQ jest pełnoprawnym depotem.** HQ ma już `GarrisonComponent`; upewnij się,
że dostaje stockpile (3a) i jest celem `DeliverPackages` (jest — ma Garrison).
Dodaj `SupplyBufferComponent`/depot do HQ jeśli go nie ma.

**3d. MANPOWER = czwarty strumień zaopatrzenia — uzupełnianie strat w walce
(ZATWIERDZONE przez użytkownika).** `strength` dywizji = manpower (żywi żołnierze);
w walce spada (zabici). Trzeba **pobierać nowych żołnierzy z puli manpoweru gracza
w miejsce zabitych** — dokładnie jak food/weapon/materiel, tylko źródłem jest
`player->strategicResources[Manpower]` (globalna pula z village), nie stockpile
budynku (manpower jest ogólnokrajowy).
- W tej samej pętli co 3b (`ResupplyDeployedDivisions`, throttled): dla deployed
  dywizji z deficytem `strength < maxStrength`, **o ile jest w zasięgu zaopatrzenia**
  przyjaznego budynku/HQ (ten sam warunek co 3b — odcięta/okrążona dywizja NIE
  dostaje uzupełnień → bleeduje i ginie, reguła miękka), przelej z puli manpoweru
  gracza do `strength`, tempem `reinforceRate` (nowy `BalanceStat`, np.
  `ReinforcementRate`, modyfikowalny tech/focus). Zużyj z puli:
  `player->strategicResources.Consume(Manpower, dodane)`; jeśli pula pusta →
  brak uzupełnień (strength stoi/spada).
- Uzupełnianie manpoweru dozbraja też proporcjonalnie ekwipunek: nowy rekrut
  potrzebuje broni — pobór `weaponSupply` (z 3b) i `strength` (3d) skoordynuj tak,
  by nie dostać armii bez broni (najpierw ile pozwala min(manpower, weaponSupply)
  — albo prościej: strength rośnie tylko do poziomu pokrytego weaponSupply).
  Wybierz jedno i opisz; rekomendacja: `maxReinforcedStrength =
  min(maxStrength, round(maxStrength * weaponSupply / weaponSupplyCapacity))`.
- **Determinizm:** pobór manpoweru w deterministycznej kolejności dywizji
  (sort po ownerze, positionId, div id).

- **Testy:** dywizja homed w koszarach, deployed obok HQ z zapasem broni →
  `ResupplyDeployedDivisions` uzupełnia ją z HQ (nie z koszar); po przesunięciu
  do innego budynku pobiera z tego drugiego. Pusty stockpile → brak uzupełnienia.
  **Manpower:** dywizja z ubytkiem `strength` w zasięgu budynku → odzyskuje strength
  kosztem puli `Manpower` gracza; pusta pula manpoweru → brak odbudowy; dywizja
  odcięta (poza zasięgiem) → brak odbudowy mimo pełnej puli.
- **Gotcha:** nie licz zaopatrzenia dwukrotnie — jeśli division jest garnizonowana
  (occupiedTile<0), obsługuje ją stary path (homed building); tylko deployed
  (occupiedTile>=0) idą przez pobór-z-najbliższego. Manpower-reinforcement dotyczy
  i garnizonu, i pola (ranni wracają wszędzie), ale zawsze gated zasięgiem/pulą.

## BUG 4 — walka nie kończy się (1/x); stała + delikatne RNG

**Root cause:** obrażenia skalują się z HP atakującego (`hpScaling`/aktualny
strength), więc gdy HP→0, damage→0 — bitwa asymptotycznie nigdy się nie kończy.
(Obecny `ResolveDivisionDuel`/`DuelOffense` w `src/UnitStats.cpp` ma tylko podłogę
`max(0.25f, ...)` na pojedynczy cios, co nie wystarcza gdy cały output skaluje HP.)

**Fix (task 4):**
- **Stały człon obrażeń niezależny od HP:** damage = `constantFloor + hpScaled`,
  gdzie `constantFloor` (np. `BalanceStat`, start ~15–25% bazowego ataku)
  gwarantuje, że nawet rozbita dywizja zadaje minimum → bitwa rozstrzyga się w
  skończonym czasie. Alternatywnie: nie skaluj org/cohesion damage przez HP tak
  agresywnie (HoI4 skaluje krokowo do min 0.1, nie do 0). Wybierz jedno i opisz.
- **Delikatne, DETERMINISTYCZNE RNG** (symuluje zdarzenia losowe): mnożnik
  `1 + variance*(r-0.5)` gdzie `variance` małe (np. 0.15 = ±7.5%). `r ∈ [0,1)` z
  **seedowanego** generatora: `seed = hash(simulationTick, attackerId, defenderId,
  divA->id, divB->id)`. To samo na wszystkich klientach → brak desyncu. **Nigdy**
  `std::rand()` ani nieseedowanego `mt19937`. Determinizm potwierdź testem
  (dwa przebiegi z tym samym tickiem/id → identyczny wynik).
- **Test:** dwie równe dywizje w walce → HP obu spada do 0 (lub retreat) w <N
  sekund symulacji (bitwa SIĘ KOŃCZY); `CombatIsDeterministicAcrossRuns` (te same
  seedy → ten sam wynik); `LowHpDivisionStillDealsMinimumDamage`.
- **Gotcha:** jeśli dodajesz RNG do stanu wpływającego na capture budynków →
  sprawdź, że seed jest częścią deterministycznego stanu; NIE dodawaj do checksumu
  chyba że coś rozjeżdża się między klientami (dziś health nie jest w checksumie —
  bo ewolucja jest deterministyczna; zachowaj to).

## BUG 5 — mieszany ekwipunek + siła per-żołnierz + FUNDAMENT tagów przedmiotów

**Część A (prawdopodobnie już naprawiona — POTWIERDŹ):** po iteracji 2
`Player::TryPayEquipmentCategory` pobiera proporcjonalnie ze **wszystkich** typów
miecza (20 miedzianych + 20 żelaznych = 40 Sword). Zbuduj grę, zrekrutuj miecznika
mając 2 typy mieczy — powinno zadziałać. Jeśli NIE działa → zgłoś, bo to sprzeczne
z kodem. **Test** (jeśli brak): rekrutacja miecznika przy 20 COPPER + 20 IRON w
magazynie przechodzi i zużywa po ~20 każdego.

**Część B (NOWE) — ekwipunek dywizji jako KOMPOZYCJA + siła per-żołnierz:**
Dziś `DivisionEquipment` (`inc/Building.h`) ma pojedyncze sloty
(`weapon/armor/rangedWeapon/ammo`), więc mix „70% brąz / 30% stal" nie da się
wyrazić — dywizja dostaje jeden ostemplowany typ.
- Zamień sloty na **kompozycję**: `std::map<ResourceType,int> weapons;` (+ analogicznie
  armor/ranged/ammo) LUB jeden `std::map<EquipmentCategory, std::map<ResourceType,int>>`.
  Trzyma faktyczny rozkład sprzętu w dywizji.
- Przy rekrutacji/zaopatrzeniu zapisuj RZECZYWISTY rozkład (co pobrał
  `TryPayEquipmentCategory` — rozszerz jego out-param z pojedynczego `representative`
  na `std::map<ResourceType,int>& consumed`).
- `DivisionEquipmentQuality` (`src/UnitStats.cpp`) → **średnia ważona** jakości po
  kompozycji: `Σ(count_i * quality_i) / Σ count_i`. Stąd 70% brąz (q≈1.0) + 30%
  stal (q≈1.25) → blend ≈ 1.075 → wyższa siła całej dywizji proporcjonalnie do mixu.
- Serializacja: bump save version, zapisz kompozycję (backward-compat: stary save
  z pojedynczym typem → kompozycja {type: capacity}).
- **Testy:** dywizja z 70% bronze + 30% steel ma equipmentQuality między czystym
  bronze a czystym steel; czysty steel > mieszany > czysty bronze.

**Część C (FUNDAMENT) — uogólnione tagi/kategorie zasobów:**
Cel: dokładać przyszłe towary (miecze magiczne, ulepszone katapulty) przez **dane,
nie kod**. Baza już istnieje: `EquipmentProfile` (`inc/Equipment.h`) mapuje
`ResourceType → {category, material, quality}`; dodanie sprzętu = 1 `ResourceType`
+ 1 wiersz tabeli. Rozszerz ten fundament:
- Dodaj do `EquipmentProfile` opcjonalne pola pod przyszłość: `tier`/`quality`
  już jest (użyj go dla „magic sword" = Sword o quality 2.0, material np. `Enchanted`
  dodany do `EquipmentMaterial`). Katapulty/artyleria = nowa `EquipmentCategory`
  (np. `Siege`) + nowe `ResourceType` + wiersze.
- Rozważ lekki, ogólny **rejestr tagów** `ResourceTag` (enum bitflag lub set) na
  poziomie zasobu, jeśli potrzebne będą właściwości poza equipmentem (np. `Fuel`,
  `Consumable`, `StrategicHigh`). Ale **nie przebudowuj** wszystkiego — jeśli
  `EquipmentProfile` wystarcza dla broni/zbroi, zostań przy nim i tylko udokumentuj
  „jak dodać nowy przedmiot" (1 ResourceType + 1 profile row + ew. koszt rekrutacji
  kategorią). Fundament = **kategoria + jakość już generalizują**; kompozycja
  (część B) domyka „mix różnych jakości w jednej dywizji".
- **Decyzja do potwierdzenia z użytkownikiem PRZED implementacją C:** czy chce
  pełny rejestr tagów zasobów (większy refaktor), czy wystarczy rozszerzyć
  `EquipmentProfile` (material `Enchanted`, kategoria `Siege`) — rekomendacja: to
  drugie (konserwatywne, mniej ryzyka), tagi dopiero gdy pojawi się realna potrzeba.

**Kolejność iteracji 3:** BUG 1 (mały) → BUG 4 (odblokowuje BUG 2) → BUG 2
(weryfikacja) → BUG 5A (potwierdzenie) → BUG 3 (redesign supply) → BUG 5B
(kompozycja) → BUG 5C (fundament, po decyzji użytkownika). Po każdym: pełne testy.
