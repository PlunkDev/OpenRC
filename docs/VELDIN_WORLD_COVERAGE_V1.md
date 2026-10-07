# Audyt pokrycia świata Veldinu (poziom 0): źródło vs pakiet v14 vs runtime

Zadanie #39. Audyt ilościowy bez zmian w kodzie: ile z każdej rodziny świata
Veldinu istnieje w źródle (ISO), ile trafia do pakietu v14 i czy runtime ją
konsumuje. Dokument zawiera wyłącznie liczby, nazwy rodzin, identyfikatory
klas i skróty SHA-256; żadnych danych gry.

Etykiety: **CONFIRMED** (liczba z uruchomienia CLI lub wprost z kodu),
**INFERRED** (wyliczone z liczb z uruchomień i reguły w kodzie),
**UNKNOWN** (żadne istniejące narzędzie tej liczby nie wypisuje).
Statusy: PEŁNE / CZĘŚCIOWE / BRAK / UNKNOWN.

Runtime nie był uruchamiany. „Czy runtime konsumuje” wynika z czytania kodu
(tylko do odczytu) i z liczb `prepared-native-level-smoke`; to nie jest dowód
renderu ani odtwarzania w normalnym przebiegu (F26).

## 1. Tożsamość wejść i zgodność z F7/F29/F32/F33

