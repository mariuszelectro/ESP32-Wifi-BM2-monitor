# MyBM - niezalezny nadajnik napiecia

Osobny projekt ESPHome 2026.4.1 dla tej samej plytki ESP32-C6.
Nie korzysta z BM2, GATT, skanera, stycznika, ntfy ani pozostalych GPIO projektu Wallbox.

## Sprzet

- GPIO0: ADC napiecia przez dzielnik **6:1**: 1 Mohm od mierzonego plusa
  do GPIO0 oraz 200 kohm od GPIO0 do masy.
- GPIO9: USER, zwierany do masy, wewnetrzny pull-up.
- GPIO15: LED aktywna stanem niskim.
- Temperatura: czujnik wewnetrzny ESP32; nie temperatura akumulatora/otoczenia.

Przykladowy dzielnik dla pomiaru akumulatora 12 V:

```text
plus mierzonego napiecia --- 1 Mohm ---+--- GPIO0
                                      |
                                   200 kohm
                                      |
masa ---------------------------------+
```

Rezystory 0,1%, 100 nF pomiedzy GPIO0 i masa. Przy 12 V na akumulatorze ADC
otrzymuje okolo 2 V, a przy 16 V okolo 2,67 V. Prad dzielnika to okolo
13,3 uA przy 16 V (okolo 10,4 uA przy 12,5 V), rowniez w deep sleep.
Kondensator zastepczo zasila ADC podczas krotkich pomiarow; dokladnosc i czas
ustalania trzeba zweryfikowac multimetrem na docelowej plytce.
Firmware usrednia 16 odczytow ADC na pojedynczy pomiar.

**Sam dzielnik nie jest zabezpieczeniem instalacji samochodowej.**
Nalezy zapewnic zabezpieczenie zasilania i toru ADC przed przepieciami,
odwrotna polaryzacja i przekroczeniem dopuszczalnego napiecia pinu.
Elementy ochronne, przetwornice 12 V -> zasilanie plytki i bezpiecznik
nalezy dobrac do warunkow instalacji; nie podlaczac 12 V do zasilania 3,3/5 V plytki.
Wspolna masa jest wymagana. Maksimum 30 V w kodzie jest walidacja danych,
**nie** deklaracja dopuszczalnego napiecia elektrycznego.

Kalibracja: `napiecie = ADC[V] * 6 * factor`, domyslny factor 1,033.
Przy pomiarze okolo 2 V na GPIO0 i okolo 12 V na wejsciu wynik powinien
wynosic okolo 12,4 V. Korekta:
`nowy factor = stary factor * napiecie multimetru / wskazanie MyBM`.
Trzy miejsca po przecinku w reklamie nie oznaczaja dokladnosci 1 mV.

## Cykl normalny

1. Timer wybiera najwczesniejszy termin: USER, pomiar lub BLE.
   WiFi pozostaje wylaczone poza portalem.
2. Pomiar ADC i temperatury tylko po nadejsciu osobnego terminu pomiaru;
   wtedy aktualizowana jest historia i ostatnia poprawna probka w RTC.
3. Ustawienie surowej reklamy i nadawanie przez domyslnie 50 ms.
4. Po zamknieciu reklamy BLE jest wylaczane, a ESP32-C6 wchodzi w deep sleep
   do nastepnego terminu. Nowa reklama rozpoczyna sie co sekunde
   (domyslnie); jesli start i nadawanie przekrocza ustawiony odstep, cykl nie
   jest nadrabiany, a faktyczny odstep bedzie dluzszy.
5. Osobny interwal sprawdzania USER (1-3600 s, domyslnie 1 s) wybudza CPU
   bez uruchamiania BLE ani ADC, chyba ze nadszedl tez ich wlasny termin.
   GPIO9 nie jest obslugiwanym pinem wybudzenia ESP32-C6 z deep sleep.
6. Pomiar ADC i temperatury ma liste: 1, 2, 5, 10, 30, 60, 120 s
   (domyslnie 1 s), niezalezna od interwalu BLE i USER. Jesli reklamy sa
   czestsze niz pomiary, nadaja ostatni poprawny wynik zachowany w RTC.
   Bledna probka nie zastepuje poprawnej; bez poprawnej probki reklama jest
   pomijana z komunikatem bledu. Samo nadawanie nie dopisuje punktow historii.
   Nowe ustawienia sa zapisywane w NVS; migracja zachowuje dotychczasowe pola.

Po bledzie asynchronicznym BLE firmware probuje ponownie uruchomic stos.
Jesli awaryjne zamykanie BLE nie zostanie potwierdzone w ciagu 5 s,
urzadzenie wykonuje restart.

