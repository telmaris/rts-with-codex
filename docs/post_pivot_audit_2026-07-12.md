# Audyt po pivocie Tower Defense + lista zadań dla agenta

- **Data:** 2026-07-12
- **Metoda:** statyczna analiza kodu (bez buildów i uruchamiania), weryfikacja notatek
  z `TODO.md` i `docs/tech_debt.md` względem aktualnego stanu repo (HEAD `fb4476e`).
- **Status (2026-07-13):** T1-T13 ZAIMPLEMENTOWANE (T11 świadomie odłożone — duży osobny
  feature wymagający decyzji projektowych; T14/T15 odłożone — GUI/AI bez pokrycia testami,
  zbyt duży blast radius bez człowieka do playtestu). Pełen suite zielony (153 passed,
  zero pre-existing failures — 8 "forestry" testów usunięte w T10). Plus dwa dodatkowe
  ficzery na życzenie użytkownika: pasek HP nad HQ pod ostrzałem (fade po
  `HqComponent::recentDamageTimer`) i małe paski HP + placeholderowe kolory per typ
  jednostki nad każdą jednostką na mapie. Szczegóły w sekcji "Follow-up #5" i niżej.
  Szczegóły wykonania: `git log`/diff na branchu, niezacommitowane na HEAD tego audytu —
  użytkownik jeszcze nie zdecydował o commicie.
- **Follow-up (ten sam dzień, po ręcznym playteście T1-T3):** user zgłosił periodyczne
  wielosekundowe zamrożenie całej gry (produkcja, kamera, GUI) + widoczne zacięcie paska
  produkcji przy 100%. Pierwsza runda poprawek (sekcja "follow-up #1") nie wystarczyła;
  właściwy root cause (AI O(mapa²) w FindBuildAnchor) znaleziony i naprawiony w rundzie
  drugiej — patrz "Perf follow-up #2" niżej (najgorszy tick 7853 ms → 9.4 ms).

## Status wykonania (aktualizacja po sesji implementacyjnej)

- **T1 (DONE):** `RoadNetwork::CalculatePath` i `Transportable::Update` przestały
  zależeć od `Tile::owner` — nowy predykat `IsTileTraversableForOwner` (własność
  budynku na kaflu: origin/road-owned-by-src/destination). Dodano `Transportable::
  originatingOwner` (capturowany raz przy `BeginTransport`, nie re-czytany z
  `sourceBuilding->owner` co tick — inaczej samo-referencyjny przy zmianie właściciela
  źródła w locie). Po drodze znaleziono i naprawiono PRE-EXISTING bug w istniejącym
  teście (`TransportableCancelsWhenPathLeavesOwnerTerritory`, teraz przemianowany):
  `cancelTransport()` zwracał stack-lokalny `Resource` do bufora budynku, a
  `ResourceBuffer::Clear()` oddawał ten NIE-poolowy wskaźnik do globalnego
  `resourcePool` — korumpując pulę dla KAŻDEGO późniejszego testu w tym samym
  procesie (objaw: losowy "Access violation - no RTTI data!" w zupełnie innym teście,
  zależny od kolejności uruchamiania). Naprawione: `.buffer.clear()` zamiast
  `.Clear()` w tym jednym miejscu. Dodano 2 nowe testy regresyjne budujące świat
  wyłącznie przez `Player::Build<T>` (prawdziwa ścieżka produkcyjna, bez ręcznego
  `tile.owner`) w `tests/RoadNetworkTests.cpp`.
- **T2 (DONE):** usunięto `Tile::CanBuild`/`SetOwner` (martwe), martwe renderowanie
  terytorium w DWÓCH miejscach (`GameWorld::DrawMap` live + `Renderer::DrawSnapshot`
  MP fallback — ten drugi nie był nawet wymieniony w oryginalnym audycie, znaleziony
  przy okazji) + flagę `territoryDirty`; naprawiono AI (`Controller.cpp`: `AssessMap`,
  oś Expansion/Risk, build-site scoring) tak, by liczyło własność po budynku na kaflu
  zamiast po `tile.owner` — i usunięto całkiem "kształt terytorium" (border tiles),
  bo nie ma dziś odpowiednika bez ownable ground; usunięto martwy
  `BalanceModifierScopeType::Territory` + `ownedTerritory`. Persistence/GameSnapshot
  CELOWO zostawione (format bez zmian), tylko okomentowane jako martwe pole.
  Zgłoszono osobno (spawn_task) martwy keybinding `ToggleTerritoryView`.
- **T3 (DONE, wariant bez T14):** `Building::GetInputBufferViews()` ujawnia teraz
  bufor `StorageComponent` jako wejście dla budynków z `TowerCombatComponent`/
  `RecruitmentComponent` (wieża/Barracks) — odblokowuje `AutoConnectBuilding`.
  Dodano `LogisticsComponent` do `Barracks` + `LogisticsComponent::
  MaintainStorageRequests` (pull wszystkich zadeklarowanych zasobów magazynu,
  analogicznie do wieży) wołane z `RecruitmentComponent::Update`. Naprawiono
  `AutoConnectBuilding`: dodano `CanAcceptResource` check przed ustawieniem nowego
  budynku storage-like jako receivera (bez tego budowa wieży/Barracks obok
  producenta niepowiązanego zasobu potrafiła po cichu przejąć jego receiver,
  blokując normalny fallback do najbliższego magazynu) + zamieniono pełny skan
  tilemapy na `GetTrackedBuildings()`. **Znaleziono i naprawiono drugi, poważniejszy
  bug w trakcie testowania:** `StorageComponent::Update` bezwarunkowo odsyłał
  ZAWARTOŚĆ bufora do dowolnego innego budynku akceptującego dany typ — więc zaraz
  po dostarczeniu amunicji/kosztów do wieży/Barracks, ich WŁASNY `StorageComponent::
  Update` odsyłał je z powrotem do najbliższego magazynu, w nieskończonej pętli
  (zasób nigdy się nie kumulował). Naprawione: `StorageComponent::Update` wczesny
  return dla budynków z `TowerCombatComponent`/`RecruitmentComponent` (ich storage to
  wyłącznie sink na własne potrzeby, nie ogólny magazyn do redystrybucji). Dodano
  2 testy regresyjne (`BuildingDomainTests.cpp`, `TowerAttackSystemTests.cpp`)
  budujące świat przez `Player::Build<T>` bez ręcznego `SetSupplier`.

---

## Znalezisko główne: cała logistyka surowcowa jest martwa (P0)

**Objaw (TODO.md):** "transport surowców nie działa np. food z HQ do village, strzały,
drewno z woodcuttera do HQ albo do lumber mill — ewidentny problem z logistyką. Drogi są."

**Root cause:** `RoadNetwork::CalculatePath` (`src/simulation/RoadNetwork.cpp:177-273`)
filtruje KAŻDY kafelek ścieżki — łącznie z kaflami startowymi — warunkiem
`tilemap->GetTile(...).owner == src->owner` (linie 213 i 252). Tymczasem po usunięciu
systemu terytorium (pivot ETAP 1) **nic w kodzie produkcyjnym nie ustawia `Tile::owner`**:

- `Tile::SetOwner` (`src/core/GameWorld.TileMap.cpp:21`) — zero wywołań poza jednym testem
  (`tests/TileMapDomainTests.cpp:32`).
- `TileMap::BuildOnTile` ustawia `building->owner`, ale nie `tile.owner`.
- `MapGenerator.cpp` i `GameWorld.Init.cpp` — zero przypisań ownera kafelków.
- Jedyne przypisanie: odczyt z save (`GameWorld.Persistence.cpp:484`) — czyli też nullptr,
  bo nowe save'y nie mają skąd wziąć ownera.

Skutek: BFS w `CalculatePath` startuje z pustą kolejką (wszystkie start tiles odrzucone,
bo `owner == nullptr != src->owner`) → ścieżka zawsze pusta → `BeginTransport` zawsze
`false` → **żaden zasób nigdy nie wyjeżdża na drogę**. To jeden wspólny root cause dla
co najmniej 4 z 9 zgłoszonych ręcznie bugów (transport, strzały do wieży, food/manpower,
pośrednio rekrutacja). Dotyka też AI (ekonomia AI tak samo martwa).

**Dlaczego testy tego nie łapią:** testy logistyki najwyraźniej budują świat przez
save/load (gdzie owner się deserializuje) albo nie przechodzą przez `CalculatePath`
z realnymi kaflami — do potwierdzenia przy naprawie (zadanie T1 wymaga testu regresyjnego
na świeżym świecie).

### Pozostałe relikty `tile.owner` (ta sama rodzina)

| Miejsce | Problem |
|---|---|
| `src/ai/Controller.cpp:580-601, 774` | Osie strategiczne AI liczą "owned strategic resource tiles" po `tile.owner` — zawsze 0; oceny Expansion/Resources częściowo z sufitu |
| `src/core/GameWorld.TileMap.cpp:27-44` (`Tile::CanBuild`) | Martwy kod (zero wywołań), loguje "player is not an owner" |
| `src/core/GameWorld.Render.cpp:191` | Render czyta `tile.owner` (tint własności?) — gałąź martwa |
| `src/core/GameWorld.Persistence.cpp:484` | Save format wciąż serializuje ownera kafelków — martwe dane |

---

## Weryfikacja bugów zgłoszonych ręcznie (TODO.md)

