# Task #40: Etap 1: aktualna paczka portable i sprawdzenie New Game → sterowalny Veldin

- Date: 2026-10-07
- Kind: test
- Agent: codex (default)
- Reviewer: claude
- Paths: docs/RUNTIME_PORTABLE_MAIN_BASELINE.md, local/forensics/portable-main-baseline, build-portable, build-portable-cmake

## Summary
Odświeżyłem główną paczkę portable dla commita `d618681caf2287ad829b4da3e5e6a23b1e481e0c`; build, audyty PE i 190/190 testów przeszły. Zweryfikowałem pakiet v14 oraz wykonałem zarówno pełny smoke New Game, jak i zwykłe uruchomienie sterowane wejściem okna — oba osiągnęły sterowalny Veldin. Raport i dowody są zapisane; końcowy raport tasku zawiera potwierdzone fakty.

## Handoff
Raport: [RUNTIME_PORTABLE_MAIN_BASELINE.md](/D:/! Projekty/PlunkAI/worktrees/slot-1/docs/RUNTIME_PORTABLE_MAIN_BASELINE.md). Logi i artefakty: `D:\! Projekty\OpenRC\local\forensics\portable-main-baseline`. Build CMake użył `OPENRC_STATIC_MINGW_RUNTIME=ON`, opublikował EXE w głównym `build-portable`, przeszedł 190/190 testów oraz audyty importów. Smoke New Game zakończył sekwencję z 600 tickami i przemieszczeniem 24.1972, zachowując revision i trwałe bajty stanu; capture Veldinu to `new-game.level-enter.ppm`. Zwykły przebieg zakończył się kodem 0 po `level/enter` i 276 tickach z przemieszczeniem 20.7449. W obu przebiegach raportowane są `camera=developer`, `physics=openrc-policy`, `original_entity_admission=0`, `original_entry_qualified=0`; fizyczny pad pozostaje nietestowany. Raport na gałęzi zadania: commit `75dfbf5`.

## Verification (agent)
`scripts/build-portable.ps1`: PASS; CMake skonfigurowany z `OPENRC_STATIC_MINGW_RUNTIME=ON`; CTest 190/190 PASS; audyty PE trzech EXE PASS. `validate-native-game` dla istniejącego `prepared-milestone1-v14`: exit 0, profil `0.1.0-native-eight-resource-v14-level-installation`, 19 poziomów, manifest `d9249c74834a1423cb6b3bdfdb5b5812991cc634521ce54b73323dc583237fd9`. Smoke New Game z głównego opublikowanego runtime: exit 0, sequence complete, 600 ticków, displacement 24.1972, 921600 zmienionych pikseli, persistent bytes unchanged. Zwykłe uruchomienie sterowane oknem: exit 0, 276 ticków, displacement 20.7449.

## Next steps
-
