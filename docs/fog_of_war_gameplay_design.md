# Fog of war — projekt mechaniki i wiedzy AI

## Stan pilota

Fog ma teraz dwie świadomie rozdzielone warstwy. Renderer nadal tworzy miękką,
radialną maskę tylko do obrazu, natomiast symulacja odtwarza deterministyczną
widoczność kafelkową z tych samych źródeł. Nie odczytujemy tekstury FBO w logice
gry: jej wynik zależałby od kamery, rozdzielczości i odwróconej osi Y.

Gdy opcja `Fog of War (pilot)` jest włączona, człowiek może rozpocząć budowę
wyłącznie wtedy, gdy **cały footprint** budynku jest obecnie widoczny. Zasada
działa zarówno dla podglądu budowy, jak i autorytatywnej komendy; wyłączenie i
ponowne włączenie opcji nie korzysta ze starego cache'u. AI celowo zachowuje
pełną wiedzę o mapie jako bonus poziomu trudności.

Aktualne promienie pilota: Headquarters `3072 px` (96 kafli), zwykły budynek
co najmniej `640 px` (20 kafli), a maszerująca armia `640 px`. Są używane przez
render i przez logikę, aby miękka granica obrazu odpowiadała regule budowania.

Ten przełącznik pozostaje lokalnym pilotem. Przed docelowym multiplayerem
powinien stać się replikowaną opcją sesji hosta, aby wszystkie klienty miały
identyczne zasady, a nie tylko identyczny efekt wizualny.

## Docelowy model informacji

Każdy gracz ma deterministyczny `FogOfWarState` o rozmiarze mapy. Dla każdego
kafla przechowuje dwa bity:

- `visibleNow` — odsłonięty w bieżącym ticku;
- `explored` — był kiedykolwiek odsłonięty.

Widoki:

- nieodkryty: brak informacji i prawie czarny fog;
- odkryty, lecz niewidoczny: teren pozostaje widoczny pod ciemniejszą zasłoną,
  ale nie pokazuje bieżących budynków, jednostek ani transportów przeciwnika;
- aktualnie widoczny: pełne informacje.

Stan aktualizujemy na fixed ticku po ruchu jednostek, a nie w rendererze.
W ten sposób ten sam tick ma identyczny rezultat w SP, host/klient MP, zapisie
i checksumie.

## Jak gracz odkrywa zasoby

Pierwsza wersja mechaniki powinna stosować kilka prostych, nakładających się
źródeł widoczności:

| Źródło | Zasięg i trwałość | Rola |
|---|---|---|
| Headquarters | duży, stały | bezpieczny start i ocena najbliższych złóż |
| Zwykły budynek | mały, stały | rozwój infrastruktury stopniowo odsłania otoczenie |
| Maszerująca armia | średni, tylko `visibleNow` | rekonesans wojskowego traktu i alarm przed przeciwnikiem |
| Survey Camp / Outpost | duży, stały, tani | celowa ekspansja w poszukiwaniu zasobów |
| Radar / Watchtower (później) | bardzo duży, stały po technologii | strategiczne pokrycie i ostrzeganie |

Najlepszym pierwszym narzędziem do szukania surowców jest **Survey Camp**:
lekki, tani budynek możliwy do postawienia tylko na już odkrytym terenie. Ma
niski koszt utrzymania, nie produkuje zasobów i odsłania szeroki promień. Daje
graczowi świadomy łańcuch decyzji: HQ → odkryte obrzeże → Survey Camp → znalezione
złoże → droga → kopalnia. Nie wymaga jeszcze swobodnie poruszającej się jednostki
zwiadowcy, której obecny system marszu po wojskowym trakcie nie obsługuje.

W dalszej kolejności można dodać jednostkę Scout/Sapper poruszającą się poza
traktem. Jej rozkazy powinny odsłaniać tylko odwiedzony pas mapy, a nie znać
położenia złóż przed przybyciem.

## AI: obecny stan

AI **nie respektuje fog of war**. Przykładowo `AIActions::FindBuildAnchor`
czyta bezpośrednio `world.GetTileMap()` i ocenia rzeczywiste typy oraz
bogactwo kafli w oknie poszukiwań. Nie ma dziś pojęcia `KnownTile`, więc przy
włączonym fog gracz jest ograniczony wizualnie, a AI nadal zna mapę.

## Plan uczciwego AI

1. Dodać `FogOfWarState` do gracza i API `IsVisible`, `IsExplored`,
   `GetKnownTile`. W tym kroku UI może już blokować budowanie na nieodkrytym
   terenie.
2. Zmienić akcje AI wybierające złoża (`FindBuildAnchor`, diagnozy ekspansji)
   na iterowanie wyłącznie po znanych kaflach. Pełny `TileMap` pozostaje
   dostępny systemowi symulacji, ale nie plannerowi AI.
3. Gdy AI potrzebuje surowca, ale nie zna odpowiedniego złoża, wybiera
   frontier: odkryty kafel graniczący z nieodkrytym obszarem. Buduje Survey
   Camp lub wysyła zwiad w deterministycznej kolejności oceniania.
4. Frontier score nie może czytać ukrytego typu terenu. Może używać tylko
   odległości od logistyki, bezpieczeństwa, znanej biomowej informacji i
   stabilnego tie-breakera po id kafla.
5. Dopiero potem filtrować snapshoty MP: klient otrzymuje szczegóły tylko
   aktualnie widocznych wrogich obiektów. To jest konieczne, bo ukrycie ich
   wyłącznie shaderem nie daje ochrony przed odczytem danych po stronie klienta.

Ten plan daje AI podobne możliwości odkrywania co graczowi, zachowuje
determinizm lockstepu i pozwala wprowadzać radar jako następne źródło
widoczności bez przebudowy modelu.
