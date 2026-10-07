# Task #37: Kamera typu 0: składanie oka (2eb060), wejście prawego drążka, stan przy wejściu do poziomu i skala świata — kompletny widok RacGameplayCameraV1 z adapterem dla renderera

- Date: 2026-10-07
- Kind: reverse
- Agent: claude (claude-opus-5-5)
- Reviewer: codex
- Paths: include/openrc/third_person_camera.hpp, src/core/third_person_camera.cpp, tests/third_person_camera_tests.cpp, docs/RAC_GAMEPLAY_CAMERA_V1.md, local/forensics/gameplay-camera

## Summary
Zweryfikowałem i poprawiłem niezacommitowany model kamery typu 0 pozostawiony przez poprzedniego agenta. Odczyt pól bitowych VU (1ff370 = a2×a1, 1ffe18 = a⊗b) oraz macierz widoku 1f7284..1f730c (ekranowe X = −row1) pokazały, że 260c80 to obrót prawoskrętny q·v·conj(q). Poprawiłem w modelu znak obrotu yaw drążkiem, lerp 0,75 w 2ea3f0 wraz z ograniczeniem długości przez +512 (offset_shortfall), kolejność złożenia oka (najpierw korekta 15° bieguna, potem oko) oraz wektor patrzenia 2ea4c0, czyli V = H + up·((+320·up) + (+40 − +36)). Przy wejściu widok jest teraz pochylony o ~6,2° zamiast 23°. Dokument, wyciągi dowodów i testy są zaktualizowane. Pełny build kończy się kodem 0, ctest daje 189/189.

## Handoff
## Co zrobiono (#37)
- Commity: `b7cf54e` (checkpoint pracy poprzedniego agenta), `a99a344` (poprawki modelu), `af05108` (dokument).
- `RacGameplayCameraV1::step_pal_frame` odwzorowuje jedną aktualizację 1ed428 w domenie: tryb 0, up=+Z, bez kolizji. Kolejno:
  - ruch z 1ecdf0;
  - fokus 2e6ce0;
  - 2ea3f0: lerp 0,75 z `unfiltered_eye` (+0) i ograniczenie długości do `[+348−+512, +348]`;
  - 2e9e60/2ea068: yaw k=d=1, pitch z krokiem 0,01/0,02, krok ≤1,75°;
  - 2ea9c8: wydłużenie dystansu do 6,0 przy ruchu ku kamerze;
  - 2eabd0: promień, elewacja, azymut, korekta bieguna 15°, potem oko = fokus+2up+S;
  - 2ea4c0: V, bias, clamp ±70°, baza left/up;
  - 2e72e8: sprężyna celu 0,003.
- Obroty idą ścieżką kwaternionową w kolejności źródła, z hostowym sin/cos. asin i atan2 to wielomiany ze źródła (160820, 1c27a0).
- Adapter `rac_gameplay_camera_renderer_view_v1` / `view()`:
  - target = eye + forward;
  - FOV pionowy = 2·atan(tan_v), aspect = tan_h/tan_v;
  - near/far podzielone przez 1024, czyli 0,03125 i 728.

## Dowody
- `local/forensics/gameplay-camera/task37_evidence.py` regeneruje `task37-source.asm` i `task37-values.json`. Skrypt asertuje hashe, pola fs/ft rozkazów VOPMULA/VOPMSUB oraz słowa macierzy widoku.
- Notatki: `task37-source-notes.md`.
- Logi `task37-full-build.log` i `task37-ctest.log` (189/189).
- Wszystko w UTF-8 bez BOM.

## Pułapki
- `dis.py` drukuje rozkazy VU „special2” (vopmula) w kolejności `ft, fs`. Poprzedni agent wyprowadził z tego błędne `q⁻¹vq` i `a1×a2`. Rozstrzygają pola bitowe: ft = bity 20..16, fs = 15..11.
- Przy statycznych `15eee0=15eedc=1` prawo na drążku skręca widok w lewo, a drążek w górę podnosi oko. Domyślne ustawienia użytkownika i nazwy opcji są UNKNOWN; runtime musi przekazać rozstrzygnięte słowa.
- `1feed0(n)` to `(int)(0,5+n·15ee68)`, czyli skalowanie liczby klatek, a nie losowanie.
- Model nie obsługuje stanów 2e5b68/2e74b0/2e5de0/2e6498 poza gałęzią domyślną. Warunki tych gałęzi są w tabeli w dokumencie; caller musi je odrzucić.

## Dalej
- Podłączenie w `runtime_gameplay` (osobne zadanie, poza tym pasem): zamienić źródło widoku na `RacGameplayCameraV1::view()`. Przekazać:
  - floaty prawego drążka (F16);
  - słowa 15eee0/15eedc/15eee4;
  - pozycję po 205278;
  - yaw gracza.
- Ślad pamięci obiektu kamery typu 0 z prawdziwego wejścia (sondy 2e5770/2e7b68, wartości 1414d4/1414dc/1414f4/13f590) do kwalifikacji modelu.

## Verification (agent)
Zbudowałem cel openrc-third-person-camera-tests przez CMake (llvm-mingw z local/tools, build-plunkai): ctest -R third-person-camera przeszedł. Potem pełny `cmake --build build-plunkai`: exit 0, wszystkie kroki 'Verifying portable imports' bez błędów. Pełny `ctest --test-dir build-plunkai`: 100% passed, 189/189 (log: local/forensics/gameplay-camera/task37-ctest.log). `python -I task37_evidence.py`: PASS, w tym asercje pól VU 1ff370/1ffe18 i słów macierzy widoku 1f7290/1f72a8.

## Next steps
- Podłączyć RacGameplayCameraV1::view() jako źródło widoku w runtime_gameplay (osobne zadanie z pasem runtime), przekazując floaty prawego drążka F16, słowa 15eee0/15eedc/15eee4, rozstrzygniętą pozycję po 205278 i yaw gracza.
- Ustalić domyślne wartości 15eee0/15eedc/15eee4 z resetu opcji lub karty pamięci oraz ich nazwy w menu.
- Odtworzyć kontrakt 1efff0 dla zapytań kamery (2e70c8/2e7140/2e7b68/2e5770) i 2e91d0, żeby włączyć kolizje i aktualizacje +512.
- Pozyskać ślad klatek obiektu kamery typu 0 (oko, fokus, +304/+320, prędkości, pad) z prawdziwego wejścia do Veldinu i porównać z modelem.
- Zdecydować politykę okna dla aspectu PAL ≈1,3228 (letterbox albo inna) przy podłączeniu renderera.
