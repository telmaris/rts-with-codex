# Zadania dla Soneta — ekonomia militarna AI + dążenie do ataku, runda 3 (2026-07-20)

Zgłoszenie z playtestu (2026-07-20): AI wciąż NIE buduje produkcji żelaza / węgla /
narzędzi / broni. Rozbudowuje wyłącznie manpower i żywność (i ta pętla się domyka),
przez co gra kompletnie pasywnie. Wymogi usera: (a) AI ma dążyć do ataku — ekonomia
broni ma powstawać z własnej woli AI, nie przypadkiem; (b) drobny, trwały bias RNG
per-AI, żeby dwa AI nie grały identycznie.

Konwencje sesji: commity LOKALNE (bez pusha — push na main auto-tworzy Release).
NIE dotykać brudnych plików usera: `AGENTS.md`, `VERSION`,
`assets/data/buildings.rtsdata`, `inc/ui/GuiController.h`, `inc/ui/UiTheme.h`,
`src/scenes/GameScene.cpp`, `src/ui/*` (trwa równoległa praca nad UI). Pełny suite
przez `.\run_tests.ps1` z roota przed każdym commitem. Weryfikacja behawioralna:
`tests/AIBehaviorHarnessTests.cpp` (tabela okien 30 s).

**PRZECZYTAJ przed dotknięciem AI:** memory
`determinism_pointer_ordering_bug_pattern.md` (kolejność iteracji ≠ heap order) —
każda nowa pętla po budynkach sortowana po `building->id`, każdy nowy RNG seedowany
wyłącznie z (seed mapy, id gracza).

---

## 0. Stan zweryfikowany — NIE diagnozować od zera

**Diagnoza `hasProducerBuilding` jest POPRAWNA per-teren.** Postawiona kopalnia
dostaje `ProductionComponent::products` wyłącznie z produkcji terenu, na którym
stoi (`ApplyProductionDefinition` + `FindTerrainProductionDefinition`,
`src/economy/BuildingConfig.cpp:657-699`) — kopalnia na COAL NIE liczy się w
`DiagnoseResourceNeed(IRON_ORE)` jako producent IRON_ORE. Nie szukać tu buga.
(Bug jest gdzie indziej: guard duplikatów w `TryBuildProducerFor` liczy po TYPIE
budynku — zadanie 4.)

**Łańcuch zejścia (chain-walk) działa i doszedłby do Mine(IRON_ORE)/Foundry.**
`TryBuildProducerFor` (`src/ai/AIModel.cpp:563`) schodzi po `missingInputs`
(IRON → IRON_ORE → Mine), startowe poletka COAL/IRON_ORE w ringu 26..32 istnieją
(commit `6571652`) i są w oknie skanu `FindBuildAnchor` (margin 48). Problem NIE
leży w aktuatorach — leży w tym, że drabinka deficytów prawie nigdy nie wykonuje
zasobu tier-2 (sekcja niżej).

**AI rekrutuje i deployuje milicję.** Pierwsza jednostka w `units.rtsdata` kosztuje
tylko `FOOD_PROVISIONS 5` + manpower — harness asertuje recruit ≥1 i deploy ≥1
i przechodzi. „Pasywność" z playtestu to NIE zero rekrutacji, tylko brak całej
ekonomii broni: fale milicji bez mieczy/oblężenia są bezwartościowe, a AI nigdy
nie wchodzi na wyższe jednostki. Nie szukać buga „AI w ogóle nie rekrutuje".

**Pierwotna przyczyna (strukturalna, nie strojenie):** tier-2 jest trwale
zagłodzony przez drabinkę deficytów:

1. Bias tier-1 (`WOOD 20, STONE 15, PLANKS 12, FOOD_PROVISIONS 12`) jest WIECZNY —
   a reguła „low reserve" w `DiagnoseResourceNeed` (`src/ai/AIActions.cpp:436`)
   odpala się przy `stored < consumed*2`. Budowy nieustannie zjadają stock, więc
   tier-1 generuje deficyty 0.28–0.63 w każdym cyklu, na zawsze.
2. IRON/TOOLS/miecze mają `priority 40` → urgency ≤ 0.9·0.4 = **0.36** — zwykle
   poniżej bieżącego pasma tier-1. Seed kosztu preferowanej jednostki
   (`AIModel.cpp:338-355`) daje ledwie `clamp(0.5·0.4)` = **0.2**.
