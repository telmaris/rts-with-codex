# Plan shaderów, oświetlenia i cyklu dobowego

- **Status:** propozycja wdrożeniowa
- **Zakres:** renderer świata, materiały sprite'ów, multiplayerowy snapshot renderujący, assety
- **Silnik:** raylib 5.0, C++20, desktop OpenGL
- **Cel pierwszego wydania:** kolory frakcji + czytelny cykl dnia i nocy + lokalne światła bez wpływu na determinizm symulacji

---

## 1. Cel i zasady

Docelowa oprawa ma:

1. zachować czytelność RTS niezależnie od pory dnia;
2. pozwalać rozpoznawać właściciela budynku i jednostki po kolorze detali;
3. ożywić łańcuchy produkcyjne przez piece, okna, pochodnie, pociski, dym i ogień;
4. nie wymuszać przerysowywania statycznej mapy przy każdej zmianie światła;
5. działać identycznie w ścieżce renderowania bezpośrednio z `GameWorld` i ze snapshotu klienta MP;
6. nie wprowadzać danych czysto wizualnych do gameplayowego checksumu;
7. posiadać ustawienia jakości i bezpieczny fallback bez shaderów.

Najważniejsza decyzja: zastosować **stylizowane oświetlenie 2D oparte na ekranowej lightmapie**.
Nie wdrażać na początku normal map ani pełnego oświetlenia pseudo-3D. Obecne sprite'y mają już
namalowany kierunek światła, więc obracające się realistyczne światło dawałoby sprzeczne cienie
i wymagałoby przygotowania dodatkowej grafiki dla każdego obiektu.

### Poza zakresem pierwszego wydania

- wpływ nocy na walkę, produkcję lub AI;
- ray-marching i cienie z okluzją;
- normal mapy terenu i wszystkich budynków;
- HDR;
- pełny system pogody;
- fog of war jako mechanika gry.

Te elementy mogą użyć przygotowanego render graphu później, ale nie powinny blokować podstawowego
wdrożenia.

---

## 2. Stan obecny i ograniczenia

### 2.1. Aktualne warstwy

`Renderer` ma obecnie cztery `CanvasLayer`, każda oparta na `RenderTexture2D` o stałej
rozdzielczości 1920x1080:

- warstwa `0` — teren i placeholder dróg militarnych;
- warstwa `1` — budynki;
- warstwa `2` — obecnie niewykorzystana w głównej ścieżce;
- warstwa `3` — jednostki, pociski i część pasków HP.

Warstwy są składane bezpośrednio do okna w `Renderer::DrawContent`. UI jest rysowane później przez
widżety. Teren i budynki są cache'owane; warstwa jednostek jest odświeżana co klatkę.

Pliki:

- `inc/ui/Renderer.h`
- `src/ui/Renderer.cpp`
- `src/core/GameWorld.Render.cpp`
- `src/scenes/GameScene.cpp`

### 2.2. Obowiązkowa naprawa lifecycle'u GPU

Każdy `Scene` zawiera własny `Renderer`. `CanvasLayer::CanvasLayer()` natychmiast wykonuje
`LoadRenderTexture(1920, 1080)`, więc utworzenie wszystkich scen alokuje cztery duże FBO dla każdej
sceny, także menu. Dołożenie `worldCompositeFbo`, lightmapy, emisji i buforów bloom bez zmiany tego
mechanizmu niepotrzebnie pomnoży zużycie VRAM.

Drugi problem: sceny są nadal własnością `GameWindow` w chwili wywołania `CloseWindow()`.
Destruktory scen wykonają się dopiero później, już bez aktywnego kontekstu OpenGL. Nie można więc
polegać wyłącznie na destruktorze `Renderer`, jeśli ma on wywoływać `UnloadTexture`,
`UnloadRenderTexture` lub `UnloadShader`.

**Wniosek:** etap 0 poniżej jest wymagany przed dodaniem jakiegokolwiek nowego FBO.

### 2.3. Snapshot nie zna właściciela budynku

W bezpośredniej ścieżce renderowania dostępny jest `building->owner->color`. `GameSnapshotTile`
posiada `ownerColor`, ale dotyczy on starego `Tile::owner`, który po pivocie nie jest ustawiany.
Snapshot nie niesie dziś właściciela budynku. Bez rozszerzenia formatu klient MP nie może poprawnie
wykonać rekolorowania frakcji.

### 2.4. Konfiguracja tekstur jest w trakcie wdrażania

`assets/data/textures.rtsdata`, `inc/data/TextureConfig.h` i `src/data/TextureConfig.cpp` opisują
docelowe powiązanie slotów z atlasami, lecz komentarz w `TextureConfig.h` mówi, że runtime nie czyta
jeszcze tego pliku. Rozszerzenia materiałów należy dodać do tego formatu, ale dopiero po zakończeniu
jego podstawowego podłączenia do `GameScene`.

Nie należy tworzyć drugiego, równoległego pliku konfiguracyjnego dla shaderów budynków.

---

## 3. Docelowy render graph

```text
statyczny teren ------------\
cienie kontaktowe ----------+--> worldColorFbo
statyczne budynki -----------+
dynamiczne jednostki/FX ----/

ambient pory dnia ----------\
lokalne źródła światła -----+--> lightFbo (1/2 rozdzielczości)

worldColorFbo + lightFbo
        |
        v
world_lighting.fs -----------> litWorldFbo

emissiveFbo -----------------\
opcjonalny bloom ------------+--> final world

tacticalOverlayFbo ----------\
UI i panele -----------------+--> okno
```

### 3.1. Kolejność rysowania

1. Zbuduj lub wykorzystaj cache terenu.
2. Zbuduj lub wykorzystaj cache cieni statycznych.
3. Zbuduj lub wykorzystaj cache budynków już po rekolorowaniu właściciela.
4. Przerysuj jednostki, transporty i pociski.
5. Złóż warstwy świata do `worldColorFbo`.
6. Wyczyść i narysuj `lightFbo`.
7. Wykonaj jeden pełnoekranowy przebieg `world_lighting.fs`.
8. Dodaj piksele emisyjne.
9. Opcjonalnie dodaj bloom.
10. Narysuj paski HP, zaznaczenia, zasięgi i ostrzeżenia.
11. Narysuj HUD, panele i pozostałe UI.
12. Zaprezentuj klatkę przez `EndDrawing()`.

UI, paski HP i overlaye decyzyjne nie mogą być przyciemniane przez noc. Obecne paski HP rysowane
na warstwie jednostek należy przenieść do `tacticalOverlayFbo` lub kolejki overlayów składanej po
oświetleniu.

### 3.2. Nazwane warstwy zamiast magicznych indeksów

Przed rozbudową zastąpić publiczne `int layer` typem:

```cpp
enum class WorldRenderLayer : std::uint8_t
{
    Terrain,
    GroundOverlay,
    StaticShadow,
    StaticObject,
    DynamicObject,
    Count
};
```

`TacticalOverlay` nie powinien należeć do warstw świata, ponieważ jest składany po oświetleniu.
`Emissive` również powinno być osobnym buforem o własnych zasadach blendowania.

Jeżeli pełna zamiana sygnatur na enum okaże się zbyt duża na jeden commit, najpierw wprowadzić
stałe nazwane i usunąć surowe `0`, `1`, `2`, `3` z `GameWorld.Render.cpp`.

### 3.3. Kontrakt współrzędnych i odwrócenie FBO

Raylib przechowuje teksturę `RenderTexture2D` odwróconą względem zwykłej tekstury. Aktualny kod
dodatkowo ręcznie odwraca oś Y obiektów świata. Przed dodaniem kolejnych przebiegów utworzyć jeden
helper:

```cpp
void DrawRenderTarget(
    const RenderTexture2D& source,
    Rectangle destination,
    Color tint = WHITE);
```

