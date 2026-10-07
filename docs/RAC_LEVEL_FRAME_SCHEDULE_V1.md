# Harmonogram klatki poziomu RAC V1

`openrc/rac_level_frame_schedule.hpp` jest czystym komponentem core. Modeluje
słowa prologu entry `0x2465f8`, wybór trybu `2901a8` i jedną iterację pętli
klatki `0x2468a8..0x246e84`. Źródło to PAL ELF `SCES_509.16` o SHA-256
`17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b`
(F33/F34). Dowody leżą w `local/forensics/level-enter-source/lvl.asm`, a
notatki tego zadania w `local/forensics/level-frame-schedule/source-notes.md`.

Komponent nie wykonuje kodu poziomu i nie jest podłączony do runtime.
`plan_rac_level_frame_v1(state, input)` zwraca uporządkowane kroki z adresami
źródłowymi, następny stan słów harmonogramu i wynik bramek. Wejście
`RacLevelFrameInputV1` zawiera wartości odczytane w konkretnych punktach
źródła. Między tymi punktami działają nieprzezroczyste wywołania, które mogą
zmienić słowo trybu, więc każdy odczyt jest osobnym polem i nic nie ma
wartości domyślnej.

## Adres → zachowanie → status

| Adres | Zachowanie | Status |
|---|---|---|
| `0x2466d4`, `0x2466dc` | Prolog zapisuje `0x15f6ac = 0` (frames-in-mode) i `0x15f6a8 = 6`. | CONFIRMED |
| `0x246748`, `0x24674c` | `122598(0)`; w slocie opóźnienia `sw zero, -0x7808(gp)` czyli `0x15f4f8 = 0`. | CONFIRMED |
| `0x15ee40` | Prolog nie zapisuje 64-bitowej sumy TIMER1, więc wartość sprzed prologu jest obowiązkowym argumentem `rac_level_loading_state_v1`. | CONFIRMED brak zapisu w `2465f8..2468a4`; UNKNOWN wartość |
| `0x2901d4..0x29021c` | `2901a8`: poziom 1 i bajt `0x13de4b == 0` → 0 (`beql`, zapis w slocie `2901ec`); poziom 0 → 0 (`290218`, zapis w slocie `29021c`); poziom 14 i bajt `0x13d4f8 == 0` → 0; pozostałe `slti level,20` (ze znakiem) → 6, w tym poziomy ujemne; `>= 20` → 0. | CONFIRMED |
| `0x290220..0x290244` | Gałąź trybu 6 zapisuje też `0x13e154 = -1` (półsłowo), `0x15f4fc = 1.0` i woła `2a1a58`. Model zwraca tylko tryb. | CONFIRMED gałąź; efekty poza modelem |
| `0x2468a8..0x2468b0`, `0x246e4c..0x246e50` | Głowa pętli: niezerowe `0x15f650` → `2940e0` (`246e88`) i wyjście, przed odczytem TIMER1. | CONFIRMED |
| `0x2468b8..0x2468e4` | `0x15ee40 += zext32(lw 0x10000800)` (64-bit `daddu`), potem `sw zero, 0x10000800`. | CONFIRMED |
| `0x2468e8..0x24691c` | `1f70f0`, w slocie `2468f4` `0x15f6bc = 0`, potem `2a1b58`, `2a1ae8`, `200ff0`, `201300`, `200ef0`, `12ddc0`. | CONFIRMED kolejność; UNKNOWN role |
| `0x246924..0x246948`, `0x1e8e50` | `lw s0, 0x15f6a8`; `addiu v1,s0,1`; `sltiu v1,9`; skok przez tablicę 9 ramion. Tryb spoza −1..7 (także ujemny, bo porównanie jest bez znaku) omija ramię i przechodzi do `246a58`. | CONFIRMED |
| `0x246a58..0x246a78` | Słowo trybu po ramieniu porównywane z `s0` (`beq`). Zmiana → `0x15f6ac = 0` (`246a6c`), brak zmiany → `addiu +1` i zapis (`246a78`), z zawinięciem 32-bit. W `lvl.asm` i `elf.asm` jedynymi zapisami `0x15f6ac` są `2466d4`, `246a6c` i `246a78`. | CONFIRMED |
| `0x246a7c..0x246aa0` | Niezerowe `0x15f6bc` → `2a1c68(1)`, `2a1a88`, skok do `2468b0` (bez F9, vsync i licznika prezentacji). Flagę zeruje każda iteracja (`2468f4`). Poza `2468f4` zapisuje ją 25 innych instrukcji w `lvl.asm`, m.in. `1f713c` w `1f7128` (ustawia 1, wołane z `2a1cc4`). | CONFIRMED gałąź; UNKNOWN znaczenie producentów |
| `0x246aa4..0x246ab0` | `24c308`, `24b638`. | CONFIRMED kolejność; UNKNOWN role |
| `0x246ab4..0x246c08` | Nakładka: jeśli `0x13d46c` ∈ {3..6, 9..12, 15..20} i `0x1ba1ac == 0`, to `1f93e0(0)`, `2a1e38`, `23bba0`, `23be70`, `23bba0`, `23bc50`, `23cc38`, `1f94f8`. Kąt w `f16` to `(0x15f4f8 mod 55)·(−2π)/55`. | CONFIRMED warunek i wywołania; INFERRED wirująca ikona; UNKNOWN rola `0x13d46c` |
| `0x246c0c..0x246c38` | `0x1ba1ac = 0` (slot `246c14`), `1f3a78`, `1f7118` i `1f7120` (oba to puste `jr ra`), `2a1c68(1)`. | CONFIRMED |
| `0x246c40..0x246c8c` | `0x15f6b4 = cvt.s.w(lw 0x10000800) / 9600.0` przy `0x15ee80 == 0`, inaczej `/ 11520.0` (`2001f0` to samo `cvt.s.w`). Model liczy bity przez `ee_cop1_div_bits_v1` (referencja FDIV PS2, `physical_console_qualified=false`, F25). | CONFIRMED wzór; INFERRED dokładność bitowa |
| `0x246c84..0x246ca4` | Tryb 0 i `0x16c170 != 0` → `1f7110` (pusty `jr ra`). | CONFIRMED |
| `0x246cb4..0x246cb8` | Niezerowy bajt `0x141501` (gracz `+0x20b1`) omija dogonienie, vsync i licznik i przechodzi do `246d88`. | CONFIRMED |
| `0x246cbc..0x246cf4` | Tryb (odczyt `246cbc`) 0 lub 2: `lw 0x10000800`, próg `movn 9600 → 11520` przy `0x15ee80 != 0`, `slt prog,licznik`. Dogonienie zachodzi tylko przy liczniku **ściśle większym** od progu (ze znakiem). | CONFIRMED |
| `0x246cfc..0x246d14` | Dogonienie w trybie 0: `268738`, `299250`. Nie ma renderu `1f91b0`. | CONFIRMED |
| `0x246d1c..0x246d3c` | Dogonienie w trybie 2 tylko przy `lh 0x16c9b0 == 0`: `268738`, `29a300`. | CONFIRMED |
| `0x246d40..0x246d60`, `0x246d64..0x246d84` | Obie ścieżki prezentacji: `122598(0)`, `0x15f4f8 += 1` (zapis w slocie `jal 285f18`, więc przed wejściem do `285f18`), `285f18`, `0x15f690 = 0`. Dokładnie raz na iterację, która doszła do F10. | CONFIRMED |
| `0x246d88` | `1f88c0` na każdej ścieżce poza frame skip. | CONFIRMED |
| `0x246d90..0x246e14` | Tryb 0 (odczyt `246d94`) i niezerowy `0x141501` → `2a1a88`, `12ec40`, `12ddc0`, `28f320`, `266508`, `244ae0(0,1)`, w slocie `0x15f6a8 = 0`, `266210`, `235828`. | CONFIRMED gałąź; UNKNOWN znaczenie (respawn?) |
| `0x246e18..0x246e84` | Rekord czasu: `q = 1feed0(lw 0x15efa4) / 600`; jeśli `lhu [0x151782+8·lvl] < q` (ze znakiem), drugie `1feed0` i `sh` jego ilorazu. | CONFIRMED |
| `0x2992e8..0x299324` | Bramka pauzy wewnątrz `299250`: `0x15f6a8 == 0` (`bne`), `0x15efb4 & 0x401 != 0` (`andi`), `slti frames,8` → zamknięta przy `< 8` (ze znakiem), więc otwarta przy `>= 8`; `0x1414d4 != 29` → `29950c`. | CONFIRMED |
| `0x29950c..0x299518` | Otwarta bramka: `2016e0(3,0,0)` i skok do epilogu `299b4c`, czyli reszta update jest pomijana. | CONFIRMED |
| `0x20170c..0x20174c` | `2016e0` zapisuje poprzedni tryb do `0x173154` i ustawia `0x15f6a8 = 4`; gdy poprzedni tryb ≠ 3, woła `12e528(29)` i `266670(0)`. | CONFIRMED; INFERRED rola „menu” trybu 4 |
| `0x23e248..0x23e258` | TIMER1: `T1_MODE (0x10000810) = 0x82`, `T1_COUNT (0x10000800) = 0`. | CONFIRMED |
| `0x10000810 = 0x82` | CLKS=2 (BUSCLK/256), CUE=1. Przy BUSCLK 147,456 MHz daje to 576 000 tików/s. 9600 tików = 1/60 s, a 11520 = 1/50 s = 20 ms. | INFERRED (dokumentacja sprzętu EE, nie pomiar) |
| `0x2623d0..0x2624ec` | Inicjalizacja wideo: `a0 == 0` → `0x15ee80 = 0`, krok `0x15ee6c = 1/60` (`0x3c888889`, slot `26244c`); `a0 != 0` → `0x15ee80 = 1`, `0x15ee6c = 0.02` (`0x3ca3d70b`), `0x15ee60 = 1.2`, `0x15ee68 = 0.8333`. Proporcja progów 9600:11520 = 5:6 odpowiada krokom 1/60:1/50. Wywołują ją `242400` i `24242c`, warunkowo względem `0x15ee60`. | CONFIRMED stałe; UNKNOWN wartość `0x15ee80` w danym przebiegu |
| `0x15f6b4` | Odbiorcy m.in. `25f4f0`, `25fe14`, `26b8fc..26b97c` (porównanie z 0.85), `26c054..26cfdc`, `2b17a4`, `2cad34..2cb1a4`, `2d274c`. Globalny krok ruchu `0x15ee6c` (F17) **nie** pochodzi z `0x15f6b4`, tylko ze stałej z `2623d0`. | CONFIRMED adresy; UNKNOWN semantyka odbiorców |
| T1_COUNT 16-bit | Licznik sprzętowy ma 16 bitów i zawija się po 65 536 tikach (~113,8 ms). `lw` zwraca go z wyzerowaną górną połową, więc porównanie `slt` jest w praktyce nieujemne. | INFERRED (dokumentacja sprzętu) |