Wszystkie polecenia: `openrc-cli.exe` opublikowany w build-portable, ścieżki
bezwzględne. Log: `local/forensics/veldin-coverage/` (fizycznie
`D:\! Projekty\OpenRC\local\forensics\veldin-coverage\`, bo `local` w worktree
jest junctionem do głównego checkoutu).

| Obiekt | Wartość (uruchomienie) | Oczekiwanie | Zgodność | Log |
|---|---|---|---|---|
| `openrc-cli.exe` SHA-256 | `b3e9017c993c0cdc70e7fdb6e6e37c6c6d4af21d5742d9efc03f462f03b622f6` | F29 | zgodne | `00-pe-audit-cli.log` (audyt PE: exit 0, brak zakazanych importów; skrót: `Get-FileHash`) |
| ISO SHA-256 | `0f18a6c84cd8d727ec8c21000a236ed5ce2f279cbb8d0682fa9747199ef73260` | F32/F33/F34 | zgodne | `09-package-hashes.log` |
| ELF `SCES_509.16` SHA-256 | `17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b` | F33/F34 | zgodne (też `inventory`) | `16-inventory.log` |
| Manifest `prepared-v2.orpg` SHA-256 | `d9249c74834a1423cb6b3bdfdb5b5812991cc634521ce54b73323dc583237fd9` | F32 (`d9249c74…`) | zgodne | `07-validate-native-game.log`, `09-package-hashes.log` |
| `shared.orlevel` SHA-256 | `950a7c6488402e082a7b537033319c848ba8b22b878e1c1e3008fa696091bb01` | F32 (`950a7c64…`) | zgodne | `09-package-hashes.log` |
| `levels/000.orlvl` SHA-256 | `024a09ec7640f8e4502d85f71b8d4a2fecc52dfab0df4d0b7d398eb53dd7b11f` (48 048 829 B) | brak wcześniejszej wartości | nowa referencja | `09-package-hashes.log` |
| Profil / poziomy | `0.1.0-native-eight-resource-v14-level-installation`, 19 poziomów | F32/F33 | zgodne | `07-validate-native-game.log` (exit 0, 93 s) |

**Rozbieżności z F7/F32: brak w hashach.** Nie powtarzałem smoke'a
`new-game-sequence` (3457 zapisów, 1194→1195, F7/F32), więc tych liczb ten audyt
nie potwierdza ani nie obala. Obserwacje poboczne, nie rozbieżności:

- Poziom 0 ma w źródle **dwa** banki gameplay (`wad-gameplay` unikalne 705 i 706,
  oba 1 113 856 B, te same podsumowania: 296 Moby, 63/1114 TIE, 33/1697 shrub),
  ale z różnymi skrótami dekodowanymi. Kompilator bierze `primary_extents.front()`
  (`src/core/rac_level_moby_assets.cpp:248`), czyli pierwszy (705). Różnica
  między 705 a 706 jest UNKNOWN (logi `03-wad-gameplay-705.log`, `-706.log`).
- `prepared-native-level-smoke 0` raportuje 149 encji, 29 collectibles i 103
  destructibles; zgadza się to ze składem z sekcji 3.

## 2. Tabela pokrycia

Skróty poleceń (patrz sekcja 6): `ISO`, `ELF`, `PREP` to ścieżki bezwzględne z
sekcji 6. Kolumna „Log” to plik w `local/forensics/veldin-coverage/`.

| Rodzina | Źródło (poz. 0) | Pakiet v14 | Konsumpcja w runtime | Status | Polecenie → log |
|---|---|---|---|---|---|
| tfrag (chunki) | 460 rekordów scene-block (CONFIRMED); 78 tekstur tfrag | geometria terenu wypieczona w `world/render-scene`; **24 520 trójkątów, 29 386 wierzchołków** (INFERRED: wynik `level-native-package` 434 358/550 266 minus TIE 396 708/500 510 minus Moby 13 130/20 370 z `level-moby-scene`). Liczby rekordów zdekodowanych/pominiętych: UNKNOWN (żadne CLI ich nie wypisuje, pola `LevelSceneRecoveryResultV1` nie są drukowane) | Wypiekane przez `runtime::recover_level_scene_v1` (`src/runtime/level_scene_recovery.cpp`, `load_scene_geometry`, tylko entry 16) i `compile_level_scene_render_v1`; rysowane przez `enter_gameplay_level` (`src/runtime/windows_main.cpp:1228`) → `D3d11Renderer::set_gameplay_scene` (`src/runtime/d3d11_renderer.cpp:3183`) | CZĘŚCIOWE (geometria jest; morfing terenu zależny od kamery i mgła niezintegrowane wg ROADMAP.md:12-14) | `openrc-cli scene-blocks ISO 0` → `10-scene-blocks-0.log`; `level-tfrag-texture ISO 0 0` → `15-level-tfrag-texture-0-0.log`; `level-native-package ISO ELF 0 out.orlvl` → `14-level-native-package-0.log` (SHA pakietu `523d0ae35be37c337e48ff4bc1e502c4e32f894d4177365e14daa6f25a1f0aeb`, plik usunięty po uruchomieniu) |
| TIE (klasy / instancje) | 63 klasy / 1114 instancje, 965 pakietów, 50 338 wierzchołków, 40 672 trójkąty modelu, 131 tekstur (CONFIRMED) | wypieczone 63/1114 (wyjście: 500 510 wierzchołków, 396 708 trójkątów); w pakiecie v14 łączna suma trójkątów 423 394 jest spójna (INFERRED) | ten sam tor co tfrag; kolor wierzchołka TIE to stała diagnostyczna `0xffffffff` (`src/runtime/tie_scene_geometry.cpp:22`), a `rac_tie_lit_compile` jest używany tylko przez kompilator frontendu | CZĘŚCIOWE (geometria pełna, oświetlenie instancji BRAK) | `level-moby-scene ISO 0` → `06-level-moby-scene-0.log`; `wad-gameplay ISO 705` → `03-wad-gameplay-705.log` |
| Shrub | 33 klasy / 1697 instancji, 70 tekstur (CONFIRMED) | **0** (CONFIRMED z kodu: `LevelSceneRecoveryResultV1` nie ma rodziny shrub; `native_game_prepare.cpp` jej nie wywołuje; `rac_shrub_lit_compile` tylko we frontendzie) | brak konsumenta | BRAK | `level-moby-scene ISO 0` → `06-level-moby-scene-0.log`; `wad-gameplay ISO 705` → `03-wad-gameplay-705.log` |
| Moby | 296 placementów, 125 klas (96 lokalnych + 21 wspólnych + 8 zewnętrznych/zerowych), 21 klas faktycznie ustawionych, 256 „spawnable” (CONFIRMED) | 149 encji (1 gracz + 29 + 103 + 16), 132 powiązania renderu; rozkład 296 w sekcji 3 | encje/gameplay: `src/core/runtime_gameplay.cpp:186-198` (`load_scene`); aktorzy świata: `src/core/runtime_world_actor.cpp:46` `resolve_runtime_world_actors_v1`; gracz: `runtime_player_actor.cpp` | CZĘŚCIOWE (179 z 296 reprezentowanych, 117 pominiętych) | `level-core ISO 0` → `04-level-core-0.log`; `level-moby-scene ISO 0` → `06-level-moby-scene-0.log`; `prepared-native-level-smoke PREP 0` → `08-prepared-native-level-smoke-0.log` |
| Sky | UNKNOWN (nagłówek level-core ma `sky_offset`, `src/core/rac_level_core.cpp:262`, ale żadne CLI nie wypisuje liczby powłok/sprite'ów) | **0** (kompilacja sky jest wyłącznie we frontendzie: `compile_rac_frontend_sky_shells_v1`, `native_game_prepare.cpp:1974`) | brak konsumenta w torze poziomu | BRAK | `level-core ISO 0` → `04-level-core-0.log` (brak pola sky) |
| Woda i efekty | UNKNOWN (tabele `fx_textures`, `glass_map_texture`, `chrome_map_texture` istnieją w `include/openrc/rac_level_core.hpp:74-89`, liczby nie są drukowane). W kodzie brak słów „water”/„hud” | **0** | brak | BRAK | `level-core ISO 0` → `04-level-core-0.log` |
| Cząsteczki | UNKNOWN (tablica `particle_textures`, bank i definicje parsowane w `rac_level_core.cpp:299-312`, liczby nie są drukowane) | **0** | brak (LEVEL_ENTER_SOURCE_V1.md:155 „Particles: none”) | BRAK | `level-core ISO 0` → `04-level-core-0.log` |
| Kolizja | główna siatka: 109 906 wierzchołków, 53 460 ścian, 32 792 quady, 5784 oktanty; hero groups 0; powierzchnie 0x09=77, 0x0A=2287, 0x0C=24924, 0x1F=26172 (CONFIRMED) | `world/collision`: **39 085 trójkątów** (CONFIRMED w smoke). Reguła: ściany 3/4-wierzchołkowe są triangulowane, a duplikaty trójkątów (te same wierzchołki Q6, warstwa, typ) odrzucane (`src/core/rac_level_collision_compile.cpp`, `MeshCompiler::add_triangle`) | `load_runtime_level_foundation_v1` (`src/core/runtime_level_foundation.cpp:121`, wołane z `runtime_level_content.cpp:550`) → `runtime_gameplay.cpp` | PEŁNE dla głównej siatki; bloki cuboids (0x710 B), camera collision grid (0x4010 B), paths (0x77d0 B), grind paths nie są pakowane (INFERRED z listy 8 zasobów) | `level-collision ISO 0` → `11-level-collision-0.log`; `prepared-native-level-smoke PREP 0` → `08-prepared-native-level-smoke-0.log` |
| 7 kontenerów scene-animation | 7 kontenerów, 271 banków (kontener 0..6: 16/6/25/29/9/26/25 rekordów w przebiegu 0 i 16/6/25/29/9/26/24 w przebiegu 1); 49 klatek i 97 rekordów kamer w każdym zbadanym banku; aktorzy m.in. klasa 0 (Ratchet), 0xa (Clank), 0x6e, 0x212, 0x555, 0x663, 0x2ee, 0x5eb, 0x55f, 0x5c0, 0x640, 0x4e1 | **0** w pakiecie poziomu (`rac_scene_animation_compile` jest wołany tylko z `rac_frontend_scene_compile`) | brak konsumenta w torze poziomu | BRAK | `wad-payload-inventory ISO tsv` → `02-wad-payload-inventory.log` + `wad-payload-inventory.tsv`; `wad-scene-animation ISO 429/461/473/523/581/599/651` → `05-wad-scene-animation-<unique>.log` (po jednym banku z każdego kontenera) |
| Tekstury | tfrag 78, Moby 124, TIE 131, shrub 70 (razem 403 wpisy tabel); tekstury Moby 1 592 320 pikseli | **217 tekstur / 218 materiałów** w `world/render-scene` (CONFIRMED w smoke); wypiekanie przenosi tylko tekstury referencjonowane (`src/runtime/level_scene_render_compile.hpp`: „Only referenced source textures”). Przed dodaniem Bolt/Crate: 215/216 | `D3d11Renderer::set_gameplay_scene` | CZĘŚCIOWE (shrub, cząsteczki, fx, chrome/glass: brak) | `level-core ISO 0`, `level-moby-scene ISO 0`, `level-tfrag-texture ISO 0 0`, `prepared-native-level-smoke PREP 0` → `04-`, `06-`, `15-`, `08-` |
| Audio poziomu | SBlk: 236 deskryptorów, 456 elementów, 303 odniesienia audio, **218 bloków audio** (196 jednorazowych, 22 zapętlone), 86 820 ramek ADPCM, 1 389 120 B danych; blok „sound instances” w banku gameplay 0x1060 B; poza WAD: „auxiliary” poziomu 0 = 19 471 sektorów (zawartość UNKNOWN) | **0** zasobów audio poziomu (zasoby dźwięku w pakiecie pochodzą z `rac1/frontend-sound-bank`, tylko `frontend/audio/*`; `native_game_prepare.cpp:1988-1990`) | `windows_main.cpp` `admit_startup_audio` i `frontend_menu_audio` obsługują wyłącznie `frontend/audio/*`; w pętli gameplay brak odtwarzacza | BRAK | `sblk ISO 0` → `12-sblk-0.log`; `toc ISO` → `17-toc.log`; `wad-gameplay ISO 705` → `03-wad-gameplay-705.log` |
| HUD | UNKNOWN (żadne CLI nie liczy zasobów HUD; oryginał rysuje HUD w `1f91b0`→`1f8938`, LEVEL_ENTER_SOURCE_V1.md:135-137) | **0** | brak; w `src/` i `include/` nie ma słowa „hud” | BRAK | wyszukiwanie `hud` w `src` i `include`: 0 trafień — dowód kodu w `18-code-evidence.log` (brak liczby ze źródła, więc brak polecenia CLI) |
| Clank (klasa 10) | lokalny model: 44/0/6 pakietów, 75 stawów, 4502 trójkąty, 0 placementów; występuje jako aktor w kontenerach scene-animation 473, 523, 651 | **0** (biblioteka aktorów ma 2 modele: Ratchet i klasa 749) | brak | BRAK | `level-core ISO 0` → `04-level-core-0.log`; `prepared-native-level-smoke PREP 0` → `08-…` (Actor rigs/models 2/2) |
| Przedmiot w ręce (klasa 71) | wspólny gadżet #0 z 21: 79 376 B zdekodowane, 9 pakietów, 12 stawów, 20 sekwencji, 5 dźwięków; 0 placementów | **0** | brak (LEVEL_ENTER_SOURCE_V1.md:112 „INFERRED wrench”) | BRAK | `companion-wads ISO 0` → `13-companion-wads-0.log` |
| Pozostałe bloki gameplay | kamery 0xf0 B, światła kierunkowe 0x90 B, światła punktowe + siatka (0x10 + 0x4010 B), przejścia środowiska 0xa0 B, ścieżki 0x77d0 B, occlusion mappings 0x34c0 B, sound instances 0x1060 B | tylko bootstrap (śmierć, spawn); reszta **0** (INFERRED z listy 8 zasobów) | brak | BRAK | `wad-gameplay ISO 705` → `03-wad-gameplay-705.log` |
| Nierozpoznane ładunki poziomu | 5 ładunków bez klasyfikacji w extent0 poziomu 0 (131 072 B, 326 912 B, 2×0 B, 70 656 B) | 0 | — | UNKNOWN | `wad-payload-inventory` → `02-…log` i `wad-payload-inventory.tsv`; `wad-families` → `01-wad-families.log`, `wad-families.tsv` |

Uwaga o pakiecie: poziom v14 ma 8 zasobów (`world/collision`, `world/bootstrap`,
`world/render-scene`, `actors/library`, `actors/animations`, `world/entities`,
`world/gameplay`, `world/destructibles`) — kolejność składania w
`native_game_prepare.cpp:2486-2641`. `world/actor-behaviors` jest w runtime
opcjonalne (`runtime_level_content.cpp:613`), lecz kompilator go tu nie wytwarza.

## 3. Moby: 296 placementów → co kompilator z nimi robi

Liczby źródłowe: `04-level-core-0.log` (placementy na klasę) i
`06-level-moby-scene-0.log` (133 wyrenderowane, 153 animowane, 10 bez modelu).
Podział poniżej jest **INFERRED** (suma zgadza się z 296 i ze smoke'iem:
149 encji, 132 powiązania renderu), ale nie jest wypisany wprost przez CLI.

| Los | Liczba | Klasy (placementy) | Reguła w kodzie |
|---|---|---|---|
| Spawn gracza / encja gracza | 1 | 0 (1) | pierwsza Moby klasy 0 daje spawn (F22); `make_player_entity_scene`, `native_game_prepare.cpp:2513` |
| Collectible | 29 | 13 (29) | `compile_rac_collectible_scene_v1`, gdy `has_static_moby_class(…, 13)` (`native_game_prepare.cpp:2447-2448, 2518-2525`); polityka OpenRC, nie oryginał (F23) |
| Destructible | 103 | 500 (103) | klasa 500 wyłączona z płaskiej rodziny (`native_game_prepare.cpp:2471-2474`), kompilowana przez `compile_rac_destructible_scene_v1` (`:2530-2540`); polityka OpenRC (F23) |
| Aktor świata | 16 | 749 (16) | wyjątek tylko dla poziomu 0: `kVeldinLevelId`, `compile_moby_actor`, `compile_rac_moby_actor_scene_v1` (`:2449-2454, 2546-2563`) |
| Wypieczone statycznie | 30 | 501 (2), 511 (6), 1060 (21), 1964 (1) | `build_filtered_moby_scene_geometry_v1`: model bez `requires_bind_transforms` (`src/runtime/moby_scene_geometry.cpp:302-305`) |
| Pominięte: animowane | 107 | 1440 (9), 1564 (25), 1781 (33), 1782 (36), 530 (1), 834 (1), 1137 (1), 1520 (1) | `requires_bind_transforms` → `animated_model_placement_count`, `continue` (`moby_scene_geometry.cpp:302-305`); komunikat CLI „bind transforms pending” |
| Pominięte: brak modelu | 10 | 5 klas ustawionych bez modelu lokalnego/wspólnego (21 klas ustawionych − 16 z modelem) | brak klasy w `class_to_model` → `missing_model_placement_count` (`moby_scene_geometry.cpp:295-298`) |
| **Razem** | **296** | | 1+29+103+16+30+107+10 |

Kontrola krzyżowa: 153 animowane w CLI = 29 (13) + 16 (749) + 1 (0) + 107 pominiętych;
133 wyrenderowane w CLI = 103 (500) + 30 wypieczonych; 149 encji = 1+29+103+16;
132 powiązania renderu = 29+103; 135 instancji renderu = 3 (teren, Moby, TIE) + 132.

Nie można stąd wywnioskować, które z 107 animowanych są wrogami, NPC-ami lub
obiektami środowiska: CLI nie wypisuje ról klas. Placement klasy 834 (1) to
obiekt startujący scenkę (LEVEL_ENTER_SOURCE_V1.md §2) i jest wśród
pominiętych.

## 4. Pięć największych luk widocznych lub słyszalnych dla gracza

1. **Brak jakiegokolwiek dźwięku poziomu.** Źródło ma 218 bloków audio SBlk
   (303 odniesienia, 456 elementów), blok sound instances i 19 471 sektorów
   „auxiliary”; pakiet ma 0 zasobów audio poziomu, a runtime odtwarza tylko
   `frontend/audio/*`. Dokumenty: `docs/FIRST_PLAYABLE.md:56`,
   `docs/LEVEL_ENTER_SOURCE_V1.md` §4 (`sound update 28dee0`, mapowanie
   UNKNOWN), `docs/ARCHITECTURE.md:76-79`, `docs/AUDIO_VOICE_BANK_V1.md`.
2. **117 z 296 Moby (39,5%) nie istnieje dla gracza:** 107 animowanych (klasy
   1440, 1564, 1781, 1782 oraz 530, 834, 1137, 1520) i 10 bez modelu. Wśród nich
   obiekt klasy 834 (scenka startowa). Dokumenty:
   `docs/RENDER_SCENE_V1.md:224`, `docs/RAC_MOBY_ADMISSION_V1.md`,
   `docs/RAC_MOBY_POST_V1.md`, `docs/RAC_MOBY_FRESH_CONSTRUCTOR_V1.md`,
   `docs/LEVEL_ENTER_SOURCE_V1.md` §2 i §5 (zadania 2 i 5), F12/F13/F14.
3. **Brak shrubów, nieba, wody i efektów:** 1697 instancji shrub (33 klasy,
   70 tekstur) i cała warstwa środowiska poza geometrią tfrag/TIE mają w pakiecie
   0; tfrag/TIE są bez morfingu, mgły i oświetlenia instancji (kolor TIE =
   stała biała). Dokumenty: `docs/ROADMAP.md:12-14, 161`,
   `docs/RENDER_SCENE_V1.md:20, 224-228`, `docs/RAC_INSTANCE_LIGHTING_V1.md`,
   `docs/LEVEL_ENTER_SOURCE_V1.md` §4 pkt 3.
4. **Brak HUD i cząsteczek:** oryginał rysuje je w `1f91b0`→`1f8938` i w kroku
   cząsteczek z `299250`; w kodzie OpenRC nie ma ani jednego, ani drugiego.
   Dokumenty: `docs/FIRST_PLAYABLE.md:56, 103`,
   `docs/LEVEL_ENTER_SOURCE_V1.md:135-137, 155-157`.
5. **Brak Clanka, przedmiotu w ręce (klasa 71) i scenek poziomu:** Clank
   (klasa 10, 4502 trójkąty, 75 stawów) i wspólny gadżet klasy 71 nie trafiają
   do biblioteki aktorów (2 modele: Ratchet i klasa 749); 7 kontenerów
   scene-animation (271 banków) nie ma w pakiecie poziomu żadnej reprezentacji.
   Dokumenty: `docs/LEVEL_ENTER_SOURCE_V1.md:112-116` (Clank UNKNOWN, klasa 71
   INFERRED), `docs/RAC_SCENE_ANIMATION_COMPILE_V1.md`,
   `docs/SCENE_TIMELINE_V1.md`.

Poza pierwszą piątką: 5 ładunków poziomu 0 bez klasyfikacji (sekcja 2, ostatni
wiersz) i brak liczb źródłowych dla sky, cząsteczek i fx — wymagają nowego
narzędzia diagnostycznego, nie istniejącego CLI.

## 5. Sugerowane zadania implementacyjne (kolejność wg widoczności)

1. Dźwięk poziomu: kompilacja banku SBlk poziomu 0 do zasobów
   `openrc.audio-*` i odtwarzacz w pętli gameplay (komponenty audio już istnieją
   dla frontendu).
2. Animowane Moby: połączenie `RacMobyRotationSourceV1`/bind-pose z wypiekaniem
   lub aktorami dla klas 1440, 1564, 1781, 1782 (103 placementy to 35%).
3. Shrub i oświetlenie instancji TIE: istnieją `rac_shrub_class`,
   `rac_shrub_lit_compile`, `rac_tie_lit_compile`, `rac_instance_lighting`,
   używane dziś tylko we frontendzie.
4. Narzędzie CLI drukujące liczby sky/particles/fx/terrain-record z
   `LevelSceneRecoveryResultV1` i nagłówka level-core (zamyka UNKNOWN z tabeli).
5. HUD, Clank i klasa 71 po rozstrzygnięciu zadań #38 i reverse 210fe0.

## 6. Polecenia i logi

Zmienne używane w tabelach:

```
CLI  = D:\! Projekty\OpenRC\build-portable\openrc-cli.exe
ISO  = D:\! Projekty\OpenRC\local\ratchet-and-clank.iso
PREP = D:\! Projekty\OpenRC\local\prepared-milestone1-v14
ELF  = D:\! Projekty\OpenRC\local\prepared-stage1\SCES-50916\0f18a6c84cd8d727ec8c21000a236ed5ce2f279cbb8d0682fa9747199ef73260\files\SCES_509.16
```

| Log (`local/forensics/veldin-coverage/`) | Polecenie | exit / czas |
|---|---|---|
| `00-pe-audit-cli.log` | `openrc-portable-executable-audit.exe CLI` | 0 |
| `01-wad-families.log`, `wad-families.tsv` | `CLI wad-families ISO wad-families.tsv` | 0 / 31 s |
| `02-wad-payload-inventory.log`, `wad-payload-inventory.tsv` | `CLI wad-payload-inventory ISO wad-payload-inventory.tsv` | 0 / 34 s |
| `03-wad-gameplay-705.log`, `03-wad-gameplay-706.log` | `CLI wad-gameplay ISO 705` / `706` | 0 / 31 s |
| `04-level-core-0.log` | `CLI level-core ISO 0` | 0 |
| `05-wad-scene-animation-{429,461,473,523,581,599,651}.log` | `CLI wad-scene-animation ISO <unique>` | 0 / 20-28 s każde |
| `06-level-moby-scene-0.log` | `CLI level-moby-scene ISO 0` | 0 |
| `07-validate-native-game.log` | `CLI validate-native-game PREP` | 0 / 93 s |
| `08-prepared-native-level-smoke-0.log` | `CLI prepared-native-level-smoke PREP 0` | 0 / 2 s |
| `09-package-hashes.log` | `Get-FileHash -Algorithm SHA256` na manifeście, shared, `000.orlvl`, ISO | — |
| `10-scene-blocks-0.log` | `CLI scene-blocks ISO 0` | 0 |
| `11-level-collision-0.log` | `CLI level-collision ISO 0` | 0 |
| `12-sblk-0.log` | `CLI sblk ISO 0` | 0 |
| `13-companion-wads-0.log` | `CLI companion-wads ISO 0` | 0 |
| `14-level-native-package-0.log` | `CLI level-native-package ISO ELF 0 level-native-package-0.orlvl` (plik po uruchomieniu usunięty) | 0 / 58 s |
| `15-level-tfrag-texture-0-0.log` | `CLI level-tfrag-texture ISO 0 0` (bez pliku wyjściowego) | 0 |
| `16-inventory.log` | `CLI inventory ISO` | 0 |
| `17-toc.log` | `CLI toc ISO` | 0 |
| `18-code-evidence.log` | wyszukiwania w kodzie (HUD, shrub, scene-animation, sky, audio) | — |

Indeksy ładunków: unikalne 705/706 = banki gameplay poziomu 0, 704 = katalog
scene-block, 429/461/473/523/581/599/651 = pierwszy bank każdego z 7 kontenerów
scene-animation (z `wad-payload-inventory.tsv`, kolumny `container`/`run`).

## 7. Ograniczenia audytu

- Tory CLI `level-native-package` i `level-moby-scene` nie stosują wyłączeń
  klasy 500 ani dodatków Bolt/Crate z `native_game_prepare.cpp`; dlatego
  liczby trójkątów pakietu v14 (423 394) i toru CLI (434 358) różnią się.
  Residuum v14 po odjęciu terenu i TIE (2166 trójkątów) to Moby wypieczone
  plus siatki Bolt i Crate (INFERRED, nie rozbijane dalej).
- Liczba rekordów tfrag zdekodowanych/pustych/niekompletnych i liczby rodzin
  sky/cząsteczek/fx są UNKNOWN: zamyka je dopiero nowe narzędzie (poza lane #39).
- Nie uruchamiano runtime (brak `--level 0`, brak audio); wnioski o konsumpcji
  pochodzą z kodu i z liczb smoke'a CLI.
