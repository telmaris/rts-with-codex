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

- [x] **Przejęte budynki (TD etap-6.3) nie były rejestrowane w sieci dróg zwycięzcy**
  (`src/core/GameWorld.Elimination.cpp`, sekcja "Production buildings change hands"). Naprawione
  2026-07-13 (T12, `docs/post_pivot_audit_2026-07-12.md`): `UpdateNavMap(id, nullptr)` API na
  usunięcie węzła JUŻ ISTNIEJE (wpis poniżej twierdzący inaczej był nieaktualny) — `EliminatePlayer`
  teraz dla każdego kafla footprintu przejętego budynku woła
  `defeated->roadNetwork->UpdateNavMap(id, nullptr)` + `conqueror->roadNetwork->UpdateNavMap(id, building)`.
  Drogi (`Road`) świadomie NIE przechodzą razem z budynkami (brak `ProductionComponent`) — zwycięzca
  musi zbudować własne połączenie, dokładnie jak przy dowolnym innym nowym budynku. Test regresyjny:
  `EliminationTests.CapturedBuildingRejoinsConquerorsRoadNetworkAfterElimination`.

- [x] **`TwoWorldsSameSeedWithNoisyAIStayInSync` (`tests/UtilityAIModelTests.cpp`) flakował
  INTERMITENTNIE w PEŁNYM suicie (NIE w izolacji) — ROOT CAUSE ZNALEZIONY I NAPRAWIONY 2026-07-19.**
  Odkryte 2026-07-18 przy pisaniu `SubmitRoadPathReusesJustPlacedCorridor` (AI economy tuning plan,
  Zadanie 1); eskalowane 2026-07-19, kiedy ten sam test zaczął realnie blokować GitHub Actions CI
  usera (nie tylko lokalny suite). Wcześniejsza hipoteza z tej samej sesji (ASLR + niezaudytowany
  `Building*`/`Resource*` adres w jakimś tie-breaku) była BLISKA, ale nietrafiona co do
  konkretnego mechanizmu — patrz niżej.
  **Root cause:** `static ResourcePool resourcePool;` (`src/data/Resource.cpp:5`) to
  PROCES-GLOBALNY singleton dzielony przez KAŻDY `GameWorld` skonstruowany w binarium testowym —
  nie tylko przez oba światy w JEDNYM teście, ale przez WSZYSTKIE ~185 testów w całym uruchomieniu
  `rts_tests.exe`. `ResourceBuffer::GenerateResource(type)` pobiera instancję z
  `resourcePool.GetResource(type)` — free-listą typu (`std::deque<Resource*>`), FIFO. Gdy testy
  wcześniej w tej samej sekwencji binarium nie zwracają (albo zwracają nie w pełni) każdego
  zasobu, jaki trzymały, dany typ ma węższy free-list dla KAŻDEGO kolejnego testu. Test
  konstruujący DWA strukturalnie identyczne światy (worldA, worldB) z tym samym seedem oczekuje
  identycznego stanu startowego — ale skoro oba ciągną z TEJ SAMEJ, częściowo wyczerpanej puli,
  ten świat, którego `GenerateResource()` w danym ticku uruchomi się PIERWSZY, dostaje pierwszeństwo
  do tego, co zostało; drugi dostaje CICHO OBCIĘTY grant (pętla `for (i=0..amount)
  GenerateResource()` w `GrantDifficultyStartingBonus`/`GrantDebugResourcesToHeadquarters` po
  prostu przestaje cokolwiek dodawać, gdy `GetResource` zwróci `nullptr` — brak błędu, brak logu).
  **Potwierdzone bezpośrednim dumpem** (celowane logi w `ExecuteEconomy`/`TryBuildProducerFor`,
  usunięte po zdiagnozowaniu): zapas WOOD rozjeżdżał się MIĘDZY OBOMA ŚWIATAMI już na tick=1
  (np. 135 vs 83), zanim jakakolwiek decyzja AI się wykonała — więc to NIE był dryft decyzji AI,
  tylko odziedziczona nierówność startowa w surowym stanie ekonomii, która dopiero PÓŹNIEJ (tick
  ~301 w obserwowanym przebiegu) przełożyła się na widoczną różną decyzję budowy
  (`player->CanBuildDefinition` zwracał różne wyniki dla tego samego kosztu budynku między
  światami, bo `HasBuildResources`' sumowanie bufora faktycznie sumowało różne ilości). To
  wyjaśnia WSZYSTKIE wcześniej zaobserwowane fakty: flake tylko w PEŁNYM suicie (izolacja/
  `--gtest_repeat` na jednym teście zaczyna od świeżo skonstruowanego procesu, więc pula jest
  praktycznie pełna — nie ma z czego wyczerpać); wysoka, ale nie 100% powtarzalność MIĘDZY
  ODDZIELNYMI URUCHOMIENIAMI PROCESU bez zmian kodu (zależy od tego, ile realnego czasu/ticków
  zdążyły przetworzyć WCZEŚNIEJSZE testy z zegarem tła zanim je zniszczono — timing, nie ASLR).
  **Audyt na żywo z poprzedniej sesji pozostaje aktualny i nie stracił wartości** — wszystkie
  zweryfikowane bezpieczne miejsca (`BuildChecksum()` sortowanie po id, `Sense()`'s audyt
  łączności, `TryBuildRoads`, `TryBuildProducerFor`/`FindProducerOptions`, `FindBuildAnchor`'s
  `DistanceToNearestInfrastructure` przez `min()`, recruit's barracks-pick, `TryPayBuildCost`) są
  nadal poprawne i NIE są związane z tym bugiem — [[determinism_pointer_ordering_bug_pattern]]
  zostaje aktualne dla przyszłych audytów `GetTrackedBuildings()`, po prostu nie było to źródło
  TEGO konkretnego flake'a.
  **Fix (2026-07-19, TYLKO infrastruktura testowa, ZERO zmian w kodzie produkcyjnym):**
  `ResourcePool::Reset()` (`inc/data/Resource.h`, `src/data/Resource.cpp`) odbudowuje free-listę
  KAŻDEGO typu do pełnej pojemności; wolna funkcja `ResetResourcePool()` daje do niej dostęp z
  zewnątrz TU (pole `resourcePool` ma linkage wewnętrzny). Nowy `tests/TestResourcePoolIsolation.cpp`
  rejestruje `::testing::TestEventListener::OnTestStart` wołający `ResetResourcePool()` przed
  KAŻDYM test case'em — przywraca izolację między testami bez ruszania architektury
  współdzielonego singletona. Zweryfikowane: 15/15 pełnych przebiegów suite'u zielonych (wcześniej
  flakowało na próbie #1 niemal za każdym razem), `--gtest_repeat=20` na samym teście determinizmu
  czysty, pełny suite 186/186 zielony.
  **Osobne, NIENAPRAWIONE znalezisko do dalszej inwestygacji:** ten sam mechanizm (dzielona,
  procesowa `ResourcePool`) teoretycznie mógłby dotyczyć PRAWDZIWEGO lokalnego MP — `LocalhostHostSession`
  + `LocalhostClientSession` trzymają DWA żywe `GameWorld` w JEDNYM procesie. Nie zweryfikowano, czy
  host i klient faktycznie ścigają się o tę samą pulę w praktyce (być może są w praktyce
  zabezpieczone tym, że klient dochodzi do tego samego stanu przez IDENTYCZNE komendy, a nie przez
  niezależne wywołania `GenerateResource`, więc kolejność konstrukcji mogłaby nie mieć znaczenia) —
  ale to ZAŁOŻENIE, nie dowód. Wymaga osobnej sesji, jeśli kiedyś pojawi się niewyjaśniony desync w
  realnej lokalnej rozgrywce MP. Docelowa, poprawna naprawa architektoniczna (pula per-`GameWorld`
  zamiast procesowego singletona) to osobny, większy refaktor — `ResourceBuffer::GenerateResource/
  FreeResource/SetStoredAmount` mają dziś ~15 wywołujących plików bez referencji do właściciela puli,
  więc przewleczenie referencji do puli przez wszystkie z nich jest świadomie odłożone jako
  osobne zadanie, nie improwizowane pod presją "musi działać teraz".

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

- [x] **NAPRAWIONE 2026-07-25 — HQ↔StorageBuilding bounce.** Root cause potwierdzony i usunięty:
  `StorageComponent::Update` robił ambientowy push całego bufora do KAŻDEGO trackowanego budynku,
  który akceptował dany typ. Dwa magazyny (HQ + StorageBuilding) akceptują wszystko i oba widziały
  u siebie nawzajem wolne miejsce → wieczne odbijanie. Zgłoszone niezależnie przez usera tego dnia
  w mocniejszej formie: **postawienie nowego StorageBuildingu powodowało, że HQ próbowało
  przerzucić do niego całą swoją zawartość.**

  Fix (jedna zmiana architektoniczna, nie łatka): magazyn jest PASYWNY — przyjmuje dostawy i
  obsługuje żądania, nigdy sam nie inicjuje transportu (`StorageComponent` nie ma już `Update`).
  Wszyscy konsumenci i tak ciągnęli sami (`LogisticsComponent::MaintainRequests`,
  `PopulationComponent::RequestFoodSupply`, Tower/Barracks przez własne komponenty), więc push był
  redundantny. Żeby pasywność nie zamroziła towaru w "nie tym" magazynie, `RequestResource` ma
  fallback na całą sieć magazynów przez `StockpileIndex::RankSourcesFor` (najbliższy DROGĄ magazyn
  który faktycznie ma dany zasób; brak drogi = nie jest źródłem w ogóle). Jawne przepięcie
  dostawcy przez gracza dalej wygrywa — `LogisticsComponent::IsRestrictedToDirectSuppliers`.

  Regresja przypięta testami: `BuildingDomainTests.NewStorageBuildingDoesNotDrainExistingWarehouses`,
  `.ConsumerPullsFromUnwiredWarehouseThatHoldsTheStock`, `.RankSourcesForSkipsWarehousesWithNoRoadPath`,
  `.ConsumerStopsPullingFromHqAfterSupplierReassignment`.

  <details><summary>Oryginalny opis (historyczny)</summary>

  **Podejrzenie: HQ i StorageBuilding mogą wpadać w powtarzający się cykl transportu tego
  samego zasobu tam i z powrotem (nie tylko pojedynczy odbicie, ale ciągły, wieloztukowy
  wzorzec).** Odkryte przypadkowo 2026-07-14 przy pisaniu testu akceptacyjnego dla C1
  (`tests/AIMilitaryPipelineTests.cpp`) — po dłuższej (~60 sim-sekund), w pełni normalnej
  rozgrywce AI (bez żadnej nietypowej manipulacji zasobami) log zaczął zalewać się parami
  `"[Headquarters] Transport zasobu X zakończony, dodaję zasób"` /
  `"[StorageBuilding] zasób X usunięty z transportables"` (i analogicznie dla STONE) —
  setki/tysiące linii na sekundę, silnie korelujące ze spowolnieniem (~20-30 ms/tick zamiast
  <1 ms/tick w tym oknie). Reprodukcja pierwotnie natrafiona przy nadmiernym "seedowaniu"
  buforów magazynowych w teście (wypełnienie KAŻDEGO typu zasobu do pełna) — po zmniejszeniu
  seeda objaw był słabszy, ale wciąż obecny pod koniec dłuższych przebiegów, co sugeruje że to
  NIE jest artefakt samego testu, tylko realny wzorzec w `StorageComponent::Update`'s
  auto-redystrybucji między dwoma budynkami storage-like (HQ i StorageBuilding), które oba
  akceptują ten sam typ zasobu — możliwe że `GetReceiveCapacity`/`CanAcceptResource` nie
  uwzględnia poprawnie zasobu "w locie" (już wysłanego, jeszcze nie policzonego jako zajmujący
  miejsce u odbiorcy), pozwalając obu stronom myśleć że mają wolne miejsce jednocześnie. **Nie
  zbadane głębiej — poza zakresem C1** (dotyczy ogólnego systemu dystrybucji zasobów, nie
  AI/Military). Wymaga dedykowanej sesji: (1) reprodukować w izolowanym teście jednostkowym
  (HQ + StorageBuilding, oba akceptujące jeden typ zasobu, długi przebieg), (2) potwierdzić czy
  to faktyczna pętla nieskończona czy tylko dużo pojedynczych, poprawnych transportów w wąskim
  oknie czasowym, (3) jeśli pętla — sprawdzić `GetReceiveCapacity` pod kątem uwzględniania
  zasobów już-w-drodze do tego samego odbiorcy.

  **Aktualizacja 2026-07-15 (A5, `docs/work_plan_2026-07-13.md`):** przy wyłączeniu Barracks
  z ogólnej pętli dystrybucji `StorageComponent::Update` (Barracks miał przestać przyjmować
  zasoby ambientowo — zobacz A5), `tests/AIMilitaryPipelineTests.cpp` zwolnił z ~95s do ~157s.
  Zweryfikowane grepem: 281k linii bounce'a (Transport HQ↔StorageBuilding STONE/FOOD_PROVISIONS)
  w tym przebiegu vs ~272k wcześniej — to POTWIERDZA że to ten sam, znany bug, nie nowy, ale
  Barracks jako trzeci odbiorca w puli najwyraźniej odciążał trochę ruch między HQ i
  StorageBuilding; po jego wyłączeniu z puli cały ruch koncentruje się na tych dwóch, więc bug
  robi się nieco bardziej aktywny/kosztowny. Nie naprawione (wciąż poza zakresem A5/C1) —
  odnotowane tylko jako potwierdzenie że problem jest realny i priorytet dedykowanej sesji
  rośnie.

  </details>

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
- [x] **`std::set<Building*>`/`PlayerDataTracker::buildings` kluczowane surowym wskaźnikiem —
  pułapka na determinizm w KAŻDYM miejscu, gdzie kolejność iteracji "wygrywa" (nie tylko
  suma/min/max).** Odkryte 2026-07-13 przy naprawie generacji mapy: dwa wcześniejsze perf-fixy
  ("tracked buildings zamiast pełnego skanu tilemapy" — `TileMap::AutoConnectBuilding`,
  `PrimitiveAIModel::TryBuildRoads`) zamieniły iterację po tilemapie (kolejność po id kafla,
  deterministyczna) na iterację po `GetTrackedBuildings()` (`std::set<Building*>`, kolejność po
  adresie wskaźnika) — złamało to `HqCombatSystemTests.SiegeToEliminationIsDeterministicForSameSeed`
  ~2/3 uruchomień pełnego suite (adresy heap różnią się między niezależnie zbudowanymi `GameWorld`).
  Naprawione: oba miejsca sortują teraz kandydatów po `building->id` przed użyciem. Pełny opis w
  `docs/post_pivot_audit_2026-07-12.md` follow-up #5. **Lekcja na przyszłość:** przy każdej
  zamianie "pełny skan mapy" → "tracked registry" jawnie sprawdzić, czy wynik zależy od
  KOLEJNOŚCI iteracji, czy tylko od ZBIORU wartości — jeśli od kolejności, sortować po `id`.

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
- [x] **CI uruchamiał tylko `--gtest_filter=GameCommandTests.*`** — naprawione, `.github/workflows/
  windows-release.yml` uruchamia wszystkie stabilne testy z `rts_tests.exe`. Długotrwały
  `AIBehaviorHarnessTests.*` pozostaje kompilowany, ale jest wyłączony z przebiegu GitHub Actions
  i uruchamiany lokalnie podczas strojenia AI.
- [x] **Brak cache vcpkg w CI** — naprawione, workflow ma krok `actions/cache@v4` na
  `C:\vcpkg\installed`/`C:\Users\runneradmin\AppData\Local\vcpkg`.
- [ ] **Brak buildu Linux/macOS** mimo ścieżek UNIX w CMake.
- [ ] **Zduplikowane skrypty** (`*.bat` + `*.ps1`) — `.bat` delegują do `.ps1`, akceptowalne.
- [ ] **`ResourceType::BEER` bez producenta ani konsumenta.** Odkryte 2026-07-13 (T13, docs-sync):
  CLAUDE.md błędnie opisywał "Well → WATER → Inn → BEER" — Inn faktycznie produkuje
  `FOOD_PROVISIONS` z BREAD+MEAT+WATER (`assets/data/buildings.rtsdata`), nie BEER. Żaden budynek
  dziś nie ma `output BEER`. Zostaje jako placeholder w enumie (nie usuwać bez bumpa save/wire) —
  do świadomej decyzji: dodać producenta (np. nowy budynek "Brewery") albo usunąć zasób.

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
- [ ] **`Bridge` (B6, docs/work_plan_2026-07-13.md) też nie ma HP i nie jest niszczalny przez
  wroga** — świadomie ta sama decyzja co dla wież powyżej (usuwalny tylko ręczną komendą
  `DestroyBuilding` przez właściciela). Gdyby gameplay miał wymagać, by maszerująca kolumna
  mogła zniszczyć most po drodze (odcinając zaopatrzenie przeciwnika), to ten sam nie-trywialny
  dodatek co dla wież — HP/hardDefense + mechanizm ataku jednostek NA budynki poza HQ.
- [x] **`ARROWS` nie ma dziś żadnego producenta.** Naprawione w ETAP 9: dodano recipe "Arrows"
  do Smith (`assets/data/buildings.rtsdata`, WOOD+IRON→ARROWS), więc wieże mają teraz realną
  ścieżkę zaopatrzenia przez prawdziwy łańcuch produkcji/logistyki, nie tylko `SetStoredAmount`
  w testach.

- [x] **TD(etap-9) — ~90 technologii w `focuses.rtsdata` (cała gałąź MILITARY + część POLITICS,
  247 linii `modifier`) odwoływało się do statów ze STAREGO systemu wojny, które nie istnieją
  od ETAP 1** (`MilitaryStrength`, `RecruitmentTime`, `RecruitmentManpowerCost`,
  `GarrisonCapacity`, `TerritoryRadius`, `HitPoints` na `building GuardTower/Fortress/Castle`).
  To NIE był tylko martwy kod — `Technology.cpp`'s `ParseBalanceStat` cicho domyślał
  nierozpoznaną nazwę statu na `BalanceStat::BuildTime`, więc te ~90 technologii faktycznie
  (błędnie) mnożyło `BuildTime` za każdym razem gdy ktoś by je odblokował. Podobnie
  `ParseBuildingType` cicho domyślał nierozpoznany typ budynku (`GuardTower`/`Fortress`/`Castle`)
  na `BuildingType::Building` — filtr, którego żaden realny kontekst zapytania nigdy nie
  spełnia, więc te konkretne linie były martwym kodem (ale wciąż nieprawidłowym).
  Naprawione decyzją użytkownika (2026-07-12): cały plik `focuses.rtsdata` wymieniony na płaską
  "ściągawkę" — jedna placeholder-technologia per `BalanceStat` (stare i nowe), tytuł = nazwa
  statu, do wykorzystania przy projektowaniu prawdziwego drzewa focusów od nowa. Przy okazji
  dodano parsowanie nowych statów bojowych (`UnitHp`/`UnitRoadAttack`/`UnitSiegeAttack`/
  `UnitArmor`/`UnitMoveSpeed`/`UnitAttackSpeed`/`UnitRecruitTime`/`UnitRecruitManpowerCost`/
  `HqMaxHp`/`HqDefense`/`HqThorns`/`TowerDamage`/`TowerRange`/`TowerAttackSpeed`/
  `TowerAmmoEfficiency`) i klucz `unit <unitDefId>` do `ParseModifier` (analogiczny do
  `building`/`resource`/`category`) — zobacz `docs/tower_defense_design.md`.
  `UnitRecruitTime`/`UnitRecruitManpowerCost` to jedyne dwa staty ze starego "Recruitment*"
  zestawu, które user poprosił zachować jako REALNE (nie placeholder) — rekrutacja jednostki
  na front musi kosztować czas > 0, żeby planowanie ataku było decyzją strategiczną. Wpięte w
  `RecruitmentComponent::QueueRecruitment` (`src/economy/RecruitmentComponent.cpp`) przez
  `Player::ModifyBalanceForUnit`, z podłogą `std::max(1.0, ...)` na czasie (nigdy nie może
  zejść do zera/ujemnej wartości niezależnie od multiplikatora) i GUI panelu rekrutacji
  (`src/ui/Gui.cpp`) zaktualizowane, by pokazywać efektywne (zmodyfikowane), nie surowe wartości.

- [x] **`assets/data/technologies.rtsdata` (drzewo SCIENCE) nie zgadzało się z 8 testami, które
  oczekiwały technologii "forestry"/"Mathematics".** Naprawione 2026-07-13 (T10,
  `docs/post_pivot_audit_2026-07-12.md`) decyzją użytkownika: te 8 testów usunięte
  (`TechnologyTests.cpp`/`PlayerEconomyTests.cpp`/`ResearchCatalogTests.cpp` — ten ostatni plik
  usunięty całkowicie, zawierał tylko ten jeden test) zamiast dostosowywane, ponieważ prawdziwe
  drzewo SCIENCE i cały tech tree i tak czekają na ręczne przeprojektowanie od podstaw (patrz
  `docs/tower_defense_design.md`) — dopasowywanie testów do tymczasowych danych nie miało sensu.
  CI/lokalny suite od teraz w pełni zielony (bez `continue-on-error`), baseline "142 passed / 8
  failed" nieaktualny.

---

## Rekomendowana kolejność spłaty

1. Higiena repo + `.gitignore` — **zrobione 2026-06-27**.
2. Rozszerzyć CI o pełny suite testów (tania, duża wartość).
3. Zdecydować o kompozycji `Building` zanim dojdą kolejne typy budynków (drożeje wykładniczo).
4. Ujednolicić serializację, gdy zaczną dochodzić nowe pola/typy zasobów.
5. Determinizm (fixed-point) i snapshoty — później; działają na prototyp.