| # | Zgłoszenie | Werdykt po audycie | Zadanie |
|---|---|---|---|
| 1 | Rekrutacja w Barracks nie działa | Ścieżka GUI→komenda→`QueueRecruitment` poprawna (`src/ui/Gui.cpp:1836-1842`, `GameWorld.Commands.cpp:257`). Pada cicho w `QueueRecruitment` (`RecruitmentComponent.cpp:21-26,37-38`): brak mieczy w magazynie Barracks (transport martwy → koszty nie dojeżdżają) i/lub brak manpower (łańcuch żywności martwy). Dodatkowo zero feedbacku w GUI czemu przycisk nie działa | T1 + T4 |
| 2 | Tooltips w focus tree pokazują "+10%" bez nazwy | Potwierdzone: `FocusStatLabel` (`src/ui/GuiResearchTree.cpp:138-155`) zna tylko 11 starych statów — wszystkie staty TD (`UnitHp`…`TowerAmmoEfficiency`, 15 sztuk z `inc/economy/BalanceStats.h`) wpadają w `default: "Effect"`. `FocusBuildingLabel` (linia 207) nie zna `DefenseTower`. Filtr `unitDefId` w ogóle nie jest pokazywany | T5 |
| 3 | Transport strzał do wieży nie działa | Primary: martwa logistyka (T1). Secondary: bufor amunicji wieży nie jest widoczny w `GetInputBufferViews()` (`src/economy/Building.cpp:270-280` — tylko ProductionComponent/PopulationComponent), więc `AutoConnectBuilding` nie ma jak podpiąć suppliera; pull wieży (`TowerCombatComponent::Update` → `RequestResource`) wymaga suppliera (`LogisticsComponent.cpp:135-139`), którego nikt nie ustawia. Po naprawie T1 strzały dojadą push'em z magazynu (`StorageComponent::Update`), ale pull pozostaje martwy | T1 + T3 |
| 4 | Food supply od razu 30% na HUD, 0% w wiosce | Dwie metryki pod jedną etykietą: HUD "Food supply" = średnie `GetWorkerProductivity()` = `0.3 + 0.7*ratio` (podłoga 30%, `PopulationComponent.cpp:68`, `Player.cpp:128-143`), panel wioski = surowe `GetFoodSupplyRatio()` (0%). Spadek "od razu", bo żywność nigdy nie dojeżdża (T1). Uwaga: komentarz w `PopulationComponent.cpp:18-20` odsyła do supply packages — systemu USUNIĘTEGO w pivocie; `RequestFoodSupply` (pull) nie jest wołane nigdzie | T1 + T6 |
| 5 | Transport w ogóle nie działa mimo dróg | Potwierdzone, root cause powyżej | **T1** |
| 6 | Wieża powinna pokazywać zasięg po kliknięciu | Potwierdzone: panel pokazuje zasięg tylko tekstem (`src/ui/Gui.cpp:1743`); `SelectedBuildingWidget` (`src/ui/GuiController.h:126-134`) rysuje tylko highlight footprintu | T7 |
| 7 | Resync after desync | Potwierdzone, bez zmian od wpisu w tech_debt: brak `GameWorld::LoadFromSnapshot` (grep: istnieje tylko w docach), snapshot czysto wizualny | T11 (duże, osobne) |
| 8 | Kamera na RMB zamiast MMB | Stan: drag kamery na MMB (`src/ui/GuiCommon.cpp:66`); RMB w widoku mapy przypisuje receiverów logistyki przy zaznaczonym budynku (`src/ui/GuiController.cpp:464-497`); drzewko badań już panuje RMB | T8 (konflikt do rozstrzygnięcia) |
| 9 | ESC nie wraca z menu do gry | `GameMenuScene::Update` (`src/scenes/GameMenuScene.cpp:101-116`) MA logikę ESC, ale `escArmed` to pole instancji sceny, **nigdy nie resetowane** przy ponownym wejściu (sceny żyją cały czas, `GameWindow.cpp:48`), a GameScene nie ma symetrycznej ochrony przed tym samym edge'em ESC — ping-pong scen / martwy ESC od drugiego otwarcia menu | T9 |

---

## Weryfikacja `docs/tech_debt.md` — pozycje nieaktualne lub do korekty

| Pozycja | Stan faktyczny |
|---|---|
| 🔴 "`Building` fat interface, 40+ wirtualnych metod" | **ZROBIONE** (refactor kompozycyjny; `inc/economy/Building.h` ma dziś 5 wirtualnych metod). Odhaczyć |
| 🟢 "CI uruchamia tylko `--gtest_filter=GameCommandTests.*`" | **ZROBIONE** — workflow uruchamia pełen suite bez filtra (`.github/workflows/windows-release.yml:55-60`) |
| 🟢 "Brak cache vcpkg w CI" | **ZROBIONE** — jest `actions/cache` (workflow, linie 32-39) |
| 🟡 "`Player` god-object, 769 linii" | Zredukowane: `inc/economy/Player.h` = 365 linii, rozbite na Player/PlayerEconomy/PlayerDataTracker. Obniżyć priorytet / przeredagować wpis |
| 🟡 "Duplikacja w hierarchii `GuiSystem`" | Częściowo zrobione: baza ma `actionMap` + `OnActivate`/`OnDeactivate`, wiring przez `WireCommonSystemActions`; derived klasy wciąż redeklarują komplet handlerów. Zostawić jako niska higiena |
| 🔴 "Przejęte budynki nie w sieci dróg zwycięzcy — wymaga NavigationMap removal API, którego nie ma" | Nadal otwarte, ALE uzasadnienie nieaktualne: `UpdateNavMap(id, nullptr)` (`src/simulation/RoadNetwork.cpp:161-174`) to działające removal API. Naprawa prostsza niż opisano — patrz T12 |
| 🟡 "`GameWorld.Render.cpp` zawiera logikę symulacji" | Nadal aktualne: `UpdateSimulation` (linia 14) i `Update` (linia 66) siedzą w `.Render.cpp`; `ResupplyDeployedDivisions` już nie istnieje (pivot) |
| 🟢 "8 testów forestry" | Nadal aktualne: "forestry" jest w fallbacku `Technology.cpp` i 35 miejscach testów, brak w `assets/data/technologies.rtsdata` - usunąć gdyż będzie to przerobione manualnie. Zrobić jakąś opcje default jako fallback w przypadku błędu parsowania itp|

Pozostałe pozycje (ręczna serializacja, brak enkapsulacji, determinizm float, homing
pociski, wieże bez HP, itd.) — zweryfikowane jako nadal aktualne lub świadome decyzje,
bez zmian.

### Dodatkowe znaleziska audytu (nowe, spoza notatek)

1. **`AutoConnectBuilding` — gałąź storage-like bez walidacji** (`src/core/GameWorld.TileMap.cpp:589-609`):
   ustawia nowy budynek magazynowy jako receiver cudzych outputów **bez sprawdzenia
   `CanAcceptResource`** (wieża/Barracks stają się receiverami rzeczy, których nie przyjmą —
   nieszkodliwe funkcjonalnie dzięki late-checkom, ale zaśmieca mapę połączeń) i **skanuje
   cały tilemap** (do ~1M kafli) zamiast `GetTrackedBuildings()`/rejestrów ETAP 10.
2. **`Barracks` nie ma `LogisticsComponent`** (`inc/economy/Building.h:320-329`) —
   w przeciwieństwie do `DefenseTower` nie ma mechanizmu pull ("dosypuj do pełna");
   koszty rekrutacji jadą wyłącznie push'em z magazynów. Niespójność projektowa.
3. **Dryf dokumentacji łańcuchów produkcji:** CLAUDE.md mówi "Well → WATER → Inn → BEER",
   a `buildings.rtsdata` nie zawiera BEER w ogóle; Inn produkuje dziś
   BREAD+MEAT+WATER → FOOD_PROVISIONS (linie 412-434). `ResourceType::BEER` wisi w enumie
   bez producenta.
4. **Komentarz-relikt supply packages** w `PopulationComponent.cpp:18-20` + martwe
   `RequestFoodSupply` — odsyłają do systemu usuniętego w pivocie.
5. **Modyfikatory `Territory`-scope są martwe** (`inc/economy/BalanceModifiers.h:141-142`):
   scope matchuje po `context.ownedTerritory`, a `Player::MakeBalanceContext`
   (`inc/economy/Player.h:266,288`) liczy je z `tilemap[...].owner == this` — czyli po
   usunięciu terytorium ZAWSZE false. Dziś nic nie emituje modyfikatorów Territory
   (grep: tylko fabryka `BalanceModifierScope::Territory()` bez wywołań), więc to martwy
   kod, nie aktywny bug — ale kolejny relikt `tile.owner` do wyplenienia (→ T2).
6. **Dwie niezależne ścieżki stawiania budynku**: `Player::Build<T>`
   (`inc/economy/Player.h:94-125`) i `ExecuteCommand`/Build
   (`src/core/GameWorld.Commands.cpp:158-185`) duplikują tę samą sekwencję
   (koszt → BuildOnTile → buildTime → UpdateNavMap → AutoConnectBuilding) z drobnymi
   różnicami — ryzyko rozjazdu (AI/testy używają `Build<T>`, gracz idzie komendą).
7. **`CountIncomingResources`/`GetReceiveCapacity` zduplikowane**: raz jako wspólne
   helpery komponentów (`src/economy/BuildingComponentsInternal.h`), drugi raz jako
   kopie w anonimowej przestrzeni `src/economy/Building.cpp:11-54`.

---

## Analiza fasady `Building` i boga-`Player` (uzupełnienie na życzenie użytkownika)

### `Building` — 26 metod fasadowych wciąż omija system komponentów

`inc/economy/Building.h:117-157` + `src/economy/Building.cpp:88-379`. Łącznie ~180
call sites w src/inc/tests (najwięcej: `AddResource` 21, `GetOutputBufferViews` 17,
`IsStorageLike` 16, `GetResource` 13, `SetSupplier` 12). Klasyfikacja:

**Grupa A — czysta delegacja 1:1 do jednego komponentu (do usunięcia mechanicznie):**
`HasSupplier`, `HasReceiver`, `SetSupplier`, `SetAlternativeReceiver`, `RemoveSupplier`,
`CancelRequestedResource` (→ `LogisticsComponent`), `GetProductionProgress`
(→ `ProductionComponent`), `GetWorkerRatio`, `GetAssignedWorkers`, `GetWorkerCapacity`
(→ `WorkerComponent`), `CanBlockProduction`, `IsStorageLike` (→ `HasComponent<T>()`).
Caller robi `GetComponent<T>()` + nullcheck — dokładnie po to jest rejestr komponentów.