3. `s.deficits.resize(4)` (`AIModel.cpp:364`) + „pierwszy sukces wygrywa cykl"
   w `ExecuteEconomy` → wpisy 0.36/0.2 wypadają z listy albo nigdy nie dostają
   tury. Zamierzony handoff („urgency tier-1 naturalnie spadnie, tier-2 wygra
   późniejszy cykl" — komentarz w `ai.rtsdata`) NIGDY nie następuje.
4. `ExecuteRecruitDeploy` (`AIModel.cpp:746`) przy zablokowanej topowej jednostce
   po prostu schodzi do milicji (affordable) — najwyższy score w modelu (0.8)
   nigdy nie pracuje na rzecz odblokowania własnych braków.

Do tego COAL: zero linii bias, zero konsumenta przed Foundry → kopalnia COAL z
opening planu zatyka bufor, flaguje `logisticsProblem`/`storageProblem` i tylko
szumi w drabince (urgency 0.55 przy pełnej wadze — bo COAL nie ma linii
`priority`!), potrafiąc sprowokować jednego bezsensownego duplikata.

---

## Zadanie 1 — RecruitDeploy buduje łańcuch swoich brakujących kosztów (rdzeń „dążenia do ataku")

**Wymóg:** najwyżej punktowana potrzeba AI ma sama stawiać ekonomię broni, zamiast
w nieskończoność rekrutować gołą milicję.

**Kroki:**
1. W `ExecuteRecruitDeploy` (`src/ai/AIModel.cpp:746`), w pętli po
   `RankUnitChoices(s)`: dla NAJWYŻEJ rankowanej jednostki (pierwszy element,
   nie każdy) policz brakujące koszty bezpośrednio — iteruj `def->cost`,
   porównaj z `AIActions::CountStoredResource` (NIE parsować stringa z
   `DiagnoseRecruitmentBlock`). Jeżeli brakuje zasobu i jego produkcja == 0
   (`GetResourceRate(...productionRatesPerMinute...)`), zawołaj
   `TryBuildProducerFor(world, player, brakującyZasób)` — jeśli zwróci true,
   cykl skonsumowany, return true.
2. Dopiero gdy budowa łańcucha nie wyszła (nic do zbudowania / cooldowny /
   backoff), spadnij do dzisiejszego zachowania: rekrutuj pierwszą affordable
   jednostkę z rankingu. Kolejność ma być: „spróbuj odblokować najlepszą" →
   „w międzyczasie rekrutuj co się da" (to drugie już działa, nie psuć).
3. Uszanuj istniejący `deficitBackoff` (`AIActionState`): po nieudanym
   `TryBuildProducerFor` dla kosztu ustaw backoff 12 s temu zasobowi, żeby
   RecruitDeploy nie mielił co cykl tej samej ślepej uliczki (wzorzec z
   `ExecuteEconomy`, `AIModel.cpp:551-558`).
4. Manpower bez zmian: jednostka zablokowana TYLKO manpowerem nie uruchamia
   budowy producentów (manpower-emergency z rundy 2 to załatwia po stronie
   Economy).
5. Determinizm: `TryBuildProducerFor` i `CountStoredResource` to czyste odczyty +
   `SubmitCommand` — bez nowych źródeł niedeterminizmu.

**Testy:** unit w `UtilityAIModelTests`: świat z Barracks, żywym łańcuchem
żywności i ZEREM produkcji IRON/mieczy → po kilku cyklach `Update` AI submituje
build Mine/Foundry/Smith (sprawdzić po komendach), a NIE ogranicza się do
rekrutacji milicji. Drugi przypadek: wszystkie koszty topowej jednostki dostępne →
zachowanie identyczne jak dziś (rekrutacja, zero budów z tej ścieżki).

---

## Zadanie 2 — handoff priorytetów: tier-2 wchodzi na pełną wagę, gdy tier-1 stoi

**Wymóg:** statyczne `priority 40` nigdy nie odda pierwszeństwa. Zamiar z
`ai.rtsdata` („suppressed until tier 1 has producers") ma być zaimplementowany
jawnie, jako reguła w kodzie.

**Kroki:**
1. `UtilityAIModel::Sense` (`src/ai/AIModel.cpp`): przed pętlą kandydatów policz
   `bool economyEstablished = s.foodProductionAlive &&
   s.productionBuildingCount >= (int)OpeningPlan.size()` (12 — opening plan
   ukończony).
2. Gdy `economyEstablished`: użyj kopii `priorityWeights` z wagami podniesionymi
   do 1.0 dla wszystkich zasobów (efektywnie: przestań przekazywać wagi tier-2 <
   1.0; najprościej — lokalna mapa `effectiveWeights` przekazywana do WSZYSTKICH
   wywołań `DiagnoseResourceNeed` w `Sense` i w `TryBuildProducerFor`; pole
   `priorityWeights` zostaje jako baza). Komentarz w kodzie ma mówić wprost:
   wagi z ai.rtsdata rządzą TYLKO otwarciem — po zbudowaniu bazy tier-2 ma
   konkurować pełną urgency, bo bias tier-1 jest wieczny i sam nigdy nie odda.
3. `resize(4)` → `resize(6)` (`AIModel.cpp:364`), żeby seed kosztów rekrutacji
   i tier-2 nie wypadały z listy w cyklach, gdzie tier-1 chwilowo skacze.
4. NIE zmieniać wartości w `ai.rtsdata` w tym zadaniu (jedna zmienna naraz;
   strojenie = zadanie 6).

**Testy:** unit: gospodarka z ukończonym opening planem i żywym foodem →
`Sense().deficits` zawiera IRON z urgency ≥ 0.62 (pełna waga), nie 0.36.
Kontrprzypadek: opening plan w połowie → wagi 0.4 działają jak dziś (Foundry
przed Woodcutterem ma NIE wrócić — to był bug naprawiony w rundzie 1).

---

## Zadanie 3 — rotacja drabinki: sukces też dostaje cooldown

**Wymóg:** dziś backoff dostaje tylko PORAŻKA (`deficitBackoff`), a sukces może
serwować ten sam zasób co cykl (przez `TrySubmitBuild`-owy cooldown typu budynku
zwykle inny typ tego samego łańcucha) — niższe wpisy drabinki głodują.

**Kroki:**
1. W `ExecuteEconomy` (`src/ai/AIModel.cpp:551`): po UDANYM
   `TryBuildProducerFor(deficit.resource)` ustaw temu zasobowi krótki cooldown
   sukcesu, np. `actions.deficitBackoff[deficit.resource] = 4.0` (mniej niż 12 s
   porażki — chodzi o przepuszczenie 2–3 cykli, nie o karę), i dopiero potem
   `return true`.
2. To samo w ścieżce z zadania 1 (RecruitDeploy → udany build kosztu).
3. Nic więcej — `producerUnderConstruction` cap (0.3) już obniża urgency
   świeżo zamówionych producentów; ten cooldown domyka lukę między submitem
   a startem budowy.

**Test:** unit: dwa deficyty (WOOD 0.5, IRON 0.4), oba wykonywalne → dwa kolejne
cykle budują NAJPIERW producenta WOOD, POTEM producenta IRON (dziś: WOOD, WOOD).

---

## Zadanie 4 — COAL: bias + guard duplikatów liczony per-produkt, nie per-typ budynku

**Kroki:**
1. `assets/data/ai.rtsdata`: dodać `consumption COAL 6` (Foundry pali węgiel;
   amortyzacja utrzyma kopalnię COAL przy życiu zanim Foundry stanie) oraz
   `priority COAL 40` (tier-2 — COAL bez linii priority ma dziś pełną wagę 1.0
   i szumi w drabince ponad prawdziwym tier-1).
2. `TryBuildProducerFor` (`src/ai/AIModel.cpp:600-609`): `ownedProducers` liczyć
   NIE przez `CountOwnedBuildings(player, option.buildingType)` (każdy Mine,
   obojętnie na jakim terenie), tylko licząc budynki, których
   `ProductionComponent::products` zawiera `target` (wzorzec pętli z
   `DiagnoseResourceNeed`, `src/ai/AIActions.cpp:407-427`; sortowanie
   niepotrzebne — to czysta suma). Analogicznie sort „diversify before
   duplicating" (`AIModel.cpp:610-618`): porównywać liczbę producentów DANEGO
   ZASOBU, nie typu budynku.
3. Efekt do zweryfikowania w harness: kopalnia COAL nie blokuje decyzji o
   kopalni IRON_ORE (i odwrotnie), duplikaty COAL znikają.

**Testy:** unit: gracz z 2 kopalniami na COAL (obie z flagami problemów) i zerem
kopalń IRON_ORE → `TryBuildProducerFor(IRON_ORE)` NIE jest zablokowane guardem
i submituje build. Istniejące testy guardu duplikatów (harness catch z rundy 2)
mają zostać zielone.

---

## Zadanie 5 — trwały bias osobowości per-AI (wymóg usera: „2 AI nie gra identycznie")

**Wymóg:** drobny, TRWAŁY skos decyzyjny per gracz, aktywny na KAŻDYM poziomie
trudności (dziś na Hard szum = 0.0 → dwa Hard AI to ten sam skrypt). To ma być
osobowość (stały kierunek), nie szum (dzisiejszy per-cykl jitter zostaje bez
zmian jako mechanizm trudności).

**Kroki:**
1. `UtilityAIModel` (inc/ai/AIModel.h + AIModel.cpp): w bloku seedowania
   (`AIModel.cpp:132-143`), ZARAZ PO `noiseRng.seed(...)`, wylosuj i zapisz raz:
   - `std::array<double, AINeed::Count> personalityNeedBias` — każdy z
     `uniform_real_distribution(-0.08, 0.08)` (±8%),
   - `int personalityWaveBias` — `uniform_int_distribution(-1, 2)` (WaveSize
     6 → efektywnie 5..8).
   Losowanie PRZED pierwszym użyciem noiseRng w cyklach decyzyjnych i w stałej
   liczbie wywołań — sekwencja RNG pozostaje deterministyczna funkcją
   (seed mapy, id gracza), identyczna w obu światach lockstepu.
2. W `Update`, po policzeniu `scores[]` a PRZED szumem trudności:
   `scores[i] *= 1.0 + personalityNeedBias[i]`. Skala ±8% jest celowo mniejsza
   niż progi separacji potrzeb (0.6 vs 0.65 vs 0.75) — osobowość rozstrzyga
   remisy i tempo, nie łamie architektury priorytetów. NIE biasować emergency
   (score 0.9/0.95 ścieżek — te są ustawiane PO... uwaga: one wychodzą ze
   `ScoreNeed`, więc bias JE też pomnoży; 0.9·0.92 = 0.83 nadal > podłoga 0.8 —
   policzyć i zostawić komentarz, że marginesy emergency przeżywają ±8%).
3. `WaveSize` w `ExecuteRecruitDeploy` i `ScoreNeed`: zamienić stałą na
   `WaveSize + personalityWaveBias` (jeden helper/pole, nie dwa miejsca z gołą
   arytmetyką).
4. Determinizm: rozszerzyć istniejący test
   `TwoWorldsSameSeedWithNoisyAIStayInSync` o difficulty 3 (Hard — dotąd bez
   żadnego RNG w ogóle; teraz z osobowością) — checksumy obu światów identyczne.
5. Test różnorodności: dwóch AI graczy w jednym świecie → ich
   `personalityNeedBias` różnią się (seed XOR playerId), oraz sanity-check, że
   dla ustalonego seeda wartości są stabilne między uruchomieniami.

---

## Zadanie 6 — strojenie i weryfikacja końcowa (dopiero PO 1–5)

1. Pełny suite `.\run_tests.ps1` zielony po KAŻDYM zadaniu osobno (commit per
   zadanie, konwencja `AI-rework(...)`).
2. `AIBehaviorHarnessTests` — rozszerzyć tabelę o kolumny `smith` i `brk`
   (Barracks) obok istniejących wcut/lmil/mine/fdry; nowe asercje końcowe:
   **fdry ≥ 1, smith ≥ 1** w horyzoncie testu oraz roster zawiera co najmniej
   jedną jednostkę inną niż najtańsza (miecz w obiegu = łańcuch przeszedł
   end-to-end). Jeżeli nie przechodzą — wracać do zadań 1–4, nie podkręcać
   biasu na ślepo.
3. Determinizm ×3 (pętla pełnego suite'u) — zmiany dotykają decyzji AI i RNG.
4. Manualny playtest (user): (a) w ~10–15 min AI ma stojący Foundry + Smith i
   rekrutuje jednostki z mieczami, (b) fale idą na tor regularnie (dążenie do
   ataku widoczne), (c) dwa AI na jednej mapie rozwijają się zauważalnie
   inaczej (kolejność budynków / wielkość fal), (d) manpower/food nadal się
   spina — pętla z rundy 2 nie może się zepsuć.
