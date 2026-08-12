# tech-tree-editor

Edytor drzewka badań (`assets/data/technologies.rtsdata` i `assets/data/focuses.rtsdata`),
renderowany tak samo jak panel w grze. Podgląd + edycja nodów + eksport + sumator bonusów
w stylu Path of Building.

## Uruchomienie

```bash
tools/tech-tree-editor/build_and_run.bat
```

Wykrywanie raylib identyczne jak w głównym `build_and_run.ps1`: `$env:RAYLIB_ROOT`,
potem `deps/raylib`, potem `../work/local/raylib`. `-NoRun` buduje bez uruchamiania,
`-Config Debug` zmienia konfigurację.

Tool **nie jest** wpięty w główny `CMakeLists.txt` — konfiguruje się i buduje osobno,
żeby zepsuty edytor nigdy nie zepsuł buildu gry.

### Self-test serializacji

```bash
tools/tech-tree-editor/build/Release/tech_tree_editor.exe --selftest C:\temp\rt
```

Wczytuje oba prawdziwe pliki, zapisuje je do podanego katalogu i weryfikuje round-trip.
Nie otwiera okna i **nie dotyka `assets/`**. Odpalaj po każdej zmianie w tabelach nazw
w `TreeSerializer.cpp`.

## Sterowanie

| Akcja | Wejście |
|---|---|
| Zaznacz node (inspector + sumator) | LPM na nodzie |
| Zaznacz grupę nodów | `Ctrl` + przeciągnięcie LPM (prostokąt zaznaczenia) |
| **Dodaj node-dziecko** | **PPM na nodzie** — nowy node łapie się myszy |
| Przesuń istniejący node / grupę | LPM przeciągnięcie zaznaczonego node'a (próg ~6 px) |
| Zatwierdź pozycję | LPM |
| Anuluj wstawianie/przesuwanie | ESC lub PPM |
| Usuń zaznaczone nody | `Delete` albo przycisk w inspectorze |
| Cofnij / ponów edycję | `Ctrl+Z` / `Ctrl+Y` |
| Pan | PPM przeciągnięcie po pustym miejscu |
| Zoom | kółko |
| Scroll pionowy | Ctrl + kółko |
| Zapisz | `Ctrl+S` lub przycisk `Save` |
| Przeładuj z dysku | `F5` lub przycisk `Reload` |
| Przełącz drzewko | `Tab` lub przyciski `Technologies`/`Decisions` |

Podczas wstawiania/przesuwania nad kursorem leci plakietka z **koordynatami z pliku**:
`lane | layer N | order M (layout_order X)`. Snap: layer po wierszach, order do 25.

## Koordynaty: layer i order

W pliku pozycja to jedna liczba `layout_order`, która koduje dwie rzeczy:

```
layout_order = layer * 1000 + order        order: 0..999 (pozycja w poziomie)
```

Tak to czyta gra (`preferredDepth` w `GuiResearchTree.cpp`): `layout_order 2300` = layer 2,
order 300. Inspector rozbija to na dwa osobne pola, bo nikt nie myśli w "2300".

Dwie rzeczy, które warto wiedzieć:

- **Prerekwizyt wygrywa z layerem.** Realny layer to `max(twój layer, layer rodzica + 1)`
  — node nie może wylądować nad swoim prerekwizytem. Dlatego przy dodawaniu dziecka
  edytor sam podbija layer.
- **Order to dokładna pozycja.** `order` mapuje się liniowo na szerokość lane:
  0 = lewa krawędź, 999 = prawa. Node z jawnym `layout_order` stoi twardo i **nie
  przesuwa się, gdy obok pojawi się inny node** — rusza się tylko wtedy, gdy sam go
  przeciągniesz. Nody bez `layout_order` w pliku układają się od lewej.

> ### ⚠️ Layout edytora ≠ layout gry
>
> Gra (`GuiResearchTree.cpp`) liczy pozycję inaczej: miesza `layout_order` z pozycją
> rodzica i rozpycha kolizje. Przez to w grze order ledwo rusza nodem, a dodanie
> nowego przesuwa sąsiadów. Edytor ma layout **deterministyczny** (order = pozycja),
> bo bez tego nie da się nic sensownie ustawić.
>
> Skutek: **pozycje nodów w grze nie będą wyglądać tak jak w edytorze.** Dane
> (`layout_order`) są oczywiście te same — różni się tylko interpretacja. Jeśli chcesz
> spójności, trzeba przenieść deterministyczny layout do gry (to ~40 linii w
> `GuiResearchTree.cpp`, ta sama zmiana co tutaj).

## Zapis

`Save` nadpisuje plik w całości z modelu — edytor jest źródłem prawdy, komentarze
i ręczne formatowanie nie są zachowywane (świadoma decyzja projektowa). Przy pierwszym
zapisie powstaje jednorazowy `<plik>.rtsdata.bak`.

Po zapisie edytor **czyta plik z powrotem prawdziwym parserem gry i porównuje** pole po
polu. Jeśli cokolwiek się nie zgadza, dostajesz `Round-trip mismatch: ...` na czerwono
zamiast cichego pliku, który gra czyta inaczej niż edytor pokazywał. Zapis pustego drzewka
jest odrzucany (gra wpadłaby wtedy w wbudowane definicje domyślne).

## Co jest współdzielone z grą, a co skopiowane

**Współdzielone** — kompilowane po ścieżce względnej z `../../src` (`RTS_SHARED_SOURCES`
w `CMakeLists.txt`):

