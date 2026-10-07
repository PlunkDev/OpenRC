# Task #39: Audyt pokrycia świata Veldinu: źródło vs pakiet v14 vs to, co runtime rysuje i odtwarza

- Date: 2026-10-07
- Kind: test
- Agent: claude (claude-sonnet-5-5)
- Reviewer: codex
- Paths: docs/VELDIN_WORLD_COVERAGE_V1.md, local/forensics/veldin-coverage

## Summary
Audyt jest gotowy i zacommitowany (ee97d37): `docs/VELDIN_WORLD_COVERAGE_V1.md` z tabelą pokrycia 16 rodzin poziomu 0, rozbiorem 296 Moby, 5 największymi lukami i indeksem poleceń. Liczby pochodzą z uruchomień opublikowanego `openrc-cli.exe` (SHA b3e9017c…, audyt PE exit 0). Logi leżą w `local/forensics/veldin-coverage/`. Branch różni się od `main` tylko tym jednym plikiem dokumentu. Hashe pakietu v14 (manifest d9249c74…, shared 950a7c64…) i ISO są zgodne z F32/F33, więc rozbieżności hashy nie ma. Runtime nie był uruchamiany; wnioski o konsumpcji wynikają z czytania kodu.

## Handoff
## Stan
Praca zakończona w poprzedniej sesji, w tej tylko sprawdziłem stan repozytorium: drzewo robocze czyste, jedyna zmiana względem `main` to `docs/VELDIN_WORLD_COVERAGE_V1.md` (200 linii).

## Najważniejsze wyniki (szczegóły w dokumencie)
- Pakiet poziomu v14 ma 8 zasobów: collision, bootstrap, render-scene, actors/library, actors/animations, entities, gameplay, destructibles. Nie ma w nim audio poziomu, sky, shrubów, scene-animation, HUD, cząsteczek, Clanka ani klasy 71.
- Moby 296 = 1 spawn + 29 collectibles (klasa 13) + 103 destructibles (klasa 500) + 16 aktorów (klasa 749) + 30 wypieczonych + 107 pominiętych jako animowane + 10 bez modelu. Podział jest INFERRED, ale zgadza się z 149 encjami, 132 powiązaniami renderu i 135 instancjami ze smoke'a.
- Kolizja: 53 460 ścian źródłowych → 39 085 unikalnych trójkątów.
- Audio poziomu: 218 bloków SBlk w źródle, 0 w pakiecie.

## Pułapki
- Logi są w `D:\! Projekty\OpenRC\local\forensics\veldin-coverage\`. `local` w worktree to junction, a git ich nie widzi (ignorowane).
- Liczby sky, fx, cząsteczek i HUD oraz statystyki rekordów tfrag są UNKNOWN, bo żadne CLI ich nie drukuje.
- Poziom 0 ma dwa banki gameplay (unikalne 705 i 706) z różnymi skrótami dekodowanymi. Kompilator bierze pierwszy; różnica jest nieznana.
- Smoke `new-game-sequence` (F7) nie był powtarzany.

## Verification (agent)
W tej sesji tylko `git status` (czysto) i `git log`/`git diff main --stat` (jedna zmiana: dokument, +200 linii). Wyniki uruchomień CLI z poprzedniej sesji: wszystkie polecenia exit 0, audyt PE exit 0, `validate-native-game` potwierdza profil v14 i manifest d9249c74…. Nie było zmian w src/include/tests/CMakeLists.txt, więc nie budowano ani nie uruchamiano testów. Runtime nie był uruchamiany.

## Next steps
- Zadanie implementacyjne: audio poziomu 0 (218 bloków SBlk) — kompilacja do zasobów pakietu i odtwarzacz w pętli gameplay.
- Zadanie implementacyjne: animowane Moby (klasy 1440, 1564, 1781, 1782 oraz 530, 834, 1137, 1520; razem 107 placementów).
- Zadanie implementacyjne: shrub (1697 instancji) i oświetlenie instancji TIE.
- Dodać CLI drukujące statystyki rekordów tfrag oraz liczby sky/particles/fx, żeby zamknąć UNKNOWN z tabeli.
- Rozstrzygnąć różnicę między bankami gameplay 705 i 706 poziomu 0.
