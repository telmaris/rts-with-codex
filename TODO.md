## TODO List of tasks

######

### Znany dług: brak realnego resync-after-desync w MP (odkryty ETAP 1, 2026-07-12)

`GameSnapshot` jest czysto wizualny (tylko tekstury/kolory/typ budynku per tile), nie ma
`GameWorld::LoadFromSnapshot`, a "correction snapshot" wysyłany po wykryciu desyncu nigdy nie
jest realnie aplikowany do klienta — tylko do fallbacku rysowania mapy. Jeśli host i klient
kiedykolwiek faktycznie się rozjadą stanem ekonomicznym, klient zostaje rozjechany na stałe.
Sam checksum miał dodatkowo osobny bug (hashowanie `std::set<Building*>` w kolejności wg adresu
wskaźnika, niestabilnej między procesami) — to już naprawione. Pełny opis + kierunek naprawy:
`docs/tech_debt.md`, sekcja "🔴 Wysokie" → "Pełne snapshoty mapy przez TCP".

######

### DONE: Transformacja w grę Tower Defense z elementem PVP (2026-07-11 → 2026-07-12)

Zrealizowane w całości, ETAP 0–9 z `docs/tower_defense_rework_plan.md` (commity `08c8962`..`19fa980`
i dalsze etap-9 po nich). Finalne decyzje architektoniczne i mapa systemów: `docs/tower_defense_design.md`.
Oryginalny opis wymagań (13 punktów) — usunięty stąd, w pełni pokryty przez plan wykonawczy i jego
realizację; historia w git log / `docs/tower_defense_rework_plan.md`.

Otwarte po zakończeniu (nie blokują, do osobnej sesji):
- Cały focus tree (`assets/data/focuses.rtsdata`) czeka na przeprojektowanie przez użytkownika —
  obecnie zawiera tylko płaską "ściągawkę" (jedna technologia placeholder per `BalanceStat`).
- Drugi typ wieży / dodatkowe tiery jednostek — architektura już to wspiera (data-driven +
  komponenty), ale nikt jeszcze nie zaprojektował ich parametrów.
- Pre-istniejący, niezwiązany z tym pivotem bug: `assets/data/technologies.rtsdata` (drzewo SCIENCE)
  nie zawiera technologii "forestry"/"mathematics", której oczekuje 8 testów w `TechnologyTests.cpp`
  / `PlayerEconomyTests.cpp` / `ResearchCatalogTests.cpp` — prawdopodobnie SCIENCE trunk został
  przeprojektowany (algebra/trygonometria/...) bez aktualizacji testów. Szczegóły w `docs/tech_debt.md`.


