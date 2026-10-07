# Wyzwalacze początku Veldinu — PAL v2.00, V1

**New Game na Veldinie: scena przed sterowaniem TAK (INFERRED).** Odtworzona
ścieżka źródłowa uruchamia automatycznie scenę **4** w pierwszym przebiegu
Moby w trybie 0, przed aktualizacją bohatera. Sam prolog kończy się w trybie 0;
nie oznacza to pierwszej sterowalnej klatki. Poszczególne bramki i dane poniżej
są CONFIRMED; wniosek o całym przebiegu jest INFERRED, ponieważ nie wykonano
pełnego przeplotu wszystkich callbacków ani New Game w emulatorze/na PS2.

**Wysokość spawnu: wzór CONFIRMED, dokładna wartość źródłowa UNKNOWN.**
`Z_start = (R > 0 ? R : 31.43000030517578125)`, gdzie `R` jest wysokością
trafienia sondy opisanej w §1. Geometryczny kandydat z przygotowanej kolizji
to około `31.4266172586` (INFERRED); nie jest to potwierdzony wynik EE/VU.

CONFIRMED oznacza sprawdzone bajty, instrukcje i ich lokalny przepływ,
INFERRED — wniosek z połączonych ścieżek bez pełnego wykonania,
UNKNOWN — brakujący dowód wskazany wprost. Nie zmieniono kodu runtime.
Dowody: `local/forensics/veldin-start-triggers/`, w tym porównanie **4145**
słów listingu z obrazami. SHA-256 źródeł:

| Źródło | SHA-256 |
|---|---|
| ISO | `0f18a6c84cd8d727ec8c21000a236ed5ce2f279cbb8d0682fa9747199ef73260` |
| ELF | `17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b` |
| Nakładka Veldinu, s00 | `922fd06e1ac6c717cc7832a3b8ac7be5aaf178ebe1b7982ae4cd294af1876c79` |

## 0. Odpowiedzi na pytania #38

| # | Pytanie | Odpowiedź | Status | Adresy |
|---|---|---|---|---|
| 1a | Co czyta i zwraca `25a6d0` | Odcinek kolizji `(X,Y,Z+0.5)→(X,Y,0.01)` przez `1efff0`, flagi `a1\|2`; zwraca bezwzględne Z trafienia albo 0 | CONFIRMED | `25a6dc..25a73c`, `1f0098`, `1f0fd4..1f0fec` |
| 1b | Wzór wysokości startu | `Z = R > 0 ? R : Z_rekordu0` | CONFIRMED | `205368..205390` |
| 1c | Wartość liczbowa | Dokładne bity R nieznane; kandydat geometryczny ≈31.4266 | UNKNOWN / INFERRED | brak wykonania `1efff0` na żywym stanie |
| 2a | Instancja klasy 834 | Rekord 165, pvar 200 (160 B) | CONFIRMED | dane rekordu, `1ea9f8` |
| 2b | Admission w New Game | TAK, bez dereferencji selektora | CONFIRMED | `243260 → 2433d8`, `243414` |
| 2c | Zapis `13d498=1` w trybie 6 | TAK, w przebiegu ładowania `2657b8` | CONFIRMED (ścieżka lokalna) | `265880`, `2db29c`, `2db324→2db344`, `2db35c` |
| 2d | P14 | `2666a8` | CONFIRMED (wynika z 2c) | `24678c..24679c` |
| 3 | Scena trybu 2 w pierwszych klatkach | TAK, scena 4 (kontener 4 z 0–6), automat, bez przycisku | bramki CONFIRMED, całość INFERRED | `267290..267530`, `299b68`, `299ccc` |
| 4 | Role `2666a8`/`266670`/`266210`/`200ec8` | Żądania stanu kanałów strumieni audio; konfiguracja wyświetlania | zapisy CONFIRMED, nazwy INFERRED | §4 |
| 5 | `13d498` | Postęp `progress/primary/field-5` bajt 8; poza 7 regionami; 0 po resecie, 1 po P5 | CONFIRMED | `1a0610`, `20bed0`, `2db35c` |

## 1. Sonda `25a6d0` i `205278`

**CONFIRMED.** Argument `a0` jest wskaźnikiem pozycji, tutaj **Moby+0x10**
(`205354`, `205374`), a nie początkiem struktury Moby. `25a6dc..25a71c`
kopiuje wektor do dwóch buforów i wywołuje:

`1efff0(A=(X,Y,Z+f12), B=(X,Y,0.009999999776482582), flags=a1|2, 0, 0)`.

Dla `205278`: `f12=0.5`, `a1=0`, zatem `flags=2`. Sonda nie odczytuje
pól granic ani promienia przekazanej Moby. `1efff0` wykonuje zapytanie
odcinka do kolizji: czyta korzeń spod `173f40` (`1f0098`), testuje trójkąty
(`1f0558..1f068c`) i ma dalszą ścieżkę zarejestrowanej kolizji Moby
(`1f083c..1f0afc`). Flaga 2 pomija ścieżkę prymitywów w `1f0af4..1f0af8`;
nie dowodzi pominięcia całej kolizji Moby. Granice służą tam także selekcji
kandydatów, lecz zwracana wielkość nie jest granicą modelu.

**CONFIRMED — wynik i jednostki.** Brak trafienia daje `f0=0`
(`25a720..25a72c`); trafienie daje słowo float `173f68` (`25a734`), czyli
Z punktu przecięcia. Wewnętrzne współrzędne są mnożone przez `1024`
(`1f0010`, `1f0040..48`), a punkt wynikowy przez `1/1024`
(`1f0fd4..1f0fec`) przed zapisem wektora pod `173f60`. Wynik jest więc
bezwzględnym Z w jednostkach świata, nie odległością, Q6 ani przesunięciem
stóp. `20537c..205390` zapisuje go do Moby+0x18 tylko przy ścisłym `R>0`.
Nie dodaje wysokości kapsuły, promienia, `0.5` ani marginesu.

**CONFIRMED — wejście.** Jedyna klasa 0 to rekord **0**: pozycja
`(132.089996337890625, 115.480003356933594, 31.43000030517578125)`,
yaw `0.6627014875411987`. Polityka admission wynosi 0. Rekordowe +0x4c=0
pomija wcześniejsze dosunięcie w `2434bc`; dlatego wejściowe Z sondy
w `205278` pozostaje autorskie. Odcinek ma górny koniec
`31.93000030517578125`. Po sondzie `2053bc..2053cc` kopiuje pozycję do
bohatera i synchronizuje ją przez `208e98`.

**INFERRED — liczba geometryczna.** Pakiet v14, poziom 0, zawiera dokładnie
powyższy spawn `LevelBootstrapV1`, wysokość śmierci 27 oraz 39085 trójkątów.
Niezależne przecięcie przygotowanej siatki przy X/Y spawnu znajduje trzy
trójkąty warstwy 0: **8537 → 31.42661725860281**, **7654 →
31.424083901217664**, **6396 → 31.234375**. Najwyższy kandydat leży około
`0.003383046573` poniżej Z bootstrapu. To obliczenie geometrii w binary64,
nie substytut oryginalnego zapytania.

**UNKNOWN — dokładne Z i bity R.** Brakuje wykonania/odtworzenia `1efff0`
dla rzeczywistego stanu korzenia, filtrów i zarejestrowanych kolizji po
przeplatanym admission/post oraz oryginalnej kolejności VU z DIV/ACC.
Statyczna siatka i autorska pozycja są znane, ale nie zakwalifikowano całego
żywego zestawu wejść. Nie wolno wpisać ani `31.43`, ani kandydata powyżej jako
wiernego wyniku loweringu. Nierozstrzygnięte bity Z nie blokują testu z §3:
cały pionowy odcinek pozostaje z dużym zapasem w zasięgu 510.

## 2. Rekord 165, admission i przebieg ładowania

**CONFIRMED — instancja.** Jedyna klasa **834** ma rekord **165**, pozycję
`(171.42999267578125, 270.941986083984375, 29.256999969482422)`, rotację
`(0, -1.6619025468826294, 0)`, pvar **200**, rozmiar **160 B**.
Początkowo jedyne niezerowe słowa pvar to +0x2c=`1.2000000476837158`
i +0x60=`4`; reszta jest zerowa. To opis pól istotnych dla analizy,
nie załącznik danych gry. Rekordowe +0x74=`1` jest kluczem referencji;
nie utożsamiać go z indeksem rekordu ani końcowym indeksem żywej Moby.

**CONFIRMED — admission TAK.** Rekord +0x08=0 powoduje skok
`243260 → 2433d8` przy `s2=0`, następnie konstruktor w `243414`.
Selektor +0x04=`-1` nie jest tu dereferencjonowany. Dowód nie korzysta z
eksperymentu „296 przyjętych” i nie wymaga wartości prefiksu cache z F14.
Końcowy indeks w zwartej tablicy żywych obiektów pozostaje UNKNOWN bez
admission poprzedników; nie jest potrzebny do tej decyzji.