Tylko ten helper ma decydować o znaku `source.height`. Nie kopiować logiki odwracania do każdego
postprocessu. W trybie debug narysować wzorzec z podpisami `TOP`, `BOTTOM`, `LEFT`, `RIGHT` i
sprawdzić:

- brak pionowego odbicia świata;
- zgodność pozycji kursora i selekcji;
- zgodność światła z budynkiem przy każdym zoomie;
- prawidłowe letterboxowanie na ekranach innych niż 16:9.

---

## 4. Docelowe typy i odpowiedzialności

### Stan wdrożenia — przełączniki diagnostyczne

Implementacja zawiera już wizualne przełączniki runtime, które nie zmieniają
symulacji, zapisu ani checksumu multiplayera:

- `F6` — włącza/wyłącza cykl dnia i nocy;
- `F7` — włącza/wyłącza lokalne światła budynków;
- `F8` — przełącza widok: obraz końcowy → albedo świata → lightmapa → maska fog.
- `L` — włącza/wyłącza pilotowy overlay obciążenia dróg.

Widok lightmapy jest podstawowym testem po dodaniu nowej tekstury emitera:
podczas przesuwania kamery i zmiany zoomu plama musi pozostać przy budynku.
Przełącznik cieni kontaktowych oraz rekolorowania drużynowego jest dostępny
przez `RenderSettings`, aby później wystawić go w ekranie ustawień grafiki.
Proste cienie kontaktowe są już rysowane dla budynków i maszerujących jednostek;
nie są to jeszcze cienie kierunkowe zależne od geometrii sprite'a.

### Pilotaż fog of war

Fog jest opcjonalnym postprocesem włączanym w ekranie Options przez
`Fog of War (pilot)`. Maska jest tworzona od zera w każdej klatce: każdy
własny budynek odsłania lokalny okrąg, Headquarters większy okrąg, a własna
maszerująca armia odsłania obszar tylko podczas marszu. Równoległy,
deterministyczny stan kafelkowy w symulacji blokuje komendy budowy poza
aktualnym zasięgiem człowieka; AI świadomie zachowuje pełną wiedzę mapy.
Nie ma zapisanego cache'u odkrytego terenu — po wyłączeniu i ponownym
włączeniu efekt natychmiast używa aktualnej pozycji budynków i jednostek.

To zamierzona wersja pierwsza. Docelowe radary będą zwykłymi, data-driven
emiterami widoczności o stałym promieniu; pamięć wcześniej odkrytego terenu
można dodać osobno, jeśli okaże się potrzebna dla rozgrywki.

Aktualizacja pilota: Headquarters ma promień `3072` world units, zwykły
budynek co najmniej `640` world units, a maszerująca armia `640` world units.
Revealery korzystają z tej samej tekstury radialnej i tego samego animowanego
shadera co lokalne światła. Krawędź łagodnie faluje w lokalnych UV emitera,
więc animacja nie przeskakuje podczas ruchu kamery. Wojskowe trakty są
renderowane na osobnej warstwie i pojawiają się w pełni nieprzezroczyste
wewnątrz obszaru uznanego za odsłonięty.

Źródła fog i świateł są zbierane z całego stanu świata, nie z prostokąta
kafli aktualnie renderowanych przez kamerę. CPU odrzuca dopiero emiter, którego
pełny promień nie przecina ekranu. Dzięki temu budynek poza klatką nadal
odsłania i oświetla widoczny fragment mapy, a maska nie zmienia kształtu przy
pan/zoom.

### Globalny postprocess świata

Po oświetleniu i fog, ale przed UI, działa opcjonalny
`world_postprocess.fs`. Ekran Options udostępnia dwa niezależne ustawienia:

- `World color grading` — delikatnie chłodzi cienie, ociepla światła oraz
  podnosi kontrast i nasycenie bez przepalania kolorów frakcji;
- `Retro world filter` — dodaje wyraźną, 10-poziomową paletę na kanał,
  cieplejsze i lekko odbarwione barwy, stabilny Bayer dithering 4×4,
  10% scanline'y oraz delikatną winietę.

Color grading jest domyślnie włączony, retro domyślnie wyłączony. Shader
obejmuje wyłącznie świat: HUD, tekst, panele, tooltipy i kursor są składane
później i pozostają ostre. Jeśli shader się nie skompiluje, renderer prezentuje
poprzedni target bez postprocessu.

### Lokalna poświata świateł

Opcja `Local light bloom` jest domyślnie wyłączona i rozszerza wyłącznie
lightmapę. `world_lighting.fs` próbuje osiem sąsiednich próbek półrozdzielczej
lightmapy, a wynik dodaje z ograniczoną siłą po zwykłym oświetleniu. Jest to
celowo subtelne, tanie rozmycie pochodni, okien i świateł budynków: nie obejmuje
HUD-u, tekstu, kursora, terenu ani białych ikon. Opcję można bezpiecznie
włączać i wyłączać w trakcie gry, ponieważ jest preferencją klienta, a nie
stanem symulacji.

Pociski wież są już dynamicznymi emiterami o wysokim priorytecie. Rysują
krótki, kierunkowy ślad oraz jasny rdzeń; pozycja, migotanie i światło są
wyprowadzone z istniejącego deterministycznego stanu pocisku i nie zmieniają
walki, zapisów ani checksumu.

### Pogoda bez assetów: pilot deszczu

`Rain overlay (visual pilot)` to domyślnie wyłączany, proceduralny przebieg
w tym samym shaderze postprocessu. Generuje rzadkie ukośne smugi z hasha
ekranowych komórek i porusza je czasem pochodzącym z `simulationTick`.
Efekt nie ma stanu pogody w symulacji i nie modyfikuje produkcji, ruchu,
widoczności ani multiplayera. Deszcz jest pomijany nad niemal czarną częścią
fogu, aby nie ujawniać jego kształtu ani nie rozjaśniać nieodkrytej mapy.
Aktualny profil używa nasyconych niebieskich smug i mocniejszego kontrastu;
ma być wyraźny w ruchu, a nie tylko na statycznym zbliżeniu.

Środek każdego revealera fog zachowuje teraz niezmodyfikowany kolor już
oświetlonego świata. Ciemno-amberowa barwa jest nakładana wyłącznie w pasie
przejścia radialnego gradientu; poza nim teren przechodzi do niemal czarnej
mgły. Drogi pojawiają się binarnie dopiero na początku właściwego odsłonięcia,
więc złota krawędź nie zdradza ich przebiegu.

### Kierunkowe cienie i obrysy

W dzień budynki oraz placeholdery jednostek otrzymują obok cienia kontaktowego
krótki cień kierunkowy. Kierunek i długość pochodzą z istniejącego
`WorldLightingFrame::sunDirection` i `shadowLength`; o świcie/zachodzie cień
jest dłuższy, a nocą zanika. Cień budynku składa się z kilku słabych,
malejących elips, a nie z twardej linii z owalnym zakończeniem. Drogi i mosty
nie rzucają cienia obiektu: są płaskimi, ciągłymi kaflami, dla których taki
efekt tworzyłby sztuczny czarny pas. Ta sama geometria jest używana w ścieżce
`GameWorld` i snapshotu klienta.

Zaznaczenie budynku, jego dostawcy oraz ghost budowy używają dwuwarstwowego,
pulsującego obrysu w screen-space. Jest widoczny po compositingu świata,
dlatego pozostaje czytelny nad fogiem i nocnym oświetleniem, ale nie wymaga
maski sprite'a ani kosztownego shadera detekcji krawędzi.

Istniejący timer oblężenia Headquarters jest również przesyłany jako
`buildingDamageIndicator` (snapshot version 10). Dopóki jest dodatni, HQ
otrzymuje krótki czerwony flash i pulsujący obrys; to wizualne potwierdzenie
trafienia wspólne dla hosta i klienta. Rozszerzenie na zwykłe budynki będzie
możliwe po dodaniu ogólnego zdarzenia obrażeń do ich modelu walki.

