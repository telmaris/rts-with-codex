## TODO List of tasks

######

1) ogarnąć w AI controlerze, równolegle AIStrategicGoal i AIStrategicPlan - wtf? czy to legacy system został?

2) wyjebać building z walki!! ostatecznie

3) 

4) Dodawać kontent! więcej itemów, budynków. 

5) warstwa audio, przede wszystkim sound FX - np. klik myszy, muzyka w tle itp.

opracuj czystą strategię przerobienia systemu zaopatrzenia, bo został już tak spierdolony że trzeba go zrobić fundamentalnie od nowa. Lista instrukcji w kolejności co wdrażamy: 1) każdy resource ma posiadać swoją KATEGORIĘ/TAG. - z poziomu Resource.h. Dzięki temu umożliwiamy budynkom interakcję z szerszą gamą przedmiotów, np. możemy zunifikować miecze, armor, metale itd. Możemy dawać bonusy do całych kategorii przedmiotów np. +10% Metal production - na wszystkie metale, lub +5% sword power - by zwiększyć premie wszystkich typów mieczów. Proszę zmodyfikować wszystkie klasy przetwarzające surowce by dopasowały się do ww. wytycznych. 2) cykl życia zaopatrzenia: to spierdoliłeś koncertowo więc słuchaj; cykl ma wyglądać tak (jako przykład podam miecz): produkcja miecza -> miecze (wszystkie typy) trafiają do supply hubu -> hub tworzy z nich weapon supply package zawierający określoną ilość konkretnego uzbrojenia (czyli pamięta skład przedmiotów z których składała się paczka) -> weapon supply package jest transportowany drogą do budynku militarnego typu wieża/fortress itp. ORAZ HQ (awaryjnie) -> budynek militarny sobie rozkłada paczke na liczby, czyli prowadzi rejestr co mu przyszło, co posiada i na co ma zapotrzebowanie -> rozprowadza te zasoby do jednostek w pobliżu, które "subskrybują" dostawy od niego (dywizje sobie same obliczają, kto jest najbliżej) -> dywizja oblicza swoją siłe na podstawie sprzętu który dotarł. Haczyk jest taki, że supply hub nie wie jaki sprzęt ładować - trzeba skoordynować SupplyBufferComponent (odpowiedzialny za odbiór i przetwarzanie paczek z supply) z SupplyPackageComponent (pakowacz, trzeba dopasować ten komponent żeby mógł ładować też materiały do paczki (wood + stone + tools) - od tego będzie osobny budynek) tak by mógł mu zakomunikować jaki typ sprzętu jest potrzebny - dostawa może przebiegać bezpośrednio stamtąd - nie ma sensu gromadzić paczek ze sprzętem wojskowym w normalnych magazynach, niech od tego będą supply huby.