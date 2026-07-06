# GRAND REFACTOR — plan wykonawczy

- **Data:** 2026-07-05
- **Źródło wymagań:** `TODO.md` (sekcja GRAND REFACTOR) + audyt repo + `docs/tech_debt.md`
- **Cel:** zredukować dług technologiczny do zera w bazowej części silnika, tak by dodawanie
  nowych ficzerów było tanie i przewidywalne. Projekt ma być wysoce obiektowy, czysty i czytelny.

Ten dokument jest instrukcją krok po kroku dla wykonawcy (model/agent). Wykonuj etapy
**w kolejności**, nie mieszaj etapów, po każdym etapie build + testy muszą być zielone.

---

## 0. Jak pracować z tym planem (przeczytaj przed startem)

1. **Jeden etap na raz.** Nie zaczynaj etapu N+1, dopóki etap N nie spełnia kryteriów akceptacji.
2. **Weryfikacja po każdym kroku:** `.\run_tests.ps1` (pełny suite, Debug). Build produkcyjny:
   `.\build_and_run.ps1` tylko gdy trzeba sprawdzić grę manualnie.
3. **Commit po każdym domkniętym kroku** (nie po każdej edycji pliku). Prefiks wiadomości:
   `REFACTOR(etap-N): <co>`.
4. **Po domknięciu etapu:** odhacz go w sekcji „Postęp" na końcu tego pliku i zaktualizuj
   `docs/tech_debt.md`, jeśli spłaca pozycję długu.
5. **W razie niejasnego kontekstu — pytaj użytkownika.** Nie zgaduj wymagań gameplayowych.
6. **Niczego nie usuwaj „przy okazji".** Usuwanie martwego kodu tylko tam, gdzie plan
   wprost to wskazuje.

### Zasady stylu (obowiązują w KAŻDYM etapie)

- **Pliki `.h` to dokumentacja**: czytelne deklaracje klas z dobrymi komentarzami.
  **Zakaz funkcji inline w nagłówkach** poza: templatami i trywialnymi getterami/setterami.
  Implementacje idą do plików `.cpp`.
- **Single Responsibility Principle**: jeden ficzer = jeden plik. Nie zostawiaj 2–3 ficzerów
  w jednym pliku.
- **Zero redundancji**: zanim napiszesz funkcję pomocniczą, sprawdź czy już istnieje.
- **Determinizm symulacji jest święty**: mutacja stanu gry wyłącznie przez
  `GameCommand → SubmitCommand → ProcessCommands`. W ścieżce symulacji `std::map`
  (uporządkowana iteracja), nigdy `unordered_map`. Remisy w algorytmach rozstrzygaj po id.
- Przy każdej zmianie formatu serializacji: bump `WireVersion` / save version.
- Nowy kod od razu pisany zgodnie z enkapsulacją (etap 12) — nie dokładaj nowych pól public
  do klas symulacji.

### Co już jest zrobione — NIE rób tego od nowa

Audyt wykazał, że część punktów z TODO jest już (w całości lub częściowo) zaimplementowana:

| Punkt TODO | Stan | Gdzie |
|---|---|---|
| Budynki na komponentach + bitset sygnatury | **✅ zrobione** | `inc/economy/BuildingComponents.h` — `IBuildingComponent`, `BuildingCapability`, `std::bitset`; `Building` trzyma sloty komponentów |
| Struktura katalogów domenowych (`inc/<domena>/`, `src/<domena>/`) | **✅ zrobione (niezacommitowane!)** | patrz Etap 0.1 |
| Deduplikacja handlerów w `GuiSystem` | **✅ zrobione** | `GuiSystem` ma teraz `actionMap` zamiast 5× wirtualnych handlerów |
| Deterministyczny pathfinding dywizji (droga/teren) | **✅ istnieje, do uogólnienia** | `inc/warfare/MovementPlanner.h` — wchłonąć w Etap 3 |
| Paczki supply (Food/Weapon/Material) + demand-driven packing | **✅ częściowo** | `SupplyPackage.h`, `SupplyPackageComponent`, `SupplyBufferComponent` — brakujące części w Etapie 9 |
| 4 współczynniki dywizji (cohesion/HP/materiel/food) — pola | **✅ pola istnieją** | `SoldierDivision` — brakuje ekspozycji w GUI (Etap 11) |
| SP używa ścieżki hosta MP | **✅ koncepcyjnie** | `LocalSinglePlayerSession : HostGameSession` — ale hierarchia sesji to bałagan (Etap 1) |

---

## Mapa słabych punktów → etapy

Wynik audytu repo (2026-07-05). Liczby linii z bieżącego working tree.

| # | Słaby punkt | Dowód | Etap |
|---|---|---|---|
| 1 | **962 pliki artefaktów builda w gicie** (`build-debug/`, `build-test/` śledzone, `.obj`, `.exe`, `.pdb` w każdym commicie) | `git ls-files \| grep -c "^build"` → 962 | 0 |
| 2 | Reorganizacja katalogów domenowych **niezacommitowana** (92 pliki `D`, nowe drzewo untracked) | `git status` na branchu `war-iteration-3` | 0 |
| 3 | Martwy kod-placeholder | `inc/ui/InputHandler.h` (klasy puste), `assets/data/technologies2.rtsdata` (zweryfikować użycie) | 0 |
| 4 | CI uruchamia tylko `GameCommandTests.*` z 13 suite'ów; brak cache vcpkg | `.github/workflows/windows-release.yml:51` | 0 |
| 5 | **`GameSession.h` = 830 linii z pełnymi implementacjami inline** (LocalhostHostSession ~190 linii, LocalhostClientSession ~280 linii w nagłówku); pogmatwana hierarchia `HostGameSession` / `LocalhostHostSession` / `LocalSinglePlayerSession` / `LocalhostMultiplayerSession` / `ThreadedGameSession` | `inc/core/GameSession.h`; brak `src/**/GameSession.cpp` | 1 |
| 6 | Ręczna serializacja pozycyjna zduplikowana w 5+ miejscach (command / result / frame / snapshot / save) | `GameCommand.h`, `GameSnapshot.h`, `GameWorld.Persistence.cpp` | 2 |
| 7 | Wyszukiwanie tras/odległości rozproszone: `RoadNetwork`/`NavigationMap` (surowce), `MovementPlanner` (dywizje), ad-hoc „nearest" w `BuildingComponents.cpp`, `Controller.cpp` (AI), `ArmyOrder.cpp` | grep `Nearest/closest` | 3 |
| 8 | Input: sztywna tablica akcji sprzężona wyłącznie z `GuiController`; sceny menu nie mają klawiatury | `inc/ui/Input.h` (`InputProcessor::Init` inline z bindingami) | 4 |
| 9 | `Scenes.cpp` = 2571 linii, wszystkie sceny + runtime loopy w jednym pliku | `src/scenes/Scenes.cpp` | 7 |
| 10 | GUI bez generycznego fundamentu: `GuiPanel` szczątkowy, ~25 klas widgetów z powielonym layoutem; `Gui.cpp` = 2434 linii | `inc/ui/Gui.h:455`, `inc/ui/GuiController.h` | 6 |
| 11 | Renderer nie wspiera animacji; `Renderer.h` ma implementacje inline (`TextureAtlas`, konstruktor) | `inc/ui/Renderer.h` | 5 |
| 12 | `inc/economy/Building.h` zawiera **cały kod dywizji wojskowych** (`SoldierDivision` + 5 klas jednostek + koszty rekrutacji) — warfare w pliku ekonomii; `BuildingComponents.cpp` = 2589 linii (wszystkie komponenty w jednym TU) | `inc/economy/Building.h:120-283` | 8 |
| 13 | Brak transportu manpoweru drogą (uzupełnienia frontu); brak cyklicznej absorpcji Food przez budynki cywilne z debuffami | TODO pkt „surowce" | 9 |
| 14 | Player nie ma rejestru budynków strategicznych (iteracja = skan) | `inc/economy/PlayerDataTracker.h` | 10 |
| 15 | Walka nie jest obiektem: stan bitwy rozsmarowany po flagach dywizji (`engaged`, `retreating`, `regroupTimer`), duel bezstanowy per-tick; brak walk wielostronnych/sojuszy | `inc/warfare/UnitStats.h`, `SoldierDivision` | 11 |
| 16 | Brak enkapsulacji: `GameWorld.tilemap`/`playerHandler` public, pola `Building`/`Player` public — UI może zmutować stan poza komendami | `inc/core/GameWorld.h:88` | 12 |
| 17 | Determinizm na float/double; pełne snapshoty mapy przez TCP (12 KB chunki) | `docs/tech_debt.md` | 13 (odroczony) |