Budynki w trakcie budowy zachowują proste, jednolite przyciemnienie sprite'a.
Nie wraca wcześniejsza kurtyna odsłaniania, kreskowanie ani pasek postępu:
`isBuildingOperational` wystarcza, aby host i klient zastosowali ten sam
chłodny tint, który znika po ukończeniu budowy.

### Pilot overlayu logistyki

`Logistics load overlay (pilot)` w Options pokazuje trend obciążenia zwykłych
dróg oraz mostów. `RoadComponent` aktualizuje dziesięciosekundową średnią
wykładniczą z `transportables / capacity`; overlay jest wyłącznie diagnostyczny
i nie zmienia rezerwacji, pathfindingu ani prędkości transportu. Wartość jest
kwantowana do 32 poziomów i przekazywana w `GameSnapshotTile` razem z krótkim
wskaźnikiem niedawnego osiągnięcia pełnej przepustowości (snapshot version 13),
aby host i klient widziały tę samą sytuację bez zalewania delty snapshotu.

Overlay jest heatmapą ruchu, a nie tintem kafli: każdy obciążony odcinek rysuje
kolorowy pas od środka kafla do sąsiednich dróg. Odcinki łączą się w ciągłą
sieć, pozostają zielone przy niskim wykorzystaniu i przechodzą przez bursztyn
do czerwieni przy zatorze. Szeroki pas składa się addytywnie z łuny zewnętrznej,
miękkiego środka i półprzezroczystego rdzenia, dzięki czemu przypomina lokalne
oświetlenie budynków zamiast ostrej kreski. Grubość oraz krycie rosną z
obciążeniem; pusta droga nie dostaje znacznika. Pionowe połączenia jawnie
odwracają oś Y pomiędzy mapą i FBO, aby narożniki nie wskazywały przeciwnego
kierunku. Heatmapa trafia do `WorldEffects`, przed `StaticObjects`, więc tekstura
drogi zasłania jej środek, a na zewnątrz wystaje tylko szeroka łuna. Opcja
`Local light bloom` dodatkowo zwiększa promień zewnętrznej poświaty.

Chwilowe osiągnięcie pełnej przepustowości pokazuje mały, pulsujący punkt
ostrzegawczy przez 1,25 s. Zaznaczone budynki, ich dostawcy oraz budynki
produkcyjne w stanie idle mają delikatnie pulsujące wypełnienie i obrys w
warstwie UI ponad oświetleniem oraz fogiem.

Ten sam przełącznik jest dostępny podczas gry: klawisz `L` oraz mały przycisk
`L` po lewej stronie `Roster` w górnym HUD. Aktywny przycisk ma zielony obrys;
oba wejścia zmieniają tę samą lokalną preferencję renderera.

Świadomie pominięte w pierwszej wersji:

- krzywizna CRT i winieta — deformują geometrię mapy i przyciemniają narożniki
  już zajęte przez fog;
- aberracja chromatyczna — rozmywa małe jednostki oraz cienkie drogi;
- pełna pixelizacja render targetu — pogarsza precyzję wyboru i odczyt złóż;
- LUT 3D — wartościowy następny krok, ale wymaga uzgodnionych assetów profili
  kolorystycznych i pipeline'u ich ładowania.

### 4.1. `Renderer`

`Renderer` pozostaje właścicielem zasobów GPU i wykonuje render graph. Nie powinien pobierać
gameplayowego czasu ani graczy bezpośrednio z `GameWorld`.

Proponowane API:

```cpp
enum class RendererProfile
{
    UiOnly,
    World
};

enum class RenderDebugView
{
    None,
    WorldAlbedo,
    TeamMask,
    LightMap,
    Emissive,
    LitWorld
};

struct RenderSettings
{
    bool teamColors{true};
    bool dayNightCycle{true};
    bool dynamicLights{true};
    bool shadows{true};
    bool bloom{false};
    float lightMapScale{0.5f};
    int maxVisibleLights{192};
    RenderDebugView debugView{RenderDebugView::None};
};

class Renderer
{
public:
    void Initialize(RendererProfile profile);
    void Shutdown();
    bool IsInitialized() const;

    void SetSimulationTick(std::uint64_t tick);
    void SetRenderSettings(const RenderSettings& settings);

    void SetPlayerPalette(std::span<const RenderPlayerColor> players);
    void QueueDynamicLight(const LightEmitterView& light);
    void QueueTacticalOverlay(...);

    void DrawContent(std::vector<UiWidget*> ui, double dt);
};
```

`Initialize` i `Shutdown` muszą być idempotentne. `Renderer` powinien być niekopiowalny.

### 4.2. `ShaderLibrary`

Nowe pliki:

- `inc/ui/ShaderLibrary.h`
- `src/ui/ShaderLibrary.cpp`

Odpowiedzialności:

- załadowanie wszystkich shaderów po utworzeniu okna;
- zapamiętanie lokalizacji uniformów;
- logowanie błędów i brakujących uniformów;
- udostępnienie `IsAvailable(ShaderId)`;
- jawne `UnloadShader` w `Shutdown`;
- w buildzie debug opcjonalny reload po klawiszu.

Przykładowe identyfikatory:

```cpp
enum class ShaderId
{
    TeamColor,
    WorldLighting,
    RadialLight,
    EmissiveExtract,
    Outline,
    BloomHorizontal,
    BloomVertical
};
```

Brak lub błąd kompilacji shadera nie może wyłączyć gry. Renderer loguje błąd i przechodzi na
rysowanie bez danego efektu.

### 4.3. Czysta funkcja cyklu dobowego

Nowe pliki:

- `inc/ui/WorldLighting.h`
- `src/ui/WorldLighting.cpp`

Typy:

```cpp
struct DayNightConfig
{
    std::uint64_t ticksPerDay{120000}; // 20 minut przy 100 Hz
    float startPhase{0.50f};           // początek w południe
    float minAmbient{0.58f};
};

struct WorldLightingFrame
{
    float phase{0.0f};
    Vector3 ambientColor{1.0f, 1.0f, 1.0f};
    float ambientIntensity{1.0f};
    float exposure{1.0f};
    float saturation{1.0f};
    float contrast{1.0f};
    Vector2 sunDirection{-0.7f, 0.7f};
    float shadowLength{0.0f};
    float localLightVisibility{0.0f};
};

WorldLightingFrame ComputeWorldLighting(
    std::uint64_t simulationTick,
    const DayNightConfig& config);
```

Funkcja nie może używać `GetTime()`, zegara systemowego ani losowości. Jest łatwa do testowania bez
kontekstu graficznego.

### 4.4. Światło lokalne

```cpp
struct LightEmitterView
{
    Vector2 worldPosition{};
    Color color{WHITE};
    float radiusWorld{96.0f};
    float intensity{1.0f};
    float softness{0.65f};
    float flickerAmount{0.0f};
    int stableId{0};
    int priority{0};
};
```

Pozycja i promień są w przestrzeni świata. Dopiero `Renderer` konwertuje je przez kamerę do
lightmapy. `stableId` służy wyłącznie do stabilnej fazy migotania.

Nie przechowywać wskaźnika do `Building` ani `Player` w strukturach renderujących.

---

## 5. Materiał sprite'a i format assetów

### 5.1. Pliki

Proponowana struktura:

```text
assets/
  shaders/
    team_color.fs
    world_lighting.fs
    radial_light.fs
    emissive_extract.fs
    outline.fs
    bloom_horizontal.fs
    bloom_vertical.fs
  textures/
    masks/
      building/
        headquarters_mask.png
        foundry_mask.png
        ...
      units/
        militia_mask.png
        ...
```

Katalog `assets/` jest już kopiowany po buildzie przez `copy_directory`, więc nie trzeba dodawać
osobnych komend CMake dla plików `.fs`.

### 5.2. Kanały maski materiałowej

