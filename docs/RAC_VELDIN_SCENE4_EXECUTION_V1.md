# Scena 4 Veldinu: wykonanie, zakończenie i powrót do sterowania — PAL v2.00, V1

Dokument rozpoznaje minimalną kompletną ścieżkę źródłową po New Game:
klasa 834 → start sceny `299b68(4)` → ramię trybu 2 (`29a300`) z frame-skip
i dogonieniem → normalne zakończenie albo pominięcie → wyjście `299e70`
do trybu 0 → pierwsza sterowalna aktualizacja. Kontynuuje
[RAC_VELDIN_START_TRIGGERS_V1](RAC_VELDIN_START_TRIGGERS_V1.md) (#38) i
[RAC_LEVEL_FRAME_SCHEDULE_V1](RAC_LEVEL_FRAME_SCHEDULE_V1.md) (#36) oraz
odblokowuje decyzję „ścieżka A” dla #41. Nie zmieniono kodu.

Statusy: **CONFIRMED** — sprawdzone słowa instrukcji, dane i ich lokalny
przepływ; **INFERRED** — wniosek z kilku potwierdzonych ścieżek bez pełnego
wykonania; **UNKNOWN** — brakujący dowód nazwany wprost.

## 0. Odpowiedzi w skrócie

| # | Pytanie | Odpowiedź | Status |
|---|---|---|---|
| 1 | Długość sceny 4 w PAL | **680 update'ów trybu 2** (13,6 s przy 50 Hz); próbkowane są update'y 1..679, update 680 kończy scenę bez próbki | CONFIRMED kod `29a490..29a4e4` + nagłówek wszystkich 9 chunków |
| 2 | Długość w drugim selektorze (`15ee80 == 0`) | 818 update'ów, granica chunku 96 | CONFIRMED dane; wybór selektora w przebiegu New Game z #41 |
| 3 | Pominięcie przez gracza | Na Veldinie: zbocze START (`13cbe4 & 0x800`), gdy licznik ≥ `1feed0(18)` i zaciemnienie `15f4fc == 0.0` | CONFIRMED bramki; próg 15 dla PAL INFERRED (arytmetyka `1feed0`) |
| 4 | Czym różni się pominięcie od końca | Niczym po stronie wyjścia: obie ścieżki wołają tę samą `299e70` | CONFIRMED `29a58c..29a59c` |
| 5 | Powrót do trybu 0 | `299e70` zapisuje `15f6a8 = 0` (`299f08`) i `15f6bc = 1` (`299ec0`) | CONFIRMED |
| 6 | Stan bohatera po scenie | `222b80(0,1)` przyjęte (`+0x208c = 24` z wejścia stanu 100), więc stan 0; opcjonalne dociągnięcie Z sondą | CONFIRMED bramki; brak innych zapisów pozycji INFERRED |
| 7 | Czy zaraz potem startuje następna scena | Nie. Skrypt przechodzi na rekord 1 (scena 1, automat), ale jego promień to 3 albo 16, a bohater stoi ok. 160 jednostek od obiektu | CONFIRMED kod i dane; brak ruchu bohatera w scenie INFERRED |
| 8 | Ślad referencyjny | **Brak.** Nie ma emulatora ani śladu New Game; jedyny lokalny savestate PCSX2 (Documents) jest skompresowany zstd i nie jest śladem New Game | stan faktyczny |

## 1. Źródła i metoda

| Źródło | SHA-256 |
|---|---|
| ISO (F33) | `0f18a6c84cd8d727ec8c21000a236ed5ce2f279cbb8d0682fa9747199ef73260` |
| ELF `SCES_509.16` | `17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b` |
| Nakładka Veldinu `level00_e0_s00.bin` | `922fd06e1ac6c717cc7832a3b8ac7be5aaf178ebe1b7982ae4cd294af1876c79` |
| `gameplay-705.bin` (placementy) | `f085b4717bbe197fb8785100894eb230f6dccba124dd84c4590f44158d0c6422` |

Listingi `lvl.asm`/`elf.asm` pochodzą z #34. `verify_words.py` porównał
**2715 słów** ze wszystkich 44 zakresów cytowanych niżej z ELF i łańcuchem
sekcji nakładki: **0 niezgodności** (`listing-verification.json`).
Chunki sceny odczytano z ISO istniejącym dekoderem WadV1 (`scene4_chunks.py`).
Pliki analizy leżą w ignorowanym `local/forensics/veldin-scene4-execution/`.
Nie uruchamiano żadnego EXE ani smoke OpenRC; smoke nie jest dowodem sceny.

**Ślad referencyjny: brak.** PCSX2 nie jest zainstalowany w znanych
lokalizacjach. W `Documents\PCSX2\sstates` jest savestate SCES-50916 z
2026-09-12, ale jego wpisy mają kompresję ZIP 93 (Zstandard), której
Python w tym środowisku nie dekoduje bez instalacji. Nie byłby też śladem
New Game. Pliku nie skopiowano. Wszystkie wnioski wynikają ze źródła.

## 2. Przebieg w iteracjach pętli `2468a8`

Licznik `n` to wartość `16c994` (scena `+0x34`) po inkrementacji w `29a49c`,
czyli numer wywołania `29a300` od startu sceny.

| Iteracja | Ramię | Co się dzieje | Status |
|---|---|---|---|
| S (start) | 0 | `299250`: bramka pauzy, potem update Moby `29992c` → callback klasy 834 `2db278` → `267290` → **`299b68(4)`**: tryb 2, `15f6bc = 1`, stan bohatera 100, instalacja chunku 0, blokujące vsynci (§3). Reszta `299250` działa dalej: bohater `299980` już w stanie 100, kamera `1ed428`. Render `1f91b0` widzi `15f6bc = 1` i nie rysuje. Po ramieniu tryb 2 ≠ 0 → `15f6ac = 0` (`246a6c`); `15f6bc != 0` → `2a1c68(1)`, `2a1a88`, powrót do `2468b0` (bez renderu, dogonienia i `15f4f8++`) | CONFIRMED kroki; całość INFERRED (brak śladu) |
| M1..M679 | 2 | `268738` pad, `29a300` (update `n`), `1f9210` render sceny. Dogonienie w trybie 2 przy `16c9b0 == 0` woła drugi raz `268738` + `29a300` bez renderu, więc zegar sceny rośnie także w dogonieniu | CONFIRMED (`246d1c..246d3c`, #36) |
| E (koniec) | 2 | `29a300` z `n == 680` (albo pominięcie) → `299e70`: tryb 0, `15f6bc = 1`, 14 blokujących vsynców `1f9bb8(12)`. Próbkowanie jest pomijane (`29a59c → 29a734`). `1f9210` widzi `15f6bc = 1` i nie rysuje. Po ramieniu tryb 0 ≠ 2 → `15f6ac = 0`, frame-skip | CONFIRMED |
| C1 | 0 | Pierwsza zwykła iteracja po scenie: pad, `299250` z bohaterem w stanie 0, kamerą `1ed428`, renderem `1f91b0` | CONFIRMED kolejność (#36/#38); INFERRED brak innych zmian trybu |

**Przypadek brzegowy (CONFIRMED kodem).** Jeśli warunek końca wypadnie w
update dogonienia (`246d38`), zmiana trybu nastąpi po porównaniu `246a58`.
Wtedy `15f6ac` nie jest zerowany, a `15f6bc = 1` zostanie skasowany w
`2468f4` następnej iteracji, zanim trafi do `246a80`. Pierwsza iteracja
trybu 0 ma wtedy render, a licznik frames-in-mode kontynuuje wartość z
trybu 2 (≥ 8), więc bramka pauzy jest od razu otwarta. Host #41 musi
podawać `mode_after_catchup` z rzeczywistego wykonania, nie z założenia.

## 3. Start sceny `299b68(4)` — efekty w kolejności

Wołający: `267530` (rekord 0 skryptu, §7). Wcześniej `267424..267448`
ustawia słowo postępu `13d6c4 + 16·idx` na 1, jeśli było 0; dla idx = 1
(F54/#38) jest to **`13d6d4` = progress/primary/field-15 bajty 28..31**.
`267534` w slocie zapisuje `1601f0 = -1` po `236298(stara wartość)`.

| Adres | Efekt | Status |
|---|---|---|
| `299b8c`, `299ba0` | Wyjście przy scenie < 0 albo trybie już równym 2 | CONFIRMED |
| `299bac..299bc4` | `1416f8 == 0` → `211908(0)` i wyjście bez sceny (F53) | CONFIRMED |
| `299bc8` | `266670(0)` — wstrzymanie kanałów audio `+0x40/+0x78` | CONFIRMED wywołanie |
| `299bd8` | `266f08(0)` | CONFIRMED; rola UNKNOWN |
| `299bf0`, slot `299bf4` | `1feff0(16c960, 0, 448)` zeruje właściciela sceny (w tym `16c9b0`); **`15f6bc = 1`** | CONFIRMED |
| `299c0c..299c5c` | Zerowanie po 64 B: `17c540`, `17c440`, `17c480`, `17c500`, `17c4c0` | CONFIRMED; rola UNKNOWN |
| `299c64..299c98` | `+0x30 (16c990) = 4`; bufory `+0x58/+0x5c = 173f04/173f08 + 16128c − 0x3c000` (dekompresja i odczyt chunku) | CONFIRMED |
| `299ca0..299cb8` | `15f67c = 1.0`, `15f680 = 0`, `+0x34 = 0`, `+0x3c = 0`, `203fb8()`, w slocie `161274 = bufor` (baza bufora pakietów renderu) | CONFIRMED zapisy; rola `161274` INFERRED (przywraca ją `2a1a58`) |
| `299cc0` | **`15f4fc = 1.0`** — zaciemnienie (alfa nakładki ×128 w `1f8f00`) | CONFIRMED zapis i użycie; nazwa INFERRED |
| `299ccc` | **Tryb `15f6a8 = 2`** | CONFIRMED |
| `299cdc..299ce4` | `+0x4b = (u8)167114`, `167114 = 0` | CONFIRMED; rola słowa UNKNOWN (czytają je `1f74f0`, `28dee0`, `1ed1ac`) |
| `299ce8`, slot `299cec` | **`222b80(100, 2)`**; `15f500 = 0` | CONFIRMED |
| `299cf0`, slot `299cf4` | `233868()` ustawia bit 0 `+0x34` Moby bohatera i dołączonych; `1414f5 (+0x20a5) = 1` | CONFIRMED; bit 0 jako „ukryty” INFERRED |
| `299cf8..299d2c` | Moby `179208` (obiekt 834) i `1ba15c`: `+0x34 \|= 1` | CONFIRMED |
| `299d30` | `245b88(0)` — zlecenie odczytu chunku 0 | CONFIRMED |
| `299d38..299d44` | `1f9bb8(1feed0(6))` — blokująca pętla prezentacji, §3.1 | CONFIRMED; wartość 5 dla PAL INFERRED |
| `299d48..299d50` | `266f08(1)` (vsync, dopóki `1517d8 != 0`), `245c40()` — instalacja chunku 0, §4.1 | CONFIRMED |
| `299d58..299db4` | Moby klas 74 i 203: `+0x34 \|= 0x80` | CONFIRMED; na Veldinie brak placementów tych klas (`scene4-classes.json`) |
| `299db8..299dd4` | `1517ec = +0x30` (numer sceny do właściciela strumieni) | CONFIRMED |
| `299dd0..299df4` | **Pętla blokująca:** dopóki stan strumienia `15182a != 3`: `28dee0` (pompa łańcucha z §3.0), vsync `122598(0)` | CONFIRMED; liczba iteracji UNKNOWN (odpowiedzi IOP) |
| `299e00..299e08` | `+0x46 = −3`; `2660e0()` — przy stanie 3 uruchamia strumień `12ede0` i ustawia stan 4 | CONFIRMED |
| `299e10` | `245b88(1)` — prefetch chunku 1 | CONFIRMED |
| `299e18..299e40` | Pętla vsync, dopóki `+0x46 < s1` (s1 = 0, −1, −2, …): przy niezmienionym −3 dokładnie **3 vsynci** | CONFIRMED pętla; brak zapisu `+0x46` w tym czasie INFERRED |

### 3.0 Łańcuch stanu strumienia dialogu `15182a` (0 → 1 → 2 → 3 → 4)

Kanał sceny to struktura `151820` (właściciel `1517d0 + 0x50`): uchwyt
`+0x00`, stan `+0x0a = 15182a`, żądanie `+0x0c = 15182c`. Wszystkie kroki
po stronie EE są CONFIRMED (słowa w `listing-verification.json`):

| Krok | Adresy | Warunek i efekt |
|---|---|---|
| Pompa | `299dd0..299df4` → `28dee0` → `28eb2c` → `2668f0` | Pętla blokująca woła `28dee0` co vsync. W `28dee0` nie ma gałęzi omijającej `28eb2c`. `2668f0` nic nie robi przy bajcie `1517db != 0` (`26690c..266910`) |
| Zlecenie, stan 1 | `266a5c..266aa0` → `265f40` → `266018..2660cc` | Przy `1517ec >= 0` (= 4) i wolnym uchwycie `151820 == 0`: sektor = `*(13a764 + 592·scena + 4·15ee88)`. Przy sektorze 0 nie ma zlecenia. Inaczej `15182a = 1` (`266084`), uchwyt = −1 (`2660ac`), polecenie IOP 44 przez `12ed48` z callbackiem `267020`. Potem `1517ec = −1` (`266aa0`). Przy zajętym uchwycie stan poza 6/7 → 5 (`266a68..266a90`) |
| Stan 2 | callback `267020` (`267034..267054`) | Przy niezerowym wyniku: uchwyt = wynik; stan 1 → 2 |
| Polecenie 79 | `2668f0` → `266cd0` → `266708(151820)` → `2668a0..2668b0` | Przy stanie 9 albo uchwycie 0 wyjście (`266724..266738`), przy uchwycie −1 osobna gałąź `2668bc`. Bit `0x8000` żądania `+0x0c` obsługuje pauzę `12edb0`/`12ede0`. Stan z bitem `0x8000`, 1, 8 albo 9 kończy funkcję (`266800..26682c`). Stan 2 lub 3 (`266830..266838`) z uchwytem ≠ −1 i stanem dokładnie 2 woła `12ee40(uchwyt, 266ff0, 151820)`, czyli polecenie IOP **79** przez `12e820` (`12ee54..12ee5c`) |
| **Stan 3** | callback **`266ff0`** (`266ff8..267014`) | Przy niezerowej strukturze (`a1`), niezerowym wyniku (`a0`) i stanie nadal równym 2 zapisuje **3** pod `a1 + 0x0a = 15182a` |
| Stan 4 | `2660e0` (`299e08`, później `29a4d0`) | Przy niezerowym uchwycie i stanie 3: `12ede0`, stan 4 |

Dla sceny 4 słowa sektora z TOC (`scene4-stream-words.json`, tabela
`0x184 + 4·0x250 + 4k` lokalnego TOC) wynoszą dla `15ee88 = 0..5`:
`1626409, 0, 1626580, 1626751, 1626922, 1627093`. Wartość 1 dałaby brak
zlecenia i nieskończone oczekiwanie w `299dd0`. Zgadza się to z
dopuszczalnym zbiorem `{0, 2, 3, 4, 5}` w `rac_new_game_flow.cpp:156`
(CONFIRMED dane; zgodność z istniejącym kodem).

UNKNOWN pozostają: czas odpowiedzi IOP na polecenia 44 i 79, ich wynik
(zero blokuje przejście do 2 lub 3), a więc liczba vsynców pętli
`299dd0`. Na PS2 pętla nie ma limitu czasu. Dla #41 producentem `15182a`
jest neutralny właściciel strumienia dialogu. Musi on odwzorować kolejność
1 → 2 → 3 z kroków pompy, a nie ustawiać 3 bez odpowiedzi.

### 3.1 Blokujące prezentacje wewnątrz ramienia

`1f9bb8(k)`: `2a1c68(1)`, vsync, `15f4f8++`, `2a1a88`; potem k razy
rysowanie pakietu GS (`0x30000014/0x50000014`, źródło `13ced0`), sync,
vsync, `15f4f8++`, zamiana buforów (`2a1b58`, `2a1ae8`); na końcu sync,
vsync, `15f4f8++` (CONFIRMED). Daje to **k + 2 vsynców z inkrementacją
licznika prezentacji**. Start: k = `1feed0(6)`; dla PAL `0.5 + 6·0.8333`
→ 5 (INFERRED: obie konwencje zaokrąglenia dają 5), czyli 7 klatek. Koniec:
k = 12 literalnie (`299ed4`), czyli **14 klatek**. Wygląd tych klatek
(czarny prostokąt?) jest INFERRED. Pętle `299dd0` i `299e28` wołają
vsync bez `15f4f8++`.

## 4. Ramię trybu 2: `29a300`

| Adres | Krok | Status |
|---|---|---|
| `29a33c..29a390` | Flagi `16c160` (prolog = 15): `299108` (zerowanie `15f524`, `17e604`, `15f52c`, `15f530`, `15f528`, `15f534`, `15f3f0`, `15f3f4`), potem `23ec00` | CONFIRMED; role UNKNOWN |
| `29a3c4`, `29a3cc` | Update Moby `2658c0`, `28f2a0` (bit 2) | CONFIRMED |
| `29a410` | Bohater `2076e8` (bit 1) — stan 100 | CONFIRMED |
| `29a454` | `268aa8` (bit 4) | CONFIRMED |
| `29a470..29a4bc` | `15f4fc −= 0.34`, przy wyniku < 0 → 0 (pełne odsłonięcie po 3 update'ach); `+0x34++`, `+0x38++` | CONFIRMED |
| `29a4c0..29a4d4` | `n >= +0x46` (lh, nagłówek = −6, po starcie −3) → `2660e0` | CONFIRMED |
| `29a4dc..29a588` | Koniec i pominięcie, §5 | CONFIRMED |
| `29a5a4..29a5e8` | `+0x38 >= (15ee80 ? 80 : 96)` → `266f08(1)`, `+0x3c++`, `245c40()`, `245b88(+0x3c + 1)` | CONFIRMED |
| `29a5ec` | `29a158` — kamera z rekordu `+0x38` (§4.2) | CONFIRMED |
| `29a5fc..29a730` | Dla `+0x44` aktorów: bajty klatek `+0x50 = u>>1`, `+0x51 = +0x50 + 1`, `24fbf8`, faza `0.5·(u & 1)` lub 1 przy bajcie kamery `+0x0c` (slot `bnel` `29a674`), lerp korzenia do Moby+0x10, `251e30`, `25b238`/`2353b8`, `207a48` dla klas 10/419/1365 | CONFIRMED (sampler istniejący) |
| `29a734..29a790` | `1f7118` ×2 (puste), `28dee0`, `23f228`, `250be0`, `299148` (bit 2): `15f6b0++`, `15efa4++`, licznik bezczynności pada | CONFIRMED |

Ramię 2 **nie zawiera bramki pauzy** (`2992e8` jest tylko w `299250`).
Moby (w tym obiekt 834) i bohater są aktualizowane w każdym update sceny.
Obiekt 834 w trybie 2 tylko ustawia bit 0 `+0x34` i `+0x31 = 0`
(`2db29c..2db2c4`), a w stanie 2 nie woła `267290`.

### 4.1 Chunki kontenera 4 i zegar sceny

`245b88(i)` czyta `start[i]` i `start[i+1]` z tablicy
`13a898 + 4·0x250 + 4i` (PAL; drugi selektor `13a77c`). Odczyt następuje
tylko przy `start[i+1] − start[i] > 0`. `245c40` dekompresuje bufor
(`24e908`), zeruje `+0x38` i kopiuje z nagłówka:

| Nagłówek chunku | Pole właściciela | `SceneAnimationBankV1` | PAL scena 4 |
|---|---|---|---|
| `+0x00` u16 | `+0x40` długość | `unknown_word_0 & 0xffff` | **680** we wszystkich 9 |
| `+0x04` u32 (≥ 1024 → wskaźnik) | `+0x4c` napisy | `subtitle_table_offset` | 0 (brak napisów) |
| `+0x08` u16 → lh | `+0x46` | dolna połowa `header_tag` | −6 |
| `+0x0c` u16 | `+0x44` liczba aktorów | `actor_track_count` | 5 |
| `+0x10` u32 | `+0x54` baza kamery | `header_bytes` | — |
| `+0x14 + 4i` | aktorzy | `actor_offsets` | klasy 0, 110, 530, 1515, 1365 |

Aktor bez Moby w slocie `+0x178 + 4i` dostaje nową `24f870(klasa)`
(`245d78`); w kolejnych chunkach slot jest reużywany. Wyjątek dla
`klasa 533 → 160608[13e156]` działa tylko w trybie 6 (`245d24`), więc nie
dotyczy sceny 4.

| Chunk PAL | LBA | Sektory | Bajty | Klatki | Kamery | Bajt `+0x0c ≠ 0` |
|---|---|---|---|---|---|---|
| 0 | 1627380 | 11 | 79776 | 41 | 81 | — |
| 1 | 1627391 | 11 | 83152 | 41 | 81 | 54 |
| 2 | 1627402 | 13 | 85728 | 41 | 81 | 41 |
| 3 | 1627415 | 12 | 85872 | 41 | 81 | 44 |
| 4 | 1627427 | 13 | 85680 | 41 | 81 | — |
| 5 | 1627440 | 12 | 85680 | 41 | 81 | — |
| 6 | 1627452 | 12 | 84992 | 41 | 81 | 54 |
| 7 | 1627464 | 11 | 84448 | 41 | 81 | — |
| 8 | 1627475 | 6 | 46032 | 22 | 41 | — |

Wszystkie chunki mają `rekord k z 9` i tangens `+0x1c = 0.4141117`. Wpis
`start[10] = 0`, więc prefetch po chunku 8 nic nie czyta. Skrót chunku 0
zgadza się z #38. Drugi selektor: 9 chunków, 49/97, ostatni 27/51,
długość 818.

**Wzór zegara (CONFIRMED kodem, sprawdzony danymi):** dla
`n = 1..679` chunk = `⌊n/80⌋`, update w chunku `u = n mod 80`; `n = 680`
kończy scenę. Ostatnia próbka to chunk 8, `u = 39`: kamera 39 < 41,
klatki 19 i 20 < 22 — strażniki interpolacji są spełnione bez czytania
poza tablice. Chunk 0, `u = 0` nigdy nie jest próbkowany. Override
fazy z bajtu `+0x0c` działa tylko przy nieparzystym `u`, więc w PAL
dotyczy jedynie chunku 2, `u = 41` (`n = 201`).

### 4.2 Kamera sceny

`29a158` (CONFIRMED): `16cbf0 = rekord+0x1c` (przy `15eeb0 != 0`
pomnożony przez `15f67c = 1.0`), `1f7d00`, pozycja `rekord+0x00 → 166ec0`,
macierz `125358` → `1254a0(+0x10)` → `125548(+0x14)` → `1253f8(+0x18)`,
wiersze `1670d0 = −row2`, `1670e0 = −row0`, `1670f0 = +row1`. Przy
`15eeb4 != 0` `1670e0 = 1ff370(1670f0, 1670d0)`. Gałąź przesunięcia
pozycji wymaga `15f680 != 0`, a start zapisuje 0. Near/far
(`16cb40+a0/+a4`) nie są zmieniane przez ścieżkę sceny (INFERRED z braku
zapisów w cytowanych funkcjach). Role `15eeb0`/`15eeb4` są UNKNOWN.

## 5. Normalne zakończenie i pominięcie

`29a4dc..29a588` (CONFIRMED):

1. `s1 = !(n < +0x40)` — koniec przy `n >= 680`.
2. Jeśli `n < 1feed0(18)` albo `15f4fc != 0.0` → decyduje tylko `s1`.
3. W przeciwnym razie: jeśli którekolwiek z `15efa0`, `15ef20`, `15efd8`
   jest niezerowe **albo poziom `15ee84 <= 0`** → `s1 = 1` przy zboczu
   START (`13cbe4 & 0x800`).
4. Gdy wszystkie trzy są zerowe i poziom > 0 → `s1 = 1` dopiero przy
   `(ld 13cbe0) & 0x0000_0800_0000_000f == maska`.

Veldin ma poziom 0, więc obowiązuje punkt 3: **START w zboczu**.
`13cbe4` = `nowe & ~poprzednie` (`268314..268328`), `13cbe0` to surowe
przyciski po `xori 0xffff` (`268068..268084`). Zaciemnienie spada do
0 po 3 update'ach, a próg `1feed0(18)` dla PAL to `0.5 + 18·0.8333`
→ 15 (INFERRED; F85). Najwcześniejsze pominięcie to więc `n = 15`.
Dla drugiego selektora wartość `15ee68` nie jest tu ustalona (UNKNOWN).

`s1 = 1` → `299e70`, potem skok do `29a734`: próbki nie ma, a `28dee0`,
`23f228`, `250be0` i `299148` nadal działają. **Pominięcie i koniec
mają identyczne wyjście.** Bit `0x800` jest tym samym START, którego
używa `rac_new_game_flow.cpp:229` dla pominięcia filmu (zgodność
konwencji, nie dowód mapowania hosta).

## 6. Wyjście `299e70` — przywrócenie stanu

| Adres | Efekt | Status |
|---|---|---|
| `299e98..299eb0` | Stan strumienia sceny `15182a`: poza 6/7 → 5 | CONFIRMED; rola „stop” INFERRED |
| `299ec0` | **`15f6bc = 1`** | CONFIRMED |
| `299ec4` | `2a1a58`: `161274 = 1c2438[poziom < 19 ? poziom : 0]` (przywrócenie bazy bufora) | CONFIRMED |
| `299ed4` | `1f9bb8(12)` — 14 blokujących klatek (§3.1) | CONFIRMED |
| `299edc` | `299108` | CONFIRMED |
| `299efc` | `167114 = (u32)+0x4b` (odtworzony tylko bajt) | CONFIRMED |
| `299f00` | **`16cbf0 = 0.63` (`3f2147ae`)** = `kRacGameplayHorizontalTangentBitsV1` | CONFIRMED |
| `299f08` | **Tryb `15f6a8 = 0`** | CONFIRMED |
| `299f0c`, slot `299f10` | `1f7d00` (projekcja); **`15f4fc = 0`** | CONFIRMED |
| `299f14..299f6c` | Każdy aktor: licznik referencji klasy `−1`, wpis sekwencji = 0, `24fba0(aktor)` — usunięcie Moby sceny | CONFIRMED |
| `299f70..299fcc` | Klasy 74/203: `+0x34 &= ~0x80` | CONFIRMED |
| `299fd4` | **`222b80(0, 1)`** | CONFIRMED |
| `299fe0..29a03c` | `R = 25a6d0(13f4d0, 0)` (f12 = 0.5); jeśli `R > 2.0` i `\|Z_bohatera − R\| < 4.5` → `13f4d8 = R` | CONFIRMED |
| `29a04c`, slot `29a050` | `233950()` zdejmuje bit 0 `+0x34` bohatera i dołączonych; `1414f5 = 0` | CONFIRMED |
| `29a054..29a0bc` | Przy `+0x4a != 0`: sonda pozycji `16c970` i `217718(16c970, 16c980, 0, 1)` | CONFIRMED gałąź; **nieaktywna** dla sceny 4 (INFERRED: jedynym zapisem `+0x4a` jest memset startu) |
| `29a0c0..29a0fc` | `179208` (obiekt 834): `+0x34 &= ~1`; `1ba15c`: to samo i `1ba15c = 0` | CONFIRMED |
| `29a108..29a11c` | **`2677b8(179208, 17920c, 1)`**, potem `179208 = 17920c = 0` | CONFIRMED |
| `29a120..29a128` | `2666c8(1feed0(30))` — wznowienie kanałów audio (25 dla PAL INFERRED) | CONFIRMED wywołanie |

**Bramka set-state (CONFIRMED).** Wejście stanu 100 (`227c80`, tablica T2
`1e83b0[100]`) zapisuje `+0x208c = 24` i `+0x20ac = 1`. W `299fd4` tryb
ma już 0, `+0x208c = 24 ≠ 20`, a `+0x22a8 = 4` (#38), więc `222ce0`
zapisuje stan 0, `+0x2090 = 100` i kasuje `+0x20a5`. Inne zapisy
`+0x208c` w trakcie stanu 100 nie są wykluczone (UNKNOWN).

**Bohater w scenie (CONFIRMED).** T1 stanu 100 (`1e81a0[100] = 221a1c`)
zeruje wektor `13f530` (`+0xe0`, prędkość wg F66) przez `1ff210`. Pozycji
nie zmienia żadna cytowana funkcja sceny; aktor 0 sceny to osobna Moby
klasy 0. Bohater jest ukryty bitem 0 (`233868`/`233950`, INFERRED).

**Kamera po scenie.** `166ec0` zawiera pozycję ostatniej próbki sceny
(`n = 679`). Pierwszy update kamery `1ed428` w trybie 0 woła `1ed170`,
który czyta `166ec0` (`1ed1bc`) przed `1eea38(166ec0)` (CONFIRMED).
Wpływ tej wartości na oko typu 0 jest UNKNOWN. Model #37 opisuje wejście
z prologu, nie powrót ze sceny (F81: `entry_probes_modeled=false`).

## 7. Skrypt klasy 834 po scenie

Rekordy 28 B pod `*(1b19f8+4) = 1b0bf0` (`script-records.json`, obraz
nakładki):

| Rekord | Scena `+4` | Następny `+6` | Rodzaj `+8` | Flagi `+0x10` |
|---|---|---|---|---|
| 0 | 4 | 1 | 0 | 1 (automat) |
| 1 | 1 | 2 | 0 | 5 (automat, łańcuch) |
| 2 | 2 | 3 | 0 | 5 |
| 3 | `0x4001` (wariant `29a7d0(1)`) | 4 | 0 | 5 |
| 4 | 3 | −1 | 0 | 0 |

`2677b8(…, 1)` (CONFIRMED): `+4 = idx`, `+0x38 = 15f6b0`, przy zmianie
indeksu `+0x36 = następny`, a bajt automatu `+8 = flagi_nowego & 1`.
Łańcuch uruchamiający od razu następną scenę wymaga bitu 4 w **starym**
rekordzie (`26781c..267834`). Rekord 0 ma flagi 1, więc po scenie 4
łańcuchu nie ma. Rekord 1 ma słowo tekstu 0 i rodzaj 0, więc nie pokazuje
komunikatu i nie zmienia indeksu (`267888..2679f8`).

W trybie 0 obiekt 834 przechodzi ze stanu 2 do 1 (`2db400..2db40c`).
W stanie 1 przy `+0x56 != 0` promień `pvar+0x2c` wynosi 16, gdy
`2609d8(pozycja bohatera, pvar+0x60 = 4)` zwraca prawdę (punkt wewnątrz
prostopadłościanu #4 z tablicy `*(1601ac)`, INFERRED nazwa), a w
przeciwnym razie 3 (`2db370..2db3a4`). Bramka `267290` wymaga
odległości ≤ 2·promień, czyli ≤ 32. Bohater stoi przy spawnie
`(132.09, 115.48, ~31.4)`, a obiekt 834 w `(171.43, 270.94, 29.26)`:
odległość ok. 160,4. **Scena 1 nie startuje automatycznie po scenie 4**
(CONFIRMED nierówność; brak ruchu bohatera w scenie INFERRED). Dokładny
wynik `2609d8` nie jest tu potrzebny.

## 8. Dane kontenera 4 a neutralne struktury i luki v14

| Dane źródłowe | Istniejąca neutralna struktura / kod | Stan w v14 |
|---|---|---|
| 9 chunków PAL (WadV1, `SceneAnimationBankV1`) | `parse_scene_animation_bank_v1`, dekoder WadV1 | **brak w pakiecie** (F63) |
| Pozy 5 aktorów × 9 chunków | `compile_rac_scene_animation_bank_v1` → `ActorAnimationBankV1` (45 klipów: chunk × aktor, jak `chunk_actor_clip_ids` frontendu) | brak |
| Modele/rigi klas 110, 530, 1515, 1365 | `ActorLibraryV1`; rdzeń poziomu ma ich modele (`04-level-core-0.log`: 12/31/5/34 stawy) | brak — klasy bez placementów lub animowane są pomijane (F82) |
| Aktor 0 = klasa 0 | Model i rig gracza są już w pakiecie | jest |
| Kamera, korzenie, klatka/faza na update | `sample_rac_scene_animation_tick_v1` → `SceneTimelineV1` (679 próbek, 50 Hz, `loop = false`, wstrzymanie ostatniej próbki) | brak |
| Baza kamery z kątów | rotacje `1254a0/125548/1253f8` w `rac_frontend_numeric.cpp`; tangens pionowy `kRacPalVerticalTangentFactorBitsV1`, near/far z `third_person_camera.hpp` | brak składania dla sceny ogólnej |
| Zegar ogólny (80/96, start od 1, koniec przed próbką) | `RacFrontendBackgroundClockV1` ma inny próg (96 zawsze) i zapętlenie | **brak** |
| Rekordy skryptu 834, promienie 255/3/16, prostopadłościan #4 | `ActorBehaviorSceneV1` (program + pola + instancja z `authored_id`) | brak programu; prostopadłościany nieobecne |
| Strumień dialogu sceny (`1517ec = 4`, sektor z TOC wg `15ee88`, §3.0) | Właściciel audio | brak audio poziomu (F63) i właściciela stanów `15182a` |

**Minimalne rozszerzenie kompilatora (propozycja, nie wykonano):**

1. `RacSceneClockV1` obok zegara frontendu: próg 80/96 zależny od
   selektora, pierwsza próbka `n = 1`, koniec przy `n == duration` bez
   próbki, przeładowanie chunku z próbką `u = 0`.
2. `compile_rac_level_scene_timeline_v1` na wzór
   `compile_rac_frontend_timeline_v1`: walidacja wszystkich chunków
   (`scene_record_index == k`, wspólna długość, stałe klasy aktorów),
   45 klipów, `SceneTimelineV1` z kamerą złożoną z wierszy
   `−row2/−row0/+row1`, tangensami `+0x1c` i `+0x1c · 0x3f418937` oraz
   near/far rozgrywki.
3. Dołączenie do pakietu poziomu 0 modeli/rigów 4 klas i jednego zasobu
   timeline sceny 4. Zmienia to kontrakt ośmiu zasobów, więc wymaga
   nowego profilu (v15) — **decyzja Orkiestry**.
4. Neutralny opis wyzwalacza: instancja `ActorBehaviorSceneV1` dla
   rekordu 165 z tablicą rekordów skryptu (scena, następny, rodzaj,
   flagi), polami promienia i indeksu oraz wartościami startowymi z §7.
   Runtime implementuje bramki `267290` i wyjście `2677b8` w kodzie
   rozgrywki, nie w danych.

## 9. Producenci sygnałów #41 na tej ścieżce

| Sygnał | Producent na ścieżce sceny 4 | Wartości | Status |
|---|---|---|---|
| `15f6a8` tryb | `299ccc` (2), `299f08` (0) | 0 → 2 w iteracji S; 2 → 0 w E | CONFIRMED; brak innych zapisów w trakcie INFERRED |
| `15f6bc` frame-skip | `299bf4` (S), `299ec0` (E); czyści `2468f4`; `1f7128` przy przekroczeniu czasu `2a1c68` | 1 w S i E, inaczej 0 | CONFIRMED |
| `15f6ac` frames-in-mode | `246a6c` w S i E (zmiana trybu) | 0 po S i po E; wyjątek dogonienia §2 | CONFIRMED |
| `16c9b0` | memset `299bf0` | 0 przez całą scenę → dogonienie trybu 2 dozwolone | CONFIRMED; jedyny czytelnik `246d24` |
| `1414d4` stan bohatera | `222ce0` przez `222b80(100,2)` i `222b80(0,1)` | 100 w scenie, 0 po niej | CONFIRMED |
| `141501` (`+0x20b1`) | Brak producenta na ścieżce sceny | 0 (z `205278`) | INFERRED |
| `15efb4` maska pauzy | Nie czytana w trybie 2 | — | CONFIRMED brak bramki w `29a300` |
| `16c170` | Czytany tylko w trybie 0 (`246c84`, pusty `1f7110`) | — | CONFIRMED |
| `1ba1ac` | `246c14` w każdej iteracji bez frame-skip | — | CONFIRMED (#36) |
| `15f4f8` licznik prezentacji | `246d44/246d68` oraz `1f9bb8`: +7 w S, +14 w E | — | CONFIRMED; 7 przy k = 5 INFERRED |
| `15ee40` suma TIMER1 | Blokujące pętle wydłużają iterację S (16-bit T1 się zawija) | — | CONFIRMED mechanizm; liczby UNKNOWN |
| `15f4fc` zaciemnienie | `299cc0` (1.0), `29a4b4/29a4bc` (−0.34, min 0), `299f10` (0) | 0 od `n = 3` | CONFIRMED |
| `15f6b0` licznik skryptu | `299148` w każdym update trybu 0 i 2 | rośnie | CONFIRMED |
| `13d6d4` postęp field-15[28..31] | `267448` przy starcie | 0 → 1 | CONFIRMED |
| `13d498` postęp field-5[8] | — | 1 od P5 (#38) | CONFIRMED |
| `15182a` stan strumienia dialogu | `266084` (1), `267054` (2), `267014` w callbacku polecenia 79 (3), `266120` (4), `299eb0` (5) | 1 → 2 → 3 w pętli `299dd0`, 4 przy starcie, 5 przy wyjściu | CONFIRMED zapisy EE; czas odpowiedzi IOP UNKNOWN |
| `16cbf0` tangens | `29a1a8` (0.4141117), `299f00` (0.63) | — | CONFIRMED |

Nie podstawiać `mode_after_arm = 0` w iteracji S ani zer obrazu dla
`15ee40`. Wartości „0 przez całą scenę” powyżej wynikają z memsetu
startu, a nie z obrazu startowego.

## 10. Kryteria późniejszego testu New Game → scena 4 → sterowanie

Test integracyjny (runtime z pakietem v15) ma dowodzić:

1. **Start:** pierwsza iteracja trybu 0 po wejściu kończy się trybem 2,
   frame-skip bez renderu i bez `15f4f8++` w samej pętli, `frames_in_mode
   = 0`, stanem bohatera 100, zapisem postępu field-15[28] = 1 i
   7 blokującymi prezentacjami (PAL, przy k = 5).
2. **Zegar:** dokładnie 679 próbek; próbka `n` używa chunku `⌊n/80⌋`
   i update'u `n mod 80`; dogonienie zwiększa `n` bez renderu; brak
   bramki pauzy w trybie 2.
3. **Koniec normalny:** przy `n = 680` brak próbki, tryb 0, stan 0,
   `16cbf0 = 3f2147ae`, `15f4fc = 0`, 5 aktorów usuniętych, 14 blokujących
   prezentacji i frame-skip w tej iteracji.
4. **Pominięcie:** zbocze START przy `n < 15` (PAL) nic nie robi; przy
   `n >= 15` daje identyczny stan wyjścia jak punkt 3. Przycisk
   trzymany od wcześniej nie wywołuje pominięcia, bo liczy się zbocze.
5. **Sterowanie:** pierwsza iteracja C1 ma `frames_in_mode = 0`,
   zamkniętą bramkę pauzy przez 8 iteracji, ruch z pada w stanie 0 i brak
   startu sceny 1 przy odległości > 32 od obiektu 834.
6. **Przypadek dogonienia:** koniec w update dogonienia zostawia
   kontynuowany licznik, a renderuje się już pierwsza iteracja trybu 0.
7. **Brak wartości zastępczych:** brak pakietu sceny, właściciela
   strumienia dialogu albo kamery po scenie zwraca jawny status
   `incomplete`, a nie tryb 0.

Bez śladu referencyjnego test potwierdza zgodność z modelem źródła,
nie z przebiegiem PS2. Ślad referencyjny (EE: `15f6a8`, `16c994`,
`1414d4`, `15f4f8`, `15182a` na klatkę) pozostaje kolejnym dowodem.

## 11. Pliki potrzebne do implementacji

- `include/openrc/rac_scene_animation_compile.hpp`,
  `src/core/rac_scene_animation_compile.cpp` — `RacSceneClockV1`.
- Nowy `include/openrc/rac_level_scene_compile.hpp`,
  `src/core/rac_level_scene_compile.cpp` — timeline sceny poziomu.
- `src/core/rac_frontend_numeric.cpp` (reużycie rotacji),
  `include/openrc/third_person_camera.hpp` (stałe projekcji).
- `src/compiler/native_game_prepare.cpp`,
  `include/openrc/native_game_prepare.hpp` — profil v15 i zasoby.
- `src/core/actor_behavior_scene*.cpp` — program wyzwalacza 834.
- `src/core/runtime_gameplay.cpp`, `include/openrc/runtime_gameplay.hpp`,
  `src/runtime/windows_main.cpp` — ramię 2, start/wyjście, blokujące
  prezentacje, bez zmiany czystego modelu #36.
- Testy: `tests/rac_scene_animation_compile_tests.cpp`, nowy
  `tests/rac_level_scene_compile_tests.cpp`,
  `tests/runtime_gameplay_tests.cpp`, `tests/scene_timeline_tests.cpp`.
- `CMakeLists.txt` dla nowych celów.

## 12. Otwarte (UNKNOWN)

- Czas i wynik odpowiedzi IOP na polecenia 44 i 79 (producent stanu 3
  po stronie EE to `266ff0`, §3.0), a więc liczba vsynców w `299dd0`.
- Liczba vsynców `266f08(1)` i czas odczytu chunków z płyty.
- Role `167114`, `15eeb0`, `15eeb4`, `1601f0`, `23ec00`, `299108`,
  `17c440..17c580`.
- Wpływ `166ec0` ze sceny na pierwsze oko kamery typu 0.
- Zapisy `+0x208c` i `141501` przez callbacki w trakcie stanu 100.
- Identyfikator strumienia dialogu sceny 4 i jego dane audio.
- Arytmetyka `1feed0` na sprzęcie (F85) oraz `15ee68` dla drugiego
  selektora.