---

## ETAP 0 — Higiena repo i stabilizacja punktu wyjścia

**Cel:** czysty punkt startowy — bez artefaktów builda w historii roboczej, z pełnym CI.
Tani etap o dużej wartości; wszystko dalej opiera się na zielonym pełnym suite.

### Kroki

- **0.1 Zamknij wiszącą reorganizację katalogów.** Working tree zawiera niezacommitowany
  przenos `inc/*.h` → `inc/<domena>/` i `src/*.cpp` → `src/<domena>/` (92 delete + nowe
  drzewo). Zbuduj (`.\run_tests.ps1`), upewnij się że wszystko przechodzi, i scommituj
  reorganizację **jako osobny commit** zanim ruszysz cokolwiek innego.
- **0.2 Wyrzuć artefakty builda z gita.** Dopisz do `.gitignore`: `build-debug/`, `build-test/`
  (katalog `build/` już jest). Następnie `git rm -r --cached build-debug build-test` i commit.
  Niczego nie kasuj z dysku — tylko z indeksu.
- **0.3 Usuń martwy kod:** `inc/ui/InputHandler.h` (puste placeholdery `InputHandler`,
  `LocalInputHandler`, `AiInputHandler`, `NetworkInputHandler` — zastąpi je Etap 4).
  Sprawdź, czy `assets/data/technologies2.rtsdata` jest gdziekolwiek ładowany
  (grep po nazwie); jeśli nie — usuń.
- **0.4 CI:** w `.github/workflows/windows-release.yml` zastąp
  `--gtest_filter=GameCommandTests.*` uruchomieniem pełnego suite'u. Dodaj cache vcpkg
  (`actions/cache` na katalogu vcpkg). Jeśli któryś test nie przechodzi w CI (a lokalnie tak),
  zgłoś to użytkownikowi zamiast wyłączać test.

### Kryteria akceptacji

- `git status` nie pokazuje żadnych plików z `build-*` po zwykłym buildzie.
- CI uruchamia wszystkie testy i jest zielone.
- `InputHandler.h` nie istnieje, projekt się kompiluje.

---

## ETAP 1 — GameSession: multiplayer jako domyślna logika symulacji