## Ramiona trybów (tablica `0x1e8e50`, indeks `mode+1`)

| Tryb | Ramię | Wywołania | Rola |
|---:|---|---|---|
| −1 | `246950` | `268738`, `1f4918`, `1f5c60` | UNKNOWN |
| 0 | `246970` | `268738` pad, `299250` update, `1f91b0` render | rozgrywka, CONFIRMED |
| 1 | `246990` | `29ad18` | UNKNOWN |
| 2 | `2469a0` | `268738`, `29a300`, `1f9210` | scena; INFERRED z właścicieli scen (`RAC_SCENE_ANIMATION_COMPILE_V1`) |
| 3 | `2469c0` | `268738`, `277a88`, `1f92c0` | przejście; INFERRED z napisu „Transition to level %d” |
| 4 | `2469e0` | `268738`, `202e48`, `1f9248` | UNKNOWN w kodzie; INFERRED „menu” (`2016e0`) |
| 5 | `246a00` | `268738`, `29d988`, `2a1540` | UNKNOWN |
| 6 | `246a20` | `268738`, `291868`, `292fd8` | przybycie; CONFIRMED wyborem `2901a8` dla poziomów 2..19 |
| 7 | `246a40` | `268738`, `29afb8`, `1f92e8` | UNKNOWN |

