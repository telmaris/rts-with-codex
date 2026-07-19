# Zadania dla Soneta — ekonomia AI, runda 2 (2026-07-19)

Zgłoszenie z playtestu (2026-07-19): AI stawia podejrzanie dużo chat drwala i „nie
rozwija się"; manpower się kończy i AI nie reaguje; żelazo często nie występuje w
pobliżu startu. Poniżej zweryfikowana diagnoza (sekcja 0 — NIE pomijać, ustala co
JUŻ jest naprawione i czego NIE szukać) i zadania 1–5.

Konwencje sesji: commity LOKALNE (bez pusha — push na main auto-tworzy Release),
nie dotykać `AGENTS.md`/`VERSION`/`assets/data/buildings.rtsdata` (brudne pliki
usera), pełny suite przez `.\run_tests.ps1` z roota przed każdym commitem.
Weryfikacja behawioralna: `AIBehaviorHarnessTests` (tabela okien 30 s z kolumnami
wcut/lmil/mine/fdry — patrz `tests/AIBehaviorHarnessTests.cpp`).

---

## 0. Stan zweryfikowany — NIE diagnozować od zera

**Telemetria konsumpcji NIE liczy już kosztów budowy.** Zweryfikowane grepem po
wszystkich wołających: `RecordConsumption` wywołują dziś WYŁĄCZNIE
`ProductionComponent.cpp:117` (inputy przepisów) i `PopulationComponent.cpp:45`
(FOOD_PROVISIONS). Wywołanie z `Player::TryPayBuildCost` usunięto w lokalnym
commicie `fd32931` wraz z aktualizacją testów
(`TelemetryDoesNotRecordBuildCostAsConsumption` /
`...BuildingPlacementCostAsConsumption` w `tests/PlayerEconomyTests.cpp` —
asertują 0). Koszty technologii też idą przez `TryPayBuildCost`, więc też są
czyste. **Jeżeli user playtestował build sprzed `fd32931`, część stackingu mogła
pochodzić stąd — ale kod jest już poprawny; nie szukać tego buga ponownie.**

**Uśredniony bias produkcyjny (ai.rtsdata) NIE wymusza wielu chat.** Rachunek:
Woodcutter = `output WOOD 2 / cycle_time 4.0` = **30 WOOD/min** przy pełnej
obsadzie i produktywności 1.0 (`buildings.rtsdata:104-108`). Bias `WOOD 20`
(difficulty 3, scale 1.0) pokrywa JEDNA zdrowa chata. Ale uwaga:
produktywność skaluje się z zaopatrzeniem w żywność
(`GetWorkerProductivity` = 0.3 + 0.7·ratio) — przy głodzie chata daje ~9/min i
bias 20 „żąda" 2–3 chat. To wtórny wzmacniacz, nie pierwotna przyczyna
(strojenie w zadaniu 4, PO naprawie zadania 3).

**Pierwotna przyczyna stackingu chat (bug logiki, nie strojenie):** pętla
sprzężenia zwrotnego w `ExecuteEconomy`/`TryBuildProducerFor`
(`src/ai/AIModel.cpp`):

1. Istniejąca chata staje (brak drogi → output nie wywożony → `IsProductionStalled`,
   albo brak obsady przy suchym manpowerze) → jej realny `productionRatesPerMinute`
   spada do 0.