**CONFIRMED — callback podczas trybu 6.** Konstruktor `24f984` zeruje
Moby, w tym stan +0x20. Wpis klasy przy `1ea9f8` wskazuje `2db278`;
model klasy (ordinal 89) ma mode bits +0x44=0, a rekord dodaje +0x60=`0x20`
w `24353c..243554`. Ścieżka z niezerowym callbackiem nie ustawia bitu 2
w `24fa30..24fa40`; pozostałe operacje konstruktora nie dodają tego bitu.
Stan 0 i brak bitu 2 przechodzą budowę listy `2657f0..265828`.
`265880` wykonuje callback. Jego bramka `2db29c` odróżnia tylko tryb 2;
tryb 6 nie pomija inicjalizacji stanu 0.

Przy stanie 0 `2db30c` inicjalizuje skrypt przez `2676e8`. Dla początkowego
`13d498=0` gałąź `2db324 → 2db344` zapisuje pvar+0x2c=`255.0` w `2db350`,
następnie **`2db35c` zapisuje `13d498=1`**, `2db364` ustawia stan Moby=1,
a `2db36c` parametr +0x30=`255`. Ta ścieżka wraca bez `267290`.
Wcześniej `2db308` zapisuje halfword `141728=1` (rola UNKNOWN). Na stanie 0
`2db300` nie czyta trybu; tryb 2 zmienia jedynie bity +0x31/+0x34
(`2db29c..2db2c4`).
Tak więc **P5 ustawia flagę, ale nie startuje sceny**.

**CONFIRMED — P14 wybiera `2666a8`.** P14 występuje po tym przebiegu;
`24678c..24679c` czyta już 1 i wywołuje `2666a8`. `266670(0)` z `2467a8`
nie jest ramieniem tej świeżej inicjalizacji.

## 3. Scena po przejściu do trybu 0

**CONFIRMED — powiązanie skryptu.** `2435b4..2435c4` przekazuje klucz 1
do `24b1b0`; tabela prefiksów `1c47b8` ma dla poziomu 0 przedział `[0,2)`.
Wpis `1792b8+4` otrzymuje wskaźnik tej Moby (`24b1e0..24b1f0`).
`267618..267698` znajduje ją pod indeksem 1, a `26771c` pobiera
`*(1b19f8+4)=1b0bf0`.

Pierwszy rekord skryptu (28 B) ma: +4=`4` (scena), +6=`1` (następny
rekord), +8=`0` (rodzaj warunku), +0x10=`1` (bit automatyczny).
Reset postępu `13d6b8+16+12=13d6d4` daje 0 (tag 15, offset 28), więc
`267764` nie przeskakuje do dalszego rekordu. `26774c..2677a0` ustawia
pvar+0x24=`-1`, +0x56=`0`, +0x5c=`1b0bf0`, +0x29=`1`, +0x28=`1`.
Pvar+0x56 jest odtąd indeksem rekordu skryptu; jego dodatkowe użycie
w `2db370` nie oznacza oddzielnej trwałej „flagi automatycznej”.

**CONFIRMED — bramki pierwszej aktualizacji.** Stan Moby=1 dochodzi przez
`2db3ac` do `267290(Moby,pvar+0x20)`:

| Bramka | Źródło | Świeży początek |
|---|---|---|
| Stan bohatera !=29, wartość +0x22a8 !=0 | `2672bc..2672d0` | stan 0; +0x22a8=4 z `2054d8..2054ec`, reset tagu 19 |
| Tryb ==0 | `2672dc..2672f8` | `29021c` zapisał 0; gp-0x7658 jest tym samym słowem `15f6a8` |
| Indeks skryptu !=-1, typ !=255 | `267304..267314` | 0 i 1 |
| Odległość 3D <=2·pvar[0x2c] | `26731c..267334`, `1ff400..1ff434` | około 160.377 wobec 510 |
| Testy kierunku | `26733c..2673cc` | pominięte, bo typ=1 |
| Rodzaj warunku rekordu 0 | `2673d4`, `267798..2679f8` | 0 nie zmienia indeksu skryptu |
| Licznik `15f6b0` >= `179210` | `2673e4..2673f8` | próg 0 z instalowanego obrazu |
| Automat albo przycisk maski `0x10` z `13cbe4` | `267400..267418` | automat=1; odczyt przycisku pominięty |

