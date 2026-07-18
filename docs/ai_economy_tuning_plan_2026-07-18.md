# Plan dostrojenia ekonomii AI — 2026-07-18 (dla wykonawcy: Sonnet)

> **STATUS: WYKONANE (2026-07-18).** Zadania 0-6 zrealizowane w kolejności, każde z zielonym
> suite'em. Podsumowanie w `docs/td_ai_design.md` (sekcja "Bias ekonomiczny AI") i
> `docs/tech_debt.md` (odkryty przy okazji, nierozwiązany determinism-flake w pełnym suicie —
> NIE blokujący, patrz wpis tam). Kod: commity `AI-rework(fix): ...` w historii gita.

Zgłoszenie usera (playtest, screenshoty): AI słabo planuje budynki — **nie buduje tartaków
(LumberMill) i ma permanentny brak desek**, a podniesienie biasów PLANKS/STONE w
`assets/data/ai.rtsdata` (12→80, 10→60, 8→80…) **nic nie zmieniło**. Dodatkowo na screenach
widać **równoległe dywany dróg** (3 rzędy obok siebie).

Diagnoza jest GOTOWA i potwierdzona w kodzie — to **trzy błędy strukturalne, nie wartości
biasów**. Podniesienie biasu nie mogło pomóc (patrz Zadanie 2: bias na WOOD sam blokuje
zejście do tartaku). Wykonaj zadania W KOLEJNOŚCI, każde z osobnym zielonym suite'em
(`.\run_tests.ps1` **z roota repo** — nigdy exe z build-dir), commit per zadanie.

## Zasady wykonawcze (obowiązują w każdym zadaniu)

- **Determinizm lockstep**: każda nowa pętla po `GetTrackedBuildings()` /
  `GetTrackedBuildingsWithComponent<T>()` z semantyką first-match/konkurencji → kopiuj do
  `std::vector` i sortuj po `building->id`. Agregacje (sumy/liczniki/OR) są bezpieczne.
  Zero nieseedowanego RNG. Szczegóły: `docs/tech_debt.md`.
- **ResourcePool jest procesowo współdzielony** (10000/typ) i wyczerpuje się w pełnym suicie —
  w testach NIE asertuj zawartości buforów wypełnianych przez `GenerateResource`/granty
  (test-order-dependent!). Asertuj liczby budynków, telemetrię, manpower, komendy.
- **Harness behawioralny** (`tests/AIBehaviorHarnessTests.cpp`) to domyślna pętla debugowania —
  przy porażce drukuje tabelę okien + powody odrzuceń komend. Najpierw harness, potem gra.
- Diagnostyka tymczasowa: `fopen/fprintf` do pliku (NIE stderr — PowerShell miesza wide-char),
  usuwana przed commitem.
- Commity: message przez `git commit -F <plik>` (PowerShell psuje wieloliniowe `-m` z
  cudzysłowami); nie zamiataj do commitów niepowiązanych brudnych plików (AGENTS.md, VERSION,
  buildings.rtsdata).

---

## Zadanie 0 — reprodukcja: dołóż mix producentów do harnessu

**Cel:** zobaczyć liczbowo, że AI nie stawia LumberMilla, zanim cokolwiek naprawisz.

1. W `tests/AIBehaviorHarnessTests.cpp` rozszerz `WindowSample` o pola:
   `woodcutters`, `lumberMills`, `mines`, `foundries` (int) oraz `planksRate` (int).
2. Wypełniaj je w pętli okien przez `AIActions::CountOwnedBuildings(ai, BuildingType::X)`
   oraz `AIActions::GetResourceRate(ai->economyTelemetry.current.productionRatesPerMinute,
   ResourceType::PLANKS)`.
3. Dodaj kolumny do `report()` (wzoruj się na istniejącym `snprintf`).
4. Odpal sam harness:
   `& ".\build-tests\tests\Debug\rts_tests.exe" --gtest_filter=AIBehaviorHarnessTests.*`
   (test przechodzi — tabelę wydrukuje tylko przy porażce; na czas odczytu tabeli dodaj
   TYMCZASOWO `EXPECT_TRUE(false) << report();` na końcu, odczytaj, usuń).
5. **Oczekiwana obserwacja (stan zepsuty):** `lumberMills == 0` przez cały run, `planksRate == 0`,
   woodcutters rosną. Zanotuj liczby w commit message Zadania 2.

