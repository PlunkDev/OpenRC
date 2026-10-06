# Task #28: Odtworzenie przypiętego toolchaina w local/tools i bazowa weryfikacja Quorum

- Date: 2026-10-06
- Kind: fix
- Agent: claude (claude-sonnet-5-5)
- Reviewer: claude
- Paths: scripts/bootstrap-toolchain.ps1, docs/BUILD_ENVIRONMENT.md

## Summary
Fakty F29 i F30 nie wpływają na mój pas. F29 dotyczy sum SHA256 opublikowanych `build-portable/openrc-runtime.exe` i `openrc-cli.exe`, a F30 commitu v14 na main. Mój skrypt i dokument nie odwołują się do tych plików ani nie zmieniają kodu, CMakeLists.txt ani build-portable.ps1. Sumy SHA256 toolchaina (llvm-mingw i CMake) są wpisane w skrypt i nie zmieniły się. Nie wprowadzałem nowych zmian. Nie uruchamiałem ponownie builda ani ctest po commicie 814b751 na main.

## Handoff
## Stan
- Pas: `scripts/bootstrap-toolchain.ps1`, `docs/BUILD_ENVIRONMENT.md`. Commity f26a4e7 i bc646ee, bez nowych zmian w tej sesji.
- F29 (SHA256 opublikowanych exe) i F30 (commit v14 na main): CONFIRMED, że nie dotyczą mojego pasa. Skrypt pobiera tylko toolchain i nie dotyka `build-portable`.
- Wyniki bazowej weryfikacji w dokumencie pochodzą ze stanu b0aa062 (141/141 testów, build 5 min 17 s). Nie sprawdzałem ich na 814b751 (UNKNOWN). Po rebase na main liczba testów może być inna, bo v14 dodał pliki.

## Pułapki
- Przy zmianie ścieżek kompilatorów w `build-plunkai` wyczyść katalog, poza `marker.txt`.
- `local` w worktree to junction do wspólnego katalogu.

## Verification (agent)
W tej sesji nic nie uruchamiałem. Ostatnie wyniki z wcześniejszej sesji, na b0aa062: configure, build (5 min 17 s), 4 audyty PE i ctest 141/141 (53 s) przeszły, bez zmian po F29/F30.

## Next steps
- Orkiestrator: po rebase na 814b751 przeprowadzić pełną weryfikację i w razie potrzeby zaktualizować liczbę testów w docs/BUILD_ENVIRONMENT.md
