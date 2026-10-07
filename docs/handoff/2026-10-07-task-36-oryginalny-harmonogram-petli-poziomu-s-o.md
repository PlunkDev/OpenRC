# Task #36: Oryginalny harmonogram pętli poziomu: słowo trybu 0x15f6a8, wybór 2901a8, licznik klatek w trybie, bramka pauzy i kadencja PAL z dogonieniem — czysty komponent core

- Date: 2026-10-07
- Kind: implement
- Agent: claude (claude-opus-5-5)
- Reviewer: codex
- Paths: include/openrc/rac_level_frame_schedule.hpp, src/core/rac_level_frame_schedule.cpp, tests/rac_level_frame_schedule_tests.cpp, docs/RAC_LEVEL_FRAME_SCHEDULE_V1.md, CMakeLists.txt, local/forensics/level-frame-schedule

## Summary
Powstał czysty komponent core `rac_level_frame_schedule`, niepodłączony do runtime. Modeluje stan po prologu 2465f8, wybór trybu 2901a8 i jedną iterację pętli 2468a8..246e84. Plan iteracji przyjmuje wartości odczytane w kolejnych punktach źródła i zwraca uporządkowane kroki z adresami: pad, update i render ramienia, bramkę pauzy (także w update dogonienia), dogonienie, vsync, licznik prezentacji, frame skip, ścieżkę reload i rekord czasu. Współczynnik 0x15f6b4 liczę referencyjnym dzieleniem FDIV PS2. Przeliczanie czasu hosta na tiki TIMER1 jest osobnym adapterem oznaczonym jako polityka OpenRC. Build RelWithDebInfo z przypiętym toolchainem i `OPENRC_STATIC_MINGW_RUNTIME=ON` przechodzi, ctest 190/190.

## Handoff
## Co zrobiono (#36)
- `include/openrc/rac_level_frame_schedule.hpp`, `src/core/rac_level_frame_schedule.cpp`, `tests/rac_level_frame_schedule_tests.cpp`, `docs/RAC_LEVEL_FRAME_SCHEDULE_V1.md`.
- `CMakeLists.txt`: źródło dodane do `openrc_core` (po `rac_level_core.cpp`), test do pętli `openrc_new_test`. Innych zmian w pliku nie ma.
- Notatki dowodowe: `local/forensics/level-frame-schedule/source-notes.md` (UTF-8 bez BOM). Logi `rel-*.log` leżą w tym samym katalogu.
- Poprzedni agent (commit 431c7f6) zostawił uproszczony model. W fe45fd2 przepisałem go po ponownym odczycie `lvl.asm`. Doszły: wyjście z poziomu (15f650 → 2940e0), zerowanie 15f6bc w 2468f4, nakładka 246ab4..246c08, puste stuby 1f7110/1f7118/1f7120, zerowanie 15f690, `1f88c0`, ścieżka reload 246d90..246e14 (zapis trybu 0 w 246e04) i rekord czasu 246e18..246e84. Bramka pauzy działa także w update dogonienia, z licznikiem już po inkrementacji. Ratio liczy `ee_cop1_div_bits_v1`.

## API
- `rac_level_loading_state_v1(elapsed_15ee40)`, `rac_level_initial_mode_v1({level, byte_13de4b, byte_13d4f8})`, `rac_level_pause_gate_open_v1`, `plan_rac_level_frame_v1(state, input)`, `rac_level_time_record_update_v1`.
- Polityka OpenRC: `openrc_virtual_timer1_ticks_v1`, `openrc_virtual_timer1_count_v1`, `kOpenrcVirtualTimer1HzV1 = 576000` (INFERRED).
- `RacLevelFrameInputV1` zawiera obserwacje trybu w trzech punktach odczytu: `mode_after_arm` (246a5c), `mode_after_post_arm` (246c84/246cbc) i `mode_at_reload_check` (246d94). Między nimi działają nieprzezroczyste wywołania, które mogą zmienić tryb.

## Dowody
- Bramka pauzy w 2992e8..299324 (`slti frames,8`, ze znakiem). Po otwarciu 29950c woła `2016e0(3,0,0)`, które ustawia tryb 4 (20174c), a reszta `299250` jest pomijana.
- Dogonienie: `slt próg,licznik` w 246cf0, czyli tylko przy liczniku ostro większym od progu.
- TIMER1: `T1_MODE = 0x82` (23e248). Zegar 576 kHz i 11520 = 20 ms są INFERRED z dokumentacji sprzętu.
- 0x2623d0 ustawia 0x15ee80 = 1 i krok 0x15ee6c = 0.02 (PAL) albo 0x15ee80 = 0 i krok 1/60 (CONFIRMED).

## Co dalej
- Integracja po scaleniu #32: opisana w sekcji „Integracja” dokumentu. Plan zastępuje `FixedStepAccumulatorV1{60,8,...}` (`runtime_gameplay.cpp:340`). Animacja ma postępować o krok na każde `arm_update`, łącznie z dogonieniem, a akumulator 50→60 Hz (F20) znika.

## Pułapki
- Build bez `CMAKE_BUILD_TYPE` robi `native-game-prepare` bardzo wolnym (ponad 10 minut). Quorum używa RelWithDebInfo, a wtedy test trwa ok. 48 s.
- Kroki `call`/`empty_stub_call` bez natywnego właściciela nie mogą udawać sukcesu (F8/F26).
- `next.mode` to ostatni tryb, który harmonogram zapisał lub odczytał. Host nadpisuje go, jeśli późniejsze wywołania zmienią tryb.

## Verification (agent)
Konfiguracja identyczna z Quorum: cmake 4.4.2, llvm-mingw 20260616 ucrt, MinGW Makefiles, RelWithDebInfo, `OPENRC_STATIC_MINGW_RUNTIME=ON`, BUILD_TESTS/LAUNCHER/RUNTIME=ON, katalog `local/forensics/level-frame-schedule/build-rel`. Wynik: build exit 0, krok „Verifying portable imports” wykonany dla 195 celów bez błędów, ctest 190/190 (100%), w tym nowy `rac-level-frame-schedule` i `native-game-prepare` (48,6 s). Logi: rel-build.log i rel-ctest.log w tym katalogu. Wcześniejszy build bez typu też dał 189/190 zaliczonych, ale nieoptymalizowany `native-game-prepare` trwał ponad 10 minut i został przerwany razem z sesją. Diff względem main obejmuje tylko 5 plików z dozwolonych ścieżek.

## Next steps
- Po scaleniu #32 podłączyć `plan_rac_level_frame_v1` do sesji poziomu zamiast `FixedStepAccumulatorV1` 60 Hz i usunąć akumulator kadencji animacji 50→60 Hz (F20).
- Ustalić wartość 0x15ee80 w przebiegu New Game PAL z warunku w 2423e0..24242c (wywołania 2623d0) zamiast ją zakładać.
- Rozpoznać role ramion −1/1/4/5/7, ścieżki reload (246d90) i nakładki (0x13d46c) oraz odbiorców 0x15f6b4.