Commit (razem z Zadaniem 5, albo osobno jako `AI-harness: producer mix columns`).

## Zadanie 1 — fix równoległych dywanów dróg (rezerwacja blokuje reużycie)

**Root cause:** `src/ai/AIActions.cpp`, `SubmitRoadPath` → lambda `canUseRoadPathTile`:

```cpp
if (state.reservedRoadTiles.contains(tileId))
    return false;
```

Rezerwacja (6 s) blokuje kafelek TAKŻE wtedy, gdy komenda już się wykonała i droga fizycznie
stoi. Efekt: przez ~5 s świeżo położony korytarz jest dla plannera "ścianą", więc kolejne
połączenie w tym samym kierunku kopie RÓWNOLEGŁY rząd (screenshot: 3 rzędy dróg obok siebie).
0-1 BFS (krok po drodze = koszt 0) nie ma szansy zadziałać, bo kafelki są odfiltrowane
zanim dojdzie do kosztu.

**Fix (dokładnie):** w `canUseRoadPathTile` rezerwacja ma blokować wyłącznie kafelki wciąż
PUSTE (czekające na wykonanie komendy):

```cpp
Tile& tile = world.GetTileMap()[tileId];
Building* building = tile.GetBuilding();
if (building == nullptr && state.reservedRoadTiles.contains(tileId))
    return false;
return building == nullptr || IsRoadLikeBuilding(building);
```

Pętla submitująca (niżej w tej samej funkcji) MA ZOSTAĆ bez zmian — jej własny check
`reservedRoadTiles.contains` chroni przed podwójnym ZAMÓWIENIEM tego samego kafelka.

**Test (dopisz do `tests/UtilityAIModelTests.cpp`):** `SubmitRoadPathReusesJustPlacedCorridor`
- Fixture jak `IsConnectedToRoadNetworkDetectsRoadPathToStorage` (bare grass map przez
  `FillGrassMap`, mapę wypełnij PRZED konstrukcją Playera!). Układ: StorageBuilding {0,1};
  dwa 1×1 fixture'owe Roady A={8,2} i B={8,8} (`player.Build<Road>(pos, false)`), mapa 14×14.
- `AIActions::AIActionState state;` → `SubmitRoadPath(world?, ...)` — uwaga: potrzebujesz
  `GameWorld`; użyj wzorca z `SubmitRoadPathCrossesTheTrackWithABridge` (świat generowany,
  seed 6, 81×81) zamiast bare mapy, z dwoma fixture'owymi Roadami w wolnym miejscu w linii
  pionowej ~8 kafelków od siebie i trzecim fixture'owym Roadem C obok korytarza (offset 2 w bok
  od środka linii A-B).
- Krok 1: `SubmitRoadPath(world, human, A, B, state)` → true; przetocz ~50 ticków
  `world.UpdateSimulation(0.01)` (komendy się wykonają, kafelki mają budynki-drogi w budowie,
  rezerwacje 6 s WCIĄŻ AKTYWNE).
- Krok 2: policz drogi gracza (`AIActions::CountOwnedBuildings(human, BuildingType::Road)`),
  wywołaj `SubmitRoadPath(world, human, C, B, state)` → przetocz ~50 ticków → policz ponownie.
- **Assert:** przyrost dróg z kroku 2 ≤ dystans C→korytarz + 2 (sam łącznik; bez fixa planner
  wykopałby pełną równoległą linię C→B). Dobierz liczby do układu i wpisz na sztywno.

## Zadanie 2 — fix "nie buduje tartaków": zejście łańcucha zatruwane biasem

**Root cause:** `src/ai/AIActions.cpp`, `DiagnoseResourceNeed`, blok `missingInputs`
(okolice linii 452-466):

```cpp
int inputConsumed = GetResourceRate(...consumptionRatesPerMinute, input.type) +
                    biasFor(input.type);
...
if (inputStored < input.amount * 2 || inputProduced < inputConsumed)
    ... push input jako brakujący
```

