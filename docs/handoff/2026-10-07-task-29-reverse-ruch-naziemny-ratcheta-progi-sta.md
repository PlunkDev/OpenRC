# Task #29: Reverse: ruch naziemny Ratcheta — progi stanów 0/2 z opóźnieniem próbki, wygładzanie tempa i obrót

- Date: 2026-10-07
- Kind: reverse
- Agent: claude (gpt-6.1-sol)
- Reviewer: codex
- Paths: include/openrc/rac_player_locomotion.hpp, src/core/rac_player_locomotion.cpp, tests/game_input_tests.cpp, docs/RAC_PLAYER_LOCOMOTION_V1.md

## Summary
Poprawiłem błąd wskazany w recenzji. W stanie 2 wartość `+0x194` zapisuje nie tylko wygładzanie i wejście, ale też sonda krawędzi `0x2178a0`: gdy licznik `+0x1f4` jest niezerowy, wywołuje ona `0x2144a0(0.0)` i zeruje tempo oraz prędkości w tej samej klatce. Ta sama sonda działa też w stanie 0 (`0x218204`, przed zanikiem tempa). W `docs/RAC_PLAYER_LOCOMOTION_V1.md` i w §10 ignorowanego dokumentu dowodowego opisałem ją jako CONFIRMED, znaczenie `+0x1f4` i wnętrze `0x1efff0` oznaczyłem jako UNKNOWN i dopisałem sondę do „Not implemented”. Skrypt `reach_pace.py` pokazał, że przez bezpośrednie wywołania stan 2 dochodzi tylko do dwóch miejsc zapisujących `+0x194`: wygładzania i `0x2144a0`. Logika kodu się nie zmieniła; zmieniłem tylko komentarze i zawinięcia linii. Build, test game-input, ctest 141/141 i 4 audyty PE przechodzą.

## Handoff
## Stan (#29, po recenzji)
- Gałąź `plunkai/29-...` ma commity `320d208`, `13186cc` i `22b1bdd`. Ostatni to automatyczny checkpoint Quorum z ogólną wiadomością, ale zawiera całą poprawkę po recenzji. Worktree jest czyste, a `merge-tree` z `main` 811d33e przechodzi czysto.
- Recenzja [major] jest naprawiona. Teza „w stanie 2 `+0x194` zapisują tylko wygładzanie i wejście” była fałszywa i zastąpiłem ją sekcją **Edge probe** w `docs/RAC_PLAYER_LOCOMOTION_V1.md`.
  - **CONFIRMED:** T1[2] w `0x21cb5c..0x21cb7c` sprawdza, czy `+0x1f4` jest niezerowe, i jeśli tak, woła `0x2178a0(5.0, 0.2)`. Dzieje się to po `0x212740`/`0x212790`.
  - **CONFIRMED:** wspólny T1 stanów 0/1/3/4/61/128 woła `0x2178a0(3.7, 0.0)` zawsze w `0x218204`, przed zanikiem tempa.
  - Opisałem pełne bramki i geometrię odcinka sondy. Wartości: przesunięcie `+0xe0*skala`, minimum przez `C/S` słowa `[+0x2080]+0x48`, z=0, +pozycja `+0x80`, odcinek od +0.3 do −0.2 (−0.35 w stanie 32, −0.7 przy `+0x20a4==2`).
  - Warunek zerowania: brak trafienia albo kąt `0x2345b0 > 0.8726646`. Wtedy `0x2144a0(0.0)` mnoży `+0x100`, `+0x110`, `+0xe0`, `+0x150` i `+0x194` przez 0.
  - **CONFIRMED:** `+0x1f4` to licznik halfword odliczany co aktualizację przez `0x1fef48` (wywołanie w `0x221e5c` z `0x221d50`).
  - **INFERRED:** to zatrzymanie na krawędzi lub stromej powierzchni.
  - **UNKNOWN:** kto ustawia `+0x1f4`, wnętrze `0x1efff0` i bloku `0x173f40`, ścieżki `+0x20b3` równe 1 lub 2.