Maska musi mieć identyczny rozmiar, układ klatek i źródłowy prostokąt jak albedo.

| Kanał | Znaczenie | Zakres |
|---|---|---|
| R | primary team color | 0-255 |
| G | secondary team color | 0-255 |
| B | emisja powierzchni | 0-255 |
| A | rezerwa | na razie 255 |

Zasady:

- maska jest opcjonalna;
- brak maski oznacza zwykłe rysowanie sprite'a;
- kanały R i G mogą mieć miękkie krawędzie dla antyaliasingu;
- R i G nie powinny nakładać się na siebie, chyba że zamierzony jest blend dwóch kolorów;
- przezroczyste piksele albedo pozostają przezroczyste niezależnie od maski;
- maska animacji ma tyle samo klatek co albedo.

### 5.3. Workflow przygotowania masek

1. Skopiować rozmiar albedo do nowego obrazu RGBA.
2. Wybrać obecne niebieskie elementy frakcyjne zakresem HSV.
3. Zapisać wybór do kanału R.
4. Wyczyścić z maski okna, wodę, metal i inne przypadkowo niebieskie detale.
5. Ręcznie poprawić półprzezroczyste piksele antyaliasingu.
6. Oznaczyć emisję w kanale B: ogień, okna, rozgrzany metal.
7. Sprawdzić maskę dla kolorów:
   - jasny żółty;
   - nasycony czerwony;
   - ciemny fiolet;
   - jasny cyjan;
   - neutralny szary.
8. Sprawdzić sprite na jasnym i ciemnym tle.

Wykrywanie niebieskiego w runtime może istnieć tylko jako tymczasowy tryb developerski. Nie może być
formatem produkcyjnym, ponieważ antyaliasing i przypadkowe odcienie będą powodowały artefakty.

### 5.4. Rozszerzenie `textures.rtsdata`

Rozszerzyć `BuildingTextureDefinition`:

```cpp
struct LightEmitterDefinition
{
    float normalizedX{0.5f};
    float normalizedY{0.5f};
    float radiusTiles{3.0f};
    Color color{WHITE};
    float intensity{1.0f};
    float flicker{0.0f};
    int priority{0};
};

struct BuildingTextureDefinition
{
    std::string buildingType;
    TextureRef sprite;
    TextureRef materialMask;
    TextureAnimationDefinition animation;
    std::vector<LightEmitterDefinition> lights;
};
```

Przykład:

```text
building Foundry
    sprite atlas 9 texture 0
    material_mask atlas 30 texture 0
    light x 0.72 y 0.62 radius_tiles 3.5 color 255 126 48 intensity 1.25 flicker 0.08 priority 20
end
```

`x` i `y` są znormalizowane w obrębie docelowego footprintu budynku:

- `(0, 0)` — lewy górny róg sprite'a;
- `(1, 1)` — prawy dolny róg;
- wartości muszą zostać ograniczone do `[0, 1]`.

Walidacja parsera:

- `radius_tiles > 0`;
- składowe koloru w `[0, 255]`;
- `intensity >= 0`;
- `flicker` w `[0, 1]`;
- atlas maski musi istnieć;
- rozmiar komórki maski musi odpowiadać rozmiarowi komórki albedo;
- brak maski lub światła jest poprawny;
- zapis i ponowny odczyt muszą dać identyczną konfigurację.

### 5.5. Materiał w rendererze

Zastąpić równoległe mapy tekstur i animacji jednym typem:

```cpp
struct SpriteMaterial
{
    Texture2D albedo{};
    Texture2D materialMask{};
    bool hasMaterialMask{false};
    AnimationClip animation{};
};

std::map<BuildingType, SpriteMaterial> buildingMaterials;
```

Przy atlasach docelowo przechowywać uchwyty atlasów i `TextureRef`, nie duplikować ładowania tej
samej tekstury dla wielu definicji.

---

## 6. Shader rekolorowania frakcji

### 6.1. Uniformy

`assets/shaders/team_color.fs`:

```glsl
#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D materialMask;
uniform vec4 playerPrimary;
uniform vec4 playerSecondary;

out vec4 finalColor;

void main()
{
    vec4 albedo = texture(texture0, fragTexCoord);
    vec3 mask = texture(materialMask, fragTexCoord).rgb;

    float detail = dot(albedo.rgb, vec3(0.2126, 0.7152, 0.0722));
    detail = clamp(0.20 + detail * 1.15, 0.18, 1.15);

    vec3 primary = playerPrimary.rgb * detail;
    vec3 secondary = playerSecondary.rgb * detail;

    vec3 color = mix(albedo.rgb, primary, mask.r);
    color = mix(color, secondary, mask.g);

    finalColor = vec4(color, albedo.a) * fragColor;
}
```

To kod referencyjny, nie bezwzględnie końcowe strojenie. `fragColor` musi pozostać w wyniku, ponieważ
raylib przekazuje przez niego `tint` z `DrawTexturePro`; w ten sposób zachowa się obecne
przyciemnienie budynku w konstrukcji.

### 6.2. Kolejność wywołania

W `DrawBuildingTexture`:

1. wyznaczyć klatkę animacji;
2. wyznaczyć ten sam `source rectangle` albedo i maski;
3. pobrać kolor właściciela;
4. jeżeli shader i maska są dostępne:
   - `BeginShaderMode(teamColorShader)`;
   - ustawić teksturę maski;
   - ustawić primary/secondary color;
   - wykonać `DrawTexturePro`;
   - `EndShaderMode`;
5. w przeciwnym razie wykonać dotychczasowe `DrawTexturePro`.

Nie modyfikować pikseli tekstury na CPU i nie generować osobnej pokolorowanej tekstury dla każdego
gracza.

### 6.3. Kolor dodatkowy

Na początek secondary color wyliczać z primary:

```text
secondary = primary rozjaśniony o około 25% i lekko odbarwiony
```

Nie dodawać drugiego koloru do `Player`, dopóki UI lobby faktycznie nie pozwala go wybrać.

### 6.4. Cache budynków

Wynik team-color jest zapisany w statycznej warstwie budynków. To jest pożądane:

- shader wykonuje się tylko podczas odświeżenia warstwy;
- cykl dobowy nie zmienia tego cache;
- zmiana koloru gracza musi oznaczyć warstwę budynków jako dirty.

Kolory graczy powinny być niezmienne podczas meczu w pierwszej wersji.

---

## 7. Dane właściciela w snapshotach MP

### 7.1. Format

Dodać:

```cpp
struct GameSnapshotPlayer
{
    int id{0};
    Color color{WHITE};
};

struct GameSnapshotTile
{
    // istniejące pola
    int buildingOwnerId{-1};
    bool isBuildingOperational{false}; // v8: nie oświetla budowy w toku
};

struct GameSnapshot
{
    // istniejące pola
    std::vector<GameSnapshotPlayer> players;
};
```

Kolor przechowywać raz w palecie graczy, a nie na każdym kafelku.

### 7.2. Budowanie snapshotu

W `GameWorld::BuildSnapshot()`:

1. przejść po `playerHandler.players` w deterministycznej kolejności `std::map`;
2. dodać `{playerId, player->color}` do `snapshot.players`;
3. przy budynku ustawić:

```cpp
view.buildingOwnerId =
    tile.building->owner != nullptr ? tile.building->owner->id : -1;
```

Nie używać `tile.owner` do ustalania właściciela budynku.

### 7.3. Serializacja

Podbić:

```cpp
SerializationVersion::GameSnapshotVersion
```

z wartości `6` do `7` dla palety i owner id, a następnie do `8` dla
`isBuildingOperational` używanego przez światła lokalne.

Proponowana kolejność pełnego snapshotu:

```text
version tick localPlayerId mapX mapY playerCount
[playerId r g b a]...
[tile data including buildingOwnerId]...
```

Zaktualizować:

- `GameSnapshot::Serialize`;
- `GameSnapshot::TryDeserialize`;
- `SerializeSnapshotTile`;
- `TryDeserializeSnapshotTile`;
- `operator==` dla tile;
- testy round-trip;
- delta snapshotu.

Paleta graczy jest stała podczas meczu, więc `GameSnapshotDelta` nie musi jej wysyłać. Delta zmienia
`buildingOwnerId` razem z kafelkiem. Jeśli w przyszłości kolor będzie można zmienić w trakcie gry,
zmiana musi wymusić pełny snapshot albo osobną wersjonowaną aktualizację palety.

### 7.4. Rozwiązanie koloru

`Renderer::DrawSnapshot` buduje małą mapę `playerId -> Color` po otrzymaniu pełnego snapshotu.
Podczas rysowania budynku:

```cpp
Color color = ResolvePlayerColor(tile.buildingOwnerId);
DrawBuildingTexture(tile.buildingType, ..., color, ...);
```

Brak id lub brak wpisu w palecie daje `WHITE` i loguje ostrzeżenie tylko raz na typ błędu, bez spamu
co klatkę.

---

## 8. Cykl dnia i nocy

### 8.1. Źródło czasu

Pierwsza wersja jest czysto wizualna:

```text
phase = fract(startPhase + simulationTick / ticksPerDay)
```

Przy 100 Hz i cyklu 20 minut:

```text
ticksPerDay = 100 * 60 * 20 = 120000
```

Integracja:

- `GameWorld::DrawMap()` wywołuje `render->SetSimulationTick(simulationTick)`;
- `Renderer::DrawSnapshot()` pobiera tick ze snapshotu;
- `Renderer::DrawContent()` wywołuje `ComputeWorldLighting(lastSimulationTick, config)`.

Nie zapisywać osobnego czasu wizualnego, jeśli jest całkowicie wyprowadzalny z ticka. Tick jest już
częścią snapshotu i stanu świata.

### 8.2. Profile pór dnia

Punkt startowy:

| Faza | Umowna godzina | Ambient RGB | Intensywność | Saturacja | Local light visibility |
|---|---:|---|---:|---:|---:|
| noc | 00:00 | `(0.42, 0.50, 0.68)` | `0.42` | `0.78` | `1.00` |
| świt | 05:30 | `(1.00, 0.66, 0.48)` | `0.68` | `0.92` | `0.75` |
| dzień | 09:00 | `(1.00, 0.98, 0.94)` | `1.00` | `1.00` | `0.15` |
| zmierzch | 18:30 | `(1.00, 0.58, 0.38)` | `0.62` | `0.90` | `0.80` |
| noc | 21:00 | `(0.58, 0.64, 0.80)` | `0.58` | `0.84` | `1.00` |

Wartości interpolować funkcją `smoothstep`, nie liniowo na ostrych granicach.

Noc nie może schodzić poniżej `minAmbient`. Budynki, drogi i jednostki muszą być rozpoznawalne bez
lokalnego światła. Lokalna poświata ma budować atmosferę, nie warunkować podstawową widoczność.

### 8.3. Shader globalny

`world_lighting.fs` otrzymuje:

```glsl
uniform sampler2D texture0;     // worldColorFbo
uniform sampler2D lightMap;
uniform vec3 ambientColor;
uniform float ambientIntensity;
uniform float exposure;
uniform float saturation;
uniform float contrast;
```

Referencyjna kolejność:

1. odczytaj albedo;
2. wykonaj przybliżone sRGB -> linear;
3. oblicz `ambient = albedo * ambientColor * ambientIntensity`;
4. oblicz `local = albedo * lightMap`;
5. zsumuj ambient i local;
6. wykonaj prosty tone mapping lub clamp;
7. zastosuj saturację i kontrast;
8. wykonaj linear -> sRGB;
9. zachowaj alpha albedo.

Nie nakładać półprzezroczystego granatowego prostokąta na cały ekran. Taka metoda szybko niszczy
kontrast i kolory graczy.

### 8.4. Przyszły wpływ na gameplay

Jeżeli noc zacznie wpływać na:

- zasięg widzenia;
- celność;
- ruch;
- produkcję;
- zachowanie AI;

to cykl przestaje być czysto wizualny. Wtedy należy:

1. dodać `WorldTimeState` do `GameWorld`;
2. aktualizować go wyłącznie w fixed tick;
3. zapisać go w save;
4. dodać do checksumu;
5. dodać do pełnego snapshotu i resyncu;
6. wystawić gameplayowi dyskretne stany, np. `Day`, `Dusk`, `Night`, zamiast odczytywania uniformów
   renderera.

Nie wykonywać tej migracji „przy okazji” pierwszej wersji wizualnej.

---

## 9. Lightmapa i lokalne emitery

### 9.1. Rozdzielczość

Domyślnie:

```text
worldColorFbo: 1920x1080
lightFbo:       960x540
emissiveFbo:   1920x1080 lub 960x540
bloom A/B:      480x270
```

`lightMapScale` powinno akceptować co najmniej:

- Low: `0.25`;
- Medium: `0.5`;
- High: `0.5` lub `1.0`.

Nie realokować FBO co klatkę. Rozdzielczość zmieniać tylko przy zmianie ustawień renderera.

### 9.2. Rysowanie lightmapy

Co klatkę:

1. `BeginTextureMode(lightFbo)`;
2. `ClearBackground(BLACK)`;
3. `BeginBlendMode(BLEND_ADDITIVE)`;
4. dla każdego widocznego emitera:
   - przelicz `worldPosition` przez kamerę do logical render coordinates;
   - przeskaluj pozycję i promień przez `lightMapScale`;
   - narysuj quad z `radial_light.fs`;
5. `EndBlendMode`;
6. `EndTextureMode`.

`radial_light.fs` używa tekstury artystycznej jako bazowego falloffu. Dwie
powolne fale kątowe oraz niezależna faza emitera delikatnie deformują lokalne
UV i pulsują krawędzią bez przesuwania środka światła:

```glsl
vec2 centered = fragTexCoord - vec2(0.5);
float radialScale = 1.0 + animatedEdgeWave;
float falloff = texture(texture0, vec2(0.5) + centered * radialScale).a;
finalColor = vec4(lightColor * intensity * falloff, falloff);
```

Światła poza widokiem kamery odrzucać na CPU z marginesem równym promieniowi.

### 9.3. Światła statyczne i dynamiczne

**Statyczne:**

- Foundry;
- Smith;
- Bakery;
- Inn;
- Village;
- Headquarters;
- Guard Tower;
- Fortress;
- Castle.

Ich lista może być cache'owana i odświeżana, gdy:

- zmieni się kamera;
- powstanie lub zniknie budynek;
- zmieni się footprint;
- wczytano snapshot;
- zmieniła się definicja assetu podczas hot reload.

**Dynamiczne:**

- pociski wież;
- trafienia;
- ogień uszkodzonego budynku;
- efekty specjalne technologii;
- przyszłe jednostki z pochodniami.

Kolejkę dynamiczną czyścić na początku klatki i wypełniać podczas rysowania dynamicznych obiektów.

`DrawSnapshot()` obecnie pomija pracę, gdy tick i kamera się nie zmieniły. Statyczne emitery muszą
pozostać w cache renderera, a nie istnieć wyłącznie w jednorazowej kolejce.

### 9.4. Limit i priorytet

Przed rysowaniem:

1. odrzuć emitery niewidoczne;
2. oblicz wagę z `priority`, intensywności i odległości od środka ekranu;
3. jeżeli przekroczono `maxVisibleLights`, zachowaj najważniejsze;
4. sortowanie służy wyłącznie prezentacji i nie wpływa na symulację.

Punkt startowy: maksymalnie `192` widoczne światła na Medium.

### 9.5. Migotanie

Migotanie:

```text
flicker = 1 + amount * noise(simulationTick * frequency + stableId * constant)
```

Można użyć funkcji sinusoidalnej z kilkoma częstotliwościami zamiast generatora losowego.

