# Task #30: Reverse: skok, opadanie i lądowanie Ratcheta — fizyka PAL i sloty animacji z maszyny stanów (następca #27)

- Date: 2026-10-07
- Kind: reverse
- Agent: claude (claude-opus-5-5)
- Reviewer: codex
- Paths: include/openrc/player_simulation.hpp, src/core/player_simulation.cpp, tests/player_simulation_tests.cpp, include/openrc/runtime_player_animation.hpp, src/core/runtime_player_animation.cpp, tests/runtime_player_animation_tests.cpp, docs/RAC_RATCHET_POSE_V1.md, docs/RAC_PLAYER_AIRBORNE_V1.md

## Summary
Poprawiłem uwagę recenzji: wejście stanu 7 w `0x224d68..0x224d78` kopiuje quadword `+0x110` (zmierzone przemieszczenie) na `+0xe0` (prędkość zadaną). `enter_rac_jump_vertical_v1` przyjmuje teraz jawnie `measured_vertical_displacement` (+0x110.z) i ustawia nim `vertical_velocity`. Komentarze w nagłówku i implementacji, dowód (CONFIRMED) oraz `docs/RAC_PLAYER_AIRBORNE_V1.md` są zaktualizowane. Nowy test sprawdza inicjalizację z niezerowym przemieszczeniem: wejście ma bity tej wartości, a pierwsza klatka okna wybicia i tak nadpisuje vz wartością `-dt2*48`. Wcześniejsze trajektorie się nie zmieniają, bo zaczynają od przemieszczenia 0. Build, ctest 141/141 i 4 audyty PE przechodzą.

## Handoff
## Co zrobiono
Commity na gałęzi: 804c646, b087c0c, a18ee33, 32dd010, c7adbb3, 89b6aec, d25f26d.

**Ostatnia poprawka (d25f26d):**
- `enter_rac_jump_vertical_v1(float measured_vertical_displacement = 0.0F)` przepisuje `+0x110.z` do `vertical_velocity` (źródło: `0x224d68..0x224d78`, `lui/addiu` budują `0x13f530`/`0x13f560`, potem `lq`/`sq`).
- Test w `test_rac_jump_entry_and_takeoff_window` wchodzi z -0.0375, oczekuje tych samych bitów i sprawdza, że pierwsza klatka wybicia daje 0xbc9d4952. Dodany jest też test odrzucenia wartości nieskończonej.
- Dowód: `local/forensics/player-airborne/ratchet-airborne-source-evidence.md` §3 (nowy punkt CONFIRMED). Dokumentacja: `docs/RAC_PLAYER_AIRBORNE_V1.md` §Vertical step.

**Stan całości:**
- Modele stanu 7 (wejście, apeks, rampa impulsu, wybicie, grawitacja i limity) oraz stanu 6 (krok spadania, ograniczenie przy lądowaniu, faza slotu 11, selektor lądowania z blokadą po `set_state`).
- Opcjonalne role animacji: jump (7), fall (10), long fall (11), fall landing (12).
- Dokumenty `docs/RAC_PLAYER_AIRBORNE_V1.md` i `docs/RAC_RATCHET_POSE_V1.md`.

## Następne kroki
1. Sonda krawędzi `0x2178a0`/`0x1efff0` oraz zapisujący `+0x1f4` (luka jawnie opisana; F44–F47).
2. Cel prędkości poziomej w locie (`0x214520..0x2145bc`), stan 45 oraz parametry stanów 9/11/17.
3. Sprzętowo frames(3/9/15) i VU0 VSQRT.
4. Podłączenie do runtime jako osobne zadanie (klucze source-sequence/007/010/011/012).

## Pułapki
- Wejścia stanów 6 i 7 biorą prędkość z `+0x110` (zmierzonej), nie z poprzedniego `+0xe0`.
- W oknie wybicia vz jest i tak nadpisywane wartością `-dt2*48`.

## Verification (agent)
Pełny build w `build-slot4` (CMake 4.4.2, llvm-mingw-20260616, Release, OPENRC_STATIC_MINGW_RUNTIME=ON, testy, launcher i runtime włączone) zakończył się exit 0, bez ostrzeżeń i błędów; każdy link przeszedł post-buildową weryfikację importów PE. ctest: 141/141 PASS, w tym player-simulation z nowym testem wejścia ze zmierzonym przemieszczeniem -0.0375. Audyty `openrc-portable-executable-audit.exe` na openrc-cli.exe, openrc-launcher.exe, openrc-runtime.exe i na samym audytorze zakończyły się exit 0. Worktree jest czysty po commicie d25f26d.

## Next steps
- Odtworzyć sondę krawędzi 0x2178a0/0x1efff0 i zapisującego licznik +0x1f4 (F44–F46)
- Odtworzyć cel prędkości poziomej w locie (0x214520..0x2145bc), stan 45 i parametry stanów 9/11/17
- Sprzętowo rozstrzygnąć frames(3), frames(9), frames(15) oraz dokładność VU0 VSQRT
- W osobnym zadaniu podłączyć modele stanów 7/6 opt-in do PlayerSimulationV1/runtime i ustawić klucze animacji source-sequence/007, /010, /011, /012
