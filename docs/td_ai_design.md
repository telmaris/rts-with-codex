# AI pod Tower Defense — model utility (AI-rework 2026-07-16)

Finalny opis modelu AI po reworku z TODO #2 (commity `AI-rework(etap-0..5)`).
Zastępuje w całości 3-tierowy system osi/celów/milestone'ów opisany w
`docs/strategic_ai_design.md` (superseded) — osie priorytetów były niepraktyczne
w konwencji tower defense.

## Cel projektowy

AI dąży do zniszczenia gracza jednostkami na torze wojskowym. Cel nadrzędny:
**deployować jak najwięcej jednostek**, wsparty ekonomią utrzymującą koszty
rekrutacji (manpower, żywność, surowce kosztów jednostek) i warstwą defensywną
(wieże + amunicja). Algorytm prosty i pewny: mierzalne wskaźniki → utility
potrzeb → jedna konkretna akcja.

## Pliki

| Co | Gdzie |
|---|---|
| Model decyzyjny (`UtilityAIModel`, `AISituation`, `AINeed`) | `inc/ai/AIModel.h`, `src/ai/AIModel.cpp` |
| Aktuatory + zapytania read-only (`AIActions::*`, `AIActionState`) | `inc/ai/AIActions.h`, `src/ai/AIActions.cpp` |
| Kontroler (seam `IController`) | `inc/ai/Controller.h`, `src/ai/Controller.cpp` |
| Grant startowy trudności | `src/core/GameWorld.Init.cpp` — `GrantDifficultyStartingBonus` |
| Check podłączenia do dróg | `src/economy/LogisticsComponent.cpp` — `IsConnectedToRoadNetwork` |
| Testy | `tests/UtilityAIModelTests.cpp` |

## Cykl decyzyjny

Timery (sim-time, tick 100 Hz): sensing 1 s, decyzja 1.5 s, utrzymanie dróg 2 s
(`AIActions::TryBuildRoads` poza pulą utility, jak w starym modelu), audyt
łączności 3 s (BFS po sieci dróg nie jest darmowy).

1. **Sensing → `AISituation`** (wyłącznie odczyty):
   - tor: liczba/siła moich jednostek i jednostek wroga idących NA MNIE
     (`routeToPlayerId == mój id`); siła = effective roadAttack + 0.1·HP;
   - `hqHpRatio`, roster per unitDefId, wieże (suma damage×attackSpeed),
     zapas+produkcja `ARROWS`, manpower, `foodProductionAlive`
     (produkcja/min `FOOD_PROVISIONS` > 0);
   - **deficyty**: deterministyczna lista kandydatów (FOOD_PROVISIONS →
     zasoby kosztów jednostek z katalogu → ARROWS przy wieżach → wszystko
     aktualnie konsumowane) → `AIActions::DiagnoseResourceNeed` → sort
     (urgency desc, enum asc), top 4; dodatkowo koszty preferowanej jednostki
     z kompozycji bez zapasu i produkcji dostają deficyt 0.5 (buduje łańcuch
     Smith/miecze, zanim telemetria zobaczy konsumpcję);
   - **budynki niepodłączone**: `IsConnectedToRoadNetwork` == false, cache'owane
     jako positionId (nie Building* — brak wiszących wskaźników), posortowane
     po id budynku.
2. **Utility potrzeb** (`AINeed`, wynik [0,1]):
   - `Threat() = siła wroga na moim torze / max(1, deployed + wieże)`;
     `UnderAttack() = enemyIncomingCount > myDeployedCount`.
   - **Defense**: max(braki garnizonu 0.3; wieże bez amunicji 0.45;
     `clamp01(0.5·Threat + 0.5·(1−hqHpRatio))`). Docelowy garnizon
     (`DesiredTowerCount`): 0 przed minimalną ekonomią, potem 2 (+1 przy
     Threat>0.5, +1 przy HQ<70%), cap 4.
   - **RecruitDeploy**: `0.55 + 0.25·(1−clamp01(Threat))`; 0.95 gdy możliwy
     emergency deploy (Threat>1 i roster>0). Trwale najwyższy — to jest cel gry.
   - **EconomySustain**: max(0.6 dopóki opening plan niedokończony; 0.75 gdy
     produkcja żywności martwa; urgency najgorszego deficytu).
   - **LogisticsRepair**: `min(1, 0.4 · liczba niepodłączonych)`.
   - **Research**: 0 przy Threat>0.5; inaczej max(0.35 gdy stoi bezczynny
     University; 0.25 gdy brak aktywnego focusa; 0.3 na budowę University gdy
     ekonomia się utrzymuje).
3. **Egzekucja**: potrzeby w kolejności (score desc; remis → kolejność enumu =
   priorytet Defense > RecruitDeploy > Economy > Logistics > Research), pierwsza,
   która wykona realny `GameCommand`, kończy cykl. Poniżej 0.05 — nic.

## Akcje per potrzeba

- **Defense**: buduj `DefenseTower` przy HQ (anchor z targetem HQ); wieże bez
  amunicji → `TryBuildProducerFor(ARROWS)` (łańcuch Smith).