- Poprawiłem też drobne uwagi z recenzji:
  - „walls can act” ma teraz status INFERRED;
  - warunek resetu `+0x30e` jest pełny: dystans < 0.02 i (kąt ≤ 0.8726646 albo `+0x20b3==1` albo kategoria 22);
  - `+0x20b3` nie jest już przypisywane strafe/lock, tylko UNKNOWN;
  - linie powyżej 80 kolumn w `.hpp`/`.cpp` są zawinięte.
- Komentarz przy `step_rac_player_ground_frame_v1` mówi teraz, że sonda krawędzi nie jest odwzorowana.

## Dowody
- Disasemblacja: `local/forensics/player-ground/level00_code.asm` (ignorowany). Pełny opis jest w §10.1–10.3 pliku `ratchet-ground-source-evidence.md`, z notą o korekcie.
- Nowy skrypt `local/forensics/player-ground/reach_pace.py` śledzi bezpośredni przepływ i `jal`; nie podąża za `jalr` ani za `jr` z tablic skoków. Wyniki:
  - `21c6d8`: zapisy w `0x212768`/`0x212780` i `0x21450c`; z `--skip 2178a0` znika `0x21450c`;
  - `22bc04 --skip 222b80`: brak zapisów;
  - `2076e8 --skip 2180f8 222b80`: brak zapisów, 21 nierozwiązanych skoków pośrednich;
  - `218188`: te same trzy miejsca.

## Pułapki przy podłączaniu do runtime (#32 / następne zadanie)
- Tempa ze `step_rac_player_ground_frame_v1` nie wolno traktować jako ostatecznego w klatkach, w których działa sonda krawędzi. Potrzebna jest kolizja i sonda podłoża.
- Pozostałe pułapki bez zmian:
  - bramka 0.25 w `gate_rac_player_stick_sample_v1`;
  - poprzednia próbka dla przejść;
  - krok nie przełącza stanów;
  - arytmetyka to binary32 hosta, nie EE/VU0.

## Verification (agent)
Na HEAD `22b1bdd` w `build-slot2` (cmake 4.4.2 z `local/tools`):
- `cmake --build build-slot2`: exit 0, bez ostrzeżeń i błędów;
- `openrc-game-input-tests.exe`: wypisuje „game_input_tests: ok”, exit 0;
- `ctest --test-dir build-slot2`: 100% z 141, exit 0;
- `openrc-portable-executable-audit.exe` dla openrc-portable-executable-audit.exe, openrc-cli.exe, openrc-launcher.exe i openrc-runtime.exe: każdy exit 0;
- `git merge-tree --write-tree main HEAD` (main 811d33e): exit 0, bez konfliktów.

Tezy dowodowe sprawdziłem ręcznie w `level00_code.asm` oraz skryptem `python -I reach_pace.py` z korzeniami 21c6d8, 22bc04, 2076e8 i 218188.

## Next steps
- Podłączyć prymitywy do `runtime_gameplay.cpp` po zwolnieniu pasa przez #32, w kolejności: bramka 0.25 → poprzednia próbka → predykaty T3 → `step_rac_player_ground_frame_v1` → `enter_rac_player_ground_move_v1`. Sondę krawędzi `0x2178a0` traktować jako osobny, jeszcze niezaimplementowany krok.
- Ustalić, kto ustawia licznik `+0x1f4` (bezpośredniego `sh ..., 500(` nie ma; szukać zapisów przez inne bazy lub wskaźniki) i zdekodować sondę odcinka `0x1efff0` oraz blok wyników `0x173f40`.
- Rozwiązać skoki pośrednie (`jalr` i `jr` z tablic skoków) pominięte przez `reach_pace.py`, żeby domknąć listę miejsc zapisujących `+0x194`.
- Zdekodować kolizję i ślizg w `0x213f38` oraz helpery `0x1ff860`, `0x1ff798`/`0x1ff7b0`.
- Zaktualizować `docs/REFERENCE_BUILD.md` i `docs/ROADMAP.md` (poza pasem) o ustalenia z `docs/RAC_PLAYER_LOCOMOTION_V1.md`.