Kod nazywa stałymi tylko tryby 0, 2, 3 i 6. Krok `arm_render` dla trzeciego
wywołania ramienia jest CONFIRMED dla trybu 0. Dla pozostałych ramion to
INFERRED: każdy trzeci callee (`1f5c98`, `1f9218`, `1f9250`, `1f92c8`,
`1f92f0`, `2a1548`, `292fe0`) na początku czyta flagę frame-skip `0x15f6bc`,
podobnie jak render trybu 0 (`1f91b8`).

## Pytania z zakresu zadania

1. **Stan ładowania**: tryb 6 i `0x15f6ac = 0` od `2466d4/2466dc` do `2901a8`,
   potem `0x15f4f8 = 0` w `24674c` (CONFIRMED).
2. **`2901a8`**: tabela wyżej. `RacLevelModeSelectionInputV1` wymaga obu bajtów
   flag bez wartości domyślnych (CONFIRMED).
3. **Dispatch**: dziewięć ramion, tryb spoza zakresu nie wywołuje niczego i
   przechodzi do porównania trybu (CONFIRMED).
4. **frames-in-mode**: rośnie o 1 w `246a78` tylko wtedy, gdy słowo trybu po
   ramieniu równa się trybowi z `246924`. W przeciwnym razie zeruje się w
   `246a6c`. Zawija się jako 32-bit `addiu`. Bramka pauzy czyta je jako
   liczbę ze znakiem, więc po 2³¹ iteracjach pauza się zamyka (CONFIRMED).
   Dogonienie widzi już nową wartość licznika.