`TryBuildProducerFor(PLANKS)` (w `src/ai/AIModel.cpp`) schodzi po `missingInputs.front()`:
diagnoza PLANKS → input WOOD → `inputConsumed` zawiera **bias WOOD (80!)** → realna produkcja
drewna zawsze < 80 → WOOD ZAWSZE "brakujący" → target przestawia się na WOOD → AI stawia
KOLEJNEGO Woodcuttera zamiast tartaku. **Dlatego podnoszenie biasu PLANKS nic nie daje** —
im wyższy bias WOOD, tym mocniej łańcuch ucieka w dół. Bias amortyzuje konsumpcję KOŃCOWĄ
zasobu; używanie go po raz drugi do oceny półproduktów łańcucha podwójnie liczy ten sam popyt.

**Fix (dokładnie):** zejście do inputu tylko, gdy input NAPRAWDĘ nie istnieje w ekonomii —
bez biasu i bez luźnego `||`:

```cpp
int inputProduced = GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute, input.type);
int inputStored = CountStoredResource(player, input.type);
// Descend only when the input genuinely doesn't exist in this economy yet:
// no real production AND no meaningful stock. The consumption bias is
// deliberately NOT applied here — it amortizes the FINAL resource's demand
// and already drives that resource's own deficit; counting it again against
// chain inputs made every chain walk collapse to its raw material (playtest
// 2026-07-18: PLANKS forever redirected to WOOD, no LumberMill ever built).
if (inputProduced == 0 && inputStored < input.amount * 2)
    ... push input
```

(usuń zmienną `inputConsumed` — po zmianie nieużywana).

**Test (dopisz do `tests/UtilityAIModelTests.cpp`):** `ChainWalkDoesNotDescendPastAProducedInput`
- Bare fixture: `TileMap map; FillGrassMap(map, 8, 8); Player player{0, map};`
- Ustaw realną produkcję drewna wprost:
  `player.economyTelemetry.current.productionRatesPerMinute[ResourceType::WOOD] = 10;`
- Bias: `std::map<ResourceType,int> bias{{ResourceType::WOOD, 80}, {ResourceType::PLANKS, 80}};`
- `auto diag = AIActions::DiagnoseResourceNeed(&player, ResourceType::PLANKS, 0, &bias);`
- **Assert:** `diag.urgency > 0.5` (deficyt desek istnieje) ORAZ `diag.missingInputs` NIE
  zawiera `ResourceType::WOOD` (produkcja drewna działa → nie schodzimy niżej).
- Drugi przypadek w tym samym teście: wyzeruj produkcję WOOD i zapasy → `missingInputs` MA
  zawierać WOOD (bootstrap świeżej ekonomii wciąż działa).

## Zadanie 3 — fix tunelowania na topowym deficycie (rotacja przez kredyt "w budowie")

**Root cause:** `src/ai/AIModel.cpp`, `ExecuteEconomy`:

```cpp
for (const auto& deficit : s.deficits)
    if (TryBuildProducerFor(world, player, deficit.resource))
        return true;
```

Deficyty są posortowane malejąco po urgency i pierwszy wykonany build kończy cykl. Producent
JUŻ ZAMÓWIONY (w budowie) w ogóle nie obniża urgency (diagnoza pomija budynki
`IsUnderConstruction`), więc topowy deficyt (WOOD przy biasie 80) wygrywa KAŻDY cykl aż do
wyczerpania afordowalności — stąd las Woodcutterów i nic poza tym.

**Fix (dokładnie):** w `DiagnoseResourceNeed` (src/ai/AIActions.cpp), w pętli po
`GetTrackedBuildingsWithComponent<ProductionComponent>()` dodaj równoległą flagę:

```cpp
bool producerUnderConstruction = false;
...w pętli, PRZED istniejącym `continue` na IsUnderConstruction:
if (building->owner == player && building->IsUnderConstruction() &&
    production->products.contains(resource))
{
    producerUnderConstruction = true;
    continue;  // dalej nie liczymy go do stalled/undermanned — jak dotąd
}
```

a po wyliczeniu urgency (przed progiem `<= 0.05`):

```cpp
// A producer for this resource is already ordered and building — its future
// output is credited by capping the urgency, so the deficit ladder rotates
// to the NEXT problem instead of stacking another copy every cycle
// (playtest 2026-07-18: a big WOOD bias produced a forest of Woodcutters
// and nothing else).
if (producerUnderConstruction)
    diagnosis.urgency = std::min(diagnosis.urgency, 0.3);
```

