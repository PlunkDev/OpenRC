# Task #38: Reverse: wyzwalacze startu Veldinu — sonda 25a6d0 i wysokość spawnu, instancja klasy 834 i bajt 0x13d498, role 2666a8/266670

- Date: 2026-10-07
- Kind: reverse
- Agent: claude (claude-opus-5-5)
- Reviewer: codex
- Paths: docs/RAC_VELDIN_START_TRIGGERS_V1.md, docs/LEVEL_ENTER_SOURCE_V1.md, local/forensics/veldin-start-triggers

## Summary
Ukończyłem i zacommitowałem (e206b7d) analizę wyzwalaczy startu Veldinu, którą poprzednia próba zostawiła niezacommitowaną. Najważniejsze ścieżki instrukcji sprawdziłem ponownie w listingu: 25a6d0, 205368..2053cc, 2db278, 267290..267530, 2677b8, 299b68 i P14 246748..2467ac. Doszły nowe ustalenia: sprawdzenie startera scen statku 28f458 i jego bajtów postępu po resecie, wywołanie 266670(0) w 299bc8 oraz zapis 141728=1. Wniosek: New Game na Veldinie uruchamia przed sterowaniem scenę 4 w trybie 2. Wszystkie bramki są CONFIRMED, cały przebieg INFERRED. Wzór wysokości spawnu `Z = R>0 ? R : 31.43` jest CONFIRMED, a dokładna wartość R UNKNOWN (kandydat geometryczny ≈31.4266, INFERRED). Zaktualizowałem LEVEL_ENTER_SOURCE_V1 (§1 w.10, §2, §3, §5, prerequisites). Kodu nie zmieniałem.

## Handoff
## #38 — wyzwalacze startu Veldinu

**Co zrobiono**
- Dodano `docs/RAC_VELDIN_START_TRIGGERS_V1.md`. §0 to tabela odpowiedzi na pytania 1–5 ze statusami i adresami, §1–§5 zawierają szczegóły.
- Zaktualizowano `docs/LEVEL_ENTER_SOURCE_V1.md`:
  - w §1 wiersz 10 (gałąź P14 dla New Game to `2666a8`);
  - w §2 scena przed sterowaniem: TAK (INFERRED);
  - w §3 kontrakt sondy i wzór wysokości;
  - w §5 nowe wiersze tabeli oraz lista prerequisites.
- Dowody leżą w `D:\! Projekty\OpenRC\local\forensics\veldin-start-triggers` (README.md z hashami ISO/ELF/nakładki/pakietu, UTF-8 bez BOM).

**Konkluzje**
- New Game na Veldinie: scena przed sterowaniem **TAK (INFERRED)**.
  - Rekord 165 klasy 834 przechodzi admission.
  - Callback `2db278` w przebiegu ładowania `2657b8` (tryb 6) zapisuje `13d498=1` w `2db35c` i zasięg 255.
  - P14 woła `2666a8`.
  - W pierwszym przebiegu Moby trybu 0 bramki `267290` przechodzą (rekord skryptu 0 automatyczny, odległość ≈160 ≤ 510), co prowadzi do `299b68(4)`: tryb 2 w `299ccc` i `222b80(100,2)`.
  - Odtworzona zostałaby scena 4 z 0–6 (PAL unique 590).
- Wysokość spawnu: `Z = R>0 ? R : 31.43`, gdzie R to bezwzględne Z trafienia odcinka `(X,Y,Z+0.5)→(X,Y,0.01)` w `1efff0`. Wzór jest CONFIRMED. Dokładne bity R są UNKNOWN; statyczna siatka daje ≈31.4266 (INFERRED).

**Dowody i weryfikacja**
- `python -I verify_listings.py` daje PASS: 4145 słów listingu zgodnych z ELF/nakładką (dodano zakres `28f458..28f810`).
- `python -I inspect_source.py` daje PASS: hashe, rekordy 0 i 165, klasa 834, tag 5 resetu z sumą kontrolną, nowe bajty resetu `13d4d8..db`, `13d5e4`, `13de53`, `13de4b`, wszystkie równe 0.

**Co dalej**
- Ślad normalnego New Game (emulator), który obejmie `2657b8`, pierwsze `2658c0` i `299b68` z żywymi globalami; to podniesie wniosek do CONFIRMED.
- Długość sceny 4 i pierwsza sterowalna klatka po niej: właściciel trybu 2 i powrót do trybu 0.
- Wykonanie lub odtworzenie `1efff0` dla dokładnego Z spawnu.
- Lowering 205278/205598 nie może przyjmować pierwszej klatki jako sterowalnej.

**Pułapki**
- W worktree `local` jest junctionem; skrypty używają ścieżek bezwzględnych.
- Gałąź jest na 811d33e, a main poszedł dalej. Zmieniono wyłącznie dwa pliki docs, więc konflikt jest możliwy tylko w LEVEL_ENTER_SOURCE_V1.md.
- Wynik runtime v14 `13d498=0` nie przeczy jedynce, bo runtime zatrzymuje się przed entry.

## Verification (agent)
Projektu nie budowałem i nie uruchamiałem testów, bo zmiany dotyczą wyłącznie dokumentacji; src/, include/, tests/ i CMakeLists.txt są nietknięte. `python -I verify_listings.py` przechodzi (PASS, 4145 słów listingu zgodnych z ELF i nakładką). `python -I inspect_source.py` przechodzi (PASS: hashe ISO 0f18a6c8…3260 i ELF 17f8a846…b122b, suma kontrolna resetu 0x9ad4, nowe bajty resetu równe 0). portable-checks.json z poprzedniej próby to audyty PE i 3 testy z build-portable z exit 0; nie uruchamiałem ich ponownie. Ręcznie odczytałem 25a6d0, 205340..2053cc, 2db278..2db41c, 246748..2467ac, 267290..26753c, 2676e8..2677a4, 2677b8..267888, 299b68..299cec i 28f458..28f80c. Pliki docs i dowody są w UTF-8 bez BOM, a local/ jest ignorowane przez .gitignore. Commit e206b7d.

## Next steps
- Ślad normalnego New Game (emulator) przez 2657b8 → 2658c0 → 299b68 z żywymi globalami, aby podnieść 'scena przed sterowaniem TAK' z INFERRED do CONFIRMED
- Reverse właściciela trybu 2 (scena 4): długość sceny i pierwsza sterowalna klatka po powrocie do trybu 0
- Wykonanie/odtworzenie 1efff0 (korzeń 173f40, filtry, kolizje Moby, kolejność VU) dla dokładnych bitów Z spawnu
- Lowering 205278/205598 i callbacku 2db278/skryptu 267290 jako komponent core po zadaniach harmonogramu (#36) i transakcji wejścia
- Ustalić, czy obiekt statku (2440c8) powstaje na Veldinie, oraz rolę halfwordu 0x141728
