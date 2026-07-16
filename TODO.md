## **TODO**

**Lista rzeczy do poprawy/reworku/zdebugowania po migracji na Tower Defense oraz dalszy plan pracy**

1) ✅ **DONE (2026-07-16, commit `AI-rework(etap-0)`)** — barracks requestuje dokładnie koszt
   następnej jednostki (odejmując zasoby w drodze), jeden po drugim (strict FIFO), a label
   "Waiting for resources" odświeża się natychmiast po dostawie (także dla wpisów za frontem).
   Testy regresyjne w `tests/BattleUnitTests.cpp`.

   Oryginalna treść: fix kolejkowani barracks: jest prawie idealnie - jednostki wchodzą do kolejki i czekają na dostawę surowców by rozpocząć szkolenie. Natomiast nie do końca płynnie to działa. Po pierwsze - barracks powinno requestować dokładnie tyle zasobów ile trzeba, nie "na zapas" tak jak to teraz robi. Czyli załóżmy scenariusz: jeżeli zostało zamówione 3 swordsmanów, to barracks requestuje zasoby na 3 swordsmanów, jeden po drugim. Druga sprawa - jeżeli surowce dotarły, dla tych zakolejkowanych, można odświeżyć ich label "waiting for resources" - no bo w końcu już przyszły. Aby to zrealizować, przy każdym odebranym resource, należy zrobić check ile surowców już jest w buforze, i jeżeli surowce na następną jednostkę w kolejce już są - można ją oznaczyć jako normalnie oczekującą (nie waiting for resources). Jak to poprawimy to będzie cacy.

2) ✅ **DONE (2026-07-16, commity `AI-rework(etap-1..5)`)** — czystka wykonana (stary 3-tier
   model + DiplomaticState usunięte, save 26→27; aktuatory zachowane w `ai/AIActions.*` zgodnie
   z pkt "akcje są już zakodowane — reuse"), nowy deterministyczny model utility
   (`UtilityAIModel`: sensing → utility 5 potrzeb → akcja; kompozycja rosteru wg postawy;
   `IsConnectedToRoadNetwork` na LogisticsComponent; trudność = grant startowy + seedowany szum
   RNG). Pełny opis: `docs/td_ai_design.md`. Poza zakresem v1 (świadomie): kontry na skład wież
   (jeden typ wieży/ataku dziś), dodatkowe budynki startowe na Hard, telemetria strat.
   **Zalecany ręczny playtest balansu** (`.\build_and_run.ps1`).

   Oryginalna treść: AI - to jest duży temat. 

- po pierwsze: czystka. Wypierdalamy z bazy kodu wszystko co związane z AI. Bez pytania, oczywiście nie naruszając obecnej struktury gry. Zostawiamy IController i te podstawowe interfejsy wymagane dla kompatybilności, ale wszystkie behaviory do wyjebania, enumy, classy. Totalnie będziemy przerabiać ten model.

- po drugie: analiza nowej strategii AI. Odchodzimy od modelu osi priorytetów gdyż w konwencji TD jest to niepraktyczne. Celem nadrzędnym AI jest deployowanie jak największej ilości jednostek na tor. W tym celu, będzie ono poszukiwało surowców by utrzymać koszty rekrutacji - odpowiedni manpower, wyżywienie, surowce. Oraz warstwa defensywna: wieże obronne i amunicja.

- zmierzałbym w kierunku utility functions. Mierzymy wskaźniki ile czego mamy: ile jest jednostek na torze moich / jednostek przeciwnika. Ile mam surowców obecnie / ile surowców potrzebuję by zdeployować X jednostek. Ile HP ma moja baza? Mało? Muszę poprawić defensywę. Dużo? Mogę skupić się na ataku. Który focus obecnie będzie najbardziej korzystny? Czy mam zapas surowców na prowadzenie badań w uniwersytecie? (i uniwersytet). Tworzymy zatem takie drzewko decyzyjne które będzie obliczało najbardziej korzystną w danym momencie akcję. Wykorzystujemy telemetrię do badania czy łańcuchy produkcyjne się spinają (o tym mówiłem - supply musi być >= consumption w kosztach stałych czyli sustainie obecnej produkcji), jak wygląda sytuacja na torach jednostek itd. Każdy budynek musi być połączony do sieci logistycznej, tak by był połączony z HQ/magazynem. Dodałbym check do logistic componentu typu IsConnectedWithRoadSystem() który sprawdza czy budynek jest podpięty. Domyślnie AI musi się upewnić że każdy jest podłączony!!

- w takim wypadku poziomy trudności AI są mniej bolesne do implementacji. Tworzymy jeden model, deterministyczny. Poziomy trudności będą po prostu dawać przewagę przeciwnikowi: więcej surowców i budynków na start. Każdą decyzję/wskaźnik można zsumować z RNG komponentem, który będzie imitował "złe" decyzje. Na niższych poziomach trudności czynnik RNG będzie troszkę większy by zasymulować gorszego gracza.

- AI powinno dobierać kompozycję swojego rosteru ofensywnego na podstawie sytuacji na torach. Jeżeli wróg jest w ofensywie, powinien preferować jednostki defensywne: z wysokim soft atakiem / defense. Jeżeli jest w defensywie: odwrotnie - wywierać presję szybkimi jednostkami czyszczącymi tor a za nimi jednostki oblężnicze do ataku HQ. Do tego można by analizować skład wież przeciwnika: np. jeżeli dominuje dany typ ataku, próbować go kontrować innymi jednostkami albo badaniami/ulepszeniami dającymi premie np. resistance / armor / movement speed.

- podsumowując: Można podzielić algorytm na etapy: checki typu AmIAttacked(), CurrentResourceBalance() itd. które będą kształtować decyzje, czyli konkretne akcje. Konkretnych akcji mamy niedużo, w zasadzie: wybuduj budynek, zbuduj drogę, rozpocznij focus, rozpocznij badanie, zrekrutuj jednostkę, deploy jednostek. 6 akcji. Te akcje są jak mniemam już zakodowane więc można je zreusować (oczywiście czystka wszystkich niepotrzebnych elementów jest aktualna)

**działaj etapami!** robimy to step by step. Rozpocznij od analizy tej instrukcji i zaproponuj też swoje rozwiązania. Celem jest stworzenie AI do gry tower defense z elementem produkcyjno-logistycznym a la factorio. AI dąży do zniszczenia gracza, atakując go swoimi jednostkami - w tym celu musi rozwinąć swój przemysł i zrekrutować odpowiednią ilość żołnierzy.