- **RecruitDeploy**: brak Barracks → buduj Barracks. Roster ≥ `WaveSize` (6)
  lub emergency → `DeployUnits` CAŁYM rosterem (kolejność po instanceId) na
  cel z `FindAttackTargetPlayer` (cache 3 s). Inaczej rekrutuj wg kompozycji.
  Gate'y zdrowego rozsądku (wyniesione z realnego stalla znalezionego testem):
  nie kolejkuj za wpisem czekającym na dostawę (strict FIFO głodzi resztę),
  kolejka max 2, a przy Barracks bez połączenia z drogami tylko jednostki
  z kosztem już zbuforowanym lokalnie (DiagnoseRecruitmentBlock liczy zapasy
  GLOBALNIE — bez drogi nic nie dojedzie).
- **EconomySustain**: manpower <5 przy żywej żywności → dodatkowa Village;
  deficyty → `TryBuildProducerFor` (schodzi po brakujących inputach łańcucha,
  depth ≤3; wybór producenta: najmniej posiadanych, potem enum); fallback:
  **opening plan** — stała sekwencja bootstrapująca łańcuch FOOD_PROVISIONS
  (Woodcutter → WheatFarm → Windmill → Bakery → Well → HuntersHut → Inn →
  LumberMill → Woodcutter#2 → Mine → Village#2 → StorageBuilding); krok
  nieopłacalny = oszczędzaj (nie przeskakuj), krok bez miejsca = pomiń.
- **LogisticsRepair**: `TryBuildRoads` (stuby przy magazynach/HQ + podłączenie
  pierwszego odciętego producenta), potem naprawy ścieżkowe dla budynków
  z przyległą drogą bez trasy do magazynu.
- **Research**: focus (pierwszy odblokowywalny — focuses.rtsdata to płaska
  ściągawka do czasu przeprojektowania) → budowa University → najtańsza
  dostępna technologia z preferencją tagu (`military` pod presją, inaczej
  `production`).

## Kompozycja rosteru (`RankUnitChoices`, pure + unit-tested)

- Postawa **Defensive** (`UnderAttack`): maksymalizuj
  `(roadAttack + armor + 0.5·maxHp) / (manpower + Σkosztów)` — dziś knight.
- Postawa **Offensive**: utrzymuj mix 2:1 lane-fighter:siege
  (`siegeCount·3 < rosterCount`; siege = `siegeAttack > roadAttack`); pusty
  roster zaczyna od fighterów (ram wymaga eskorty — notka w units.rtsdata);
  w klasie fighter ranking po `moveSpeed·roadAttack`, w siege po `siegeAttack`.
- Kontrowanie składu wież przeciwnika — **poza zakresem v1**: istnieje jeden
  typ wieży i jeden `DamageType`; architektura (resistances w UnitDefinition,
  postawa w kompozycji) jest gotowa na rozszerzenie danymi.

## Poziomy trudności (jeden model, zero różnic w logice)

`MapParameters::aiDifficulty` (0 Primitive … 3 Hard, z UI nowej gry/lobby):

| Poziom | Grant startowy (HQ) | Manpower (% cap) | Szum utility | Skip cyklu |
|---|---|---|---|---|
| 0 Primitive | 0 | 0% | ±30% | 15% |
| 1 Easy | +15 | 10% | ±20% | 10% |
| 2 Normal | +40 | 25% | ±10% | 5% |
| 3 Hard | +80 | 50% | 0% | 0% |

Grant (`GrantDifficultyStartingBonus`): tylko podstawy ekonomiczne
(WOOD/STONE/PLANKS/IRON/TOOLS/FOOD_PROVISIONS) — bez broni/amunicji, przewaga
przyspiesza ekonomię, nie rozdaje armii. Wołany przy inicie świata dla slotów
AI (po **id slotu**, nie controllerType — mirror klienta musi zbudować
identyczny stan). Dodatkowe budynki startowe odłożone (helpery placementu
splecione z generacją patchy). Szum: `mt19937` seedowany
`seed_mapy ^ (0x9E3779B9 · (playerId+1))` — identyczna sekwencja w światach
o tym samym seedzie, stan nieserializowany (konwencja stanu AI).

## Determinizm (twarde reguły)

- Wyłącznie odczyty + `SubmitCommand`; AI biega tylko na hoście
  (`InitMultiplayerWorld`: klient dostaje `Remote`), ale testy dwóch światów
  wymagają pełnej powtarzalności per seed — także z aktywnym szumem
  (`TwoWorldsSameSeedWithNoisyAIStayInSync`, weryfikowane `--gtest_repeat`).
- Każda pętla po `GetTrackedBuildings()` z semantyką first-match/konkurencji —
  kopia do wektora + sort po `building->id` (patrz `docs/tech_debt.md`,
  klasa bugów pointer-ordering). Agregacje (sumy/liczniki/OR) są bezpieczne.
- Cache międzycyklowe trzymają positionId/wartości, nie `Building*`.

## Świadome uproszczenia / przyszłe rozszerzenia

- Kontry kompozycją na typy ataku wież — czeka na 2+ typy wież/damage.
- Telemetria strat jednostek (dawne C4) — Threat liczy siłę obecną na torze,
  nie historię strat; wystarcza dla v1.
- Dodatkowe budynki startowe na Hard — odłożone (patrz wyżej).
- Brak komendy anulowania wpisu kolejki rekrutacji (dotyczy też gracza) —
  gate'y rekrutera minimalizują ryzyko zaklinowania, ale cancel to osobny temat.