**Grupa B — realna polityka między-komponentowa ("który komponent jest endpointem
zasobowym budynku"):** `AddResource`, `GetResource`, `ReturnOutgoingResource`,
`CanAcceptResource`, `CanReceiveResource`, `GetInputBufferViews`, `GetOutputBufferViews`,
`HandleTransport`, `SetReceiver`/`RemoveReceiver` (wariant log+prod vs storage-hub),
`GetSupplierViews`/`GetReceiverViews`, `IsProductionStalled`. Każda z nich to ręczny
łańcuch `if prod → if storage → if population` — polityka priorytetu endpointu
zakodowana N razy. Tego NIE da się "po prostu usunąć" — trzeba ją przenieść w jedno
miejsce (interfejs endpointu, patrz T14).

**Grupa C — fasady w podklasach i duplikacja konstrukcji:**
- `Village` ma 4 wrappery delegujące do `population` (`Building.h:307-310`),
  `Road` 2 wrappery do `road` (`Building.h:268-269`).
- 16 niemal identycznych konstruktorów budynków produkcyjnych
  (`Building.cpp:552-770`): `RegisterComponent`×4 + `ApplyBuildingDefinition` +
  `ApplyProductionDefinition` + `ApplyProductionRecipes` — do zwinięcia w jeden helper.
- Metody komponentów wszędzie przeciągają parametr `Building& self`
  (np. `LogisticsComponent::SetSupplier(type, supplier, self)`), mimo że
  `IBuildingComponent::OnAttached(Building&)` już istnieje i mógłby dać komponentowi
  stały back-pointer.

### `Player` — ~45 metod, z czego większość to delegacja do istniejących submodułów

`inc/economy/Player.h` (365 linii). Katalog wg domen:

| Domena | Metody | Docelowy dom |
|---|---|---|
| Balans/staty (13) | `ModifyBalance`, `ModifyBalanceInt`, `ModifyBalanceAt`, `ModifyBalanceIntAt`, `ModifyBalanceForBuilding`, `ModifyBalanceIntForBuilding`, `ModifyBalanceForUnit`, `ResolveStat`×2, `ResolveStatAt`×2, `MakeBalanceContext`×2 | Wszystkie to cienkie wrappery na `balanceModifiers.ModifyDouble/Int` + budowa kontekstu → submoduł `PlayerBalance` (owns `BalanceModifierSet` + fabryka kontekstów) |
| Research/focus/state (11) | `UpdateFocus`, `UpdateResearch`, `IsTechnologyInProgress`, `CanResearchTechnology`, `CanUnlockFocus`, `UnlockFocus`, `StartFocus`, `UnlockTechnology`, `StartTechnologyResearch`, `RefreshTechnologyModifiers`, `ResetResearchState` | Submoduł `PlayerResearch` agregujący `TechnologyState`+`FocusState`+`StateDevelopment`, emitujący do `PlayerBalance` |
| Delegaty do dataTracker (6) | `TrackAcceptedCommand`, `GetTrackedBuildings`, `GetTrackedBuildingsWithComponent`, `HasTrackedBuilding`, `GetTrackedBuildingCount`, `GetAcceptedCommandCount` | `dataTracker` jest publiczny — usunąć wrappery, callers używają go wprost; rejestry ETAP 10 (`storages`/`villages`/`registryGeneration`) przenieść do `PlayerDataTracker`, wtedy `Register/UnregisterBuilding` w całości tam |
| Budowa/koszty (7) | `Build<T>`×2, `HasBuildResources`, `GetBuildRequirementFailures`, `GetEffectiveBuildCosts`, `CanBuildDefinition`, `TryPayBuildCost`, `RefundBuildCost` | Scalić z `BFactory` (`build`) w jeden moduł stawiania + JEDNA wspólna procedura placementu dzielona z `ExecuteCommand` (znalezisko #6) |
| Populacja/manpower (5) | `GetPopulationCap`, `GetTotalPopulation`, `GetFoodProductivity`, `AddManpower`, `AutoAssignWorkers` | Mały submoduł `PlayerPopulation` (albo świadomie zostają — jedyna domena bez własnego domu; decyzja przy T15) |
| Transport (1) | `BeginTransport` | Trywialny delegat do `roadNetwork` — usunąć lub zostawić (1 linia) |

`PlayerEconomyTelemetry` (w `PlayerEconomy.h`) już jest poprawnie wydzielonym modułem —
wzór do naśladowania dla powyższych.

---

## Lista zadań dla agenta

**Zalecana kolejność:** T1 → T2 → T14 (refactor `Building`, wchłania część T3) → T3 →
T4-T9 (GUI/UX) → T15 (dekompozycja `Player`) → T10 → T12 → T13; T11 osobno, po decyzjach
projektowych. Uzasadnienie: T1/T2 odblokowują ekonomię (warunek testowania reszty);
T14 przed T3, bo endpoint zasobowy rozwiązuje połowę T3 czyściej; T15 po fali bugfixów,
żeby czysto mechaniczny refactor nie mieszał się w diffach z poprawkami zachowania
(GUI nie ma testów automatycznych — zmiany zachowania weryfikuje ręcznie user, refactor
musi być behavior-neutral). Każde zadanie: osobny commit (lub seria), pełen suite testów
po zmianie (`.\run_tests.ps1`), bez zmian formatów serializacji poza zadaniami, które
jawnie to przewidują.

### T1 (P0) — Przywrócić transport: naprawić `RoadNetwork::CalculatePath`

**Pliki:** `src/simulation/RoadNetwork.cpp` (177-273), `inc/simulation/RoadNetwork.h`, testy.

**Zmiana:**
- Usunąć warunek `tilemap->GetTile(start).owner != src->owner` dla kafli startowych
  (kafle startowe to footprint budynku źródłowego — z definicji "nasze").
- Zastąpić warunek `tilemap->GetTile(next).owner != src->owner` (linia 252) testem
  własności **budynku drogi z nav mapy**, nie kafelka: kafelek przechodzi, jeśli
  `navMap->map[next].IsRoad() && navMap->map[next].node->owner == src->owner`
  LUB `navMap->map[next].node == dest`. To zachowuje regułę "nie jeździmy po drogach
  wroga" bez reliktu terytorium.
- Sprawdzić, czy `CanReserveTransportPath` / `CountReservedRoadCapacity` nie mają
  analogicznych filtrów po `tile.owner` (audyt reszty pliku).

**Test regresyjny (kluczowy — obecne testy tego nie łapią):** świeży świat BEZ save/load:
HQ + Woodcutter + droga między nimi zbudowane komendami → po N tickach drewno ląduje
w magazynie HQ. Drugi test: droga gracza A nie może być użyta przez transport gracza B.

**Kryteria akceptacji:** oba nowe testy zielone; istniejące testy RoadNetwork/logistyki
bez regresji; w grze (weryfikacja manualna użytkownika) drewno/food/strzały płyną.

### T2 (P0) — Wyplenić relikty `tile.owner` (decyzja: terytorium nie wraca)

**Pliki:** `src/core/GameWorld.TileMap.cpp`, `src/ai/Controller.cpp`,
`src/core/GameWorld.Render.cpp`, `src/core/GameWorld.Persistence.cpp`,
`inc/simulation/MapGenerator.h`.

**Zmiana (po T1):**
- Usunąć martwe `Tile::CanBuild` i `Tile::SetOwner` (zaktualizować
  `tests/TileMapDomainTests.cpp`).
- AI (`Controller.cpp:580-601, 774`): przepisać oceny "owned/enemy strategic tiles"
  z `tile.owner` na własność pobliskich BUDYNKÓW (np. dystans do wrogich struktur,
  posiadane kopalnie) — minimalnie: udokumentować, że metryka jest martwa, i wyzerować
  jej wpływ, żeby nie fałszowała osi. Pełny AI overhaul to osobny projekt — tu tylko
  nie kłamać.
- Render (`GameWorld.Render.cpp:191`): usunąć martwą gałąź tintu własności lub oprzeć
  o właściciela budynku.
- Persistence: NIE zmieniać formatu w tym zadaniu (owner zapisuje się jako pusty) —
  jedynie dodać komentarz; usunięcie pola z formatu = razem z najbliższym bumpem
  save version.
- Balance: usunąć martwy `BalanceModifierScopeType::Territory` + pole
  `BalanceModifierContext::ownedTerritory` + jego wyliczanie w
  `Player::MakeBalanceContext` (`Player.h:266,288`) — nic tego nie emituje (znalezisko #5),
  a wyliczenie czyta `tile.owner`. Jeśli user chce zachować scope "na przyszłość",
  zostawić enum, ale usunąć kłamliwe wyliczenie.
- Usunąć pole `Tile::owner` dopiero, jeśli po powyższym nie ma już żadnych czytelników
  (decyzja w trakcie — jeśli zostaje w save, zostaje pole).

**Kryteria:** brak odwołań do `tile.owner` poza (ewentualnie) serializacją; testy zielone.

### T3 (P1) — Zaopatrzenie wieży i Barracks: domknąć pull + poprawić auto-connect

**UWAGA — zależność od T14:** sedno tego zadania ("bufor amunicji/kosztów widoczny jako
wejście") to dokładnie problem, który rozwiązuje interfejs endpointu zasobowego z T14
(StorageComponent deklaruje swoje bufory także jako input views). Rekomendacja: wykonać
T14 najpierw, a T3 zredukować do punktów auto-connect + pull poniżej. Jeśli user woli
szybki bugfix przed refactorem — wariant minimalny opisany niżej działa samodzielnie.

**Pliki:** `src/core/GameWorld.TileMap.cpp` (`AutoConnectBuilding`, 584-627),
`src/economy/Building.cpp` (`GetInputBufferViews`, 270-280), `inc/economy/Building.h`,
`src/economy/TowerCombatComponent.cpp`, ew. `src/economy/RecruitmentComponent.cpp`.

**Zmiana:**
- W gałęzi storage-like `AutoConnectBuilding`: dodać warunek `building->CanAcceptResource(output.type)`
  przed `SetReceiver` oraz zamienić pełny skan tilemapy na iterację po
  `owner->GetTrackedBuildings()`.
- Wystawić bufory "input-like" budynków militarnych: po T14 — StorageComponent
  implementuje input views w interfejsie endpointu; wariant minimalny bez T14 —
  `GetInputBufferViews()` czyta też StorageComponent, gdy budynek ma
  TowerCombatComponent/RecruitmentComponent. Cel: auto-connect ustawia im suppliera
  (najbliższy magazyn) jak każdemu odbiorcy.
- Ujednolicić pull: `TowerCombatComponent::Update` już zamawia braki; Barracks powinien
  robić to samo dla kosztów jednostek (dodać `LogisticsComponent` do Barracks + analogiczny
  top-up, wzorem wieży) — albo świadomie udokumentować model push-only i wtedy usunąć
  martwy pull wieży. Rekomendacja: pull w obu (spójnie z wieżą).

**Kryteria:** test: świat z HQ (strzały w magazynie), wieżą i drogą → amunicja dojeżdża
bez ręcznych połączeń; test: Barracks + magazyn z mieczami + droga → rekrutacja możliwa.
Uwaga na determinizm: iteracje po kontenerach deterministycznych (std::map / posortowane).

### T4 (P1) — Feedback rekrutacji w GUI

**Pliki:** `src/ui/Gui.cpp` (panel rekrutacji, ~1804-1850),
`src/economy/RecruitmentComponent.cpp`, ew. `inc/economy/BuildingComponents.h`.

**Zmiana:** `QueueRecruitment` zwraca dziś gołe `bool` — panel nie mówi, czemu nie można
rekrutować. Dodać introspekcję (np. metoda `DiagnoseRecruitmentBlock(def)` zwracająca
enum/string: brak zasobu X w magazynie Barracks / brak manpower / kolejka pełna) i w GUI:
przycisk disabled + powód w tooltipie/podpisie. Pokazywać stan bufora kosztów
(ile mieczy jest / ile trzeba).

**Kryteria:** przy braku zasobów przycisk wyszarzony z czytelnym powodem; komenda dalej
walidowana po stronie symulacji (GUI to tylko podpowiedź, nie autorytet).

### T5 (P1) — Tooltips focus/tech tree: nazwy wszystkich statów

**Pliki:** `src/ui/GuiResearchTree.cpp` (138-234, 249-283), `inc/economy/BalanceStats.h`.

**Zmiana:**
- Uzupełnić `FocusStatLabel` o wszystkie wartości z `BalanceStat` (UnitHp, UnitRoadAttack,
  UnitSiegeAttack, UnitArmor, UnitMoveSpeed, UnitAttackSpeed, UnitRecruitTime,
  UnitRecruitManpowerCost, HqMaxHp, HqDefense, HqThorns, TowerDamage, TowerRange,
  TowerAttackSpeed, TowerAmmoEfficiency). Rozważyć przeniesienie mapy stat→label do
  wspólnego helpera przy `BalanceStats.h` (jedno źródło prawdy; GUI nie może znowu
  rozjechać się z enumem — statyczny `switch` bez `default` + `-Werror=switch` albo
  test kompletności).
- Dodać `DefenseTower` (i inne brakujące typy) do `FocusBuildingLabel`.
- Pokazywać filtr `unitDefId` ("for <unit displayName>") i `resourceCategory` w
  `FormatModifierForFocusTooltip`.
- Zaktualizować `LowerValueIsBetter`/`IsPositiveModifier` o nowe staty, gdzie "mniej =
  lepiej" (UnitRecruitTime, UnitRecruitManpowerCost) — inaczej bonusy pokażą się jako kary.

**Kryteria:** hover na każdej pozycji płaskiej ściągawki w focuses.rtsdata pokazuje nazwę
statu + wartość; test jednostkowy kompletności etykiet względem enuma (iteracja po
wszystkich wartościach).

### T6 (P1) — Spójna metryka żywności (HUD 30% vs wioska 0%) + sprzątanie reliktu

**Pliki:** `src/ui/GuiHudPanels.cpp` (22, 42, 183, 322-326, 499),
`src/economy/PopulationComponent.cpp`, `src/economy/Player.cpp` (128-143).

**Zmiana:**
- HUD-owy chip "Food" pokazuje dziś `GetFoodProductivity()` = średnia
  `GetWorkerProductivity()` = `0.3 + 0.7*ratio` (podłoga 30%), a panel wioski surowy
  `GetFoodSupplyRatio()`. Ujednolicić: chip = średni **food supply ratio** (0-100%),
  a produktywność pracowników (z podłogą 30%) pokazywać osobno i pod własną nazwą
  ("Worker productivity") tam, gdzie ma znaczenie. Tooltip HUD niech pokazuje obie liczby.
- Usunąć/przepisać komentarz o supply packages (`PopulationComponent.cpp:18-20`) i podjąć
  decyzję o `RequestFoodSupply`: skoro pull ma wrócić w T3 dla wieży/Barracks, spójnie
  przywrócić pull żywności wioski (top-up bufora jak wieża) zamiast polegać wyłącznie
  na push'u magazynów — albo usunąć martwą metodę. Rekomendacja: przywrócić pull.

**Kryteria:** te same liczby w HUD i panelu wioski przy tych samych warunkach; brak
odwołań do usuniętego systemu pakietów.

### T7 (P1) — Okrąg zasięgu wieży po zaznaczeniu

**Pliki:** `src/ui/GuiController.cpp`/`GuiController.h` (`SelectedBuildingWidget`),
ew. `src/ui/GuiMapWidgets.cpp`.

**Zmiana:** gdy zaznaczony budynek ma `TowerCombatComponent`, rysować w world-space okrąg
o promieniu `GetModifiedRange(*building)` (w kaflach → piksele przez skalę kamery),
środek = środek footprintu. Półprzezroczysty ring (fill + obwódka). Bonus (opcjonalny,
jeśli tanie): ten sam ring w ghost preview trybu budowania wieży (`BuildGhostWidget`).

**Kryteria:** manualna weryfikacja użytkownika; zero wpływu na symulację (czysty render).

### T8 (P1) — Kamera na RMB (decyzja UX z konfliktem do rozwiązania)

**Pliki:** `src/ui/GuiCommon.cpp` (66, `UpdateCameraDrag`), `src/ui/GuiController.cpp`
(`BasicMapViewSystem::RmbPressed/RmbReleased`, 464-503), pozostałe systemy z kamerą
(`GuiBuildModes.cpp`, `GuiHudPanels.cpp`, `GuiRoster.cpp`, `GuiResearchTree.cpp`).

**Konflikt:** RMB w widoku mapy przy zaznaczonym budynku przypisuje dziś receiverów
logistyki (`GameCommand::SetReceiver`). Propozycja: rozróżnić klik od przeciągnięcia —
RMB-drag (przesunięcie > ~4 px) = pan kamery, RMB-click bez ruchu = dotychczasowa akcja
przypisania receivera. MMB może zostać jako alias pan.

**Zmiana:** przenieść drag kamery z `MOUSE_BUTTON_MIDDLE` na RMB we wspólnym
`UpdateCameraDrag` + spiąć `rmbp`/`rmbr` we wszystkich systemach; drzewko badań już
panuje RMB (spójność zachowana).

**Kryteria:** pan działa RMB we wszystkich trybach (mapa, build, road, destroy, roster);
przypisywanie receiverów kliknięciem RMB nadal działa; manualna weryfikacja użytkownika.

### T9 (P1) — ESC: powrót z menu gry + spójna obsługa w menu głównym

**Pliki:** `src/scenes/GameMenuScene.cpp` (101-116), `src/scenes/GameScene.cpp`,
`inc/scenes/Scenes.h`, ew. `src/ui/GuiController.cpp` (`BasicMapViewSystem::EscPressed`,
265-277), sceny menu głównego.

**Diagnoza:** `escArmed` jest polem żyjącej cały czas instancji sceny — po pierwszej
wizycie zostaje `true` na zawsze; brak symetrycznej ochrony w GameScene przed tym samym
edge'em ESC, który zamknął menu → ping-pong otwórz/zamknij albo martwy ESC.

**Zmiana (kierunek zgodny z życzeniem użytkownika — scena menu ma własny input handling):**
- Dodać do scen hook aktywacji (np. `Scene::OnEnter()` wołany przy `ChangeSceneEvent`)
  i resetować w nim `escArmed = false` w GameMenuScene; analogiczne uzbrajanie po stronie
  GameScene (nie reagować na ESC w klatce, w której scena została aktywowana).
- Docelowo: GameMenuScene dostaje własny lekki `InputEventSubscriber`/kontroler zamiast
  ręcznego pollingu w `Update` — spójnie z architekturą input-scopingu z refactoru
  (`GuiSystem::OnActivate/OnDeactivate`).
- Menu główne: ESC w podmenu (Options/Load/Save/Multiplayer) wraca do poprzedniego menu
  tym samym mechanizmem.

**Kryteria:** wielokrotne otwieranie/zamykanie menu ESC-iem działa deterministycznie
(także trzymając ESC); ESC w podmenu głównego menu cofa o poziom; brak regresji skrótów
w grze.

### T10 (P2) — Naprawić 8 testów "forestry" (zielone CI)

**Pliki:** `tests/TechnologyTests.cpp`, `tests/PlayerEconomyTests.cpp`,
`tests/ResearchCatalogTests.cpp`, ew. `assets/data/technologies.rtsdata`.

<!-- **Zmiana:** dostosować 8 testów do realnego drzewa SCIENCE z `technologies.rtsdata`
(algebra→trygonometria→…) — testy mają testować mechanikę (unlock, prereq, koszty),
nie konkretne nazwy; tam gdzie test potrzebuje "jakiejś" technologii root, pobierać
pierwszą z katalogu zamiast hardcode "forestry". Alternatywa (jeśli user woli): dodać
"forestry" do danych. Decyzja domyślna: poprawić testy, danych nie ruszać. -->

<!-- **Kryteria:** pełen suite lokalnie zielony; CI zielone bez `continue-on-error`. -->

Nieaktualne - po prostu usunąć te 8 testów ponieważ tech tree będzie manualnie robione od podstaw. Wrócimy do tego w przyszłości.

### T11 (P2, duże) — Realny resync-after-desync (`GameWorld::LoadFromSnapshot`)

Bez zmian względem opisu w `docs/tech_debt.md` (sekcja "Pełne snapshoty mapy przez TCP") —
nowy ekonomiczny format snapshotu (może reużyć format save), `LoadFromSnapshot`,
podpięcie w `ClientSession`/`GameScene`, bump wire version. Osobna sesja/feature —
nie mieszać z zadaniami wyżej. Wymaga decyzji projektowych użytkownika przed startem
(zakres stanu, strategia chunkowania, zachowanie w trakcie walki).

### T12 (P2) — Przejęte budynki w sieci dróg zwycięzcy

**Pliki:** `src/core/GameWorld.Elimination.cpp` (sekcja "Production buildings change hands").

**Zmiana:** przy przejęciu: dla każdego kafla footprintu
`loser->roadNetwork->UpdateNavMap(tileId, nullptr)` +
`winner->roadNetwork->UpdateNavMap(tileId, building)` (API istnieje —
`UpdateNavMap(id, nullptr)` czyści węzeł; wpis w tech_debt.md twierdzący, że brak
removal API, jest nieaktualny). Przemyśleć też drogi (Road) pokonanego na trasie do
przejętych budynków — bez nich zwycięzca i tak nie pojedzie (decyzja: drogi przechodzą
razem z budynkami produkcyjnymi?).

**Kryteria:** test: po eliminacji zwycięzca buduje drogę do przejętego budynku i transport
działa; stare połączenia nie duplikują się.

### T13 (P2) — Sprzątanie dokumentacji i docs-sync

**Pliki:** `docs/tech_debt.md`, `CLAUDE.md`, `TODO.md`, `inc/data/Resource.h`.

**Zmiana:**
- tech_debt.md: odhaczyć pozycje zrobione (fat interface, CI filter, vcpkg cache),
  przeredagować wpis o Player (365 linii po splicie), skorygować wpis o removal API
  nav mapy, dopisać relikt `tile.owner` (naprawiony w T1/T2) jako lesson-learned.
- CLAUDE.md: łańcuchy produkcji — Inn produkuje FOOD_PROVISIONS (BREAD+MEAT+WATER),
  BEER nie ma producenta (usunąć z łańcucha albo zaznaczyć "planowany"); decyzja co
  z `ResourceType::BEER` w enumie (zostaje jako placeholder — nie ruszać enuma bez
  bumpa save/wire).
- TODO.md: przenieść zweryfikowane bugi do odnośnika na ten dokument, usunąć
  zdezaktualizowane opisy.

**Kryteria:** dokumenty zgodne ze stanem kodu na HEAD po wykonaniu T1-T12.

## Prawdziwa przyczyna "freeze produkcji przy 100%" — NIE perf, tylko bug logiczny (2026-07-12, follow-up #4)

Po follow-up #3 user zgłosił: duże freezy zniknęły, ale mikro-freeze przy 100%
**dokładnie taki sam jak wcześniej**. To był sygnał, że #1-#3 (wszystkie perf-owe)
naprawiały RÓŻNY problem niż ten, o który user pytał od początku. Poproszony
o ponowną analizę samej funkcji `ProductionComponent::Produce`, znaleziono
prawdziwy, czysto LOGICZNY bug — zero związku z wydajnością.

**Root cause:** `ProductionComponent::GetProgress()` liczyło procent względem
`cycleTime.GetBase()` (surowa, niezmodyfikowana wartość) — komentarz w kodzie
mówił wprost `// caller applies modifiers`, ale ŻADEN caller (`Building::
GetProductionProgress()`, jedyne realne wywołanie, `src/economy/Building.cpp:335`)
nigdy tego nie robił. Tymczasem `Produce()` decyduje, że cykl się kończy, gdy
`elapsed >= GetModifiedCycleTime(self)` — wartość PO zastosowaniu modyfikatorów
(technologie, focusy, **automatyczne bonusy StateDevelopment przy awansie
cywilizacji** — nie wybór gracza, dostają je WSZYSCY grający wystarczająco długo).
Te dwie wartości progu (surowa vs zmodyfikowana) były rozjechane w KAŻDEJ grze,
w której działał jakikolwiek modyfikator `ProductionCycleTime` — co, jak
potwierdził nowy test, dotyczy nawet ŚWIEŻEGO gracza (bazowy poziom
StateDevelopment sam w sobie daje niejednostkowy mnożnik).

Skutek zależny od kierunku modyfikatora:
- **Spowolnienie** (mnożnik >1, najczęstszy w normalnej progresji): prawdziwy próg
  ukończenia (`effective`) jest WIĘKSZY niż baza. Pasek liczony względem bazy
  osiąga i CLAMPuje się na 100% dużo WCZEŚNIEJ niż cykl faktycznie się kończy —
  wizualnie: pasek dochodzi do 100% i **zamiera tam** na resztę prawdziwego czasu
  cyklu, aż `elapsed` faktycznie dogoni `effective`. Dokładnie opisany objaw.
- Przyspieszenie (mnożnik <1): odwrotnie — cykl kończy się (i resetuje pasek do
  0%) zanim pasek zdąży wizualnie dojść do 100%, więc widać skok np. 80%→0%.

**Fix:** `ProductionComponent::GetProgress()` przyjmuje teraz `const Building&
self` i liczy względem `GetModifiedCycleTime(self)` (ta sama wartość, której
używa `Produce()` do faktycznego zakończenia cyklu) zamiast `cycleTime.GetBase()`.
Zaktualizowano jedyne realne wywołanie (`Building::GetProductionProgress()`).
Dodano test regresyjny (`tests/BuildingDomainTests.cpp`,
`ProductionProgressMatchesModifiedCycleTimeNotBaseline`) z jawnie dodanym
modyfikatorem spowalniającym — test empirycznie potwierdził, że nawet ŚWIEŻY
`Player` ma niejednostkowy `GetModifiedCycleTime` (15.75 zamiast oczekiwanych 15.0
przy dodaniu mnożnika 1.5x), dowodząc że bug uderzał od samego początku gry, nie
tylko po jakiejś specyficznej ścieżce rozwoju. Sprawdzono też `ResearchComponent::
GetProgress()` — TEN sam problem NIE występuje (tam `total`/`remaining` są
ustawiane raz na już-zmodyfikowanej wartości w `Start()`, nie ma osobnej pary
baza/modyfikator).

Pełen suite: 150 passed (nowy test), ten sam znany baseline 8 pre-existing.

**Lekcja:** trzy rundy poprawek wydajnościowych (#1-#3) były realne i wartościowe
(naprawiły faktyczne freezy AI), ale nie były przyczyną TEGO konkretnego zgłoszenia
— user musiał dwukrotnie powiedzieć "to nadal się dzieje" zanim analiza wróciła do
punktu wyjścia (samej funkcji `Produce`) zamiast dalej szukać w profilowaniu.
Gdy zgłoszony objaw nie ustępuje mimo zmierzonej i zweryfikowanej poprawki
wydajności, prawdopodobnie szuka się złej kategorii przyczyny — warto **wcześniej**
założyć "to może nie być perf" i przejrzeć logikę od zera, zamiast pogłębiać
profilowanie.

---

## Perf follow-up #3 (2026-07-12, po TRZECIM playteście — prawdziwy głęboki root cause)

User zgłosił po follow-up #2: duże zamrożenia (co kilka sekund) ZNIKNĘŁY, ale mikro-freeze
produkcji przy 100% NADAL występował. To dowodziło, że follow-up #2 naprawił tylko
NAJWIĘKSZY spike, nie WSZYSTKIE. Ponowny headless profiling (ta sama metoda: realny
świat 301×301 + 1 AI, teraz 300 sim-s) ujawnił DRUGI, subtelniejszy problem: ~18-22ms
spike co ~1.2-2s (kadencja decisionTimer AI), zaczynający się dopiero po t≈65.7s.

**Root cause — kaskada trzech nakładających się bugów, wszystkie w AI:**

1. **`RunUnifiedDecision` (Tier 3) systematycznie zawodzi i cofa się do
   `TryBuildEconomy` (legacy silnik ekonomiczny)** na KAŻDEJ decyzji — nie rzadko,
   jak sugerowały komentarze w kodzie ("safety net"). Naprawiono pośrednio przez
   punkty 2-3 poniżej (Tier 3 teraz częściej się udaje).
2. **`TryBuildEconomy`/`RunUnifiedDecision` próbowały KAŻDEGO kandydata budowy
   w pętli**, każdy płacąc pełne `FindBuildAnchor`. Ograniczono do 3 prób
   (`kMaxAnchorSearchAttempts`) w obu miejscach.
3. **Prawdziwy winowajca — `FindBuildAnchor` dla `HuntersHut` (typ terenu WOOD)
   zawodził KAŻDORAZOWO, płacąc pełny koszt okna nawet dla "taniej" warstwy 24
   kafli.** Przyczyna: kolejność sprawdzeń w `evaluateTile` — drogie
   `CanPlaceBuilding` (zawiera `IsWithinEnemyProximity`, własne zagnieżdżone
   7×7=49 sprawdzeń NA KANDYDATA) wykonywało się PRZED tanim sprawdzeniem typu
   terenu. Nawet po zamianie kolejności, las WOOD blisko bazy bywa już częściowo
   zajęty/wyczerpany przez istniejący Woodcutter — więc WIELE kafli WOOD
   przechodzi tani filtr terenu, ale i tak odpada dopiero na drogim sprawdzeniu
   zasobności/zajętości/sąsiedztwa wroga wewnątrz `CanPlaceBuilding`. Żaden tani
   pre-filter tego nie wyłapie bez znajomości stanu gry.

**Zmierzone empirycznie (Debug, headless, `player.roadNetwork` real world):**
- `FindBuildAnchor` sukces (1 próba, znaleziono blisko): 3.0-3.5ms → **0.7-1.0ms**
  po zmniejszeniu progów okien (12,24,48,96,fullMargin zamiast 24,48,96,fullMargin).
- `FindBuildAnchor` porażka dla HuntersHut (wyczerpuje tanie warstwy): stabilne
  **16-18ms KAŻDORAZOWO** — nie zmieniało się mimo poprzednich fixów, bo cooldown
  z follow-up #2 obejmował TYLKO warstwy 96+fullMargin, nie 12/24/48.

**Fix ostateczny:** cooldown (`expensiveAnchorSearchCooldown`, per `BuildingType`)
przeprojektowany z "gate tylko dla drogich warstw" na "gate całej funkcji" —
jeśli PEŁNE przeszukanie (wszystkie warstwy) zawiodło dla danego typu budynku,
KOLEJNE wywołania zwracają `{-1,-1}` natychmiast (bez żadnej warstwy) przez
120 sekund. To poprawne, bo żadna tania warstwa i tak nie pomaga, gdy problem
jest w głębokiej walidacji (zasobność/zajętość), nie w typie terenu.

**Wynik końcowy (60 sim-s, `tests/SimulationPerfTests.cpp`):** najgorszy tick
**7853ms → 9.4ms (follow-up #2) → 8.1ms (follow-up #3, po dodaniu stałego testu)**.
Spike'i >20ms: z dziesiątek co 1-2s do PRAKTYCZNIE ZERA (poza jednym ~180ms co
120s przy odświeżeniu cooldownu dla trwale nieplacowalnego HuntersHut — znacznie
rzadziej niż wcześniej i wciąż do rozważenia jako dalszy follow-up, np. wykrywanie
"ten typ budynku nie ma gdzie stanąć" i REZYGNACJA z niego zamiast ponawiania
w nieskończoność).

**Metoda debugowania (dla przyszłych sesji):** zgadywanie z lektury kodu (2 razy)
nie wystarczyło — dopiero BEZPOŚREDNIA INSTRUMENTACJA CZASOWA (tymczasowe
`std::chrono` + `fprintf(stderr,...)` wstawiane STOPNIOWO, coraz głębiej w stos
wywołań: `PrimitiveAIModel::Update` → `RunUnifiedDecision` → `ExecuteAction` →
`TryBuildEconomy` → `FindBuildAnchor`'s `evaluateTile`) precyzyjnie zlokalizowała
winowajcę (`building=16` = HuntersHut, widoczne w logu). Cała instrumentacja
diagnostyczna usunięta po zakończeniu — w kodzie zostały tylko fixy + komentarze
wyjaśniające.

---

## Perf follow-up #2 (2026-07-12, po DRUGIM playteście — właściwy root cause)

Pierwsza runda poprawek perf (sekcja niżej) okazała się NIEWYSTARCZAJĄCA — user
zgłosił, że oba objawy występują nadal. Tym razem problem odtworzono headless
(test mierzący czas każdego ticka `UpdateSimulation` na realnym świecie 301×301
z 1 AI, 6000 ticków): **10 ticków po ~7.5-7.9 SEKUND każdy** (Debug), w stałej
kadencji co ~124 ticki (1.24 s) — dokładnie interwał decyzyjny AI.

**Właściwy root cause — AI, nie transport:** po naprawie T1 ekonomia AI ożyła
(wcześniej martwa logistyka = zero zasobów = AI nigdy nie stać na budowę), więc
AI zaczęło realnie oceniać akcje budowy. `PrimitiveAIModel::FindBuildAnchor`
skanowało CAŁĄ tilemapę (~90k kafli), a dla każdego kafla-kandydata wołało
`DistanceToNearestInfrastructure`, które też skanowało całą tilemapę —
**O(mapa²) ≈ miliardy odwiedzin kafli na jedno wyszukiwanie**, a `TryBuildEconomy`
woła to per kandydat-budynek aż któryś się uda. Wątek symulacji trzyma world
lock przez cały tick → zamrożenie WSZYSTKIEGO (kamera, GUI, produkcja) na
sekundy, co ~1.2-2 s. Objaw "produkcja zacina się przy 100%" to niemal na pewno
ten sam freeze — pasek wizualnie zastyga na 100% gdy completion+restart wypada
w trakcie zamrożonego ticka.

**Fixy:**
1. `DistanceToNearestInfrastructure` (`src/ai/Controller.cpp`): iteracja po
   `GetTrackedBuildings()` (dziesiątki) zamiast po całej tilemapie (90k).
   Determinizm zachowany (min() jest niezależne od kolejności iteracji setu).
2. `FindBuildAnchor`: rozszerzające się okno wokół HQ/celu ({24, 48, 96, cała
   mapa} — scoring i tak silnie preferuje bliskość, więc praktyczne optimum
   zawsze jest blisko; pełny skan tylko jako ostatni fallback, np. ekstraktor
   szukający odległego złoża).
3. `RoadNetwork::CountIncomingToDestination`: pełny skan tilemapy per
   `BeginTransport` (per jednostka zasobu!) → iteracja po tracked buildings
   (ten sam kształt zapytania co `CountIncomingResources` w Building.cpp).
4. Cache ścieżek z rundy #1 rozszerzony z single-entry na pełną mapę
   `(src->id, dest->id) → path` (single-entry thrashował przy wielu parach
   w jednym ticku), czyszczoną w `UpdateNavMap`.
5. Usunięto martwe `CountReservedRoadCapacity`/`CheckIfPathWasTaken` (zero wywołań,
   oba z pełnymi skanami tilemapy).

**Pomiar (Debug, 60 sim-s, 6000 ticków):** najgorszy tick 7853.6 ms → **9.4 ms**
(~835x); ticków >10 ms: 21 → 0; suma 81.4 s → 2.9 s. Dodano STAŁY test
regresyjny `tests/SimulationPerfTests.cpp` (realny świat, 6000 ticków, fail gdy
jakikolwiek tick > 1000 ms — trzy rzędy wielkości zapasu na wolne CI, a regresja
O(mapa²) mierzyła 7700+ ms, więc nie przejdzie).

**Lekcja:** naprawa funkcjonalności (T1) odsłoniła DWA niezależne pokłady
latentnego kosztu — transportowy (runda #1, realny ale nie dominujący) i AI
(runda #2, właściwy winowajca). Zgadywanie z lektury kodu wskazało pierwszy;
dopiero headless pomiar per-tick wskazał drugi. Przy objawach perf ZAWSZE
najpierw odtworzyć i zmierzyć, potem naprawiać.

---

## Perf regression po T1 + fix stanu maszyny produkcji (2026-07-12, follow-up #1)

Zgłoszone przez użytkownika po ręcznym playteście T1-T3: (a) periodyczne zamrożenie
całej gry na kilka sekund (produkcja/kamera/GUI — wszystko), (b) widoczne zacięcie
paska produkcji dokładnie przy 100%, brak płynnego wejścia w kolejny cykl.

**(a) Root cause:** `RoadNetwork::CalculatePath` (`src/simulation/RoadNetwork.cpp`)
robi pełne BFS po siatce, alokując 3 wektory rozmiaru `sizeX*sizeY` (domyślnie
301×301 ≈ 90 601 kafli, `inc/simulation/MapGenerator.h:47`) **na każde wywołanie**.
Przed T1 ten koszt był latentny — każde wywołanie kończyło się natychmiast pustym
wynikiem (BFS queue pusta od startu przez zepsuty check `tile.owner`), więc pętle
dyspozycji (`LogisticsComponent::DispatchOutputs`, `StorageComponent::HandleTransport`)
wołały `BeginTransport`→`CalculatePath` raz i przerywały po pierwszym niepowodzeniu.
Po T1 transport faktycznie się udaje, więc te pętle (`while (freeCapacity > 0) { ...
BeginTransport(...); freeCapacity--; }`) lecą do końca — **raz na KAŻDĄ jednostkę
zasobu** wysyłaną w danym ticku do tego samego odbiorcy, każda z osobnym pełnym BFS.
Przy większym magazynie (`freeCapacity` rzędu dziesiątek/setek) lub burst po
"odblokowaniu" całej wcześniej zablokowanej logistyki (wszystkie bufory produkcyjne
w grze były pełne przez cały czas trwania buga T1 — pierwszy tick po fixie odblokował
je WSZYSTKIE naraz) to setki-tysiące pełnych BFS w jednym ticku.

Zmierzone empirycznie (tymczasowy mikrobenchmark, usunięty po użyciu, mapa 301×301,
build Debug): **~0.62 ms/wywołanie** bez cache vs **~0.0037 ms/wywołanie** z cache
trafiającym dla tej samej pary (src, dest) — **~170x przyspieszenie**. Przy dziesiątkach-
-setkach wywołań w jednym ticku to różnica między niezauważalnym kosztem a
wielosekundowym zamrożeniem całego świata (symulacja działa na jednym wątku
blokującym cały tick, stąd zamrożenie obejmuje też kamerę/GUI, nie tylko produkcję).

**Fix:** dodano cache ostatniej pary (src, dest)→path w `RoadNetwork`
(`cachedPathSrc`/`cachedPathDest`/`cachedPath`/`cachedPathValid`), invalidowany przy
KAŻDEJ zmianie topologii sieci (`RoadNetwork::UpdateNavMap`, wołane przy budowie/
zniszczeniu dowolnego budynku/drogi). Ponieważ pętle dyspozycji wołają `BeginTransport`
wielokrotnie pod rząd dla TEJ SAMEJ pary (src, dest), pojedynczy wpis cache eliminuje
niemal cały narzut powtórnego obliczania w ramach jednej paczki dyspozycji.
Głębszy fix (zamiana pełnego BFS po siatce na graf połączony wyłącznie przez drogi,
O(reachable) zamiast O(sizeX*sizeY) niezależnie od cache'a) — poza zakresem tej
poprawki, do rozważenia jeśli cache okaże się niewystarczający przy bardzo dużych
mapach lub bardzo częstej zmianie topologii dróg.

**(b) Root cause (osobny, niepowiązany z T1 — ujawniony teraz, bo produkcja w ogóle
kończy cykle):** `ProductionComponent::Produce` (`src/economy/ProductionComponent.cpp`)
miało strukturę `if (started) {...} else {...}` — po zakończeniu cyklu
(`started = false; elapsed = 0.0`) sprawdzenie "czy mogę zacząć nowy cykl" (branch
`else`) wykonywało się dopiero w NASTĘPNYM wywołaniu `Update()` (kolejny tick
symulacji), nie w tym samym. `GetProgress()` zwraca `0.0f` gdy `!started` — więc przez
co najmniej jeden tick symulacji pasek pokazywał 0%/zatrzymanie zamiast płynnie
przejść w nowy cykl, mimo że surowce były już dostępne. **Fix:** po zakończeniu cyklu
kod "spada" (fallthrough) do tej samej logiki sprawdzania warunków startu w TYM SAMYM
wywołaniu `Produce()`, zamiast czekać na kolejny tick — zero-tickowe przejście, gdy
zasoby są dostępne.

Oba fixy zweryfikowane pełnym test suite (148 passed, ten sam baseline 8 pre-existing).

---

### T14 (P1, refactor) — ODŁOŻONE w sesji implementacyjnej 2026-07-12

Po zaimplementowaniu T1/T2/T3 podjęto decyzję (bez usera obecnego do konsultacji —
osąd autonomicznej sesji) o ODŁOŻENIU tego zadania. Powód: rzeczywisty rozmiar
call sites dla samej "Grupy A" (proste delegaty) to ~54 miejsca w `Gui.cpp`,
`Controller.cpp` (12!), `LogisticsComponent.cpp`, `GameWorld.TileMap.cpp`,
`GameWorld.Persistence.cpp`, `Player.cpp`, `RoadNetwork.cpp`, `Building.cpp` +
3 pliki testowe — a GUI i AI **nie mają pokrycia testami automatycznymi**
(potwierdzone w tech_debt.md), więc autonomiczna sesja nie ma jak zweryfikować,
że refactor nie zepsuł zachowania GUI/AI ponad to, co pokrywają testy jednostkowe.
To czysty refactor jakości kodu (nic dziś nie jest złamane przez fasadę) —
zbyt duży blast radius, żeby ciąć bez człowieka do ręcznego playtestu po drodze.
Plan poniżej pozostaje w pełni aktualny i gotowy do wykonania w dedykowanej sesji
(najlepiej z userem sprawdzającym GUI po każdym etapie 1-4).

Oryginalny plan:

**Kontekst:** patrz sekcja "Analiza fasady `Building`" wyżej — 26 metod fasadowych,
~180 call sites, polityka endpointu zasobowego skopiowana w ~10 if-łańcuchach.

**Pliki:** `inc/economy/Building.h`, `src/economy/Building.cpp`,
`inc/economy/BuildingComponents.h`, `src/economy/*Component.cpp`,
`src/economy/BuildingComponentsInternal.h` + wszystkie call sites
(`GameWorld.TileMap.cpp`, `GameWorld.Persistence.cpp`, `LogisticsComponent.cpp`,
`StorageComponent.cpp`, `ai/Controller.cpp`, `ui/Gui.cpp`, `ui/GuiMapWidgets.cpp`, testy).

**Zmiana — etapami, każdy osobny commit, zachowanie bez zmian:**

1. **Back-pointer komponentu:** `IBuildingComponent::OnAttached(Building&)` zapisuje
   `Building* self` w komponencie → usunąć parametr `Building& self` ze wszystkich
   sygnatur komponentów (`LogisticsComponent::SetSupplier(type, supplier, self)` →
   `SetSupplier(type, supplier)` itd.). Czysto mechaniczne, odblokowuje resztę.
2. **Grupa A (czysta delegacja) — usunąć z Building:** `HasSupplier`, `HasReceiver`,
   `SetSupplier`, `SetAlternativeReceiver`, `RemoveSupplier`, `CancelRequestedResource`,
   `GetProductionProgress`, `GetWorkerRatio`, `GetAssignedWorkers`, `GetWorkerCapacity`,
   `CanBlockProduction`, `IsStorageLike`. Callers przechodzą na
   `GetComponent<T>()`/`HasComponent<T>()` z nullcheckiem. `IsStorageLike()` →
   `HasComponent<StorageComponent>()` wprost.
3. **Grupa B — interfejs endpointu zasobowego:** nowy interfejs (np. `IResourceEndpoint`:
   `CanAccept`, `CanReceive`, `Add`, `Take`, `Return`, `GetInputViews`, `GetOutputViews`,
   `HandleTransport`), implementowany przez `ProductionComponent`, `StorageComponent`,
   `PopulationComponent`. `Building` rozwiązuje endpoint RAZ przy `RegisterComponent`
   (pierwszy zarejestrowany endpoint wygrywa — zachowuje dzisiejszy priorytet
   prod > storage > population) i wystawia `GetResourceEndpoint()`. Dzisiejsze
   if-łańcuchy z `Building.cpp` znikają; logika per-komponent (np. dekrement
   `pendingRequests` po przyjęciu zasobu, storage-hub semantyka `SetReceiver`)
   przenosi się DO komponentów. **Kluczowa synergia z T3:** `StorageComponent`
   deklaruje swoje bufory także jako input views → wieża/Barracks stają się widocznymi
   odbiorcami dla auto-connect bez specjalnych przypadków.
   `IsProductionStalled` (diagnostyka między-komponentowa dla GUI) → przenieść do
   `ProductionComponent::IsStalled()` (ma back-pointer, sam odpyta sąsiednie komponenty).
4. **Grupa C — sprzątanie:** usunąć wrappery `Village` (4) i `Road` (2) — callers
   używają komponentów; zwinąć 16 konstruktorów produkcyjnych w jeden helper
   (`InitProductionBuilding(*this, BuildingType::X)` rejestrujący komplet komponentów
   i aplikujący definicje); zdeduplikować `CountIncomingResources`/`GetReceiveCapacity`
   (kopie w `Building.cpp` vs `BuildingComponentsInternal.h`) do jednej definicji.
5. **Docelowa powierzchnia `Building`:** id/lifecycle/konstrukcja/transportables +
   `RegisterComponent`/`GetComponent`/`HasComponent` + `GetResourceEndpoint()` +
   metadane renderu. Nic domenowego.

**Kryteria:** zero zmian zachowania (testy przechodzą bez zmiany oczekiwań — dozwolone
tylko mechaniczne poprawki wywołań); liczba metod `Building` spada z ~40 do ~15;
`grep -c "GetComponent<"` rośnie w call sites zamiast fasad; save/wire format bez zmian.
Po tym zadaniu wpis tech_debt.md o kompozycji `Building` można ostatecznie zamknąć.

### T15 (P2, refactor) — `Player`: agregacja metod w submodułach

**Kontekst:** patrz tabela "Analiza boga-`Player`" wyżej — ~45 metod w 6 domenach,
większość to cienkie wrappery na już istniejące publiczne submoduły.

**Pliki:** `inc/economy/Player.h`, `src/economy/Player.cpp`, nowe
`inc/economy/PlayerBalance.h`, `inc/economy/PlayerResearch.h` (nazwy robocze),
`inc/economy/PlayerDataTracker.h`, `inc/economy/BuildingFactory.h` + call sites
(komponenty, warfare, AI, GUI, GameWorld.*, testy).

**Zmiana — etapami:**

1. **`PlayerBalance`** — przenieść `BalanceModifierSet balanceModifiers` + obie
   `MakeBalanceContext` + wszystkie `ModifyBalance*`/`ResolveStat*`/`ModifyBalanceForUnit`
   do submodułu (member `balance`); callers: `owner->ResolveStat(...)` →
   `owner->balance.ResolveStat(...)`. To najgorętsza ścieżka (komponenty, warfare) —
   zmiana mechaniczna, bez zmiany semantyki. Wyliczenie `ownedTerritory` znika już w T2.
2. **`PlayerResearch`** — przenieść `TechnologyState`+`FocusState`+`StateDevelopment`
   + 11 metod research/focus (w tym `UpdateFocus`/`UpdateResearch`/`RefreshTechnologyModifiers`,
   które emitują do `balance`). Persistence czyta/pisze te same dane — format bez zmian,
   tylko ścieżki dostępu.
3. **DataTracker wprost** — usunąć 6 delegatów (`GetTrackedBuildings` itd.), callers
   używają `player->dataTracker`; przenieść rejestry ETAP 10 (`storages`, `villages`,
   `registryGeneration`) do `PlayerDataTracker`, a `RegisterBuilding`/`UnregisterBuilding`
   w całości tam (Player woła jedną metodę trackera).
4. **Jedna procedura placementu** — scalić `Player::Build<T>` z logiką
   `ExecuteCommand`/Build (znalezisko #6) w jedną wspólną funkcję (np. w `BFactory`
   albo wolna funkcja w `GameWorldInternal.h`): koszt → BuildOnTile → buildTime →
   UpdateNavMap → AutoConnectBuilding. Komenda i `Build<T>` wołają to samo.
   Przy okazji `HasBuildResources`/`GetEffectiveBuildCosts`/`TryPayBuildCost`/
   `RefundBuildCost`/`GetBuildRequirementFailures`/`CanBuildDefinition` idą do tego
   modułu budowy.
5. **Populacja/manpower (decyzja usera):** `GetPopulationCap`/`GetTotalPopulation`/
   `GetFoodProductivity`/`AddManpower`/`AutoAssignWorkers` — albo mały `PlayerPopulation`,
   albo świadomie zostają na Player jako jedyna pozostała domena. Rekomendacja: wydzielić,
   skoro i tak dotykamy call sites.
6. `BeginTransport` — usunąć (callers → `roadNetwork->BeginTransport`) albo zostawić
   jako jedyny 1-linijkowy alias; rekomendacja: usunąć dla spójności.

**Kryteria:** zachowanie bez zmian (pełen suite zielony bez zmiany oczekiwań);
`Player.h` zostaje z danymi + konstruktorem + kilkoma hookami cyklu życia; save/wire
format bez zmian (bit-w-bit ten sam zapis); brak nowych `#include` cykli (submoduły
nie includują Player.h — dostają referencje w konstruktorze, wzorem `BFactory`).

---

## Follow-up #5 — rekrutacja ram/knight + reorder generacji mapy + bug determinizmu

**Zgłoszenie usera (po follow-up #4):** dwa nowe problemy — (1) nie da się rekrutować
`ram`/`knight` w Barracks, tylko militia/swordsman; (2) trasa marszu jednostek czasem
przecina drogi surowcowe wygenerowane na starcie oraz Village — wymagany reorder
generacji: HQ → droga wojskowa (+ więcej serpentyn/noise) → Village + droga do HQ →
połacie startowego drzewa/kamienia.

**Fix #1 — rekrutacja ram/knight:** `Barracks` w `assets/data/buildings.rtsdata` nie
deklarował buforów storage dla `STEEL_SWORD`/`PLANKS`/`IRON` (koszty knight/ram) ani
`FOOD_PROVISIONS`/`IRON_SWORD` — `QueueRecruitment` odrzucał te przepisy po cichu
(brak bufora = brak zasobu = "nie stać"). Dodano brakujące linie `storage`. Test
regresyjny: `BuildingDomainTests.BarracksCanRecruitKnightAndRamNotJustSwordsman`.

**Fix #2 — reorder generacji + serpentyny:** `GameWorld::CreateStartingBase` rozbity na
`CreateStartingHq` (tylko HQ + `PrepareStartingArea`, MUSI biec przed drogą wojskową,
inaczej jej "ignore all bases" fallback może przeciąć jeszcze niepostawione HQ — to
faktycznie wywaliło `HqCombatSystemTests` w pierwszej próbie, złapane przed commitem)
i `CreateStartingVillageAndResources` (village + start road + patch drzewa/kamienia,
biegnie PO drodze wojskowej). `InitWorld`/`InitMultiplayerWorld`: HQ (wszyscy) → droga
wojskowa → village+resources (wszyscy). Village `gap` 3→6 (nie ma już powodu się tłoczyć).
`MilitaryRoadNetwork`: nowa `SerpentineBias(x,y,seed)` (dwie fale sinus, deterministyczna
funkcja pozycji+seed) dodana do kosztu Dijkstry per-edge (`edgeSeed` osobny na parę
graczy) — droga faluje zamiast iść najkrótszą linią.

**Bug znaleziony przy weryfikacji — złamany determinizm lockstep:** po reorderze
`HqCombatSystemTests.SiegeToEliminationIsDeterministicForSameSeed` zaczął failować
~2/3 uruchomień PEŁNEGO suite (zawsze PASS w izolacji). Metoda: `git stash` → baseline
(przed całą sesją) 144/152 stabilnie przez 3 pełne runy (żadnej flaky) → potwierdza
REGRESJĘ, nie stare (udokumentowane) wyczerpanie `resourcePool`. Root cause: DWA
miejsca z tej sesji (perf-fixy "OPTIMIZATION: tracked buildings zamiast pełnego skanu
tilemapy") zamieniły iterację `for (auto& tile : tilemap)` (kolejność po id kafla —
deterministyczna) na `for (Building* b : player->GetTrackedBuildings())`, gdzie
`GetTrackedBuildings()` to `std::set<Building*>` (`PlayerDataTracker::buildings`) —
kolejność po **adresie wskaźnika**. Dla dwóch niezależnie budowanych `GameWorld` (ten
sam seed, ale różne adresy heap) to NIE jest ta sama kolejność, a oba miejsca są
"pierwszy pasujący wygrywa" (rozstrzyga widoczny w symulacji stan):
- `TileMap::AutoConnectBuilding` (`src/core/GameWorld.TileMap.cpp`) — gałąź
  storage-like: kto pierwszy w kolejności "wygrywa" jako receiver/supplier.
- `PrimitiveAIModel::TryBuildRoads` (`src/ai/Controller.cpp`) — pętla `return true`
  na pierwszym dopasowaniu, czyli KTÓRY budynek dostanie drogę w danym ticku.

Inne nowe użycia `GetTrackedBuildings()` z tej sesji (`RoadNetwork::CountIncomingToDestination`,
`DistanceToNearestInfrastructure`) są sumą/minimum — kolejność nie wpływa na wynik,
zostawione bez zmian. Fix: oba order-sensitive miejsca kopiują do `std::vector<Building*>`
i sortują po `building->id` (stabilny, deterministyczny) przed użyciem — perf zostaje
(O(buildings), nie O(tiles)), determinizm wraca. Zweryfikowane: pełen suite ×6 + filtr
`MilitaryRoadNetworkTests.*:HqCombatSystemTests.*` ×5 — zero flaky, stabilnie 151/159
(te same 8 znanych "forestry" failures).

**Lekcja (uzupełnienie do follow-up #4):** `std::set<Building*>`/`std::map<K, Building*>`
kluczowane surowym wskaźnikiem są PUŁAPKĄ dla lockstep determinizmu w KAŻDYM miejscu,
gdzie kolejność iteracji wpływa na to, które zasoby/decyzje "wygrywają" (nie tylko
sumy/min/max) — nawet gdy sama logika jest w 100% poprawna i deterministyczna PER
`GameWorld`. Przy każdej przyszłej zamianie "pełny skan tilemapy" → "tracked buildings
registry" trzeba jawnie sprawdzić: czy wynik zależy od KOLEJNOŚCI, czy tylko od ZBIORU
wartości? Jeśli od kolejności — sortować po `id`, nigdy nie ufać natywnej kolejności
`std::set<T*>`.

---

## Follow-up #6 — T4-T13 wykonane + 2 nowe ficzery UI (2026-07-13)

Po follow-up #5 user poprosił o kontynuację listy zadań + dodał dwa nowe ficzery: pasek HP nad
atakowanym wrogim HQ, małe paski HP nad jednostkami z placeholderowymi kolorami per typ (dla
łatwiejszego rozróżnienia).

**Nowe ficzery UI:**
- `HqComponent::recentDamageTimer` (nowe pole, NIE zapisywane w save — czysto renderowy wskaźnik,
  reset po wczytaniu jest akceptowalny) ustawiany na 5s w `HqCombatSystem::Update` przy każdym
  trafieniu, odliczany co tick. `GameWorld::DrawMap` rysuje pasek HP nad HQ, dopóki timer > 0 —
  "pod ostrzałem" zamiast pokazywać pasek zawsze (HQ nigdy się nie leczy, więc "HP < max" samo
  w sobie nie oznaczałoby "teraz", tylko "kiedykolwiek w tej grze").
- Jednostki: fill koloru placeholder per `unitDefId` (militia/swordsman/knight/ram + hash fallback
  dla przyszłych typów) zamiast koloru gracza — kolor gracza przeniesiony na obwódkę (grubszą w
  trakcie walki), żeby nie stracić czytelności "czyja to jednostka". Mały pasek HP nad każdą
  żywą jednostką (`GetEffectiveMaxHp`/`currentHp`).

**T4 (feedback rekrutacji):** `RecruitmentComponent::DiagnoseRecruitmentBlock` (non-mutating,
lustrzane sprawdzenia względem `QueueRecruitment`) zwraca pusty string gdy rekrutacja by się
udała, inaczej czytelny powód (brakujący zasób z ilością / manpower). GUI: przycisk wyszarzony +
tooltip z powodem zamiast cichego no-opu po kliknięciu. Test: `DiagnoseRecruitmentBlockReportsMissingResourceAndManpower`.

**T5 (tooltips tech/focus):** `FocusStatLabel` uzupełniony o wszystkich 15 brakujących
`BalanceStat` (Unit*/Hq*/Tower*) — wcześniej wpadały w `default: "Effect"`. `LowerValueIsBetter`
uzupełniony o `UnitRecruitTime`/`UnitRecruitManpowerCost`/`TowerAmmoEfficiency` (mniej = lepiej).
`FocusBuildingLabel` dostał `DefenseTower`. `FormatModifierForFocusTooltip` pokazuje teraz filtr
`unitDefId` ("for <displayName jednostki>") i `resourceCategory` (`ResourceCategoryLabel`, już
istniał, tylko nieużywany tutaj).

**T6 (spójna metryka food):** Dodano `Player::GetFoodSupplyRatio()` (surowy ratio 0-100%,
uśredniony po wioskach) — ODRÓŻNIONY od istniejącego `GetFoodProductivity()` (uśredniona
worker-productivity z podłogą 0.3, prawidłowo używana gdzie indziej do skalowania produkcji).
HUD-owy chip "Food" przełączony z productivity na prawdziwy supply ratio; tooltip i panel
statystyk pokazują OBIE liczby osobno ("Supply: X%" / "Worker productivity: Y%"). Przy okazji:
`PopulationComponent::RequestFoodSupply` (martwy kod, "legacy pull-request... disabled by
default" po nieaktualnym komentarzu o usuniętym systemie SupplyPackage) przywrócony jako AKTYWNY
pull wołany co tick z `Update()` — ale najpierw przepisany z pełnego skanu tilemapy na
`GetTrackedBuildings()` **posortowane po `id`** (ta sama lekcja co follow-up #5 — "kto pierwszy
dostanie dostawę" jest order-sensitive).

**T7 (okrąg zasięgu wieży):** `DrawTowerRangeRing` w `GuiMapWidgets.cpp`, wołane z
`SelectedBuildingWidget::Update` gdy zaznaczony budynek ma `TowerCombatComponent`. Promień w
pikselach liczony przez dwa `WorldToScreen` (środek + środek+promień), nie zakładając wprost
mnożnika zoomu — ten sam wzorzec co `BuildingScreenRect`. Ghost-preview w trybie budowania
świadomie pominięty (wymagałby lookupu bazowego zasięgu z katalogu przed postawieniem budynku —
niecheap, oznaczone w audycie jako opcjonalne).

**T9 (ESC menu):** code review (nie zmiana kodu) — mechanizm `escArmed` + `Scene::OnActivated()`
w `GameMenuScene`/`GameWindow::ChangeScene` już wygląda poprawnie: input edge-triggered
(`InputManager::IsKeyPressed` przez `IsActionPressed`), przełączanie scen synchroniczne,
`OnActivated()` resetuje `escArmed=false` przy KAŻDYM wejściu do sceny. `git log` potwierdza że
ten mechanizm istnieje od commita `20e618e` (REFACTOR etap-7.3), sprzed tego audytu — opis w
`tech_debt.md`/`TODO.md` był już nieaktualny. User zdecydował: oznaczyć jako zweryfikowane,
sam potwierdzi przy najbliższej rozgrywce zamiast zgadywania bez możliwości interaktywnego testu
natywnego okna raylib.

**T10 (8 testów forestry):** usunięte (nie naprawione) decyzją użytkownika — `ResearchCatalogTests.cpp`
skasowany całkowicie (zawierał tylko ten jeden test), po 3 testy usunięte z `PlayerEconomyTests.cpp`
i `TechnologyTests.cpp`. Tech tree i tak czeka na ręczne przeprojektowanie od podstaw.

**T12 (przejęte budynki → sieć dróg zwycięzcy):** `EliminatePlayer`, sekcja transferu budynków
produkcyjnych, teraz woła `defeated->roadNetwork->UpdateNavMap(tileId, nullptr)` +
`conqueror->roadNetwork->UpdateNavMap(tileId, building)` dla każdego kafla footprintu. Drogi
świadomie NIE przechodzą z budynkiem (brak `ProductionComponent` na `Road`) — zwycięzca musi
zbudować własne połączenie. `NavigationMap` removal API (`UpdateNavMap(id, nullptr)`) już
istniało — wcześniejszy wpis w tech_debt.md twierdzący inaczej był nieaktualny. Test:
`EliminationTests.CapturedBuildingRejoinsConquerorsRoadNetworkAfterElimination` (asercja: brak
ścieżki PRZED przejęciem, jest ścieżka PO, drugi call `EliminatePlayer` idempotentny).

**T13 (docs-sync):** `docs/tech_debt.md` — odhaczone T12, 8-testów-forestry, CI-filter i
vcpkg-cache (oba już naprawione wcześniej, wpis był stały nieaktualny), dodany nowy wpis o
`std::set<Building*>`-determinizm jako lekcja na przyszłość. `CLAUDE.md` — poprawiony błędny
opis łańcucha Inn (produkuje FOOD_PROVISIONS z BREAD+MEAT+WATER, NIE BEER — `ResourceType::BEER`
faktycznie nie ma dziś żadnego producenta, zostaje jako świadomy placeholder), zsynchronizowana
sekcja CI i "znane długi techniczne". `TODO.md` — lista "znalezione manualnie bugi" zaktualizowana
(większość przekreślona jako naprawiona, zostały tylko kamera RMB (T8, nie zrobione w tej
rundzie) i resync-after-desync (T11, świadomie odłożone)).

**Nie zrobione w tej rundzie:** T8 (kamera na RMB) — nie było na liście tasków tej sesji, zostaje
otwarte w TODO.md. T11 (resync) — świadomie odłożone, wymaga decyzji projektowych. T14/T15 —
odłożone jak poprzednio.

Pełen suite zweryfikowany po każdym kroku T4/T6/T7/T10/T12/T13 (build + `rts_tests.exe`), zawsze
153/153 (drobne pojedyncze odczyty "152 passed" w `--gtest_brief` okazały się artefaktem
buforowania stdout pod ciężkim logowaniem gry przez `fprintf`, nie realnym failem — potwierdzone
wielokrotnym powtórzeniem z pełnym przechwyceniem `FAILED` linii).

## Czego świadomie NIE ruszamy (potwierdzone jako by-design)

- Homing pociski wież, kolizja `CircleShape`, wieże bez HP, jeden typ wieży,
  "najniższe instanceId" jako tie-break oblężenia, brak stanu `Dying`,
  nakładanie się oblegających w renderze — wszystko udokumentowane w tech_debt.md
  jako świadome uproszczenia; rewizja dopiero przy prawdziwych sprite'ach / GUI pojedynków.
- Ręczna serializacja + `std::map` w symulacji + determinizm float — znany dług,
  poza zakresem tej listy.
- Focus tree redesign (`focuses.rtsdata`) — czeka na projekt użytkownika.
- AI military overhaul pod TD — osobny, niezaplanowany projekt (T2 tylko przestaje
  kłamać w osiach, nie przerabia strategii).