Reklama legacy jest **niepolaczalna i nieskanowalna** (`ADV_NONCONN_IND`),
na trzech kanalach. Pole Complete Local Name (`0x09`) zawiera np.
`MyBM:12.456 0000000001`. Po napieciu i jednej spacji jest zawsze dziesiec cyfr
licznika serii reklam RTC (0000000000-4294967295, potem 0000000000). Tylko zakonczona emisja
serii BLE zwieksza licznik; wybudzenie tylko USER lub ADC go nie zmienia.
Pierwsza seria po resecie ma numer 0000000001. Nie jest to licznik odebranych
pakietow ani zdarzen RF. Zwykly reset zeruje licznik. Najdluzszy payload
zajmuje 27 z 31 bajtow reklamy. Odbiornik powinien czytac napiecie przed spacja.
Nazwa jest w samej reklamie, nie w scan response.
Nie ma serwera GATT, parowania ani zapisu do innego urzadzenia BLE.
Odbiornik powinien identyfikowac nadajnik po MAC / prefiksie `MyBM:`,
nie po stalej pelnej nazwie. Skaner moze buforowac nazwy; aktualne bajty
reklamy sa zrodlem danych.

Krotkie okno 50 ms i interwal BLE 100 ms celuja w jedno zdarzenie reklamowe
na cykl. To nadal nie gwarantuje dokladnie jednego odebranego pakietu:
zdarzenie legacy moze wyslac pakiety na kilku kanalach, a BLE advertising nie
ma potwierdzenia odbioru.
Co dziesiata zakonczona seria powoduje krotki blysk LED.
Zmiana docelowego odstepu start-start zmienia tez rzeczywisty odstep miedzy seriami i blyskami.

Ustawiony czas 1000-60000 ms to docelowy **odstep miedzy startami cykli**.
Stos BLE jest wylaczany na czas deep sleep i uruchamiany ponownie na kolejnym
cyklu; koszt zimnego startu BLE ogranicza minimalny osiagalny odstep.
Ustawienia interwalu i okna reklamy sa odczytywane z NVS przy kazdym
uruchomieniu, a zapis z portalu jest synchronizowany do flash przed odpowiedzia
HTTP. Ustawienie okresu i okna nadawania wplywa na udzial czasu aktywnego i
deep sleep, a tym samym na sredni pobor mocy. Portal WiFi nie jest trybem
niskiego poboru. To ustawienie nie gwarantuje nizszego poboru na kazdej plytce. Porownaj
amperomierzem sredni prad pelnego cyklu przed i po zmianie; regulator, LED
zasilania, USB-UART oraz stale podlaczony dzielnik moga dominowac w bilansie.

## USER i portal

Po kazdym zwyklym resecie pierwszy pomiar ADC i temperatury jest wykonywany
po inicjalizacji czujnikow, przed wlaczeniem portalu WiFi, niezaleznie od
ustawionego interwalu. Poprawna probka trafia do RTC i historii (z zachowaniem
usredniania); bledny odczyt jest jawnie raportowany. Kolejne pomiary stosuja
zapisany interwal. Samo wybudzenie z deep sleep nie wymusza pomiaru.

Prog podtrzymania portalu WiFi jest zapisywany w NVS (domyslnie 13,3 V,
zakres 0,5-30 V). Poprawny pomiar napiecia po kalibracji powyzej progu
uruchamia WiFi/WWW i blokuje deep sleep. Pomiary nadal stosuja swoj interwal.
Pierwszy poprawny pomiar rowny lub nizszy od progu rozpoczyna opoznienie
powrotu: co najmniej 60 s, do kolejnego poprawnego pomiaru potwierdzajacego
spadek. Przy interwale 120 s powrot zostanie zatem potwierdzony po 120 s.
Ponowny wzrost powyzej progu kasuje odliczanie. Bledne probki nie zwalniaja
podtrzymania. Po zwolnieniu WiFi jest wylaczane i wraca harmonogram deep sleep,
chyba ze nadal trwa niezalezny czas portalu po resecie lub USER.
Przycisk Zakoncz portal nie omija podtrzymania napieciem (HTTP 409).
Migracja zachowuje wszystkie wczesniejsze ustawienia.

