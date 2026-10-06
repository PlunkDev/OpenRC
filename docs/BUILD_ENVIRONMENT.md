# Środowisko budowania

Toolchain jest przypięty i leży w ignorowanym `local/tools`. `scripts/build-portable.ps1`
wymaga dokładnie jednego clang++ i jednego cmake, dlatego nie wolno tam trzymać innych wersji.

| Narzędzie | Wersja | Katalog w `local/tools` |
| --- | --- | --- |
| llvm-mingw (clang++, mingw32-make) | 20260616, UCRT, x86_64 | `llvm-mingw-20260616-ucrt-x86_64` |
| CMake | 4.4.2 | `cmake-4.4.2-windows-x86_64` |

## Źródła i SHA256

Tylko oficjalne wydania GitHub; sumy pochodzą z pola `digest` assetu w API wydań
(dla CMake dodatkowo zgodne z `cmake-4.4.2-SHA-256.txt` Kitware) i są wpisane na stałe w skrypt.

- https://github.com/mstorsjo/llvm-mingw/releases/download/20260616/llvm-mingw-20260616-ucrt-x86_64.zip
  — `b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35` (187504083 B)
- https://github.com/Kitware/CMake/releases/download/v4.4.2/cmake-4.4.2-windows-x86_64.zip
  — `e8139d85b3813bc38833142ae1940472e9a587e9b5d2718ac1804c60f4e57a64` (54405968 B)

## Użycie

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bootstrap-toolchain.ps1
```

Skrypt (Windows PowerShell 5.1) jest idempotentny: gdy pliki `bin\cmake.exe`,
`bin\x86_64-w64-mingw32-clang++.exe` i `bin\mingw32-make.exe` istnieją, nic nie pobiera.
Archiwum trafia do nowego, pustego katalogu w `%TEMP%`, jest sprawdzane SHA256, rozpakowywane
i dopiero potem przenoszone do `local/tools/<dokładna nazwa>`; katalog tymczasowy jest usuwany.
Skrypt odmawia działania, gdy w `local/tools` jest inny lub drugi katalog `llvm-mingw-*`/`cmake-*`,
oraz gdy docelowy katalog jest niekompletny. Nie zmienia PATH, rejestru ani instalacji systemowych.
W worktree `local` jest junctionem do `D:\! Projekty\OpenRC\local`, więc instalacja jest wspólna.

## Bazowa weryfikacja (worktree na b0aa062, 2026-10-06)

Kroki dokładnie jak w `plunkai.json`, bez zmiany PATH, na świeżym katalogu `build-plunkai`:
configure `-G "MinGW Makefiles"` z `-DCMAKE_CXX_COMPILER=<clang++>` i `-DCMAKE_MAKE_PROGRAM=<mingw32-make>`
(ścieżki `D:/! Projekty/OpenRC/local/tools/...`), `-DCMAKE_BUILD_TYPE=RelWithDebInfo
-DOPENRC_STATIC_MINGW_RUNTIME=ON -DOPENRC_BUILD_TESTS=ON -DOPENRC_BUILD_LAUNCHER=ON
-DOPENRC_BUILD_RUNTIME=ON`, `--build --parallel 6`, audyty PE, `ctest --test-dir ... --output-on-failure
--no-tests=error --parallel 4`.

- configure: exit 0 (ok. 2 s + generowanie 4 s)
- build: exit 0, 5 min 17 s (czysty build od zera)
- audyty PE (`openrc-portable-executable-audit.exe`): auditor, `openrc-cli`, `openrc-launcher`,
  `openrc-runtime` — wszystkie exit 0
- ctest: 141/141 testów przeszło, 53 s (`--parallel 4`)

Uwaga: wcześniejszy błąd configure („unable to find a build program … CMAKE_MAKE_PROGRAM is not set”)
wynikał ze starego `build-plunkai/CMakeCache.txt` z ręcznego configure z innymi ścieżkami kompilatorów
(zmiana `CMAKE_CXX_COMPILER` wymusza reset cache). Na czystym katalogu build ten sam configure przechodzi.
Przy takim błędzie wyczyść `build-plunkai` (poza `marker.txt`).
