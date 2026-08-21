# texture-editor

Edytor przypisań tekstur: `assets/data/textures.rtsdata`. Podgląd + edycja + animacje.

Adresowanie jest jednolite — **wszystko jest „kafel `texture` atlasu `atlas`"**. Atlas to para
(obrazek, rozmiar kafla), więc pojedynczy sprite 96×96 to legalny atlas 1-kaflowy: **żadna grafika
nie wymaga przepakowania**, a animacja „poziomy pasek klatek" to po prostu ciąg kolejnych kafli.

> **Budynki są już zintegrowane z grą.** `GameScene` czyta z tego pliku atlas i klip animacji
> każdego wspieranego budynku, więc zapis w edytorze jest widoczny po kolejnym uruchomieniu gry.
> Teren i ikony zasobów nadal korzystają z legacy runtime'u.

## Uruchomienie

```bash
tools/texture-editor/build_and_run.bat
```

Wykrywanie raylib identyczne jak w głównym `build_and_run.ps1`: `$env:RAYLIB_ROOT`, potem
`deps/raylib`, potem `../work/local/raylib`. `-NoRun` buduje bez uruchamiania, `-Config Debug`
zmienia konfigurację. Tool **nie jest** wpięty w główny `CMakeLists.txt` — buduje się osobno, żeby
zepsuty tool nigdy nie zepsuł buildu gry.

## Workflow

Typowa sesja — „mam nową grafikę, gdzie ją wpiąć":

1. **Zakładka 4 (All assets)** → znajdź plik → `Assign this file to a slot...` → wybierz budynek /
   typ terenu / surowiec. Albo odwrotnie, od strony slotu:
2. **Zakładka 1–3** → wybierz slot → `Change texture...` → wybierz plik (modal filtruje się pisaniem).
3. **Ustaw rozmiar kafla** — `full` (cały obrazek = 1 kafel) albo 16/32/64, albo ręcznie.
   Rozmiar kafla należy do **atlasu**, więc zmiana rusza każdy slot dzielący ten plik — edytor
   pokazuje wtedy plakietkę `shared by N slots`.
4. **Kliknij kafel w siatce atlasu**, żeby przypisać go do slotu. Zaznaczenie = niebieski.
5. **Animacja** (budynki): `animated` → `frames` / `frame time` / `loop`. Klatki to **kolejne
   kafle od lewej do prawej** od kafla slotu — bez tasowania i bez ping-ponga. Podświetlają się
   na bursztynowo w siatce, a podgląd na mapie odtwarza je na żywo.
6. `Ctrl+S`. Zapis czyta plik z powrotem **prawdziwym parserem gry** i porównuje pole po polu.

### Sterowanie

| Akcja | Wejście |
|---|---|
| Zmiana zakładki | `1`–`4`, `Tab`, albo klik |
| Zapis | `Ctrl+S` albo przycisk |
| Przeładowanie z dysku (odrzuca zmiany) | `F5` |
| Przypisanie kafla | LPM na kaflu w siatce atlasu |
| Zoom / pan atlasu | kółko / PPM (lub środkowy) + przeciągnięcie |
| Filtr w modalu | po prostu pisz; `ESC` anuluje |

## Zakładki

Zakładka = klasa implementująca `ITextureTab` (`src/Tabs.h`) + jedna linia w `BuildTabs()`
(`src/main.cpp`). Shell nie wie nic o konkretnej zakładce — piąta (jednostki, UI, pociski) to nowy
`.cpp` i jeden `push_back`. `AssetTab` powstał dokładnie tą drogą.

1. **Tilemap** — `TileType` → ważone warianty. Warianty jednego typu dzielą jeden atlas (bo tak
   ładuje się teren w grze); edytujesz kafel + wagę, a procent obok wagi to realna szansa rolla.
2. **Buildings + animations** — sprite + klip. Podgląd rysuje sprite **tak jak mapa**: rozciągnięty
   na `footprint × TILE_SIZE`. Stopka mówi wprost, kiedy to oznacza resampling co klatkę.
3. **Resources** — ikona per `ResourceType`. Dwa bulk-i, bo podmiana arkusza ikon to operacja na
   49 slotach: `Use this atlas for every resource` i `Reset cells to enum order`.
4. **All assets** — każdy plik pod `assets/`, jego atlas i liczba slotów, plus `Assign to a slot...`.