Wymagania:

- brak `GetRandomValue`;
- brak RNG z `GameWorld`;
- identyczny rezultat dla tego samego `tick` i `stableId`;
- maksymalna zmiana intensywności ograniczona przez `flickerAmount`.

### 9.6. Emisja sprite'a

Kanał B material maski określa piksele, które mają pozostać jasne po przyciemnieniu świata.

Najprostsza implementacja bez MRT:

1. podczas przebudowy statycznej warstwy budynków narysować budynki emisyjne drugi raz do
   `emissiveFbo`;
2. `emissive_extract.fs` zachowuje tylko piksele `mask.b`;
3. po `world_lighting.fs` dodać `emissiveFbo` addytywnie, skalowane przez
   `localLightVisibility`;
4. bloom, jeśli aktywny, korzysta wyłącznie z `emissiveFbo`.

Nie wykonywać bloom na pełnym finalnym obrazie, ponieważ UI, jasna ziemia i białe ikony zaczęłyby
świecić.

---

## 10. Cienie

### 10.1. Etap pierwszy: cień kontaktowy

Każdy budynek i jednostka otrzymuje miękki, ciemny kształt pod podstawą:

- budynki: spłaszczona elipsa lub ręcznie przygotowana maska;
- jednostki: mała elipsa;
- alpha około `40-90`, zależnie od pory dnia;
- cień rysowany przed obiektem, po terenie.

To efekt tani i niewrażliwy na konflikt z namalowanym światłem sprite'a.

### 10.2. Etap drugi: cień kierunkowy

Opcjonalnie rysować sylwetkę alpha sprite'a:

1. pobrać alpha albedo;
2. narysować ciemną sylwetkę z przesunięciem `sunDirection * shadowLength`;
3. zmniejszyć alpha w południe i w nocy;
4. zwiększyć długość o świcie i zmierzchu;
5. nie obracać kierunku słońca o pełne 360 stopni.

Sprite'y mają namalowane światło, dlatego kierunek powinien pozostać mniej więcej stały, np.
północny zachód. Zmienia się przede wszystkim długość i intensywność cienia.

### 10.3. Czego nie robić

- nie wykonywać per-pixel ray-marchingu po całej mapie;
- nie generować normal map automatycznie z luminancji;
- nie liczyć okluzji wszystkich świateł przez wszystkie budynki;
- nie aktualizować statycznych cieni co klatkę, jeśli zmieniła się tylko intensywność globalna.

Statyczna geometria cienia może być cache'owana, a pora dnia może modulować ją przy składaniu.

---

## 11. Dodatkowe efekty korzystające z tego pipeline'u

### 11.1. Outline zaznaczenia

`outline.fs` wykonuje kilka próbek alpha wokół sprite'a i rysuje kontur, gdy sąsiad jest
nieprzezroczysty, a bieżący piksel przezroczysty.

Zastosowania:

- wybrany budynek;
- wybrana jednostka;
- cel ataku;
- dostawcy wybranego budynku;
- budynek pod kursorem.

Outline i prostokąty selekcji należą do tactical overlay, więc nie są przyciemniane.

### 11.2. Stan budowy

Po rekolorowaniu dodać parametry:

- `constructionProgress`;
- `blocked`;
- `canBuild`.

Pierwsza wersja:

- zachować obecny chłodny tint;
- dodać pionowy reveal od dołu;
- dla zablokowanej budowy pulsować outline'em;
- nie używać fantastycznego dissolve, jeśli grafika ma pozostać historyczna.

Wymaga dodania postępu konstrukcji do wizualnego snapshotu, jeżeli klient MP ma widzieć ten efekt.

### 11.3. Trafienie i uszkodzenia

- flash budynku przez 80-120 ms;
- pył w kolorze podłoża;
- emisja ognia poniżej ustalonego progu HP;
- dym jako osobny sprite/particle, nie część lightmapy;
- pocisk wieży jako dynamiczny emiter o wysokim priorytecie.

Timer flasha może być czysto wizualny, ale zdarzenie trafienia musi być dostępne w renderowanej
ścieżce MP. Nie odczytywać zmian HP przez wyścig ze światem bez blokady.

### 11.4. Fog of war

Przyszły `visibilityFbo` może kodować:

- `0.0` — nigdy nieodkryte;
- `0.5` — odkryte, ale aktualnie niewidoczne;
- `1.0` — aktualnie widoczne.

Maskę składać po oświetleniu świata, ale przed tactical overlay. Mechanika odkrywania i widoczności
jest osobnym zadaniem gameplayowym i musi być deterministyczna.

### 11.5. Overlay logistyki

Oddzielny shader maski może wizualizować:

- wykorzystanie przepustowości dróg;
- niedobory zaopatrzenia;
- trasy transportów;
- zasięg wojskowy;
- presję AI;
- zasoby terenowe.

Animacja przepływu musi być zakotwiczona w przestrzeni świata, aby nie przesuwała się przy ruchu
kamery.

### 11.6. Pogoda

Po ustabilizowaniu światła:

- powolne cienie chmur na `worldColorFbo`;
- deszcz w osobnym przebiegu po świecie, przed UI;
- mgła jako niskokontrastowa warstwa;
- kierunek dymu zależny od wizualnego wiatru.

Pogoda czysto wizualna nie trafia do checksumu. Jeśli wpływa na produkcję lub walkę, musi mieć
oddzielny deterministyczny stan symulacji.

---

## 12. Instrukcja wdrożenia krok po kroku

Każdy etap ma zakończyć się działającą grą. Nie łączyć wszystkich etapów w jeden duży commit.

### Etap 0 — bezpieczny lifecycle GPU i baseline

**Pliki:**

- `inc/ui/Renderer.h`
- `src/ui/Renderer.cpp`
- `inc/scenes/GameWindow.h`
- `src/scenes/GameWindow.cpp`
- `src/scenes/GameScene.cpp`

**Kroki:**

1. Zrobić referencyjne screenshoty:
   - mapa w SP;
   - zoom minimalny i maksymalny;
   - UI budowy;
   - klient localhost;
   - scena menu.
2. Usunąć `LoadRenderTexture` z konstruktora `CanvasLayer`.
3. Dodać jawne `CanvasLayer::Initialize(width, height)` i `Shutdown()`.
4. Dodać `Renderer::Initialize(RendererProfile)`.
5. Profil `UiOnly` nie tworzy warstw świata.
6. Profil `World` tworzy aktualne warstwy dopiero w `GameScene`.
7. Dodać `Renderer::Shutdown()` zwalniający:
   - wszystkie tekstury należące do renderera;
   - wszystkie render textures;
   - wszystkie shadery.
8. W `GameWindow::LaunchGame`, po `MainLoop()` i przed `CloseWindow()`:
   - wywołać `Shutdown()` na rendererach scen;
   - zwolnić `activeScene`;
   - wyczyścić mapę scen, jeżeli lifecycle pozostałych zasobów na to pozwala.
9. Zabezpieczyć `Shutdown()` przed drugim wywołaniem.
10. Zablokować kopiowanie `Renderer`, pozostawić poprawne przenoszenie albo również je zablokować.
11. Uruchomić grę i porównać screenshoty — brak zmiany wizualnej.

**Definition of Done:**

- menu nie alokuje world FBO;
- tylko `GameScene` posiada world FBO;
- zamknięcie gry nie wywołuje OpenGL unload po `CloseWindow`;
- brak zmiany wizualnej i inputowej;
- brak crasha przy wielokrotnym wejściu i wyjściu z gry.

### Etap 1 — nazwanie warstw i centralne składanie

**Pliki:**

- `inc/ui/Renderer.h`
- `src/ui/Renderer.cpp`
- `src/core/GameWorld.Render.cpp`

**Kroki:**

