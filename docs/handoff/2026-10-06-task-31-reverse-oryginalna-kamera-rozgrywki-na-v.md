# Task #31: Reverse: oryginalna kamera rozgrywki na Veldinie i projekcja 1f7bc8/1f7d00 — dowód i czysty model obok kamery deweloperskiej

- Date: 2026-10-06
- Kind: reverse
- Agent: claude (claude-opus-5-5)
- Reviewer: claude
- Paths: include/openrc/third_person_camera.hpp, src/core/third_person_camera.cpp, tests/third_person_camera_tests.cpp, docs/RAC_GAMEPLAY_CAMERA_V1.md

## Summary
F33 jest zgodny z moimi dowodami i nie wymaga zmian. Analizę oparłem na tym samym ISO PAL v2.00. Mój ELF `local/forensics/gameplay-camera/SCES_509.16` ma SHA256 17F8A846…B122B, bajtowo zgodny z `prepared-stage1` z F33 (oba hashe sprawdziłem w tej sesji). Profil v14 (58 zasobów shared, 19 poziomów) nie dotyczy pasa kamery. Gałąź jest czysta, HEAD 05e295b. Kod się nie zmienił, więc wyniki weryfikacji pochodzą z poprzedniej sesji: pełny build, 4 audyty PE i ctest 189/189.

## Handoff
## Stan (#31) po sprawdzeniu F33
- **Zgodność ze źródłem z F33:**
  - ELF użyty w analizie (`local/forensics/gameplay-camera/SCES_509.16`) ma ten sam SHA256 17f8a846…b122b co ELF w `D:\! Projekty\OpenRC\local\prepared-stage1\…\files\SCES_509.16`.
  - ISO to ta sama kopia PAL v2.00 (0f18a6c8…3260).
  - Nakładkę Veldinu (LBA 1886019, SHA256 922fd06e…) weryfikuje `extract_overlay.py`.
- **Commity:**
  - b3450e2 — kamera: projekcja 1f7bc8/1f7d00, sprężyna 1eb5c0, fokus typu 0, pitch drążka;
  - 05e295b — poprawka buildu v14 w `tests/rac_moby_post_execute_tests.cpp` (poza pasem, wpisana w `extra_paths`).
- **Dowody:** `local/forensics/gameplay-camera/gameplay-camera-source-evidence.md`, dokument `docs/RAC_GAMEPLAY_CAMERA_V1.md`.

## Pułapki
- Arytmetyka to host IEEE single (`ee_bit_exact=false`).
- Fokus jest modelowany tylko dla up=+Z (INFERRED).
- Składanie oka i kolizja nie są zaimplementowane.

## Verification (agent)
W tej sesji: Get-FileHash obu plików SCES_509.16 (mojego forensycznego i z prepared-stage1) daje ten sam 17F8A846…B122B; `git status` jest czysty, HEAD 05e295b. Kod się nie zmienił, więc nie przebudowywałem. Ostatni wynik (poprzednia sesja, ten sam HEAD): pełny build RelWithDebInfo ze statycznym runtime MinGW, 4 audyty PE z exit 0, ctest 189/189.

## Next steps
- Odtworzyć składanie oka typu 0 (2eb060 → 2e9e60/2ea068, 2ea3f0, 2ea4c0, 2eabd0) przed zwracaniem ThirdPersonCameraViewV1.
- Ustalić runtime wartości 1519d0/1519d2, 15ee80 i wektora up 166f40 (powiązane z #34).
- Podłączyć API do renderera w osobnym zadaniu.