## Skąd się bierze plik przy pierwszym uruchomieniu

Gdy `textures.rtsdata` nie istnieje, edytor **seeduje** model z tego, co gra robi dziś: zahardkodowana
tablica `TileMap::terrainVariants`, `texture` z `buildings.rtsdata`, ikony surowców po wartości enuma.
Sprite'y standalone stają się atlasami 1-kaflowymi. Ten seed pozostaje migracją dla terenu i ikon;
konfiguracja budynków jest już czytana przez grę.

Stan „zaseedowane, jeszcze niezapisane" ma własny żółty baner i od razu ustawia dirty. To celowe:
`LoadTechnologyDefinitionsFromFile` i `GetBuildingDefinitions` po cichu podstawiają wbudowane
domyślne definicje, gdy plik nie parsuje — dla gry OK, dla edytora zabójcze, bo edytowałbyś
definicje, których gra nigdy nie przeczyta. Dlatego `LoadTextureConfig` **nigdy** nie podstawia
domyślnych: zgłasza błąd i zwraca pustkę.

## Zapis

`Save` nadpisuje plik w całości z modelu — edytor jest źródłem prawdy, komentarze i ręczne
formatowanie nie są zachowywane. Przy pierwszym zapisie powstaje jednorazowy `.bak`.

Po zapisie plik jest **czytany z powrotem `LoadTextureConfig` i porównywany pole po polu**
(`TextureConfigEquals`). Rozjazd = czerwony `Round-trip mismatch: ...` zamiast cichego pliku, który
gra rozumie inaczej, niż edytor pokazywał. Zapis pustego modelu jest odrzucany.

## Co jest współdzielone z grą, a co skopiowane

**Współdzielone** — kompilowane po ścieżce względnej z `../../src` (`RTS_SHARED_SOURCES`):

- `src/data/TextureConfig.cpp` — **model + parser + serializer `textures.rtsdata`**, kompilowany
  zarówno przez grę, jak i przez edytor. Oba programy czytają dokładnie ten sam kod.
- `src/data/RtsDataFile.cpp` — tokenizer `.rtsdata`
- `src/data/Resource.cpp`, `src/data/Equipment.cpp` — typy zasobów
- `src/ui/UiText.cpp`, `src/ui/UiWidgets.cpp` — metryki tekstu i widżety wejściowe

Enumy `TileType`/`ResourceType` z prawdziwych nagłówków gry (nic nie jest instancjonowane, więc
zero kosztu linkowania).

**Skopiowane** — `ResolveFrame` w `TabBuildings.cpp` to kopia `ResolveAnimationFrame`
z `src/ui/Renderer.cpp` (10 linii czystej matematyki, ale siedzi w TU ciągnącym
`Building`/`GameSnapshot`). Etap 3 powinien wyciągnąć ją do osobnego TU — tak jak wyciągnięto
`UiText` z `Gui.cpp` na potrzeby `tech-tree-editor`.

**Motyw jest własny** (`src/EditorTheme.h`, antracyt) — świadomie nie `UiTheme` z gry: to gęsta
powierzchnia robocza czytana godzinami, a brąz/pergamin gry jest strojony pod klimat, nie pod
kontrast w tabeli liczb. Jeden krój (Segoe UI), bo małe kapitaliki `MarcellusSC` są nieczytelne
w formularzu.

## Stan po migracji

Pierwotna lista znalezisk z pilota została usunięta, ponieważ opisywała stan sprzed
wdrożenia `textures.rtsdata`, animacji budynków i nowych atlasów dróg/zasobów.
Źródłem prawdy jest teraz `assets/data/textures.rtsdata`, a bieżące problemy i wyniki
walidacji assetów są zapisane w `docs/code_audit_2026-08-20.md`.

## Ograniczenia

- **Brak undo.** `F5` przeładowuje z dysku i wyrzuca niezapisane zmiany.
- Nie zna slotów UI/menu — pliki z `assets/ui/**` są oznaczone jako „unbound" niesłusznie.
  To argument za piątą zakładką, nie błąd w danych.
- Kolejność bloków w pliku = kolejność w modelu; brak sortowania przy zapisie.
- Rozmiar kafla jest własnością atlasu, więc dwa sloty dzielące plik nie mogą mieć różnych siatek.
  Jeśli to kiedyś zaboli, rozwiązaniem jest drugi wpis `atlas` na ten sam plik.