Odległość podano dla autorskiego Z. Dla dowolnego Z na odcinku sondy
odległość pozostaje mniejsza niż 164, więc dokładny wynik §1 nie rozstrzyga
tej bramki. Po ponownym wejściu z flagą 1 gałąź `2db32c..2db340` zmniejsza
parametr do 3 i wyłącza ten startowy zasięg; nie stosować tej gałęzi do
New Game z wyzerowanym postępem.

**CONFIRMED — lokalna ścieżka startu.** `267458..267500` wybiera scenę 4,
`267508` nie wybiera wariantu z bitem `0x4000`, a `267530` woła `299b68(4)`.
Ten zapisuje numer sceny do `16c990` (`299c8c`), **tryb 2** (`299ccc`)
i wywołuje `222b80(100,2)` (`299cd4..299cec`). Tryb 2 dopuszcza ten set-state
(`222bf0..222c14`), zapis stanu jest w `222ce0`. Moby update `29992c`
poprzedza hero update `299980`: próbka pada może już istnieć, lecz zwykła
aktualizacja stanu 0 zostaje zastąpiona stanem 100. Całej długości sceny
i pierwszej klatki sterowania po jej zakończeniu tutaj nie wyznaczono.

**CONFIRMED — który kontener.** Wybierany jest **4 z indeksów 0–6**,
nie „czwarty chunk”. `245bac..245c24` indeksuje tablicę scen krokiem
`0x250`; PAL (`15ee80!=0`) wybiera bazę `13a898`, alternatywa `13a77c`.
Odpowiada to lokalnemu TOC: `0x184 + 4*0x250 + 0x134` (PAL), albo +0x18.

| Kontener | Zakres unique obu wariantów | Pierwszy unique PAL |
|---|---|---|
| 0 | 429–460 | 445 |
| 1 | 461–472 | 467 |
| 2 | 473–522 | 498 |
| 3 | 523–580 | 552 |
| **4** | **581–598** | **590** |
| 5 | 599–650 | 625 |
| 6 | 651–699 | 676 |

Dla sceny 4 pierwszy blok PAL: **LBA 1627380**, unique **590**, rekord
`0 z 9`, 5 aktorów, 41 klatek animacji, 81 rekordów kamery, 79776 B.
Unique 581 przy LBA 1627264 ma 49/97 próbek i należy do alternatywnego
wariantu. Potwierdzenie: oba `wad-scene-animation`, tablica ISO i hashe
pierwszych bloków wszystkich siedmiu kontenerów; bez odtwarzania sceny.