- `src/research/Technology.cpp` — parser bloków `technology`, modyfikatorów, tagów
- `src/data/RtsDataFile.cpp` — tokenizer `.rtsdata`
- `src/data/Resource.cpp`, `src/data/Equipment.cpp` — typy i kategorie zasobów
- `src/ui/UiText.cpp` — metryki tekstu, `UiText`, `Tooltip`, helpery UTF-8
- `src/ui/UiWidgets.cpp` — `DropdownWidget`, `TextFieldWidget`
- `src/economy/BalanceStatDisplay.cpp` — nazwy statów i **kierunek „co jest buffem"**
  (`LowerValueIsBetter`, `IsPositiveModifier`). Współdzielone, bo rozjazd tej jednej
  funkcji renderuje nerf na zielono w jednym miejscu i na czerwono w drugim — co się
  faktycznie stało z `WorkerCapacity` przy trzech kopiach (2026-07-26).

`UiText`/`UiWidgets` zostały **wyciągnięte z `Gui.cpp` do osobnych TU** właśnie po to,
żeby tool renderował kodem gry, a nie ręcznie zsynchronizowaną kopią. Dropdown i text
field są dostępne również dla GUI gry (`inc/ui/UiWidgets.h`).

**Skopiowane** — `src/TreeView.cpp` (layout i draw z `src/ui/GuiResearchTree.cpp`).
Kopia, bo w grze render jest wpięty w `Player`/`GameScene`, a edytor go rozszerza
(zaznaczanie, przeciąganie, ghost przy wstawianiu).

**Zamiast `Player`** — `TreeModel.cpp` odtwarza `ResearchCatalog::BuildView`, ale stan
node'a bierze z edytorskiego zbioru "taken".

### Dwa kroje pisma

Drzewko rysuje się fontem gry (`MarcellusSC`) — to prawdziwy komponent UI z gry i ma
tak wyglądać. Panele po prawej (inspector, sumator, toolbar) używają zwykłego fontu UI
(`C:/Windows/Fonts/segoeui.ttf`), bo małe kapitaliki szeryfowe są nieczytelne przy
12–14 px w gęstym formularzu.

Przełącznik to `UiText::SetRole(UiFontRole::Plain|Display)` w `inc/ui/UiText.h`.
**Gra go nie woła**, więc zostaje na `Display` i renderuje się dokładnie jak wcześniej.
Brak pliku z fontem Plain = cichy fallback na font gry, nic się nie wywala.

### Świadome różnice względem gry

- **Layout deterministyczny** zamiast mieszanego — patrz ostrzeżenie wyżej.
- **Krawędzie ortogonalne** (pion / poziom / pion, same kąty proste; prosta linia gdy
  kolumny się pokrywają). W grze środkowy segment jest skośny.
- **Uproszczony chrome**: płaskie tło zamiast ramkowanego panelu, brak belki tytułowej,
  lane jako podpis z kreską zamiast wypełnionego paska, prostokątne nody z paskiem
  statusu z lewej zamiast zaokrąglonych kart.
- Node'y nie pokazują czasu badania ani paska postępu — nic tu nigdy nie jest
  `In progress`, a `research_time` jest w inspectorze.
- Modyfikator z filtrem `unit <id>` pokazuje surowe id zamiast nazwy jednostki:
  `FindUnitDefinition` czyta katalog ze ścieżki względnej do CWD gry i ciągnąłby
  całe `warfare/` do toola.
- Klik działa na każdym nodzie (toggle), nie tylko na dostępnym.

### Ostrzeżenie o domyślnych definicjach

`LoadTechnologyDefinitionsFromFile` po cichu podstawia wbudowane definicje, gdy plik nie
istnieje albo nie sparsuje żadnego bloku. Dla gry OK, dla edytora groźne — edytowałbyś
nody, których nie ma w pliku. `TreeDocument::Reload` to wykrywa i pisze ostrzeżenie
w pasku statusu.

## Sumator bonusów

Sumuje modyfikatory zaznaczonych nodów, grupując po **pełnej sygnaturze filtrów**
(stat + building + resource + category + unit), nie po samym stacie. To jest istotne:
globalne "+10% production output" i "+10% production output for Woodcutter" nigdy nie
składają się na tę samą liczbę w grze, więc pokazanie ich razem byłoby kłamstwem.

W grupie agregacja jest taka jak w `BalanceModifierSet::ModifyDouble`:
`(base + Σ additive) * Π multiplier`.

## Klatkowanie

Było `FLAG_VSYNC_HINT` **plus** `SetTargetFPS(60)` — dwa niezależne limitery: pętla
czekania raylib walczyła ze swap-waitem sterownika i zamiast równego 60 wychodził
judder. Teraz vsync jest wyłączony, został jeden limiter na 150 FPS. Licznik jest
w prawym górnym rogu, więc widać czy trzyma.

Jeśli kiedyś zacznie zwalniać przy dużym drzewku: `TreeDocument::BuildNodes()` kopiuje
komplet stringów i wektorów dla każdego node'a **co klatkę**. Przy 27 nodach to nic,
przy kilkuset będzie to pierwsze miejsce do keszowania.

## Znane ograniczenia

- Pola tekstowe edytują się tylko na końcu (dopisywanie + backspace), bez karetki
  w środku — tak samo jak istniejący text box w grze.
- Brak undo. `F5` przeładowuje plik z dysku i wyrzuca niezapisane zmiany.
- Kolejność nodów w pliku = kolejność dodawania; nie ma sortowania przy zapisie.