5. **Bramka pauzy**: tryb 0, `(0x15efb4 & 0x401) != 0`, `!(frames < 8)` ze
   znakiem, `0x1414d4 != 29`. Działa w update `299250`, czyli także w update
   dogonienia (CONFIRMED). Skutkiem jest tryb 4, więc w następnej klatce
   licznik zaczyna się od 0.
6. **Dogonienie**: `+0x20b1 == 0`, tryb z `246cbc` ∈ {0,2},
   `T1_COUNT > próg` (ostro). Tryb 2 wymaga dodatkowo `0x16c9b0 == 0`. W trybie 0:
   `268738` + `299250` bez renderu, potem `122598(0)`, `0x15f4f8 += 1`,
   `285f18`. Maksymalnie jedno dodatkowe update na iterację (CONFIRMED).
   Źródło zegara: `T1_MODE = 0x82` (CONFIRMED). 11520 = 20 ms PAL przy 576 kHz
   (INFERRED).
7. **frame skip i `0x15f6b4`**: wyjście frame-skip jest CONFIRMED. Współczynnik
   jest zapisywany tylko na ścieżce bez frame skip. Odbiorcy są znani z
   adresu, ich semantyka pozostaje UNKNOWN. `frame_time_ratio_bits_15f6b4 ==
   std::nullopt` oznacza brak zapisu, a nie zero.

## API

- `rac_level_loading_state_v1(elapsed_15ee40)` zwraca słowa po prologu.
- `rac_level_initial_mode_v1({level, byte_13de4b, byte_13d4f8})` realizuje `2901a8`.
- `rac_level_pause_gate_open_v1(observation, frames_in_mode)` realizuje `2992e8..299324`.
- `plan_rac_level_frame_v1(state, input)` zwraca plan jednej iteracji. Typy
  kroków: `pad`, `arm_update`, `arm_render`, `call`, `empty_stub_call`,
  `word_clear`, `vsync`, `presented_frames_write`, `frames_in_mode_write`,
  `catchup_test` i inne. `catchup = true` oznacza kroki update dogonienia.
  `next.mode` to ostatnia wartość słowa trybu, którą harmonogram zapisał lub
  odczytał. Host musi ją nadpisać, jeśli późniejsze wywołania nieprzezroczyste
  (ścieżka reload, `1feed0` albo wywołania przed ramieniem następnej iteracji)
  zmienią tryb.