Po wlaczeniu zasilania, wgraniu firmware lub zwyklym restarcie portal
uruchamia sie automatycznie na zapisany czas 5-20 minut (domyslnie 5), tak jak po USER.
Powrot do cyklu snu wymaga jednoczesnie resetu ESP_RST_DEEPSLEEP,
wybudzenia timerowego i poprawnego stanu RTC. Sam zapisany powod wybudzenia
nie moze pominac portalu po zwyklym resecie. Aktywny portal blokuje wejscie
w deep sleep; mozna go zakonczyc jawnie przyciskiem WWW.
GPIO9 nie moze bezposrednio wybudzic ESP32-C6 z deep sleep. Timer wybudza CPU
wedlug ustawionego odstepu sprawdzania USER lub wczesniejszego terminu
pomiaru albo BLE. Przy kazdym wybudzeniu sprawdzany jest tez USER,
ale sam przycisk nie powoduje uruchomienia BLE. Trzymaj przycisk do
najblizszego sprawdzenia i pusc w ciagu 3 s po zapaleniu LED. Dluzsze trzymanie
jest blokowane do wykrycia puszczenia.

Kazdy odczyt USER (takze przy pomiarze ADC, reklamie i podczas aktywnej
petli) przesuwa jego kolejny termin o zapisany interwal. Nie ma osobnej
pobudki USER, jezeli wczesniej wykona sie pomiar lub BLE i sprawdzi przycisk.
Np. BLE co 3 s i USER co 5 s oznacza sprawdzanie USER przy BLE, bez dodatkowych
wybudzen co 5 s. USER co 2 s nadal wymaga dodatkowego sprawdzenia, gdy do ADC
i BLE pozostaje wiecej niz 2 s. Interwaly ADC/BLE nie sa przy tym zmieniane.

Po wykryciu aktywuje sie na **zapisany czas 5-20 minut** AP WiFi `MyBM`, haslo `mybm-setup`.
**Przed wgraniem zmien haslo AP w mybm.yaml.**
Otworz `http://192.168.4.1/`. Strona dziala lokalnie bez Internetu,
CDN i aplikacji Home Assistant. To portal WWW przez AP, bez automatycznego
przekierowania DNS systemowego captive portal.
Podczas pracy portalu urzadzenie pozostaje aktywne na obsluge WiFi i WWW;
napiecie i temperatura sa mierzone wedlug wybranego odstepu (1-120 s), bez BLE.
Wykresy stosuja zapisany interwal historii i usrednianie, a podglad kalibracji
korzysta z aktualnych pomiarow ADC. Po uplywie ustawionego czasu WiFi jest wylaczane,
urzadzenie przechodzi w deep sleep wedlug harmonogramu i wraca do cykli BLE.
Przycisk nie przedluza aktywnego portalu.

Zakladki:
- **Widok**: aktualne napiecie, temperatura ESP, dwa wykresy.
- **Ustawienia**: factor 0,5-1,5; docelowy odstep start-start 1000-60000 ms;
  okno nadawania 50-500 ms; interwal historii 1-3600 s;
  usrednianie 1-60 probek na punkt.
- Niezaleznie: sprawdzanie USER 1-3600 s oraz lista interwalu pomiarow
  1, 2, 5, 10, 30, 60, 120 s. Oba nowe ustawienia domyslnie 1 s.
- Czas portalu 5-20 minut, domyslnie 5. Zmiana czasu po Zapisz uruchamia
  nowy pelny okres. Przycisk Przedluz portal liczy pelny zapisany czas od
  klikniecia (nie dodaje go do pozostalego czasu).
- Zakoncz portal konczy dostep WWW i przywraca cykl snu po okolo 1 s na
  wyslanie odpowiedzi HTTP oraz zakonczeniu trwajacej operacji BLE.
- Aktualizacja zachowuje ustawienia ze starszej wersji bez pola czasu portalu.

Obok ADC factor jest podglad napiecia z ostatniego pomiaru przeliczonego
przez aktualnie wpisany factor, takze przed zapisem. Edycja nie zmienia
kalibracji urzadzenia do nacisniecia Zapisz; podglad odswieza sie z pomiarami.

Historia: 400 punktow RTC, tylko napiecie i temperatura oraz czas probki.
Przy kazdym wykresie licznik wszystkich zapisanych punktow rosnie takze
po zapelnieniu, np. 40000/400; osobno pokazana jest zajetosc ringa 400/400.
Licznik dotyczy punktow po usrednianiu, nie reklam ani surowych odczytow ADC.
Przetrwa deep sleep razem z historia i zeruje sie przy zwyklym restarcie.
Domyslnie interwal 1 s i bez usredniania (okolo 6 min 40 s przy cyklu 1 s).
Probka historii jest przyjmowana przy pierwszym pomiarze po uplywie interwalu;
nie da sie uzyskac wiecej probek niz cykli pomiarowych.
Usrednianie nie zmienia napiecia w reklamie, tylko punkty wykresu.
Zmiana factor/interwalu/usredniania zeruje niedokonczona srednia, zachowuje
stare punkty. Historia i srednia przezywaja deep sleep, nie zwykly restart
ani utrate zasilania. Ustawienia zapisane z portalu pozostaja we flash.
Normalne cykle pomiarowe nie zapisuja historii ani licznikow do flash.