**Cel (TODO):** gra pracuje w trybach **MultiplayerHost** (hostujemy — także grając „single")
i **MultiplayerJoin** (dołączamy po TCP/Steam/itd.). SinglePlayer to nakładka na
MultiplayerHost, która nie odpala warstwy transportowej dla dodatkowych graczy. Silnik logiki
działa na osobnym wątku i „podpina" do siebie graczy: lokalny gracz to standard, AI jest
symulowane przez serwer (hosta), gracze zdalni dochodzą przez transport.

### Problem dzisiaj

`inc/core/GameSession.h` (830 linii) zawiera pełne implementacje inline i pięć nakładających
się klas sesji: `HostGameSession`, `LocalSinglePlayerSession`, `LocalhostHostSession`,
`LocalhostMultiplayerSession`, `LocalhostClientSession`, `ThreadedGameSession` (dekorator
wątku). W `Scenes.cpp` do tego dochodzą polimorficzne `IGameRuntimeLoop` /
`SinglePlayerLoop` / `MultiplayerLoop`. Za dużo bytów na dwa rzeczywiste tryby pracy.

### Docelowy kształt

```
IGameSession (interfejs, czysty .h)
  ├─ HostSession      ← autorytatywna symulacja; ZAWSZE na własnym wątku;
  │                     lista podpiętych graczy: LocalPlayer | AIPlayer | RemotePlayer(transport)
  │                     tryb SinglePlayer = HostSession bez otwartego transportu sieciowego
  └─ ClientSession    ← mirror stanu po transporcie + resync snapshotem (MultiplayerJoin)

IGameTransport (bez zmian koncepcyjnych: Localhost | Tcp)
```

### Kroki

- **1.1** Utwórz `src/core/GameSession.cpp` i przenieś tam WSZYSTKIE implementacje
  z nagłówka (mechanicznie, bez zmian logiki). Nagłówek ma zostać czystą deklaracją
  z komentarzami. Testy green → commit.
- **1.2** Scal `HostGameSession` + `LocalhostHostSession` + `LocalhostMultiplayerSession`
  + `LocalSinglePlayerSession` w jedną klasę `HostSession` z listą podpiętych graczy.
  Wchłoń `ThreadedGameSession` — wątek symulacji to wewnętrzna sprawa `HostSession`
  (konstruktor startuje wątek, destruktor joinuje). Zachowaj: fixed-tick 100 Hz,
  checksum co 1 s, resync snapshotem, `FixedSimulationClock` bez zmian.
- **1.3** Przemianuj/uprość `LocalhostClientSession` → `ClientSession` (transport jako
  parametr: localhost lub TCP — już jest abstrakcją, więc głównie rename + przenosiny do .cpp).
- **1.4** Uprość `IGameRuntimeLoop` w `Scenes`: skoro SP == host bez transportu, powinien
  zostać jeden loop dla hosta i jeden dla klienta (albo jeden wspólny, jeśli różnice znikną).
- **1.5** Zaktualizuj `CLAUDE.md` (sekcja Architektura) i `docs/tech_debt.md`.

### Kryteria akceptacji

- `inc/core/GameSession.h` — tylko deklaracje + komentarze (dopuszczalne trywialne gettery).
- Działa: nowa gra SP, host+join po localhost, host+join po TCP (manualny test),
  save/load, resync po desync (wymuś debugowo, jeśli jest taka ścieżka).
- Pełny suite testów green. Liczba klas sesji: 2 (+ interfejs).

---

## ETAP 2 — Jedna warstwa serializacji

**Cel:** jedno miejsce definiuje pola i ich kolejność; save, wire i snapshot dzielą definicję.

### Problem dzisiaj

Pozycyjny `stream <<`/`>>` z ręcznym wersjonowaniem powielony w: `GameCommand`,
`GameCommandResult`, `GameServerFrame`, `GameSnapshot`, `GameWorld.Persistence.cpp`.
Zmiana kolejności pola psuje jednocześnie wire i save; łatwo o rozjazd.

### Kroki

- **2.1** Zaprojektuj `inc/core/Serialization.h` + `src/core/Serialization.cpp`:
  wzorzec **jednej funkcji `Serialize(Archive&, T&)` na typ**, gdzie `Archive` ma dwa tryby
  (Writer/Reader) — pola wymienione RAZ, działa w obie strony. Archive niesie `version`
  (odczytany z nagłówka strumienia), żeby `Serialize` mogło warunkowo czytać stare formaty.
  Format pozostaje tekstowy (kompatybilność z `RTS_SAVE`), binarny NIE jest celem tego etapu.
- **2.2** Migruj po jednym typie na commit, w kolejności: `GameCommand` →
  `GameCommandResult` → `GameServerFrame` → `GameSnapshot` → zawartość
  `GameWorld.Persistence.cpp`. Po każdej migracji: round-trip test (serialize → deserialize
  → porównanie) w `tests/`.
- **2.3** Ujednolić stałe wersji: jedna tabela wersji formatów w `Serialization.h`
  z komentarzem kiedy bumpować.
- **2.4** Dodaj test regresyjny: wczytanie przykładowego save'a w starym formacie
  (zapisz fixture w `tests/data/`), żeby migracje nie łamały starych zapisów — jeśli
  wsteczna kompatybilność jest wymagana; **zapytaj użytkownika, czy stare save'y mają się
  wczytywać** (jeśli nie: po prostu bump save version i czytelny błąd przy starym pliku).

### Kryteria akceptacji

- Żaden typ nie ma osobnych, ręcznie zdublowanych ścieżek zapisu i odczytu.
- Round-trip testy dla wszystkich migrowanych typów w suite.
- Save/load i MP (localhost) działają.

---

## ETAP 3 — Wspólna baza obliczeń przestrzennych (`PathingService`)

**Cel (doprecyzowany przez użytkownika):** DWIE funkcjonalności zebrane w jednej wspólnej
bazie odpowiedzialnej za tego typu obliczenia:

1. **Ścieżki po drogach** — wyliczenie path jako danych: lista kafelków (id), ich liczba,
   koszt/czas przejścia itp.
2. **Odległość w linii prostej** — wyznaczenie dystansu między dwoma punktami/kaflami
   (bez grafu, tanie zapytanie).

Te algorytmy mają różne zastosowania, ale mają mieszkać w JEDNYM serwisie, który odpytują:

- **AIController** — poniekąd jako telemetria: logistyka, wojskowość, śledzenie pozycji
  wrogów na mapie, szacowanie ryzyka itd.,
- **logistic network gracza** — trasy surowców, najbliższy magazyn dla producenta,
- **dywizje na mapie** — marsz, najbliższy garnizon/supply,
- komponenty budynków i GUI (podglądy tras).

**Agregacja jest kluczowa:** jedna instancja serwisu **per `GameWorld`** (własność
`GameWorld`, trzyma referencje do `TileMap`/`RoadNetwork`), przekazywana konsumentom przez
referencję. Żadnych singletonów/globali — dwie równoległe symulacje (host + testy) muszą
mieć niezależne instancje.

### Kroki

- **3.1** Zaprojektuj `inc/simulation/PathingService.h` + `src/simulation/PathingService.cpp`.
  Publiczne API (nazwy przykładowe — utrzymaj konwencję repo):
  - `RoadPath FindRoadPath(Vec2i from, Vec2i to, const PathOptions&)` — deterministyczny
    Dijkstra/A* po grafie dróg; `RoadPath` = { `std::vector<int> tiles`, długość, łączny
    koszt/czas }; pusta = brak trasy,
  - `FieldPath FindFieldPath(...)` — marsz mieszany droga/teren (obecna logika
    `MovementPlanner`, z `blockedTiles`),
  - `double Distance(Vec2f a, Vec2f b)` / `int TileDistance(Vec2i a, Vec2i b)` — linia
    prosta, bez grafu,
  - `FindNearest(from, predicate, domain)` — najbliższy kafelek/budynek spełniający predykat
    („magazyn przyjmujący PLANKS", „garnizon gracza X"), z filtrem dziedziny:
    `Domain::Global()`, `Domain::Territory(playerId)`, `Domain::TerritoryUnion({ids})`.
  - Remisy zawsze po id kafelka (determinizm lockstep — wzorzec już jest w `MovementPlanner`).
- **3.2** Agregacja: `GameWorld` tworzy i posiada `PathingService`; wywołujący dostają
  referencję (AIController przez swój dostęp do świata, komponenty budynków przez
  właściciela/parametr Update — wybierz najmniej inwazyjną ścieżkę przekazania, ale nie
  przez globala).
- **3.3** Wchłoń `MovementPlanner` (`PlanDivisionPath` staje się cienkim wrapperem albo
  znika — wywołujących przepnij na `PathingService::FindFieldPath`).
- **3.4** Przepnij trasowanie surowców: `RoadNetwork`/`NavigationMap` ma używać
  `FindRoadPath` jako silnika. Publiczne API `RoadNetwork` (rezerwacje pojemności,
  `BeginTransport`) zostaje — wymieniasz tylko szukanie trasy.
- **3.5** Przepnij wyszukiwania „najbliższy X" i liczenie dystansów: `BuildingComponents.cpp`
  (najbliższy magazyn), logika garnizonów/supply w `warfare`, dystanse i ocena zagrożeń w AI
  (`Controller.cpp`). Usuń lokalne kopie tych obliczeń.
- **3.6** Testy: `tests/PathingServiceTests.cpp` — trasa po drodze (lista tile + koszt),
  marsz mieszany vs na przełaj, dystans w linii prostej, filtr terytorium,
  nearest-z-predykatem, determinizm (dwa uruchomienia → identyczny wynik), niedostępny cel,
  niezależność dwóch instancji serwisu.

### Kryteria akceptacji

- Grep po repo nie znajduje drugiej implementacji Dijkstry/BFS/A* ani ręcznych pętli
  „najbliższy budynek po dystansie" poza `PathingService` (poza generatorem mapy — nie ruszać).
- `PathingService` nie ma stanu globalnego; instancja żyje w `GameWorld`.
- `RoadNetworkTests`, `WarSystemTests` green bez zmiany oczekiwań.
- MP localhost bez desync (checksum stabilny przez ~2 min gry z transportem i marszem).

---

## ETAP 4 — Globalny InputManager (observer pattern)

**Cel (doprecyzowany przez użytkownika):** subskrypcyjny input. `InputEventSubscriber` to
**template struct** `<InputType T, InputKey K>` będący **opakowaniem callbacku**, agregowanym
jako składowa obiektu, który chce reagować na input. Wzorcowy przykład:

```cpp
// GuiPanel można zamknąć ESC. Panel ma metodę Close() oraz zagregowanego subskrybenta:
class GuiPanel : public UiWidget
{
    void Close();

    // opakowanie metody Close() tego panelu:
    InputEventSubscriber<InputType::KeyPressed, KEY_ESCAPE> escClose{
        [this](const InputEvent&) { Close(); }};
};
// Po naciśnięciu ESC InputManager wywołuje trigger subskrybenta, a ten calluje Close().
```

`InputManager` w każdej klatce wykrywa aktywne inputy, iteruje po zarejestrowanych
subskrybentach i wywołuje `subscriber()` u tych, którzy pasują typem i klawiszem.

### Kroki

- **4.1** `inc/ui/InputManager.h` + `src/ui/InputManager.cpp`:
  - `enum class InputType { KeyPressed, KeyReleased, KeyDown, MouseButtonPressed,
    MouseButtonReleased, MouseButtonDown, MouseScroll, MouseMove }` (dopasuj do realnych potrzeb
    obecnego kodu — sprawdź wszystkie użycia `IsKeyPressed`/`IsMouseButton*` w `src/ui/`),
  - `template<InputType T, int K> struct InputEventSubscriber` — opakowanie callbacku:
    przechowuje `std::function<void(const InputEvent&)>` ustawianą z zewnątrz (konstruktor
    lub setter), `operator()` ją wywołuje. `InputEvent` niesie pozycję myszy/scroll delta.
    Subskrybent **rejestruje się w `InputManager` przy konstrukcji i wyrejestrowuje
    w destruktorze** (RAII) — dzięki temu agregacja jako pole klasy (jak `escClose` wyżej)
    sama zarządza cyklem życia subskrypcji; obiekt niszczy się → subskrypcja znika.
  - `InputManager`: rejestr subskrybentów (nieszablonowy interfejs bazowy, np.
    `IInputSubscriber` z `Matches(type,key)` i `Trigger(event)`), `Poll()` raz na klatkę
    (jedyne miejsce w grze wołające raylibowe `IsKey*`/`IsMouse*`), iteracja i dispatch.
    **Kolejność dispatch deterministyczna** (kolejność rejestracji) + mechanizm konsumpcji
    eventu (GUI nad mapą zjada klik, panel na wierzchu zjada ESC).
- **4.2** Bindingi klawiszy jako dane (mapa akcja→klawisz, jak obecne `actionInputs`),
  nie zaszyte w typach — typy szablonowe używaj dla fizycznych inputów, a mapowanie
  logicznych akcji trzymaj w jednym miejscu (przyszły rebinding).
- **4.3** Przepnij `GuiController`/`InputProcessor`: `HandleInputs` znika, systemy GUI
  subskrybują to, czego potrzebują. Usuń `inc/ui/Input.h` po przepięciu wszystkich akcji
  (`CLOSE_TOP_GUI`, `OPEN_*`, `CENTER_CAMERA...`, `DEBUG_GRANT_RESOURCES`, przyciski myszy,
  scroll — pełna lista w obecnym `Input.h`).
- **4.4** Test manualny każdego skrótu z `ControlsScene` + scroll/zoom/drag kamery.

### Kryteria akceptacji

- Jedyne wywołania raylibowego API inputu są w `InputManager::Poll()`
  (grep: `IsKeyPressed|IsKeyDown|IsMouseButton|GetMouseWheel` → tylko InputManager).
- Wszystkie dotychczasowe skróty działają. GUI nadal przechwytuje mysz nad panelami
  (kliknięcie w panel nie klika w mapę).

---

## ETAP 5 — Renderer z obsługą animacji

**Cel (TODO):** animowane budynki, drogi i elementy mapy. Całe API renderu oraz
tekstury/atlasy dostosowane do wczytywania animacji o zadanej długości;
**długość == 1 oznacza render statycznej teksturki** (pełna kompatybilność wsteczna).

Kolejność w bloku UI (ustalona przez użytkownika): **najpierw Renderer (ten etap),
potem GuiPanel (Etap 6), na końcu GuiController/sceny (Etap 7)** — fundament renderu
musi być gotowy, zanim GUI zacznie na nim budować.

### Kroki

- **5.1** Najpierw porządek: przenieś implementacje inline z `inc/ui/Renderer.h`
  (`TextureAtlas::LoadTextureAtlas`, `GetRectFromId`, konstruktor `Renderer`) do
  `src/ui/Renderer.cpp`.
- **5.2** Rozszerz `TextureAtlas` o pojęcie animacji: definicja klipu = {startowy id kafelka,
  liczba klatek, czas klatki}. `frames == 1` → statyczny. Klipy definiowane w danych
  (np. sekcja w `.rtsdata` albo osobny plik `assets/data/animations.rtsdata` — spójnie
  z resztą data-driven configu budynków).
- **5.3** API renderu: `DrawAtlasTile`/`DrawBuildingTexture` dostają wariant przyjmujący
  stan animacji (czas życia obiektu — `Building::lifetime` już istnieje i się tyka).
  Wybór klatki = `(elapsed / frameTime) % frames` liczony z czasu RENDERU (animacja jest
  kosmetyczna — NIE wpływa na symulację ani checksum).
- **5.4** Podepnij animację przynajmniej do jednego budynku end-to-end (np. Windmill),
  żeby ścieżka była udowodniona; reszta assetów to praca contentowa, nie silnikowa.

### Kryteria akceptacji

- Wszystkie dotychczasowe statyczne tekstury renderują się bez żadnych zmian w danych.
- Jeden budynek animuje się w grze; FPS bez regresu (porównaj licznik przed/po).
- Zero wpływu na checksum symulacji (desync test w MP localhost).

---

## ETAP 6 — Fundament GUI: GuiPanel jako podstawowa jednostka

**Cel (TODO):** GuiWidgety generycznie współpracujące z Rendererem; podstawową jednostką
renderowaną jest `GuiPanel`, po którym dziedziczy każdy custom panel. `GuiPanel` obsługuje:
scrollowanie kontentu, przechwytywanie inputu, niezależną kamerę/viewport wewnątrz,
oraz częste elementy: title bar z nazwą, przycisk X do zamknięcia. Obecny system jest
zbloatowany przez brak takiego fundamentu.

### Kroki

- **6.1** Rozbuduj `GuiPanel` (obecnie `inc/ui/Gui.h:455`, szczątkowy):
  - opcjonalny title bar + tytuł + przycisk X (callback `onClose`); zamykanie klawiszem ESC
    przez zagregowanego subskrybenta z Etapu 4:
    `InputEventSubscriber<InputType::KeyPressed, KEY_ESCAPE> escClose{[this](auto&){ Close(); }}`
    — to wzorcowe użycie nowego inputu, panel na wierzchu konsumuje event,
  - clipping i scroll kontentu (scissor rect + offset; scroll z `InputManager` gdy kursor
    nad panelem),
  - przechwytywanie inputu: panel rejestruje swój prostokąt, kliknięcia w obszar panelu nie
    przechodzą do mapy (spina się z mechanizmem konsumpcji z Etapu 4),
  - niezależna kamera wewnętrzna (viewport) — użyj tego samego mechanizmu, którym
    `ResearchTreePanelWidget` przesuwa drzewko (przenieś ten kod DO `GuiPanel`).
- **6.2** Migruj istniejące panele na dziedziczenie z `GuiPanel`, po jednym na commit:
  `BuildingInfoPanel`, `ResearchPanel`/`ResearchTreePanelWidget`, `StatsPanelWidget`,
  `BuildPanelWidget`, `ArmyOrderPanelWidget`, `MilitaryDivisionBarWidget`/`ArmyBarWidget`
  (jeśli są panelami, nie overlayami — overlaye mapy zostają `UiWidget`).
  Usuwaj przy tym zduplikowany kod ramek/tła/tytułów.
- **6.3** Rozbij `src/ui/Gui.cpp` (2434 linii): `GuiWidgets.cpp` (UiButton, CheckBox, Slider,
  VBox, HBox, TextBox, Label, Dropdown, ProgressBar, Image), `GuiPanel.cpp`, `GuiText.cpp`
  (UiText/Tooltip/atlas ikon). Nagłówek `Gui.h` analogicznie, jeśli urósł.
- **6.4** Przejrzyj `inc/ui/GuiController.h` — widgety map-overlay (~15 klas) przenieś
  do dedykowanego nagłówka `inc/ui/GuiMapWidgets.h` (deklaracje tam, gdzie implementacje już
  są: `src/ui/GuiMapWidgets.cpp`).

### Kryteria akceptacji

- Każdy panel okienkowy dziedziczy z `GuiPanel`; ramka/tytuł/X wyglądają i zachowują się
  spójnie we wszystkich panelach.
- Scroll działa w panelu badań i w panelu budowy (długie listy).
- Brak regresu wizualnego w podstawowym przebiegu gry (manualny smoke test).

---

## ETAP 7 — Sceny: IGuiController i input per scena, podział Scenes.cpp

**Cel (TODO):** każda scena posiada swój renderer oraz gui controller
(np. `MainMenuController : IGuiController`) zawierający komponenty GUI, subskrybentów inputu
i całą logikę prezentacji (animowane panele, progress bary). Każda scena ma niezależny
input, co daje obsługę klawiatury wszędzie (menu, lobby, opcje).

Wykonywany PO fundamencie GuiPanel (Etap 6) — kontrolery scen budują już na nowych panelach,
więc nie migrujesz ich dwa razy.

### Kroki

- **7.1** Wydziel interfejs `IGuiController` (Update, budowa widgetów, dostęp do listy
  widgetów do renderu). Obecny `GuiController` (gry) zostaje jego najbogatszą implementacją —
  nie przepisuj jego logiki, tylko dopasuj do interfejsu.
- **7.2** `Scene` dostaje: własny `Renderer` (lub referencję do współdzielonego — **zapytaj
  użytkownika**, czy chce fizycznie osobne renderery, czy jeden współdzielony z per-scenowym
  stanem kamery; rekomendacja: jeden `Renderer`, per-scenowa kamera i warstwy), własny
  `IGuiController`, własne subskrypcje `InputManager` — subskrybenci jako pola kontrolera
  sceny (RAII z Etapu 4), aktywowane w `OnActivated()` / zwalniane przy dezaktywacji.
- **7.3** Rozbij `src/scenes/Scenes.cpp` (2571 linii) na pliki per scena:
  `MainMenuScene.cpp`, `OptionsScene.cpp`, `NewGameScene.cpp`, `MultiplayerScene.cpp`,
  `LoadGameScene.cpp`, `SaveGameScene.cpp`, `GameScene.cpp`, `ControlsScene.cpp`,
  `GameMenuScene.cpp` (+ `RuntimeLoops.cpp` jeśli po Etapie 1 coś zostało).
  Analogicznie rozważ podział `inc/scenes/Scenes.h` na nagłówki per scena.
- **7.4** Dodaj nawigację klawiaturą w menu (strzałki/Enter/Escape) jako dowód działania
  inputu per scena — minimalny zakres: MainMenu i GameMenu.

### Kryteria akceptacji

- Żaden plik sceny > ~600 linii; jedna scena = jeden plik.
- Escape/Enter/strzałki działają w MainMenu; wejście i wyjście z gry nie zostawia
  subskrypcji (sprawdź licznik subskrybentów w debugu).

---

## ETAP 8 — Domknięcie refaktoru budynków i wydzielenie dywizji

**Cel (TODO):** budynek to bardzo lekka, prawie pusta klasa; logika = pobierz komponent
i operuj na nim. Kod dywizji wojskowych **nie jest** częścią plików budynku.

### Problem dzisiaj

Kompozycja komponentów już działa, ale: (a) `inc/economy/Building.h` zawiera `SoldierDivision`
+ 5 konkretnych klas jednostek + koszty rekrutacji (~160 linii warfare w pliku ekonomii),
(b) `src/economy/BuildingComponents.cpp` ma 2589 linii — wszystkie komponenty w jednym TU,
(c) `Building` ma szeroką fasadę delegującą (`AddResource`, `SetSupplier`, `HasSupplier`, …).

### Kroki

- **8.1** Przenieś `SoldierDivision`, `MilitiaDivision`…`CavalryDivision`,
  `CreateMilitaryDivision`, `DivisionEquipment`, `MilitaryUnitType`, `MilitaryOrderType`,
  `MilitaryUnitLabel`, `GetBaseRecruitment*` do `inc/warfare/Division.h` +
  `src/warfare/Division.cpp`. Zaktualizuj `warfare/Warfare.h` (hub includów) i wszystkich
  wywołujących. `Building.h` zostaje czysto ekonomiczno-budynkowy.
- **8.2** Rozbij `src/economy/BuildingComponents.cpp` na pliki per komponent (lub per spójna
  grupa): `ProductionComponent.cpp`, `LogisticsComponent.cpp`, `StorageComponent.cpp`,
  `WorkersComponent.cpp`, `TerritoryComponent.cpp`, `GarrisonComponent.cpp`,
  `RecruitmentComponent.cpp`, `PopulationComponent.cpp`, `SupplyBufferComponent.cpp`,
  `SupplyPackageComponent.cpp`. Analogicznie rozważ podział nagłówka
  `BuildingComponents.h` (bazowy interfejs + nagłówki per komponent).
- **8.3** Odchudź fasadę `Building`: przejdź po wywołujących metody fasadowe
  (`HasSupplier`, `GetInputBufferViews`, `SetReceiver`, …) i przepnij ich na
  `GetComponent<T>()` + metodę komponentu. Fasadę zostaw TYLKO tam, gdzie operacja
  koordynuje kilka komponentów naraz (np. `AddResource` trafiające do produkcji LUB magazynu)
  — wtedy fasada ma jasny komentarz, czemu istnieje.
- **8.4** Przejrzyj `Building` pod kątem pól do przeniesienia w komponenty
  (np. `totalProduced` duplikuje pole `ProductionComponent`; `transportables` +
  `transportTime` to kandydat na komponent/pole logistyczne). Każde przeniesienie = commit
  + testy (uwaga na serializację — po Etapie 2 zmiana jest w jednym miejscu).

### Kryteria akceptacji

- `inc/economy/Building.h` < ~300 linii, zero typów warfare.
- Żaden plik komponentów > ~500 linii.
- `BuildingDomainTests`, `BuildingConfigTests`, `BuildingPlacementTests`, `WarSystemTests` green.

---

## ETAP 9 — Taksonomia zasobów i domknięcie systemu supply

**Cel (TODO):** jawny podział zasobów na cztery klasy + brakujące mechaniki supply:

1. **Konkretne** — produkowane przez budynki, składowane w HQ/magazynach, transportowane
   po drogach (`Transportable`). *(istnieje)*
2. **Strategiczne** — globalne wartości: manpower, budowniczowie itd. *(istnieje:
   `Stat<int>` na Player)*
3. **Lokalne** — sprzęt/zapasy gromadzone w budynkach militarnych, rozprowadzane off-map
   do pobliskiego wojska. *(istnieje: `SupplyBufferComponent`)*
4. **Mieszane (paczki)** — amalgamaty przedmiotów tej samej klasy (`ItemClass` /
   `EquipmentCategory`), znające swoją zawartość; po dotarciu do budynku z komponentem
   buforującym zamieniane na supply. Typy: **Food**, **Weapon**, **Material** *(istnieją)*
   + **NOWY: Manpower** — ludzie transportowani drogą jako uzupełnienia frontu.

### Kroki

- **9.1** Udokumentuj taksonomię w kodzie: komentarz-mapa w `inc/data/Resource.h`
  (lub `docs/`), każdy zasób przypisany do klasy. To komentarz porządkujący — bez zmian logiki.
- **9.2** **Paczki manpower:** rozszerz `SupplyPackage` o typ Manpower (skład: N ludzi).
  Źródło: wioski/HQ (pobiera z globalnej puli manpower przez komendę), transport drogą jak
  inne paczki, absorpcja w `SupplyBufferComponent` budynków militarnych, dystrybucja do
  dywizji z niepełnym stanem osobowym (`health`/`maxHealth`, istniejący
  `reinforcementBuffer`). Serializacja: bump wersji (po Etapie 2 — jedno miejsce).
- **9.3** **FoodSupplyBuffer dla budynków cywilnych:** cykliczna absorpcja paczek Food
  (np. raz na minutę — stała w danych balansu, nie w kodzie). Brak dostawy →
  spadek productivity workerów i przyrostu manpower (spina się z istniejącym
  `PopulationComponent::GetWorkerProductivity` / `GetManpowerProductivity`).
  **Zapytaj użytkownika** przed implementacją: czy Village'e mają nadal mieć obecny
  mechanizm `RequestFoodSupply`, czy przechodzą w całości na paczki.
- **9.4** **Telemetria demand:** buffery (Weapon/Material/Food) prowadzą pomiar: co jest
  potrzebne i w jakim tempie znika (średnia krocząca zużycia per kategoria). Żądania paczek
  o konkretnym składzie generowane z deficytów (część istnieje jako `SupplyDemand` —
  rozszerz o tempo zużycia, nie buduj od zera).
- **9.5** Testy: `StrategicResourceTests` + nowe przypadki: paczka manpower end-to-end,
  głodujący budynek cywilny traci produktywność, demand-driven skład paczki.

### Kryteria akceptacji

- Dywizja na froncie z ubytkiem osobowym odzyskuje stan po dostarczeniu paczki manpower.
- Odcięcie dróg do wioski → mierzalny spadek przyrostu manpower.
- Pełny suite green; brak desync w MP (checksum).

---

## ETAP 10 — Rejestr strategicznych budynków w Player

**Cel (TODO):** Player chomikuje najważniejsze strategiczne dane — analogicznie do klas
telemetrii, ma mieć wykaz budynków strategicznych (magazyny, budynki wojskowe, supply huby,
HQ), by tanio po nich iterować i uzyskiwać referencje.

### Kroki

- **10.1** Rozszerz `PlayerDataTracker` (`inc/economy/PlayerDataTracker.h`) o kolekcje:
  `storages`, `militaryBuildings`, `supplyHubs`, `headquarters`, `villages` — aktualizowane
  zdarzeniowo przy budowie/zniszczeniu/przejęciu (te eventy już przechodzą przez tracker —
  dopnij nowe listy do istniejących haczyków). Kolekcje deterministycznie uporządkowane
  (po id budynku).
- **10.2** Przepnij wywołujących, którzy dziś skanują wszystkie budynki/mapę, żeby znaleźć
  budynki danego typu: AI (`Controller.cpp`), logistyka supply (`SupplyPackageComponent` —
  wybór huba/magazynu), GUI (listy budynków). Wyszukiwania przestrzenne („najbliższy z listy")
  łącz z `PathingService` z Etapu 3: rejestr daje kandydatów, PathingService wybiera najbliższego.
- **10.3** Inwariant: rejestr NIE jest serializowany — odbudowywany przy load
  (jak `ConstructionQueue` — wzorzec już jest w repo).

### Kryteria akceptacji

- Grep: brak pętli po wszystkich budynkach gracza filtrującej po `buildingType` w AI
  i logistyce (poza samym trackerem).
- Save → load → rejestr identyczny (test jednostkowy).

---

## ETAP 11 — Walka jako obiekt pierwszej klasy + ekspozycja 4 współczynników

**Cel (TODO):** walka to osobny obiekt podsumowujący starcie: strony walczące + wsparcia,
śledzony przez telemetrię graczy (z referencją na drugą stronę konfliktu). Wsparcie dla walk
między-sojuszowych (kilka państw po różnych stronach). Koniec walki = wyczerpanie kohezji
którejś ze stron (jak HoI4): atakujący po porażce zostaje na swoim kwadrancie z czasową
blokadą ruchu/ataku; obrońca po porażce wycofuje się na kwadrant w głąb frontu, a atakujący
zajmuje jego kwadrant. Instancje powstają automatycznie, gdy dywizja wchodzi na kwadrant
zajęty przez przeciwnika (niezależnie czyj to teren). Sojusznicy nie walczą — bez zgody
dyplomatycznej nie wejdą na zajęty kwadrant.

### Stan zastany

Duel per-tick istnieje (`ResolveDivisionDuel` w `UnitStats.h`), stan walki rozsmarowany po
flagach `SoldierDivision` (`engaged`, `retreating`, `regroupTimer`). Kwadranty:
`DivisionSector.h` / `sectorCell`. Dyplomacja: `DiplomaticState`. Cohesion/HP/Material/Food —
pola już są.

### Kroki

- **11.1** `inc/warfare/Battle.h` + `src/warfare/Battle.cpp`: klasa `Battle`
  (id, kwadrant, strony: listy id dywizji per strona + id graczy per strona, wsparcia,
  zagregowany postęp, czas trwania). Właścicielem instancji jest `GameWorld` (symulacja,
  deterministycznie — `std::map<int, Battle>`), a `Player` (telemetria) trzyma id swoich
  bitew + referencję/wskaźnik na strony przeciwne.
- **11.2** Cykl życia: tworzenie przy próbie wejścia na wrogi kwadrant (przenieś logikę
  z obecnych flag na dywizjach do `Battle`); dołączanie (kolejne dywizje/sojusznicy
  dochodzą do istniejącej instancji po właściwej stronie); rozstrzyganie per-tick przez
  istniejący `ResolveDivisionDuel` zagregowany po stronach; zakończenie przy wyczerpaniu
  kohezji strony → skutki jak w celu (blokada atakującego / wymuszony odwrót obrońcy —
  kwadrant odwrotu wybierz `PathingService` z filtrem „w głąb własnego frontu").
  Flagi `engaged`/`retreating`/`regroupTimer` na dywizji zamień na stan pochodny z `Battle`
  (lub zostaw jako cache aktualizowany wyłącznie przez `Battle`).
- **11.3** Sojusze: strony `Battle` grupowane po stronach konfliktu, nie per gracz;
  wejście na kwadrant sojusznika bez military access → ruch zablokowany (bez walki).
  Sprawdzenie przez `DiplomaticState`.
- **11.4** GUI: w panelu armii i tooltipie dywizji **pierwsze w kolejności** cztery
  współczynniki, z kolorami: **Cohesion** (zdolność bojowa), **HP** (current/max —
  ile supply+manpoweru trzeba dostarczyć), **Material** (100% = max bonus atak/obrona),
  **Food** (100% = brak debuffów). Kolorystyka progowa (np. zielony >66%, żółty 33–66%,
  czerwony <33%) — stałe w jednym miejscu. Dodaj widget bitwy (istnieje `FieldBattleMarker` —
  podepnij pod `Battle` zamiast obecnych flag).
- **11.5** Serializacja `Battle` (save + snapshot MP) — bump wersji. Testy:
  rozszerz `WarSystemTests` o: auto-utworzenie bitwy, dołączenie sojusznika, wygrana
  atakującego (zajęcie kwadrantu), przegrana atakującego (blokada), odwrót obrońcy w głąb,
  blokada wejścia na kwadrant sojusznika bez access.

### Kryteria akceptacji

- Cały stan starcia żyje w `Battle`; dywizja nie ma pól opisujących cudzy stan walki.
- Scenariusz 2v1 (dwóch sojuszników vs jeden) rozgrywa się w JEDNEJ instancji `Battle`.
- Determinizm: MP localhost bez desync podczas bitwy; pełny suite green.

---

## ETAP 12 — Enkapsulacja stanu symulacji

**Cel:** UI i warstwy zewnętrzne nie mogą zmutować stanu symulacji poza ścieżką komend.

### Kroki

- **12.1** `GameWorld`: `tilemap`, `playerHandler` → private; publicznie `const` accessory
  dla odczytu (UI/renderer) i kontrolowane, nie-const wejście wyłącznie dla ścieżki komend
  i persystencji (friend lub wąski interfejs wewnętrzny `GameWorldInternal.h` — już istnieje
  jako koncept).
- **12.2** `Building`: pola publiczne (`owner`, `placement`, `id`, `constructionRemaining`, …)
  → private/protected z getterami; settery tylko tam, gdzie realnie mutuje symulacja.
  Rób mechanicznie: jedno pole na raz, kompilator wskazuje wywołujących.
- **12.3** `Player` (`inc/economy/Player.h`): to samo. Przy okazji oceń, czy po Etapach 9–10
  coś z Playera nie powinno wyjechać do już istniejących agregatów (telemetria, tracker,
  registry) — Player ma być koordynatorem, nie workiem na pola.
- **12.4** Audyt końcowy: grep w `src/ui/` i `src/scenes/` po bezpośrednich zapisach do
  obiektów symulacji — wynik ma być pusty (wyjątek: stan czysto prezentacyjny).

### Kryteria akceptacji

- Kod UI kompiluje się wyłącznie z `const` widokiem na symulację.
- Pełny suite green; MP bez desync.

---

## ETAP 13 (ODROCZONY — nie wykonuj bez decyzji użytkownika)

Pozycje świadomie zostawione na później (działają na prototyp, koszt wysoki):

- **Fixed-point dla wielkości symulacyjnych** zamiast float/double (ryzyko desync
  cross-platform). Duża, inwazyjna zmiana — dopiero gdy pojawi się realny target
  innej platformy/kompilatora.
- **Skalowalne snapshoty MP** (delta/kompresja zamiast pełnej mapy w chunkach 12 KB) —
  dopiero gdy mapy urosną lub join-in-progress zacznie boleć.
- **Build Linux/macOS w CI.**

---

## Postęp

- [ ] Etap 0 — higiena repo, pełne CI
- [x] Etap 1 — GameSession: HostSession/ClientSession, implementacje w .cpp (2026-07-05: 1.2, 1.3, 1.4, 1.5 skończone)
- [x] Etap 2 — jedna warstwa serializacji (2026-07-05: Archive pattern, GameCommand/Result/ServerFrame/Snapshot zmigrowane)
- [x] Etap 3 — wspólna baza obliczeń przestrzennych (PathingService) (2026-07-05: 3.1-3.6 skończone, testy w PathingServiceTests.cpp)
- [~] Etap 4 — InputManager (observer) — 4.1 zrobione i poprawione (2026-07-05: naprawiono bug RAII — singleton + raw-pointer subscribers zamiast kopiującego shared_ptr), 4.2 zrobione (KeyBindingMap). 4.3 częściowo (2026-07-05, commit 759257e): KAŻDE bezpośrednie wywołanie raylib input API (IsKeyPressed/IsKeyDown/IsKeyReleased/IsMouseButtonPressed/IsMouseButtonDown/IsMouseButtonReleased/GetMouseWheelMove/IsKeyPressedRepeat) w całym repo zmigrowane na statyczne wrappery `InputManager::*` — zweryfikowane grepem: zero wystąpień poza InputManager.h/.cpp. To spełnia dosłownie kryterium akceptacji "jedyne wywołania raylibowego API inputu są w InputManager".
  - 2026-07-05 (kontynuacja): naprawiono zgłoszony przez użytkownika brak "context safety" — `GuiSystem` ma teraz `OnActivate()/OnDeactivate()` wołane z `ChangeSystem()`; `BasicMapViewSystem::OnDeactivate()` czyści wybór budynku; `GuiPanel::escClose` dodatkowo strzeże się `HasBuilding()`. Patrz commit a691513.
  - NIE zrobione: pełne zastąpienie `InputProcessor`/`actionMap`/`Input.h` przez subskrybentów per-widget dla WSZYSTKICH nazwanych akcji (q/r/d/e/s/f/t/space itd.) — `actionMap` sam w sobie jest już poprawnie kontekstowy (patrzy tylko w mapę aktywnego systemu), więc **nie jest to pilne** — nowy `OnActivate/OnDeactivate` daje bezpieczny fundament, gdyby ktoś chciał to zrobić później. 4.4 (test manualny wszystkich skrótów) nie do zrobienia bez uruchomienia okna gry.
- [x] Etap 5 — renderer z animacjami (2026-07-05: 5.1-5.4 skończone; 5.4 to plumbing + testy ResolveAnimationFrame, bez realnego assetu Windmill — patrz commit a899d6f)
- [~] Etap 6 — fundament GuiPanel, podział Gui.cpp — 6.1 częściowo (2026-07-05: Close()/escClose/BeginContentClip/EndContentClip dodane; GuiPanel::DrawChrome wydzielone z 608-liniowego Update() czystym Extract Method — patrz commity 2598e6c, 1d710e3). NIE zrobione: niezależna kamera/viewport w GuiPanel (kod pan/zoom żyje w ResearchTreePanelWidget, osobnej klasie `: public UiWidget`, NIE `: public GuiPanel` — migracja wymaga zmiany bazy klasy + przeniesienia panOffset/zoom/panning + rozplecenia jej też-bardzo-długiego Update() na chrome/content, analogicznie do tego co zrobiono dla GuiPanel). 6.2 (migracja ResearchPanel/StatsPanelWidget/BuildPanelWidget/ArmyOrderPanelWidget/MilitaryDivisionBarWidget na GuiPanel), 6.3 (podział Gui.cpp), 6.4 (GuiMapWidgets.h) NIE zrobione.
  - **Dlaczego przerwano tutaj:** zero pokrycia testami automatycznymi dla renderowania GUI (raylib wymaga okna, nie da się tego odpalić w tym środowisku). Ekstrakcja DrawChrome była bezpieczna (czysta zmiana miejsca kodu, identyczna arytmetyka, zweryfikowana ręcznie). Migracja ResearchTreePanelWidget to inny kaliber: zmiana klasy bazowej + przeniesienie stanu + rozbiórka drugiej ~500-liniowej funkcji z logiką layoutu drzewka badań. Zalecane: zrobić to pojedynczo, jeden panel na commit (jak mówi plan), z ręcznym smoke-testem w grze po każdym — czego nie mogę zrobić bez uruchomienia okna.
- [~] Etap 7 — sceny: IGuiController + input per scena, podział Scenes.cpp — 7.3 zrobione (2026-07-05, commit 20e618e): 2560-liniowy Scenes.cpp rozbity mechanicznie na 10 plików (jedna scena = jeden plik, + SceneUtils.h/cpp dla dzielonych helperów), zweryfikowane pełnym rebuildem obu targetów (`game`, `rts`) + suite testów (176/184, ten sam baseline). Wszystkie pliki < 600 linii poza MultiplayerScene.cpp (1027 linii — jeden spójny zestaw odpowiedzialności: formularz+lobby+chat+config, warto dalej dzielić ale odłożone). 7.1 (IGuiController), 7.2 (per-scenowy input przez InputManager), 7.4 (nawigacja klawiaturą w menu) NIE zrobione — zależą od dokończenia fundamentu GuiPanel z Etapu 6.
- [~] Etap 8 — Building: wydzielenie dywizji, podział komponentów, odchudzenie fasady — 8.1 zrobione (2026-07-05, commit d5ddbd0): SoldierDivision + 5 klas jednostek + MilitaryUnitType/MilitaryOrderType/GetBaseRecruitment* przeniesione do inc/warfare/Division.h + src/warfare/Division.cpp; Building.h forwarduje include (zero zmian u wywołujących); Building.h 617→415 linii, zero typów warfare. 8.2 zrobione (commit eb368d9): 2589-liniowy BuildingComponents.cpp rozbity mechanicznie (sed, byte-exact) na 10 plików per komponent + BuildingComponentsInternal.h/cpp (CountIncomingResources/GetReceiveCapacity dzielone między Logistics/Storage/Population). Wszystkie < 500 linii poza GarrisonComponent.cpp (740) i SupplyPackageComponent.cpp (724) — nadal wielka poprawa z 2589, dalszy podział możliwy później.
  - NIE zrobione: 8.3 (odchudzenie fasady Building — HasSupplier/GetInputBufferViews/SetReceiver itd. przez GetComponent<T>()) i 8.4 (audyt pól Building pod kątem duplikacji z komponentami). Oba wymagają śledzenia dziesiątek miejsc wywołań w całym repo per metoda fasady — wyższe ryzyko subtelnej zmiany zachowania koordynacji między komponentami niż czysto mechaniczny przenoszenie kodu zrobione w 8.1/8.2. Building.h wciąż 415 linii (cel z planu: ~300) — reszta to sama hierarchia Building/ProductionBuilding/StorageBuilding/MilitaryBuilding, nie warfare.
- [ ] Etap 9 — taksonomia zasobów + paczki manpower + food dla cywilów
- [ ] Etap 10 — rejestr budynków strategicznych w Player
- [ ] Etap 11 — Battle jako obiekt + 4 współczynniki w GUI
- [ ] Etap 12 — enkapsulacja stanu symulacji
- [ ] Etap 13 — ODROCZONY (decyzja użytkownika)