- `rac_level_time_record_update_v1` realizuje `246e18..246e84`.
- **Polityka OpenRC** (nie zachowanie źródła): `openrc_virtual_timer1_ticks_v1(host_ns,
  hz)` przelicza czas hosta na wirtualne tiki z zaokrągleniem w dół i
  saturacją. `kOpenrcVirtualTimer1HzV1 = 576000` jest wartością wywiedzioną
  (INFERRED). `openrc_virtual_timer1_count_v1` daje 16-bitowy obraz rejestru.

## Integracja

Integracja wymaga scalenia #32. Ten komponent nie zmienia `src/runtime/`,
`runtime_gameplay.*` ani `fixed_step.*`.

1. **Zamiana `fixed_step` 60 Hz.** Właściciel sesji z #32 przechowuje
   `RacLevelFrameStateV1` od chwili `level/enter`. Inicjuje go przez
   `rac_level_loading_state_v1`, a `state.mode` ustawia z
   `rac_level_initial_mode_v1`. Dla Veldinu (poziom 0) daje to tryb 0
   niezależnie od flag. Każda prezentowana klatka hosta to jedna iteracja
   planu. Wirtualny TIMER1 jest zerowany w `2468e4`. Odczyty `timer1_at_ratio`
   i `timer1_at_catchup` to czas pracy od tego resetu, a `timer1_at_entry` to
   czas od poprzedniego resetu, przeliczony adapterem polityki. Host wykonuje
   kroki w kolejności planu. Słowo trybu, które zostawi natywne ramię, podaje
   jako kolejne obserwacje (`mode_after_arm`, `mode_after_post_arm`,
   `mode_at_reload_check`). Dla poziomu zastępuje to
   `game::FixedStepAccumulatorV1` (60 tików/s, do 8 kroków na `advance`,
   użyte w `runtime_gameplay.cpp`). W trybie PAL (`0x15ee80 != 0`) każdy krok `arm_update`
   trybu 0 jest jednym update z krokiem `0x15ee6c = 0.02 s`. Dogonienie
   dodaje drugi update bez renderu, gdy praca przekroczy 11520 tików. Nie ma
   pętli „wiele kroków na klatkę” poza tym jednym dogonieniem.
2. **Kadencja animacji 50→60 Hz (F20).** Obecny całkowity akumulator
   przeliczający kadencję PAL 50 Hz na tick 60 Hz trzeba usunąć razem z
   `fixed_step`. Animacje (`RuntimePlayerAnimationV1`) postępują o jedną
   klatkę PAL na każdy krok `arm_update` z planu, także na kroku dogonienia
   (`catchup = true`), a nie według czasu ściennego. Bez tego ruch (krok
   0.02 s) i animacja rozjadą się przy dogonieniu.
3. **Prezentacja.** `vsync` odpowiada prezentacji hosta (swap chain). Licznik
   `presented_frames` (`0x15f4f8`) zasila nakładkę (kąt mod 55) i musi należeć
   do tej samej sesji. Przy niezerowym `+0x20b1` iteracja nie prezentuje.
4. **Kroki bez właściciela.** `call` i `empty_stub_call` bez natywnego
   odpowiednika host pomija, ale je loguje. Puste stuby są CONFIRMED jako
   no-op. Frame skip, reload i nakładka potrzebują własnych właścicieli,
   zanim zostaną podłączone. Do tego czasu iteracja, która na nie trafi,
   powinna zgłaszać `incomplete` zamiast udawać sukces (F8, F26).
5. **Selektor wideo.** `video_selector_15ee80` to wejście. Wartość dla
   przebiegu New Game PAL musi pochodzić z modelu `2623d0` albo z obserwacji,
   a nie z założenia (UNKNOWN w tym zadaniu).

## Otwarte (UNKNOWN, bez wartości zastępczych)

- Role ramion −1, 1, 4, 5 i 7 oraz wywołań przed ramieniem i po nim.
- Znaczenie producentów `0x15f6bc`, odbiorców `0x15f6b4`, selektora
  `0x13d46c` i ścieżki reload.
- Czy na fizycznej konsoli bity `div.s` pokrywają się z referencją FDIV
  (`physical_console_qualified=false`).
- Faktyczna częstotliwość TIMER1 nie została zmierzona na sprzęcie.