## Budowanie i wgrywanie

### Opcja kompilacji: light sleep z zachowanym BLE

Stala `mybm_light_sleep` na poczatku `mybm.yaml` steruje makrem
`MYBM_LIGHT_SLEEP` i stala C++ `mybm::LIGHT_SLEEP_MODE`:

- `"0"` (domyslnie): dotychczasowe wylaczenie BLE i deep sleep.
- `"1"`: eksperymentalny automatyczny light sleep ESP-IDF z tickless idle,
  zarzadzaniem czestotliwoscia CPU i BLE modem-sleep. Po serii zatrzymywana
  jest reklama, ale host Bluedroid i kontroler pozostaja uruchomione.
  Koszt startu BLE (w tym oczekiwanie 200 ms ESPHome) wystepuje przy pierwszej
  serii po resecie lub po portalu, nie przy kazdej kolejnej reklamie.

Nadajnikiem nadal jest `esp32_ble` ESPHome i API reklam GAP ESP-IDF,
bez serwera/klienta GATT, skanera ani polaczen. Nie jest to migracja na
NimBLE ani nowy stos radiowy. Uzywamy automatycznego light sleep, a nie
`esp_light_sleep_start()` przy aktywnym kontrolerze BLE.
W czasie oczekiwania glowna petla oddaje CPU do najblizszego terminu
USER/ADC/BLE (z obsluga LED i watchdog); ESP-IDF wybiera faktyczny czas
snu wedlug blokad zasilania oraz gotowosci pozostalych zadan.

Reset nadal uruchamia portal na zapisany czas i pierwszy pomiar ADC/temp.
Portal, prog napiecia i minutowe opoznienie, interwaly, usrednianie,
historia oraz 10-cyfrowy licznik reklam pozostaja takie same. Dane nadal sa
w strukturach RTC, a ustawienia w NVS. W light sleep program nie startuje
od nowa: terminy radiowe sa sprawdzane w petli. Otwarcie portalu wylacza
BLE tak jak dotychczas; po jego zakonczeniu wraca harmonogram.
Podczas portalu automatyczny light sleep jest wylaczony i CPU pracuje
na 160 MHz; oszczedzanie jest wlaczane ponownie po zakonczeniu portalu.

Test light sleep bez zmiany domyslnej stalej (z katalogu projektu):

```powershell
python -m esphome -s mybm_light_sleep 1 run mybm.yaml --device COM5 --no-logs
```

Powrot do deep sleep:

```powershell
python -m esphome -s mybm_light_sleep 0 run mybm.yaml --device COM5 --no-logs
```

Oba polecenia kompiluja przed wgraniem, aby nie pomylic ostatnio zbudowanego
wariantu. Kazdy wariant ma osobny katalog budowania: `.esphome\build\mybm-0`
oraz `.esphome\build\mybm-1` w katalogu projektu.
Samo `upload` nie przebudowuje firmware. Nie wgrywac podczas
testu automatycznie. Porownac prad spoczynkowy, energie calej serii i okres
reklam przy tych samych ustawieniach; sprawdzic USER, portal i odbior reklamy
po kilku cyklach. Light sleep moze miec wyzszy prad spoczynkowy niz deep sleep
i nie gwarantuje oszczednosci przy dlugich interwalach. USB/JTAG moze zmieniac
bilans oraz blokowac sen; do odzyskania portu sluzy BOOT + RESET.

Jesli USB/COM znika przez deep sleep, przytrzymaj USER/BOOT (GPIO9),
nacisnij i pusc RESET, po czym pusc USER/BOOT. Bootloader ROM nie uruchamia
firmware ani deep sleep. Sprawdz ponownie numer COM i wgraj firmware;
nie kasuj calej pamieci flash (ustawienia pozostaja w NVS).

Z katalogu `C:\esphome`:

```powershell
python -m esphome compile mybm\mybm.yaml
python -m esphome upload mybm\mybm.yaml --device COM5
```

Zmien COM5 na port plytki. Pierwsze i kolejne wgrywanie przez USB;
celowo nie dodano OTA, sieci domowej ani zewnetrznych uslug.
Artefakty znajduja sie w `mybm\.esphome\build\mybm-0\.pioenvs\mybm`
(deep sleep) lub `mybm\.esphome\build\mybm-1\.pioenvs\mybm` (light sleep).
Dotychczasowy projekt Wallbox nie jest modyfikowany.