UWAGA weryfikacyjna: sprawdź, że `production->products` jest wypełnione już dla budynku w
budowie (konfiguracja z BuildingConfig przy placemencie). Jeśli NIE — zamiast
`products.contains` porównaj typ budynku z typami z `FindProducerOptions(resource)`
(`option.buildingType == building->buildingType`).

Pętla jest agregacją (OR na flagę) — kolejność iteracji bez znaczenia, determinizm OK.

**Test:** `PendingProducerCapsDeficitUrgency`
- Świat generowany (wzorzec z `AIRecruitsAndDeploysAWave`, 81×81 lub 301² pinned) LUB bare
  fixture: postaw budynek-producenta z `constructionRemaining > 0` (np.
  `player.Build<LumberMill>(pos, true)` przy opłaconych kosztach — prościej:
  `Build<LumberMill>(pos, false)` i ręcznie `building->constructionRemaining = 10.0;`).
- Bias `{{PLANKS, 80}}`, zero produkcji.
- **Assert:** `DiagnoseResourceNeed(&player, PLANKS, 0, &bias).urgency <= 0.3 + 1e-9`.
- Kontrprzypadek: bez budynku w budowie urgency ≥ 0.6.

## Zadanie 4 — weryfikacja behawioralna: tartak w harnessie

Po zadaniach 1-3 rozszerz asercje harnessu (`AIBehaviorHarnessTests.cpp`):

```cpp
EXPECT_GE(AIActions::CountOwnedBuildings(ai, BuildingType::LumberMill), 1)
    << "no LumberMill - the planks chain never stood up" << report();
```

oraz (miękko, żeby nie flakować na tempie ekonomii):

```cpp
EXPECT_GE(samples.back().woodcutters, 1) << report();
EXPECT_LE(samples.back().woodcutters, 4)
    << "Woodcutter tunnel vision is back (deficit ladder not rotating)" << report();
```

Jeżeli 5 sim-minut nie wystarcza na LumberMill (sprawdź tabelę!), podnieś budżet harnessu do
7 minut (`Windows = 14`) — wolno Ci, to test akceptacyjny, nie unit.

## Zadanie 5 — strojenie wartości biasu (dopiero PO fixach!)

Obecne wartości w working tree (`assets/data/ai.rtsdata`: WOOD 80 / STONE 60 / PLANKS 80 /
IRON 40 / TOOLS 20) to eksperyment diagnostyczny usera — po fixach 1-3 będą powodowały
przesadną nadprodukcję. Ustaw punkt startowy:

```
consumption WOOD 20
consumption STONE 15
consumption PLANKS 12
consumption IRON 6
consumption TOOLS 3
consumption FOOD_PROVISIONS 12
consumption ARROWS 6
```

Metoda strojenia: odpal harness, porównaj w tabeli finalne `planksRate`/produkcje z biasem —
bias ma być osiągalny w ~5-10 minut rozbudowy (produkcja dogania bias → urgency spada →
AI przechodzi do militariów). Za wysoki bias = wieczna ekonomia; za niski = stall jak dziś.
Wartości wpisujesz TYLKO do ai.rtsdata (zero rekompilacji).

## Zadanie 6 — domknięcie

1. Pełny suite `.\run_tests.ps1` zielony; determinizm z powtórkami:
   `--gtest_filter=UtilityAIModelTests.TwoWorldsSameSeedWithNoisyAIStayInSync --gtest_repeat=10`.
2. Ręczny playtest (`.\build_and_run.ps1`): AI ma stawiać tartak w pierwszych minutach,
   drogi bez równoległych dywanów.
3. Zaktualizuj `docs/td_ai_design.md` (sekcja deficytów: zejście łańcucha bez biasu na
   inputach + kredyt za producenta w budowie) i odhacz ten plan.
4. Commity per zadanie, konwencja `AI-rework(fix): ...`, message przez `-F`.

## Kryteria akceptacji całości

- Harness: LumberMill ≥ 1, woodcutters ≤ 4, `planksRate > 0` na końcu runu, unconnected ≤ 1,
  brak spamu odrzuceń (istniejąca asercja), militaria dalej działają (Barracks/rekrut/deploy).
- Testy jednostkowe z zadań 1-3 zielone, pełny suite zielony.
- Na screenie z gry: pojedyncze korytarze dróg (bez równoległych rzędów), tartak w bazie.