2. `DiagnoseResourceNeed` (`src/ai/AIActions.cpp`) poprawnie to widzi i ustawia
   flagi: `logisticsProblem` (stalled, urgency 0.55), `storageProblem` (pełny
   output, 0.45), `manpowerProblem` (brak obsady, 0.42), a reguła
   `produced==0 && consumed>0` daje 0.62–0.9 („producer inactive").
3. **Ale `ExecuteEconomy` i `TryBuildProducerFor` IGNORUJĄ te flagi** —
   `AISituation::Deficit` niesie tylko `{resource, urgency}`, a egzekucja robi
   jedno: stawia KOLEJNEGO producenta tego zasobu. Nowa chata zużywa
   drewno/kamień/manpower, też nie jest podłączona → też staje → urgency dalej
   wysoka → następna chata. Stąd „las chat drwala i zero rozwoju".

Naprawa pętli = zadanie 3. Manpower guard (zadanie 2) domyka drugą nogę tej
pętli (nieobsadzeni producenci przy suchym manpowerze).

---

## Zadanie 1 — startowe poletka COAL i IRON_ORE (dalej od HQ)

**Wymóg usera:** żelazo jest ważne, a często nie ma go w pobliżu startu. Dodać
poletka COAL i IRON_ORE per gracz, tego samego rozmiaru co WOOD/STONE, w
pierścieniu o kilka kratek większym (dalej od HQ).

**Stan obecny:** `PlaceStartingResourcePatch` (`inc/core/GameWorldInternal.h:113`)
— radius 4, centra poletek w pierścieniu **17..23** kratek od centrum HQ
(`kMinPatchCenterDist`/`kMaxPatchCenterDist`), preferowany kierunek: WOOD −x,
„nie-WOOD" +x. Wołana 2× w `CreateStartingVillageAndResources`
(`src/core/GameWorld.Init.cpp:301-302`) dla WOOD i STONE, po zbudowaniu drogi
startowej. Malowanie pomija kafle nie-GRASS, kafle z budynkiem i `isMilitaryRoad`
(ochrona toru już jest, `GameWorldInternal.h:161`).

**Kroki:**
1. Sparametryzować pierścień i preferowany kierunek: do sygnatury
   `PlaceStartingResourcePatch` dodać `int minCenterDist, int maxCenterDist,
   Vec2i preferredDir` (kierunek jednostkowy; preferredOffset =
   `preferredDir * (maxCenterDist - radius)`). Usunąć zahardkodowany ternary
   WOOD/nie-WOOD.
2. Wywołania w `CreateStartingVillageAndResources`:
   - WOOD: ring 17..23, dir {−1,0} (bez zmian funkcjonalnych),
   - STONE: ring 17..23, dir {+1,0} (bez zmian),
   - **COAL: ring 26..32, dir {0,−1}**,
   - **IRON_ORE: ring 26..32, dir {0,+1}**.
   Cztery kierunki rozstrzelane, żeby poletka się nie zlewały; scoring
   `paintableTiles*100 − preferredDistance` i tak zejdzie z kolizji, bo
   zamalowane kafle przestają być GRASS.
3. Radius zostaje 4 („tego samego rozmiaru"). UWAGA: ring 26..32 minus radius 4 =
   kafle poletka od ~22 kratek — wciąż wewnątrz strefy startowej po rescale z
   2026-07-17 i poza 10-kratkowym apronem HQ. Nie zwiększać ponad 32 bez
   sprawdzenia minimalnego rozmiaru mapy (301) i dystansu HQ–HQ.
4. Mine ma już `terrain_production` dla COAL i IRON_ORE
   (`buildings.rtsdata:161-172`) — zero zmian w danych budynków. AI: opening
   plan preferuje `TileType::COAL` dla Mine (`AIModel.cpp`, `TryOpeningPlan`) —
   po tym zadaniu ta preferencja wreszcie ma co znajdować blisko startu.
5. Zero zmian save/wire (poletka = teren world-gen).

**Test:** w `MapGeneratorTests` (albo nowy przypadek w istniejącym pliku): dla
3–4 seedów wygeneruj świat, dla każdego HQ policz kafle `TileType::COAL` i
`TileType::IRON_ORE` w promieniu ≤35 od centrum HQ — assert ≥ 10 kafli każdego
typu (poletko r=4 ma ~49 kafli, częściowo przycięte terenem; 10 = bezpieczny
próg). Sprawdź też, że żaden kafel poletka nie ma `isMilitaryRoad` (ochrona już
jest — test ją utrwala).

---

## Zadanie 2 — manpower guard: natychmiastowy Village przy wysychającym manpowerze

**Wymóg usera:** gdy manpower się kończy (np. <20) PRZY PEŁNYM zaludnieniu
(populacja przy capie — wioski nie urosną same), budowa Village ma być
natychmiastowym priorytetem.

**Stan obecny:** `ExecuteEconomy` (`src/ai/AIModel.cpp`) ma branch
`s.manpower < 5.0 && s.foodProductionAlive` → Village; próg 5 jest za niski
(reaguje po fakcie), a warunek pełnego zaludnienia nie istnieje. `ScoreNeed`
EconomySustain jest hard-capped na 0.75 (poniżej podłogi RecruitDeploy 0.8) —
emergency musi ten cap ŚWIADOMIE ominąć.

**Kroki:**
1. `AISituation` (inc/ai/AIModel.h): dodać pola `double populationCap{0.0}` i
   `double totalPopulation{0.0}`; w `Sense` wypełnić z
   `player->GetPopulationCap()` i `player->GetTotalPopulation()`
   (`inc/economy/Player.h:154-161`).
2. Nowy knob w `ai.rtsdata` + `AIEconomyBias` (wzorzec `tower_readiness_buildings`
   i `decision_interval`): `manpower_reserve 20` (int, próg alarmu).
3. Warunek emergency (helper w AIModel.cpp, użyty w DWÓCH miejscach):
   `manpower < manpowerReserve && totalPopulation >= populationCap * 0.95 &&
   foodProductionAlive`. Wymóg `foodProductionAlive` zostaje: nowa wioska bez
   żywności nie wyprodukuje manpoweru — przy martwym łańcuchu jedzenia
   priorytetem pozostaje food (istniejąca reguła 0.75).
4. `ScoreNeed(EconomySustain)`: gdy emergency → **return 0.9 PRZED hard-capem
   0.75** (komentarz: rekrutacja i tak jest zablokowana bez manpoweru —
   `DiagnoseRecruitmentBlock` odrzuca — więc podbicie ponad podłogę
   RecruitDeploy nie głodzi militariów, ono odblokowuje ich jedyny brakujący
   zasób).
5. `ExecuteEconomy`: istniejący branch Village zmienić na warunek emergency
   (próg 5→`manpower_reserve`, plus warunek capa). Village-anchor przez
   istniejący `FindBuildAnchor(GRASS)` — bez zmian.
6. Determinizm: czysta funkcja stanu + statyczny config — bez wpływu.

**Testy:** unit w `UtilityAIModelTests`: świat z manpower=10, populacja == cap,
food żywy → `ScoreNeed(EconomySustain) >= 0.9` i pierwszy wykonany build to
Village. Drugi przypadek: manpower=10, ale populacja < 50% capa (wioski
niedoludnione — manpower sam odrośnie) → score bez emergency (≤0.75), Village
NIE jest wymuszany.

---

## Zadanie 3 — przerwać pętlę „stoi → dobuduj kolejnego" (rdzeń zgłoszenia)

**Wymóg:** AI nie może odpowiadać na zepsutego producenta budową następnego.
Diagnoza flag JUŻ ISTNIEJE (`AIResourceDiagnosis.logisticsProblem/
storageProblem/manpowerProblem`) — egzekucja ma zacząć jej słuchać.

**Kroki:**
1. `TryBuildProducerFor` (`src/ai/AIModel.cpp`): po zejściu łańcucha (pętla
   depth) wykonaj `DiagnoseResourceNeed` dla FINALNEGO `target` i:
   - `logisticsProblem || storageProblem` → **return false bez budowy**
     (naprawa należy do LogisticsRepair — po fixie z `7870c75` pojedynczy
     odłączony budynek ma score 0.65 i wygra kolejny cykl; komentarz w kodzie
     ma to mówić wprost),
   - `manpowerProblem` → return false (manpower guard z zadania 2 przejmie),
   - budowa kolejnego producenta dozwolona tylko gdy `hasProducerBuilding ==
     false` (naprawdę brak producenta) LUB istniejący producenci są zdrowi
     (żadna flaga) a bilans nadal ujemny.
   Uwaga: `AIResourceDiagnosis` nie eksponuje dziś `hasProducerBuilding` —
   dodać pole `bool hasProducerBuilding{false}` do structa
   (`inc/ai/AIActions.h`) i wypełnić w `DiagnoseResourceNeed` (zmienna lokalna
   już istnieje).
2. `deficitBackoff` (już jest w `AIActionState`): po odmowie z powodu flag też
   ustawiać backoff 12 s dla tego zasobu — deficyt nie blokuje drabinki przez
   kolejne cykle, dopóki logistyka się nie naprawi.
3. NIE zmieniać samej diagnozy ani progów urgency w tym zadaniu (strojenie =
   zadanie 4; jedna zmienna naraz).

**Testy:** unit w `UtilityAIModelTests`: świat z JEDNYM ukończonym, ale
ODŁĄCZONYM od dróg Woodcutterem (stalled) + deficyt WOOD →
`TryBuildProducerFor(WOOD)` zwraca false i NIE submituje `BuildBuilding`
(sprawdzić po `ConsumeCommandResults` / liczbie komend). Harness: istniejąca
asercja `woodcutters <= 4` zostaje (po tym fixie powinna przechodzić z dużym
zapasem — jeśli po wdrożeniu daje ≤2–3, MOŻNA ją zaostrzyć do ≤3, decyzja po
zobaczeniu tabeli).

---

## Zadanie 4 — strojenie biasu (dopiero PO zadaniu 3)

Rachunek kontrolny do tabeli w `ai.rtsdata` (pełna obsada, produktywność 1.0):

| Zasób | Producent | Wydajność/min | Bias (d3) | Chat/kopalń wymaganych |
|---|---|---|---|---|
| WOOD | Woodcutter | 30 | 20 | 0.7 |
| STONE | Mine (STONE) | 12 (1/5 s) | 15 | 1.25 |
| PLANKS | LumberMill | 10 (2/12 s) | 12 | 1.2 |

Przy produktywności 0.3 (głód) wydajności spadają ~3.3× i bias „żąda" 2–4
producentów — wzmacniacz pętli z zadania 3. Po wdrożeniu zadania 3 uruchomić
harness i playtest; jeżeli AI nadal przesadza z producentami tier-1,
skorygować bias W DÓŁ (propozycja startowa: WOOD 12, STONE 10, PLANKS 10) —
ale NIE stroić na ślepo przed zadaniem 3, bo pętla flagowa dominuje każdą
wartość biasu.

---

## Zadanie 5 — weryfikacja końcowa

1. Pełny suite `.\run_tests.ps1` — zielony po KAŻDYM zadaniu osobno (commit per
   zadanie, konwencja `AI-rework(fix): ...` / `world-gen: ...`).
2. `AIBehaviorHarnessTests` — tabela: wcut ma się ustabilizować ≤3–4, lmil ≥1,
   mine ≥1, roster rośnie, deploy następuje; brak spamu odrzuconych komend.
3. Test determinizmu `TwoWorldsSameSeedWithNoisyAIStayInSync` + pełny suite w
   pętli ×3 (zmiany dotykają decyzji AI — obie strony lockstepu identyczne, ale
   sprawdzić).
4. Manualny playtest (user): (a) poletka COAL/IRON_ORE widoczne w ~25–32
   kratkach od HQ, (b) AI stawia Village zanim manpower spadnie do zera,
   (c) koniec lasu chat drwala — AI przechodzi do LumberMill/Mine/food.
