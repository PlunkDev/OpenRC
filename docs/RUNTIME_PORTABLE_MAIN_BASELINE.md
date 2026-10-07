# Baseline aktualnej paczki portable w głównym checkoutcie

## Wynik

Paczka głównego checkoutu została przebudowana i opublikowana dla commita
`d618681caf2287ad829b4da3e5e6a23b1e481e0c`. `scripts/build-portable.ps1`
zakończył build, pełne testy i publikację w
`D:\! Projekty\OpenRC\build-portable`. Wszystkie 190 testów CTest przeszły.
Skrypt sprawdził importy EXE targetów w trakcie linkowania i zweryfikował
trzy opublikowane EXE; dodatkowy jawny audyt tych plików również zakończył się
kodem 0.

Początkowy build zatrzymał się na przestarzałym cache CMake, wskazującym
`E:/projekty/OpenRC`. Naprawiono wyłącznie ignorowany cache builda do bieżącej
ścieżki `D:/! Projekty/OpenRC`; kopia cache sprzed zmiany i log skryptu są w
`local/forensics/portable-main-baseline/`. Nie zmieniano śledzonych plików ani
historii w głównym checkoutcie.

## Commit i paczka

| Pole | Wartość |
| --- | --- |
| źródłowy commit / główny HEAD | `d618681caf2287ad829b4da3e5e6a23b1e481e0c` |
| `OPENRC_STATIC_MINGW_RUNTIME` | `ON` (konfiguracja CMake i cache) |
| CTest | `190/190 PASS` |
| `openrc-cli.exe` SHA-256 | `e12b6fd881237c47e8c115a2246de6000040cd33b7d82d91d07e6093b75ed971` |
| `openrc-launcher.exe` SHA-256 | `325904a291c4311f4659bb462e00879f538bef332bf63a4c72f31f3015f8d407` |
| `openrc-runtime.exe` SHA-256 | `cf55e2ed5db59ad1d9d7b2bd37b01c19f9f8737b157d44d7fdfb93e3936ae5e6` |

Skróty i commit zapisano w `local/forensics/portable-main-baseline/`;
log builda zawiera wynik CTest i wpisy audytu. Logi audytora dla każdego EXE
mają 0 bajtów, zgodnie z jego cichym wynikiem PASS.

## Pakiet v14

Opublikowany `openrc-cli.exe validate-native-game` zweryfikował istniejący
`D:\! Projekty\OpenRC\local\prepared-milestone1-v14` bez ponownego Prepare.
Wynik: `OpenRC native game matches the current exact profile`, 19 poziomów,
profil `0.1.0-native-eight-resource-v14-level-installation`, manifest
SHA-256 `d9249c74834a1423cb6b3bdfdb5b5812991cc634521ce54b73323dc583237fd9`.
Pełny stdout/stderr walidacji zachowano w katalogu dowodowym.

## Normalny smoke New Game → level/enter

Smoke wykonano ukrytym `Start-Process` z opublikowanego i audytowanego
`build-portable/openrc-runtime.exe`, wskazując bezwzględną ścieżkę pakietu v14.
Wartości `OPENRC_AUDIO_DEVICE_NAME=Słuchawki (Oculus Virtual Audio` i
`OPENRC_AUDIO_DEVICE_REQUIRED=1` ustawiono tylko w środowisku procesu potomnego
i przywrócono po jego zakończeniu. Wybrane wyjście to Oculus Virtual Audio,
index 1. Smoke zakończył się kodem 0.

Sekwencja osiągnęła intro → menu/dialog → New Game → trzy filmy → level load →
instalację stanu → `level/enter` → gameplay, bez `--level`. Log potwierdza
`cards=3 movies=3 fades=7`, `writes=3457`, revision `1194→1195`,
`session_transferred=1 second_session=0 window_reused=1 renderer_reused=1` i
`sequence_complete=1`. Gameplay wykonał 600 ticków, przemieścił gracza o
24.1972, zachował revision 1195 i bajty stanu trwałego, a 921600 pikseli
różni się od zamrożonej klatki przejścia. Obraz Veldinu:
`local/forensics/portable-main-baseline/new-game.level-enter.ppm`.

Wynik kwalifikacji raportowany osobno przez runtime:

| Wskaźnik | Wynik |
| --- | --- |
| `camera` | `developer` |
| `physics` | `openrc-policy` |
| `original_entity_admission` | `0` |
| `original_entry_qualified` | `0` |
| fizyczny pad | nietestowany; smoke używał wejścia drążka przez granicę próbki kontrolera |

To potwierdza działanie zaimplementowanego sterowalnego Veldinu, nie wierny
start PS2 ani oryginalną kamerę/fizykę/admission encji.

## Zwykłe uruchomienie sterowane oknem

Dodatkowy przebieg uruchomiono ukrytym `Start-Process` z tego samego EXE, bez
`--smoke-test`, `--level` ani skoku do poziomu. Po 154 sekundach do okna runtime
wysłano trzy Enter; po wykryciu `level/enter` przytrzymano W przez 4 sekundy,
a następnie wysłano WM_CLOSE. Proces zakończył się kodem 0. Log potwierdza
normalne intro i filmy New Game, `session_transferred=1 second_session=0`,
`window_reused=1 renderer_reused=1`, a pętla gry wykonała 276 ticków z
przemieszczeniem 20.7449. Dowody: `normal-run-attempt2.stdout.log`,
`normal-run-attempt2.stderr.log` i `normal-run-attempt2.driver.log`.

Pierwsza próba zwykłego uruchomienia nie osiągnęła level/enter; jej osobne
logi `attempt1-normal-run.*` zachowano jako niezaliczoną próbę automatyzacji.

## Dowody

Wszystkie wyniki leżą w
`D:\! Projekty\OpenRC\local\forensics\portable-main-baseline\`:

- `build-portable.log`, `CMakeCache.before-relocation.txt`;
- `source-commit.txt`, `published-exe-sha256.txt`, `*.audit.log`;
- `validate-native-game.stdout.log`, `validate-native-game.stderr.log`;
- `smoke-new-game.stdout.log`, `smoke-new-game.stderr.log`, `new-game*.ppm`;
- `normal-run-attempt2.stdout.log`, `normal-run-attempt2.stderr.log`,
  `normal-run-attempt2.driver.log` (zwykły przebieg zakończony exit 0);
- `attempt1-normal-run.*` (pierwsza, niezaliczona próba automatyzacji).