1. Wprowadzić `WorldRenderLayer`.
2. Zastąpić surowe indeksy warstw nazwami.
3. Dodać `DrawRenderTarget`, centralizujący Y flip.
4. Dodać `worldCompositeFbo`.
5. W `DrawContent`:
   - złożyć warstwy świata do `worldCompositeFbo`;
   - narysować `worldCompositeFbo` do okna bez shadera;
   - narysować UI jak wcześniej.
6. Przenieść paski HP z dynamicznej warstwy świata do `tacticalOverlayFbo`.
7. Narysować tactical overlay po `worldCompositeFbo`, przed UI.
8. Dodać debug test pattern orientacji.

**Definition of Done:**

- wynik bez postprocessu jest pikselowo lub wizualnie zgodny z baseline;
- paski HP i selekcja znajdują się w dobrych miejscach przy każdym zoomie;
- letterbox i mapowanie kursora działają bez regresji.

### Etap 2 — konfiguracja materiałów i pierwsze maski

**Pliki:**

- `inc/data/TextureConfig.h`
- `src/data/TextureConfig.cpp`
- `assets/data/textures.rtsdata`
- `tests/TextureConfigTests.cpp` — nowy
- `tests/CMakeLists.txt`

**Kroki:**

1. Najpierw zakończyć podłączenie istniejącego `textures.rtsdata` do runtime.
2. Dodać `materialMask` i `lights` do `BuildingTextureDefinition`.
3. Rozszerzyć parser.
4. Rozszerzyć formatter.
5. Rozszerzyć `TextureConfigEquals`.
6. Dodać walidację odwołań do atlasów.
7. Dodać testy:
   - brak opcjonalnej maski;
   - maska poprawna;
   - wiele świateł;
   - wartości graniczne;
   - round-trip parse -> format -> parse.
8. Dodać maski dla:
   - Headquarters;
   - Village;
   - GuardTower;
   - Fortress;
   - Castle;
   - Barracks;
   - Foundry;
   - Smith.
9. Dodać wpisy atlasów maski do `textures.rtsdata`.

**Definition of Done:**

- konfiguracja ładuje się bez ostrzeżeń;
- stary budynek bez maski nadal się rysuje;
- błędna maska daje czytelny log i fallback;
- test round-trip przechodzi.

### Etap 3 — właściciel budynku w snapshotach

**Pliki:**

- `inc/core/GameSnapshot.h`
- `inc/core/Serialization.h`
- `src/core/GameWorld.Render.cpp`
- test snapshotów — nowy plik lub właściwy istniejący suite
- `tests/CMakeLists.txt`

**Kroki:**

1. Dodać `GameSnapshotPlayer`.
2. Dodać `GameSnapshot::players`.
3. Dodać `GameSnapshotTile::buildingOwnerId`.
4. Rozszerzyć porównania tile.
5. Rozszerzyć pełną serializację/deserializację.
6. Rozszerzyć delta tile.
7. Podbić `GameSnapshotVersion` z `6` do `7` (oraz do `8`, jeśli snapshot
   rozróżnia ukończony budynek od budowy w toku dla świateł).
8. Wypełnić paletę i owner id w `BuildSnapshot`.
9. Dodać testy:
   - dwóch graczy o różnych kolorach;
   - budynki obu graczy;
   - budynek bez ownera;
   - pełny round-trip;
   - delta zmieniająca właściciela;
   - odrzucenie starej lub błędnej wersji.
10. Sprawdzić join localhost i TCP.

**Definition of Done:**

- host i klient rozwiązują ten sam kolor dla każdego budynku;
- delta nie gubi zmiany właściciela;
- wersja snapshotu jest jawnie podbita;
- checksum gameplayowy pozostaje bez zmian.

### Etap 4 — team-color shader

**Pliki:**

- `assets/shaders/team_color.fs`
- `inc/ui/ShaderLibrary.h`
- `src/ui/ShaderLibrary.cpp`
- `inc/ui/Renderer.h`
- `src/ui/Renderer.cpp`
- `src/CMakeLists.txt`

**Kroki:**

1. Dodać minimalny `ShaderLibrary`.
2. Załadować `team_color.fs` przy `Renderer::Initialize(World)`.
3. Dodać `SpriteMaterial`.
4. Ładować albedo i material mask razem.
5. Zmienić `DrawBuildingTexture`, aby przyjmował `ownerColor`.
6. Podłączyć owner color w bezpośrednim `DrawMap`.
7. Podłączyć owner color w `DrawSnapshot`.
8. Zachować fallback i obecny tint konstrukcji.
9. Dodać developerski podgląd samej maski.
10. Sprawdzić wszystkie kolory slotów graczy.

**Definition of Done:**

- tylko oznaczone detale zmieniają kolor;
- cienie i highlighty detalu pozostają czytelne;
- alpha sprite'a jest bez zmian;
- konstrukcja nadal ma swój tint;
- SP, host i klient pokazują te same barwy;
- wyłączenie team colors odtwarza albedo.

### Etap 5 — globalny cykl dobowy

**Pliki:**

- `assets/shaders/world_lighting.fs`
- `inc/ui/WorldLighting.h`
- `src/ui/WorldLighting.cpp`
- `inc/ui/Renderer.h`
- `src/ui/Renderer.cpp`
- `src/core/GameWorld.Render.cpp`
- `src/CMakeLists.txt`
- `tests/WorldLightingTests.cpp`
- `tests/CMakeLists.txt`

**Kroki:**

1. Zaimplementować `ComputeWorldLighting`.
2. Dodać testy faz i ciągłości przejść.
3. Przekazywać `simulationTick` z obu ścieżek renderowania.
4. Dodać `litWorldFbo`.
5. Załadować `world_lighting.fs`.
6. Po złożeniu świata wykonać pełnoekranowy shader.
7. UI i tactical overlay narysować później.
8. Dodać debugowe wymuszenie: dawn/day/dusk/night.
9. Dodać przełącznik wyłączenia cyklu.
10. Strojenie wykonać na kilku mapach i kolorach graczy.

**Definition of Done:**

- pora dnia zatrzymuje się z tickiem;
- host i klient mają tę samą fazę;
- noc nie ukrywa dróg, jednostek ani budynków;
- UI nie zmienia koloru;
- statyczne warstwy nie są odświeżane tylko dlatego, że zmienił się ambient.

### Etap 6 — lightmapa i emitery

**Pliki:**

- `assets/shaders/radial_light.fs`
- `inc/ui/Renderer.h`
- `src/ui/Renderer.cpp`
- `inc/ui/WorldLighting.h`
- `src/ui/WorldLighting.cpp`
- `src/core/GameWorld.Render.cpp`
- `assets/data/textures.rtsdata`

**Kroki:**

1. Dodać `lightFbo` z konfigurowalnym scale.
2. Dodać typ `LightEmitterView`.
3. Zbudować cache statycznych emiterów z widocznych budynków.
4. Dodać kolejkę emiterów dynamicznych.
5. Zaimplementować culling i limit.
6. Narysować radialne światła addytywnie.
7. Połączyć lightmapę w `world_lighting.fs`.
8. Skalować światła przez `localLightVisibility` pory dnia.
9. Dodać deterministyczne wizualnie migotanie.
10. Dodać światła kolejno:
    - Foundry;
    - Smith;
    - Bakery;
    - Inn;
    - Village;
    - HQ;
    - militarne wieże;
    - pociski.

**Definition of Done:**

- pozycja światła nie odjeżdża przy zoomie i ruchu kamery;
- światło znika po zniszczeniu budynku;
- brak alokacji pamięci per emiter per klatkę;
- 192 widoczne światła nie powodują gwałtownego spadku płynności;
- wyłączenie dynamic lights pozostawia poprawny cykl dobowy.

### Etap 7 — emisja i opcjonalny bloom

