# Task #33: Odtworzenie bazowego przebiegu v14 na tej maszynie: Prepare z ISO, walidacja 19 poziomów i smoke New Game do bariery level/enter

- Date: 2026-10-06
- Kind: test
- Agent: claude (claude-sonnet-5-5)
- Reviewer: claude
- Paths: docs/RUNTIME_SMOKE_BASELINE.md, local/prepared-stage1, local/prepared-milestone1-v14, local/forensics/baseline-v14

## Summary
F32 potwierdza wyniki zapisane w docs/RUNTIME_SMOKE_BASELINE.md (commit db46103) i nie wymaga zmian. Nic nie zmieniałem w tej sesji. Dokument już opisuje wszystko, co mówi F32: hashe, 3/3/7, 3457 zapisów, 1194→1195, `--level 0` z exit 0 po 4,4 s bez wyjścia oraz konieczność użycia ścieżki `D:\! Projekty\OpenRC\local`.

## Handoff
## Stan
Bez zmian względem poprzedniego raportu. Dokument `docs/RUNTIME_SMOKE_BASELINE.md` jest zacommitowany (db46103). Dane v14 leżą w `D:\! Projekty\OpenRC\local\prepared-milestone1-v14`, logi w `local\forensics\baseline-v14\`.

## Sprawdzenie względem F32
F32 jest zgodny z dokumentem, więc go nie zmieniałem. Wyniki w dokumencie pochodzą z poprzedniego przebiegu.

## Pułapki
- Używać ścieżek `D:\! Projekty\OpenRC\local\...`, bo junction w worktree blokuje `prepare`.
- `--level 0 --smoke-test` nic nie drukuje (0 B) i kończy się exit 0.
- Liczba 58 zasobów shared nie jest drukowana przez `validate-native-game` i nie została potwierdzona.
- Recenzent zgłosił dwa drobiazgi w skrypcie dokumentu, niewprowadzone: zmienna `$args` jest automatyczna w PowerShellu, a `$runs` jest zwykłym hashtable bez gwarantowanej kolejności.

## Verification (agent)
W tej sesji nie uruchamiałem żadnych komend. Poprzednio sprawdziłem na dysku, że logi w local\forensics\baseline-v14 istnieją i zawierają oczekiwane linie smoke oraz profil v14 i manifest d9249c74…. Build i ctest nie były uruchamiane, to zadanie Quorum.

## Next steps
- Uruchomić pełną weryfikację Quorum (build, 4 audyty PE, ctest).
- W #32 użyć D:\! Projekty\OpenRC\local\prepared-milestone1-v14 jako prepared-root.
- Zbadać, czemu --level 0 --smoke-test nie drukuje nic.