## Weryfikacja

### Pomiar narzutu energetycznego (wersja bazowa)

Diagnostyka zapisuje w RTC ostatni ukonczony
cykl i licznik osobno dla USER, pomiaru bez BLE i BLE (z pomiarem, jesli nalezny).
Wybudzenia zawierajace portal oraz nieudane cykle radiowe nie nadpisuja wynikow.
Podglad jest tylko w `/api/state` w polu `timings`, czasy sa w mikrosekundach.
Sekcja diagnostyczna czasow zostala usunieta ze strony WWW.
Zwykly reset zeruje wyniki, dlatego do odczytu po serii nalezy uzyc USER.

Mierzone sa: czas do konca `boot()`, laczny czas pomiarow, czas do zadania
wlaczenia BLE, start BLE, konfiguracja/start reklamy, okno reklamowe,
zatrzymanie reklamy i zamkniecie BLE. Czasy faz zawieraja opoznienie petli
i obslugi zdarzen, nie sa czasem emisji pojedynczego pakietu RF.
Czas aktywnosci jest od startu timera ESP-IDF do wywolania deep sleep:
nie obejmuje wczesniejszego ROM/bootloadera ani koncowej procedury usypiania.
Nie jest pomiarem energii; pelny impuls i jego amplitude mierzy oscyloskop.

Do porownania trzech rodzajow wybudzen mozna tymczasowo ustawic USER 2 s,
pomiary 10 s i BLE 30 s. Zamknac portal przy napieciu ponizej progu,
odczekac kilka cykli, nastepnie otworzyc przez USER (bez RESET).
Zapisac odczyty API i oscyloskopu, potem przywrocic wlasne interwaly.
Firmware nie zmienia automatycznie tych ustawien.

### Pierwsza optymalizacja wybudzen

- Odczyt USER i decyzja o ponownym snie sa na priorytecie 700, przed
  inicjalizacja czujnikow i sieci. Wybudzenie tylko USER wraca od razu
  do snu, bez czekania na zakonczenie konfiguracji pozostalych komponentow.
- Usunieto dodatkowe `delay(10)` z interwalu 10 ms; obsluga BLE nie jest
  juz celowo blokowana po kazdym wykonaniu lambdy.
- Wylaczono komunikaty bootloadera i powtarzany log zwyklego wybudzenia timerem.
- Wlaczono `CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP`: przy powrocie
  z deep sleep bootloader nie czyta ponownie calego obrazu firmware w celu
  sprawdzenia sumy/SHA. Zwykly reset i wlaczenie zasilania nadal weryfikuja
  obraz. Jest to kompromis: brak ponownego sprawdzenia integralnosci flash
  przy kazdym timerowym wybudzeniu. Projekt nie uzywa Secure Boot ani OTA;
  nie laczyc tej opcji z Secure Boot ani zmiana partycji OTA przed snem.
  Wgrac tez nowy bootloader (standardowe ESPHome upload przez USB robi to).

Start po resecie, czas portalu, prog napiecia, interwaly USER/ADC/BLE
i okno nadawania pozostaja bez zmian. Czasy RTC/API sa nadal dostepne,
ale wynik energetyczny i skrocenie pelnego impulsu nalezy potwierdzic
oscyloskopem; nie sa gwarantowane przez kompilacje.
W ESPHome 2026.4.1 start BLE nadal zawiera stale `delay(200)` w `ble_setup_()`.
Ta optymalizacja go nie usuwa i nie modyfikuje biblioteki ESPHome.

Test modelu (C++17): `tests\model_test.cpp`. Obejmuje dokladne bajty reklamy,
zakresy ustawien, ring 400 punktow, interwaly i usrednianie. Test nie
zastepuje pomiaru poboru ani odbioru radiowego.

Na plytce nalezy sprawdzic:
1. Napiecie ADC wobec multimetru, kalibracje i brak przekroczenia zakresu ADC.
2. Nazwe `MyBM:xx.xxx` w surowych reklamach oraz brak mozliwosci polaczenia GATT.
3. Rzeczywisty okres oraz zmiane ustawien po kilku cyklach.
4. Blysk co dziesiata serie, USER/release, czas portalu 5-20 minut,
   przedluzenie, zakonczenie i powrot WiFi do wylaczenia.
5. Historie po deep sleep, interwaly i srednie oraz reset historii po restarcie.
6. Sredni pobor pradu w pracy normalnej oraz podczas pracy portalu.