**Stan obecny:** zaimplementowano bezpieczny wariant przejściowy `Local light
bloom`, działający wyłącznie na istniejącej lightmapie i dostępny z Options.
Pełny etap poniżej pozostaje celowo odroczony do chwili dostarczenia masek
materiałowych: bez kanału emisji nie da się odróżnić okna i ognia od jasnych
pikseli albedo. Nie należy zastępować tego bloomem całej ramki.

**Pliki:**

- `assets/shaders/emissive_extract.fs`
- `assets/shaders/bloom_horizontal.fs`
- `assets/shaders/bloom_vertical.fs`
- `inc/ui/Renderer.h`
- `src/ui/Renderer.cpp`

**Kroki:**

1. Dodać `emissiveFbo`.
2. Narysować kanał B maski emisyjnej.
3. Dodać emisję po globalnym oświetleniu.
4. Dodać dwa małe ping-pong FBO bloom.
5. Rozmywać tylko emisję.
6. Bloom domyślnie wyłączyć na Low i Medium.
7. Dodać limit intensywności, aby okna nie stawały się białymi plamami.

**Definition of Done:**

- UI nigdy nie trafia do bloom;
- w dzień emisja jest subtelna;
- nocą okna i piece są czytelne, ale nie przepalają obrazu;
- wyłączenie bloom nie wyłącza zwykłej emisji.

### Etap 8 — cienie, outline i polish

**Kroki:**

1. Dodać cienie kontaktowe jednostek i budynków.
2. Dodać opcjonalne cienie kierunkowe statycznych obiektów.
3. Dodać outline zaznaczenia.
4. Przenieść wszystkie znaczniki gameplayowe po oświetleniu.
5. Dodać flash trafienia i światło pocisku.
6. Dopiero potem rozważyć pogodę, fog of war i overlay logistyki.

---

## 13. Testy i narzędzia diagnostyczne

### 13.1. Testy automatyczne CPU

Wymagane:

- `TextureConfig` parse/format/round-trip;
- walidacja material mask i emiterów;
- `GameSnapshot` full round-trip;
- `GameSnapshotDelta` z `buildingOwnerId`;
- `ComputeWorldLighting` dla charakterystycznych faz;
- ciągłość ambientu na granicach faz;
- stabilność funkcji flicker dla `(tick, stableId)`;
- culling światła na granicy widoku.

Testy shaderów GPU mogą pozostać manualne, ponieważ standardowy suite nie musi posiadać kontekstu
OpenGL.

### 13.2. Debug views

Przełącznik developerski powinien cyklicznie pokazywać:

1. final;
2. world albedo;
3. primary/secondary team mask;
4. emissive mask;
5. lightmap;
6. ambient only;
7. local lights only;
8. final bez UI.

W lewym górnym rogu wyświetlić:

```text
phase=0.783
ambient=0.46
lights=43/192
lightmap=960x540
teamShader=OK
worldShader=OK
```

### 13.3. Macierz manualnego QA

Sprawdzić:

| Przypadek | Wymaganie |
|---|---|
| SP | brak różnic między live world a snapshot path |
| Localhost host | poprawne barwy obu graczy |
| Localhost client | ten sam tick światła i barwy co host |
| TCP join | paleta graczy przychodzi w pełnym snapshotcie |
| Resync | światła i barwy odbudowują się bez starego cache |
| Save/load | cykl kontynuuje się z ticka |
| Kamera | światła nie odjeżdżają przy pan/zoom |
| Resize | poprawny letterbox i brak odbicia FBO |
| Alt-tab/fullscreen | brak utraty lub podwójnego unload zasobów |
| Brak shadera | gra działa z fallbackiem |
| Brak maski | budynek rysuje zwykłe albedo |
| Jasny team color | brak przepaleń |
| Ciemny team color | kolor nadal rozpoznawalny nocą |

---

## 14. Ustawienia jakości

Pierwsza wersja może przechowywać ustawienia lokalnie, bez przesyłania przez sieć.

| Efekt | Off | Low | Medium | High |
|---|---:|---:|---:|---:|
| team color | opcjonalny | tak | tak | tak |
| day/night | nie | tak | tak | tak |
| lightmap scale | brak | 0.25 | 0.5 | 0.5-1.0 |
| dynamic lights | nie | 64 | 192 | 256 |
| contact shadows | nie | tak | tak | tak |
| directional shadows | nie | nie | opcjonalne | tak |
| emissive | nie | tak | tak | tak |
| bloom | nie | nie | nie/dom. off | tak |

Zmiana jakości:

- nie może zmieniać symulacji;
- nie może zmieniać snapshotu;
- może realokować FBO tylko w momencie zatwierdzenia ustawień;
- musi wyczyścić odpowiednie cache renderera.

---

## 15. Budżet wydajności i pamięci

### 15.1. Reguły

- żadnych alokacji heap per sprite lub per light per klatkę;
- wektory świateł mają `reserve`;
- emitery statyczne są cache'owane;
- światła są cullowane przed draw call;
- bloom pracuje w 1/4 rozdzielczości;
- pełnoekranowe przebiegi są ograniczone;
- FBO świata istnieją tylko w rendererze `GameScene`;
- UI nie jest przepuszczane przez world postprocess.

### 15.2. Docelowy koszt wariantu Medium

- team-color: tylko przy przebudowie cache statycznego sprite'a;
- world composition: jeden przebieg;
- day/night + light combine: jeden przebieg;
- lightmap: pół rozdzielczości i ograniczona liczba quadów;
- emisja: jeden dodatkowy composite;
- bloom: wyłączony.

Przed i po każdym etapie mierzyć:

- FPS oraz frame time w tym samym save;
- liczbę draw calls, jeśli zostanie dodany licznik;
- liczbę widocznych świateł;
- zużycie VRAM przy menu i w `GameScene`;
- czas CPU `DrawMap` i `DrawContent`.

Nie ustalać finalnego limitu tylko na mocnej karcie deweloperskiej. Wariant Medium powinien działać
płynnie na zintegrowanym GPU przy 1080p.

---

## 16. Kolejność commitów

Rekomendowany podział:

1. `render: make GPU resources lazy and explicitly released`
2. `render: name layers and add world composite target`
3. `data: add sprite material masks and light emitters`
4. `snapshot: carry player palette and building owner`
5. `render: add team color shader`
6. `render: add deterministic visual day-night cycle`
7. `render: add screen-space local light map`
8. `render: add emissive materials`
9. `render: add optional bloom`
10. `render: add contact shadows and selection outline`

Każdy commit powinien:

- kompilować się samodzielnie;
- uruchamiać testy;
- mieć fallback;
- nie mieszać zmian gameplayowych z czysto wizualnymi;
- nie inkrementować save version, jeśli zmienia tylko snapshot renderujący;
- inkrementować `GameSnapshotVersion`, gdy zmienia wire snapshotu.

---

## 17. Ostateczna definicja ukończenia

Podstawowy system jest ukończony, gdy:

- wszystkie budynki graczy mają poprawne, maskowane detale frakcyjne;
- SP, host i klient renderują te same kolory;
- dzień i noc wynikają z `simulationTick`;
- pauza zatrzymuje cykl;
- noc zachowuje czytelność strategiczną;
- Foundry, Smith, osady i budynki militarne mają data-driven światła;
- UI, paski HP i overlaye nie są przyciemniane;
- renderer działa bez shaderów i bez masek;
- wszystkie zasoby GPU są ładowane po `InitWindow` i zwalniane przed `CloseWindow`;
- menu nie posiada niepotrzebnych world FBO;
- snapshot version i testy round-trip są aktualne;
- Medium nie używa bloom i nie wykonuje więcej pełnoekranowych przebiegów niż jest to konieczne;
- debug view pozwala osobno obejrzeć albedo, maskę, lightmapę i emisję.

Po osiągnięciu tego punktu pipeline będzie gotowy na fog of war, pogodę, logistyczne heatmapy,
animowane zasięgi militarne oraz bardziej zaawansowane efekty walki bez kolejnej przebudowy
fundamentu renderera.
