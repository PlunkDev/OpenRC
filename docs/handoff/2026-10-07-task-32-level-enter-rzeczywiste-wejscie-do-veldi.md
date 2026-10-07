# Task #32: level/enter: rzeczywiste wejście do Veldinu po New Game i sterowanie Ratchetem w tej samej sesji

- Date: 2026-10-07
- Kind: implement
- Agent: claude (claude-opus-5-5)
- Reviewer: codex
- Paths: src/runtime/windows_main.cpp, src/core/runtime_gameplay.cpp, include/openrc/runtime_gameplay.hpp, tests/runtime_gameplay_tests.cpp, HANDOFF.md, docs/FIRST_PLAYABLE.md, docs/FRONTEND_SEQUENCE_V1.md

## Summary
Gałąź jest zrebase'owana na lokalny `main` (`811d33e`). Konflikt wystąpił tylko w `tests/rac_moby_post_execute_tests.cpp`: pierwszy commit zadania zawierał własną wersję poprawki (`again_owner`), a `main` ma już tę samą poprawkę z #31 (`owner_again`). Przyjąłem wersję z `main`. Ten plik nie należy już do diffu gałęzi. Po rebase gałąź zmienia wyłącznie dozwolone ścieżki. Pełny build w `build-plunkai` przeszedł, a ctest dał 189/189 PASS. Kod runtime i testy rozgrywki są treściowo takie same jak przed rebase.

## Handoff
## #32 `level/enter`: rebase na `main` po zgłoszeniu konfliktu

### Co zrobiono
- `git rebase main` na `811d33e`. Konflikt był tylko w commicie `91d8a09` (teraz `cf09a2d`), w `tests/rac_moby_post_execute_tests.cpp`. Gałąź miała tam `again_owner`, a `main` ma identyczną semantycznie poprawkę `owner_again` z #31. Przyjąłem wersję `main` (`git checkout --ours`). Pozostałe 4 commity nałożyły się bez konfliktów.
- Nowe SHA: `cf09a2d` feat, `75c8e6e` fix, `89ba6d6`/`2e9183d`/`1ade4b7` docs.
- `git diff main...HEAD` obejmuje teraz tylko: `HANDOFF.md`, `docs/FIRST_PLAYABLE.md`, `docs/FRONTEND_SEQUENCE_V1.md`, `src/runtime/windows_main.cpp` i `tests/runtime_gameplay_tests.cpp`. `extra_paths` jest puste.

### Dowody
- `local/forensics/level-enter/rebase-build.log`: cele `openrc-runtime-gameplay-tests`, `openrc-rac-moby-post-execute-tests`, `openrc-runtime` i `openrc-portable-executable-audit` zbudowane z weryfikacją importów PE, exit 0.
- `rebase-ctest.log`: 5/5 testów docelowych PASS (runtime-gameplay*, rac-moby-post-execute, third-person-camera).
- `rebase-build-all.log` i `rebase-ctest-all.log`: pełny build exit 0 i ctest 189/189 PASS na zrebase'owanym drzewie.
- Smoke `new-game-sequence`, zwykłe uruchomienie i `build-portable` pochodzą z sesji sprzed rebase (`smoke-new-game-sequence-v2.log`, `normal-run-v1.log`, `pe-audit-and-validate-final.log`). Kod runtime się nie zmienił, ale nie uruchamiałem ich ponownie na nowych SHA, bo publikację wykonuje orkiestrator.

### Co dalej
- Orkiestrator: pełna weryfikacja (`build-portable`, smoke na v14) na `1ade4b7`.
- Kolejny krok merytoryczny: podłączyć `RacGameplayCameraV1` (#31) w `enter_gameplay_level`/`advance_gameplay_frame` w miejsce `ThirdPersonCameraV1`. Potem zrealizować §5 z `docs/LEVEL_ENTER_SOURCE_V1.md`: scheduler trybów, przeplatane admission w `2422d8` oraz lowering `205278`/`205598`.

### Pułapki
- Na tej maszynie `origin/main` jest stary (`b0aa062`). Bazą jest lokalny `main`.
- Ścieżki prepared podawaj jako `D:\! Projekty\OpenRC\local\...`, nie przez junction `local`.

## Verification (agent)
- `git rebase main` zakończony (konflikt rozwiązany wersją `main`). `git diff main HEAD -- tests/rac_moby_post_execute_tests.cpp` jest pusty, drzewo czyste.
- `cmake --build build-plunkai` dla celów `openrc-runtime-gameplay-tests`, `openrc-rac-moby-post-execute-tests`, `openrc-runtime` i `openrc-portable-executable-audit`: exit 0, importy PE zweryfikowane przy linkowaniu (`local/forensics/level-enter/rebase-build.log`).
- `ctest -R runtime-gameplay|rac-moby-post-execute|third-person-camera`: 5/5 PASS (`rebase-ctest.log`).
- Pełny `cmake --build build-plunkai`: exit 0. Pełny `ctest`: 189/189 PASS (`rebase-ctest-all.log`).
- Po rebase nie uruchamiałem ponownie `scripts/build-portable.ps1` ani smoke na v14 (publikację wykonuje orkiestrator). Wcześniejsze wyniki: smoke exit 0, 600 ticków, przemieszczenie 24,1972; zwykłe uruchomienie exit 0.

## Next steps
- Orkiestrator: scalić gałąź (teraz na `811d33e`) i uruchomić pełną weryfikację z `build-portable` oraz smoke `new-game-sequence` na v14.
- Podłączyć `RacGameplayCameraV1` (projekcja 1f7bc8/1f7d00, sprężyna 1eb5c0) w `enter_gameplay_level`/`advance_gameplay_frame` i zmienić flagę `camera` dopiero po weryfikacji.
- Zrealizować kroki z `docs/LEVEL_ENTER_SOURCE_V1.md` §5: scheduler trybów, przeplatane admission w `2422d8` oraz lowering `205278`/`205598`.
