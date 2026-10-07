# Task #45: Scena 4 Veldinu: wykonanie, zakończenie i powrót do sterowania

- Date: 2026-10-07
- Kind: reverse
- Agent: claude (claude-opus-5-5)
- Reviewer: codex
- Paths: docs/RAC_VELDIN_SCENE4_EXECUTION_V1.md, local/forensics/veldin-scene4-execution

## Summary
Uwzględniłem uwagę recenzji. Dokument zawiera teraz pełny łańcuch stanu strumienia dialogu sceny `15182a` (0 → 1 → 2 → 3 → 4) z warunkami po stronie EE. Wartość 3 zapisuje callback `266ff0` polecenia IOP 79, rejestrowany przez `12ee40` w `266708(151820)` (`2668a0..2668b0`). Pompę stanowi pętla `299dd0`, która przez `28dee0` → `28eb2c` → `2668f0` → `266cd0` dochodzi do `266708`. Dodałem też źródło sektora strumienia: słowo TOC zależne od języka `15ee88` (dla sceny 4: `1626409, 0, 1626580, 1626751, 1626922, 1627093`). Wartość 1 dałaby brak zlecenia i nieskończone oczekiwanie, co zgadza się z dopuszczalnym zbiorem `{0,2,3,4,5}` w `rac_new_game_flow.cpp`. Czas i wynik odpowiedzi IOP pozostają UNKNOWN. Zmiany są w commicie `e668ed3`.

## Handoff
## #45 — poprawka po recenzji (producent `15182a = 3`)

**Co zrobiono (commit `e668ed3`):**
- Nowa sekcja §3.0 w `docs/RAC_VELDIN_SCENE4_EXECUTION_V1.md` opisuje łańcuch stanów kanału `151820` (uchwyt `+0`, stan `+0x0a = 15182a`, żądanie `+0x0c`):
  - pompa: `299dd0` → `28dee0` → `28eb2c` → `2668f0`, bez gałęzi omijającej; całość jest pomijana przy `1517db != 0`;
  - stan 1: zlecenie `266a5c` → `265f40` → `266018..2660cc`, gdy `1517ec >= 0` i uchwyt jest równy 0; sektor = `*(13a764 + 592·scena + 4·15ee88)`, sektor 0 oznacza brak zlecenia; wynikiem jest stan 1, uchwyt −1 i polecenie IOP 44 z callbackiem `267020`;
  - stan 2: callback `267020` przy niezerowym wyniku;
  - stan 3: `266708` przy stanie 2 woła `12ee40`, czyli polecenie IOP 79 (`12e820`), z callbackiem `266ff0`; ten zapisuje 3, gdy wynik ≠ 0 i stan nadal wynosi 2;
  - stan 4: `2660e0`.
- Pozostałe aktualizacje:
  - wiersz `15182a` w mapie sygnałów §9 (wartości 1/2/3/4/5 z adresami);
  - wiersz audio w §8;
  - lista UNKNOWN: zostały tylko czas i wynik odpowiedzi IOP.

**Dowody:**
- `verify_words.py` sprawdza teraz 2715 słów z 44 zakresów, 0 niezgodności; nowe zakresy to `266708..2668ec`, `266ff0..26701c`, `2668f0`, `266a00..266cd4`, `28dee0`, `28eb2c`, `12ee40`, `265f40..2660d8`;
- nowy `scene4_stream.py` → `scene4-stream-words.json` (słowa TOC sektora dialogu sceny 4 dla `15ee88 = 0..5`).

**Pułapki dla #41:**
- Właściciel strumienia dialogu musi przejść 1 → 2 → 3 w kolejnych krokach pompy. Nie wolno ustawiać 3 bez odpowiedzi.
- Przy `15ee88 = 1` oryginał zawiesza się w `299dd0`. Runtime powinien to odrzucić jawnie, co zgadza się z walidacją w `rac_new_game_flow.cpp:156`.

Reszta handoffu z poprzedniego raportu (zegar 680 / 9 chunków, pominięcie START, wyjście `299e70`, luki v14, kryteria testu) jest aktualna.

## Verification (agent)
Zadanie było wyłącznie analityczne: nic nie budowano i nie uruchamiano żadnych EXE.
- `python -I verify_words.py`: 2715 słów z 44 zakresów zgodnych z ELF i łańcuchem sekcji nakładki, 0 niezgodności, exit 0.
- `python -I scene4_stream.py <ISO>`: odczytał 6 słów sektora strumienia sceny 4 z lokalnego TOC.
- Ręcznie, odczytem listingu, sprawdziłem, że w `28dee0` nie ma gałęzi omijającej `28eb2c` (grep celów skoku w zakresie `28dee4..28eb8c`).

## Next steps
- Decyzja Orkiestry: profil pakietu v15 z zasobem timeline sceny 4 oraz modelami i rigami klas 110, 530, 1515 i 1365.
- Kompilator: RacSceneClockV1 i compile_rac_level_scene_timeline_v1 (§8 dokumentu).
- Runtime #41: neutralny właściciel strumienia dialogu z sekwencją stanów 1→2→3→4 według §3.0, ramię trybu 2, start i wyjście sceny według §3–§6 oraz test według §10.
- Gdy będzie możliwe: ślad referencyjny New Game do kwalifikacji czasu odpowiedzi IOP (polecenia 44 i 79) i liczby vsynców w 299dd0.