**CONFIRMED — inne startery `299b68` nie konkurują na świeżym New Game.**
Bezpośredni wywołujący `299b68` (lista z #34): `267530`/`267880` (skrypt
klasy 834; `267880` tylko przy a2!=0, a `2673d4` przekazuje 0), `277d50`
(tryb 3), callback statku `28f458` (instalowany w `2440c8` i `28fc04`),
klasy 750 i 1290 (nieobecne na Veldinie). Ramiona sceny statku
`28f6a0..28f7c4` wymagają poziomu 10 z przyciskiem `0x10` albo niezerowych
bajtów postępu `13d5e4` (tag 10, offset 28) lub `13de53` (tag 14, offset 11);
oba mają 0 w resecie z ISO (`source-data.json`). Czy obiekt statku w ogóle
powstaje na Veldinie, pozostaje UNKNOWN (gałąź z #34); niezależnie od tego
nie uruchamia sceny przy wyzerowanym postępie (INFERRED: nie prześledzono
wszystkich zapisów tych bajtów przed pierwszą klatką).

**INFERRED — całość New Game.** Powyższe dane i kolejność wskazują na
automatyczną scenę przed sterowaniem. Brakującym dowodem pełnego wykonania
jest ślad normalnego New Game obejmujący cały `2657b8`, pierwsze `2658c0`,
start `299b68` i hero update, z bieżącymi globalami. Nie zastępować go
`--level`, wymuszeniem flag ani listą wszystkich 296 żywych obiektów.

## 4. `2666a8`, `266670`, `266210`, `200ec8`

**CONFIRMED — kontrakty zapisów.** Właściciel trzech struktur kanałów ma
bazę `1517d0`; żądane stany to +0x40, +0x5c, +0x78.

| Funkcja | Działanie źródłowe |
|---|---|
| `2666a8` | Zapisuje **4** do wszystkich trzech halfwordów (`2666b4..2666c0`) |
| `266670(0)` | Zapisuje **0x8000** do +0x40 i +0x78, zeruje +0x42/+0x7a; nie rusza +0x5c (`266688..2666a0`) |
| `266670(a0!=0)` | Dodatkowo +0x5c=0x8000, +0x5e=0 (`266670..266684`) |
| `266210` | Przy pustym uchwycie +0x34 i niezerowym wpisie `13a728+4*a0` zleca `12ed48`, instaluje callback `267188`, zapisuje wybór/parametry kanału (`266220..2662d4`) |
| `200ec8` | Wywołuje `122140(*(15efb8))` (`200ed0..200ed8`); `122164..1221e8` zapisuje rejestry wyświetlania pod `12000000` i +0x20/+0x70/+0x80/+0x90/+0xa0/+0xc0/+0xe0 zależnie od wariantu |

**CONFIRMED — konsument stanów.** `266cc0..266cd4` aktualizuje trzy
struktury przez `266708`. Bit `0x8000` żądania uruchamia `12edb0`
(`26679c..2667bc`), a jego zdjęcie przy dotychczas ustawionym bicie stanu
uruchamia `12ede0` (`2667e0..2667fc`). Wrappers wysyłają polecenia
**45/46** przez `12e820`; `12ed48` wysyła **44**.

**INFERRED — nazwy ról.** To sterowanie wstrzymaniem/wznowieniem kanałów
streamingu audio, w tym muzyki. Kanał +0x34 korzysta z `13a728`, czyli
lokalnej tabeli music VAG +0x148; prolog przekazuje wybór z `151808`,
argument 1 i poziom 1024 (`246758..246764`). `266670(0)` pozostawia kanał
+0x50, używany przez scenę, a `2666a8` usuwa bit blokady wszystkich trzech.
Pełna semantyka poleceń po stronie IOP nie była tu wykonana. Żadna z tych
dwóch krótkich funkcji nie zapisuje trybu gry ani pozycji/kamery; nie są
wejściem sceny. Spójne z tym: sam start sceny `299b68` woła `266670(0)`
w `299bc8`, zanim zapisze tryb 2, więc scena 4 z §3 ponownie wstrzymuje
kanały +0x40/+0x78 zaraz po `2666a8` z P14 (CONFIRMED wywołanie). `200ec8` instaluje konfigurację wyświetlania (CONFIRMED
zapisy, INFERRED nazwa roli), nie jest dowodem fade.

## 5. Własność i czas życia `13d498`

**CONFIRMED.** Deskryptor ELF **`1a0610`** ma adres `13d490`, rozmiar
128 B, tag **5**. Kompilator `rac_frontend_state.cpp` mapuje to na
**`progress/primary/field-5/bytes[8]`**. To jeden z **267 właścicieli
postępu**, a nie nowy stan lokalny poziomu. Źródłowy reset kopiujący
w `20bed0` daje 0: odczytano tag 5 rzeczywistej koperty z ISO (LBA 1515),
128 zerowych bajtów, z poprawną sumą kontrolną rekordu pierwotnego `0x9ad4`.

Nie należy do żadnego z **7 regionów / 3457 B** `StateInstallationV1`:
`15f650+4`, `15f6a4+4`, `15f6bc+4`, `15fd47+17`, `173f50+4`,
`1ba860+256`, `1bb5c0+3168` (`rac_level_installation.cpp::kRegions`).
Nakładka zaczyna się od `15f000`, a czyszczenie entry obejmuje
`161f00..1660b0`; żadne z nich nie zeruje flagi postępu.

| Moment | `13d498` | Status |
|---|---|---|
| Po resecie New Game i na granicy instalacji v14 | 0 | CONFIRMED reset i zakres instalacji; brak pełnego śladu frontendu w tym zadaniu |
| Po inicjalizacyjnym callbacku klasy 834 w P5 | 1 | CONFIRMED zapis `2db35c` |
| Test P14 / początek sceny 4 | 1 | CONFIRMED lokalne ścieżki; rekord 0 skryptu nie ma bitu 8 kasującego flagę w `2674cc` |

Nie należy określać „wartości po New Game” bez wskazania bariery: obecny
runtime v14 zatrzymuje się przed entry, więc jego zero nie przeczy jedynce
ustawianej dopiero przez oryginalny load-time callback.
