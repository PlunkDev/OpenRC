# Kamera rozgrywki RAC V1 — typ 0

Notatka obejmuje PAL v2.00, Veldin, oraz czysty model
`RacGameplayCameraV1` w `third_person_camera.hpp/.cpp`. **CONFIRMED** oznacza
odczyt instrukcji lub danych źródłowych; nie oznacza wykonania na konsoli.
**INFERRED** oznacza przełożenie tego dowodu na geometrię neutralną albo
warunkowe założenie o przebiegu. **UNKNOWN** nazywa brakujący dowód.

**CONFIRMED — repozytorium:** deweloperska `ThirdPersonCameraV1` zachowuje
swoje API i zachowanie. Model RAC dodaje oddzielny stan, wejście i adapter do
`ThirdPersonCameraViewV1`; nie jest podłączony do `runtime_gameplay`.

## Dowody i możliwość powtórzenia

**CONFIRMED — identyfikacja źródeł:** adresy poniżej są adresami EE po
instalacji nakładki; `gp=166d00` wynika z `12d9bc..12d9d0`. Źródła:

| Plik / zakres | SHA-256 |
| --- | --- |
| ELF `SCES_509.16` | `17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b` |
| Nakładka Veldinu, LBA 1886019, zakres `+80`, długość `1930e4` | `922fd06e1ac6c717cc7832a3b8ac7be5aaf178ebe1b7982ae4cd294af1876c79` |
| `level-enter-source/gameplay-705.bin` | `f085b4717bbe197fb8785100894eb230f6dccba124dd84c4590f44158d0c6422` |

**CONFIRMED — artefakty analizy:** uruchomienie
`python -I local/forensics/gameplay-camera/task37_evidence.py` sprawdza hashe,
zapisuje wybrane instrukcje jako `task37-source.asm`, a bity tabel i transform
spawnu jako `task37-values.json`. Oba wyciągi i skrypt są UTF-8 bez BOM.
`level-enter-source/lvl.asm` stanowi niezależny wyciąg miejsc wywołań wejścia.
Adnotacje propagacji rejestrów disassemblera są pomocnicze; rozstrzygają
surowe instrukcje i rzeczywiste argumenty, także w slotach opóźnienia.

## Projekcja, wyświetlanie i skala świata

**CONFIRMED — `2466e0`, `23dac4`, `23dacc`:** wejście poziomu wywołuje
`23d9c0`, a ono `1f7bc8` i `1f7d00`. Pierwszy helper inicjalizuje właściciela
`16cb40`, drugi odczytuje selektor `15ee80`:

| Status | Pole / miejsce | Dokładne bity | Wartość / operacja |
| --- | --- | --- | --- |
| CONFIRMED | `1f7bc8`, `+a0` | `42000000` | near = 32 |
| CONFIRMED | `1f7bc8`, `+a4` | `49360000` | far = 745472 |
| CONFIRMED | `1f7bc8`, `+b0` | `3f2147ae` | tangens połowy kąta poziomego, około 0,63 |
| CONFIRMED | `1f7d00`, niezerowe `15ee80` | `3f418937` | `+b4 = +b0 * 0,756` |
| CONFIRMED | `1f7d00`, zerowe `15ee80` | `3f466666` | `+b4 = +b0 * 0,775` |
| CONFIRMED | `1f7d28` | `cafffbe0` | mnożnik źródłowego depth = −8388080 |
| CONFIRMED | `1f84a0`, `1f84ac`, `20061c`, `200624` | argumenty całkowite 512, 448 | niezerowy selektor instaluje `1519d0=512`, `1519d2=448` |
| CONFIRMED | `1f7bc8`, `+200/+204` | wynik `43800000/43600000` | połówki PAL: 256, 224 |

**CONFIRMED — `23e01c..23e038`:** selektor powstaje przez porównanie bajtu
informacji regionalnej z `'N'` (`4e`), zapis bool do `15ee80`. Przy niezerowym
selektorze `1f8450` wybiera powyższą ścieżkę PAL. **UNKNOWN:** brak dumpu
tych słów podczas konkretnego wejścia; podane wartości są wynikiem
potwierdzonej gałęzi inicjalizacji PAL, nie próbką pamięci działającej gry.

**CONFIRMED — `1f7f8c..1f8058`:** dla `t = near * (far - near)` zapis do
macierzy `16cb40+c0` zachowuje następującą kolejność operandów:

| Pole | Operacja |
| --- | --- |
| `+c0` | `half_width / (horizontal_tangent * near)` |
| `+d4` | `half_height / (vertical_tangent * near)` |
| `+e8` | `((far + near) / t) * depth_scale` |
| `+f8` | `(((near * -2) * far) / t) * depth_scale` |
| `+ec` | `(1 / near) * +210` |

**UNKNOWN — `+210`:** nie odtworzono konsumenta fog/depth-range; model
projekcji zwraca sześć poprzednio odzyskanych skalarów i nie rości praw do
pełnej macierzy GS. **CONFIRMED — `28f858`, `291ce0`, `29eb30`:** scenki
wpisują swój tangens do `+b0`; `291b20`, `29ea50`, `299f00` przywracają
`3f2147ae`. Cel czasowego minimum w `2913bc..2913f8` pozostaje **UNKNOWN**.

### Korekta adresu `vf30`

**CONFIRMED — `1fd204`, słowo `db9e9fa0`:** instrukcja to
`lqc2 vf30,-24672(gp)`, czyli odczyt `160ca0`, a nie `gp-24800`.
Wektor ma bity `3ca3d70a 3fc00000 44800000 447fc000`; `vf30.z=1024`
pochodzi z `160ca8`. `1fd258..1fd264` odejmuje oko od pozycji wierzchołków,
a `1fd268..1fd274` mnoży ich XYZ przez `vf30.z` przed projekcją.

**CONFIRMED — `160c20`, `28838c..2883a8`:** rzeczywiste `gp-24800`
zawiera słowa pakietu VIF `10000000 00000000 00000000 15000063`, kopiowane
jako pakiet; jego trzecie słowo nie jest skalą świata. Poprzednia notatka
#31 i adres w opisie #37 wymagają tej korekty.

**INFERRED — przełożenie `1fd268` na F22:** pozycje neutralne pozostają
w jednostkach umieszczonych Moby; odległości projekcji dzielimy przez 1024.
Wynik `near=0,03125`, `far=728` sprawdza test adaptera. Nie należy mnożyć
pozycji gracza ani oka przez 1024 w neutralnym runtime.

## Domena modelu, up i kolejność aktualizacji

**CONFIRMED — `lvl.camvtbl=1eac00`, dispatcher `1ebda0`:** rekord typu 0
ma init `2e8210` i update `2eb0d8`. **INFERRED:** jest to domyślna kamera
podążająca za graczem, na podstawie odczytów pozycji i prawego drążka.

**CONFIRMED — `2eb0d8`:** kolejność to wybór tempa yaw z `15eee4`,
`2e5b68`, `2e74b0`, fokus `2e6ce0`, oko/baza `2eb060`, reset `2e72e8`.
`2eb060` wywołuje `2eaf50`, `2ea3f0`, `2e9e60`, `2ea068`,
`2e91d0`, `2eabd0`, pusty `2e9828`, następnie `2ea4c0`.

**CONFIRMED — `1ed788`, `1ece0c..1ece60`:** init normalizuje Moby gracza
`+e0` do globalnego up, późniejszy update normalizuje `13f6e0` z długością
−1 i zapisuje `166f40`. Zwykła gałąź bajtu gracza `+20b3==0`
w `21334c..213358` zeruje XY i ustawia Z kierunku na −1.
**INFERRED — zwykłe podłoże Veldinu:** up = `(0,0,1)`, tak samo jego
wygładzony odpowiednik `166f30`; brak dumpu up przy wejściu. Model ogranicza
się do tej domeny i trybu wewnętrznego 0, bez zmiany podłoża lub override.

**CONFIRMED — warunki domeny w kodzie:** model odpowiada wyłącznie gałęziom,
które kod wybiera przy następujących słowach (wartości runtime na Veldinie
pozostają **INFERRED** dla zwykłego stania/chodu, brak dumpu):

| Miejsce | Warunek gałęzi domyślnej | Co robi poza domeną |
| --- | --- | --- |
| `2e5b68` (`2e5ba8..2e5c5c`) | wektor platformy `13f590..13f59c` równy zero | dodaje `13f590` do oka, fokusa, `+64`, `+208`, `+0`; `+456 = 13f59c` (obrót platformy) |
| `2e5b68` (`2e5c64`) | stan `1414d4` różny od 11 i 12 | zeruje `+16` i przesuwa pozycje o prędkość poziomą |
| `2e74b0` | `1416d4!=13`, `1414dc` poza {17, 5, 16}, bajt `1414f4` poza {1, 2}, bajt `1414fa==0`, `167110==0`, `166fdc==0`, stan `1414d4!=129`, `161e64==0` | nadpisuje cele `+348/+352/+240`, sprężyny fokusa (`2e9a40`), auto-yaw, timer `+16` |
| `2e5de0` | bajt trybu `+260==0` i `1414dc` poza {2, 4}, stan `1414d4` poza 11..15 | ustawia tryby 1, 2, 3, 5, 6, 8 z interpolacją `2e6be0` |
| `2e6498` | stan `1414d4!=119`, bajt `+260` poza 2..10 | cel `+64` z tabeli `1ea5e0` lub tryb 11 |

**CONFIRMED — `2e6498` w trybie domyślnym (`2e6a0c..2e6a38`):** cel kamery
`+64` to pozycja gracza `13f4d0` rozłożona na część pionową (`+80`) i
poziomą; `2e6ce0` składa je z powrotem, więc cel = gracz. **CONFIRMED —
`2e6f2c..2e6f88`, `1feed0`:** odniesienie `+128 = 1,5*up + lerp(cel, fokus,
+216/1feed0(90))`; `1feed0(n)` to `(int)(0,5 + n*15ee68)` (skalowanie liczby
klatek, nie losowanie), więc przy liczniku `+216=0` odniesienie to
`cel + 1,5*up`. **CONFIRMED — `2e7058..2e72a4`:** pivot `+144` powstaje z sond
`1efff0` w górę i w dół o 20 jednostek od `gracz + h*up` (`h` z `2e6fb0`) z
promieniem `0,25 + (+532)*(+524)*((+528)-1) = 0,95`; bez sufitu i bez podłogi
powyżej celu `+144 = 2*up + cel`. **INFERRED:** na zwykłym podłożu trafienie
podłogi nie leży powyżej stóp, więc model przyjmuje `+144 = gracz + 2*up`.

**CONFIRMED — API:** `special_states_modeled=false`,
`collision_modeled=false`, `entry_probes_modeled=false`, `ee_bit_exact=false`.
Wejście ze `special_override_active=true` jest odrzucane bez zmiany stanu.
Caller odpowiada za rozpoznanie domeny; model nie interpretuje całego stanu
Ratchet/Moby. W szczególności nie wolno go stosować po cichu w stanach
wymienionych w sekcji luk.

## Sprężyny i składanie oka

### Helpery `1eb5c0` i `1eb6a8`

**CONFIRMED — `1eb5c0`:** przyrost i oba ograniczenia są odtwarzane:

```text
delta = target - current
velocity = velocity + (k * delta - d * velocity)
jeżeli maximum != 0: clamp velocity do [-maximum, +maximum]
clamp velocity do [-abs(delta), +abs(delta)]
result = current + velocity
```

**CONFIRMED — `1eb6a8`, `2000e0`, `200098`:** wariant kątowy zawija
różnicę i wynik. Granica dodatnia jest otwarta: `+π` przechodzi do `−π`,
`−π` pozostaje. Pi ma bity `40490fdb`; kod wykonuje dwa odejmowania albo
dwa dodawania pi, nie `fmod`. API przyjmuje kanoniczne kąty, nie dowolnie
wielkie obroty. Testy obejmują oba kierunki przejścia przez granicę.

| Status | Etap / adres | k (bity) | d (bity) |
| --- | --- | --- | --- |
| CONFIRMED | fokus `2e6da8..2e6e64`, `161d70` | 0,015 (`3c75c28f`) | 0,2 (`3e4ccccd`) |
| CONFIRMED | wysokości bazy `2ea668..2ea6c4` | 0,004 (`3b83126f`) | 0,2 (`3e4ccccd`) |
| CONFIRMED | bias patrzenia `2ea850..2ea874` | 0,005 (`3ba3d70a`) | 0,2 (`3e4ccccd`) |
| CONFIRMED | elewacja i azymut oka `2eacd4`, `2ead74` | 0,015 (`3c75c28f`) | 0,2 (`3e4ccccd`) |
| CONFIRMED | promień, gałąź domyślna `2eab98..2eaba4` | 0,02 (`3ca3d70a`) | 0,2 (`3e4ccccd`) |
| CONFIRMED | przywracanie docelowego dystansu `2e7374..2e7380` | 0,003 (`3b449ba6`) | 0,2 (`3e4ccccd`) |

**CONFIRMED — `2e6ce0`:** fokus `+160` rozkładany jest na część pionową
i poziomą; pion ma prędkości `+192..+200`, poziom `+176..+184`.
**INFERRED — specjalizacja up=+Z:** model aktualizuje Z, potem X i Y,
sprężyną fokusową bez mnożnika dt. Jedno `step_pal_frame` oznacza jedno
oryginalne wywołanie; nazwa nie uprawnia do aktualizacji per klatka renderera.

### Wejście `2e9e60` / `2ea068`

**CONFIRMED — `2e9e60`:** osie są już zdekodowanymi floatami pada
`13cb40+100/+104` (F16). X jest negowane przy zerowym `15eee0`,
Y przy niezerowym `15eedc`. Interpretacja słów jako opcji inwersji jest
**INFERRED**; API nazywa ich wartość liczbową, bez nazw ustawień użytkownika.

**CONFIRMED — `2ea068`, tabela `161e80`, wybór `2eb0d8`:** odpowiedź yaw
używa `1eb5c0` z `k=d=1`, bez maksymalnego kroku. Wynik mnożony jest przez:

| `15eee4` | Dokładne bity | Przybliżony krok przy pełnym wejściu |
| --- | --- | --- |
| 0 | `3c8efa35` | 1° |
| 1 | `3cb9dede` | 1,3° |
| 2 | `3ce4c388` | 1,6° |

**UNKNOWN — `15eee4/15eee0/15eedc` podczas konkretnego wejścia:** brak
próbki zapisanych ustawień użytkownika. Domyślne wartości struktury wejścia
(selektor 1, oba słowa niezerowe) są wygodą API; caller ma dostarczyć swoje
rozstrzygnięte wartości. Neutralny drążek w teście entry nie zależy od nich.

**CONFIRMED — `2ea0dc..2ea1dc`, ze slotami opóźnienia:** druga martwa
strefa pitch jest ścisła `axis < -0,3` / `axis > +0,3` (`3e99999a`).
Pozostały zakres mnożony jest przez `3fb6db6e`. Pitch ma `k=d=1`,
maximum `3c23d70a` (0,01) dla odpowiedzi zerowej lub zmniejszenia modułu
bez zmiany znaku; w innych przypadkach `3ca3d70a` (0,02).
Te ograniczenia nie obowiązują yaw. Test sprawdza także dokładną granicę 0,3.

**CONFIRMED — `2ea1f4..2ea2f0`:** wygładzona odpowiedź pitch jest mnożona
przez `3f32b8c2` (40°). Kąt offsetu oblicza `1ff860` z argumentami
`x=length(offset)`, `y=dot(offset,up)`, czyli `atan2(z,length)`, a nie `asin`.
Jeżeli moduł tego kąta jest mniejszy od modułu celu i użyto kroku 0,01,
`+432` otrzymuje sprzężenie `offset_pitch/40°`; bieżący cel nie jest liczony
ponownie. Różnica obrotu offsetu ma dodatkowy limit `3cfa35dd` (1,75°).

### Znak obrotu i iloczynu wektorowego

**CONFIRMED — `1ff370`, słowa `4bc112fe`/`4bc208ee`:** po `lqc2 vf1,0(a1)` i
`lqc2 vf2,0(a2)` VOPMULA ma `fs=vf2`, `ft=vf1`, a VOPMSUB zamienione
operandy, więc helper zapisuje `a2 × a1`. **CONFIRMED — `1ffe18`, słowa
`4bc20afe`/`4bc111ee`:** VOPMULA ma `fs=vf1 (a)`, `ft=vf2 (b)`; wynik to
iloczyn Hamiltona `a ⊗ b` (`xyz = b*a.w + a*b.w + a×b`, `w = a.w*b.w - a·b`).
**CONFIRMED — `260c80→25e228→260bf0`:** poniżej `3727c5ac` wektor jest
kopiowany; inaczej `q = (n*sin(θ/2), cos(θ/2))`, a wynik to
`(q ⊗ v) ⊗ conj(q)`, czyli dodatni kąt to obrót prawoskrętny wokół osi.
Pułapka: `dis.py` drukuje rozkazy VU „special2” (`vopmula`) w kolejności
`ft, fs`; rozstrzygają pola bitowe (`ft=20..16`, `fs=15..11`), zapisane
w `task37-values.json` (`vu_cross_operands`).

**CONFIRMED — niezależna kontrola, `1f7284..1f730c`:** macierz widoku
`166d80` ma kolumny `(-row1, -row2, row0)` wierszy kamery `1670d0..1670f0`,
więc ekranowe X rośnie wzdłuż `-row1`. Przy `row1 = up × forward` (`1ff370`
wywołane jako `(forward, up)` w `2e7b68`, `2ea64c`, `2ea95c`) jest to
„w prawo” w świecie prawoskrętnym Z-up (F22). Odwrotny odczyt (`q⁻¹vq`,
`a1 × a2`) z poprzedniej wersji tej notatki dawałby lustrzany obraz i został
wycofany. Wszystkie osie obrotu pochodzące z `1ff370` i tak dają ten sam
wynik w obu odczytach; różnica dotyczy obrotu yaw drążkiem wokół stałego up.

**CONFIRMED — skutek dla drążka:** przy niezerowym `15eee0` dodatnia oś X
(`13cb40+100`, F16: wychylenie w prawo) obraca offset oka dodatnio wokół up,
czyli oko krąży przeciwnie do ruchu wskazówek zegara (patrząc z góry), a widok
skręca w lewo. Zerowe `15eee0` odwraca kierunek. Statyczne obrazy mają
`15eee0=15eedc=15eee4=1`; czy to domyślne ustawienia użytkownika i jak je
nazywa menu, pozostaje **UNKNOWN**. Hostowe `sin/cos` zastępują mikroprogramy
VU0 (kwalifikacja numeryczna poniżej). Override `+452/+456/+460` wynoszą zero.

### Offset `2ea3f0`, oko `2eabd0`, baza `2ea4c0`

**CONFIRMED — `2ea3f0`, warunek `+16>0`:** `2e72e8` wpisuje `+16=1` na końcu
każdej aktualizacji, a init `2e8348` też, więc etap działa w każdej zwykłej
klatce (zeruje go tylko zewnętrzny `2e9a18`). **CONFIRMED — `2ea418..2ea4a0`,
`1ff288`:** `O=+304` jest interpolowane `O + (P0 - O)*0,75` (`3f400000`), gdzie
`P0 = +0 - +144`, czyli poprzednie niefiltrowane oko widziane z bieżącego
pivotu (to lerp, nie dodanie `0,75*P0` — poprzednia wersja notatki była
błędna). Długość jest ograniczana: powyżej `+348` skracana do `+348`, poniżej
`+348 - +512` wydłużana do tej granicy; potem `+512 = +348 - |O|`.
**CONFIRMED — `2e8540..2e8594`:** init ustawia `+512 = +348 - |O - g*(O·g)|`
z `g = 13f6e0`. **INFERRED:** dla wyprostowanego gracza (`g=(0,0,-1)`)
`+512≈0`, więc bez kolizji długość `O` równa się celowi `+348` (domyślnie
`40947ae1` = 4,64). Model przechowuje `+0` jako `unfiltered_eye` i `+512` jako
`offset_shortfall`; test ruchu bocznego i test wejścia sprawdzają oba.

**CONFIRMED — `2e6fb0..2e7008`:** bajt `1414f4` wybiera wysokość sondy:
2 → `40400000` (3), 1 → `3ecccccd` (0,4), pozostałe → `3f000000` (0,5).
**CONFIRMED — `2e5974`, `2e5994`, `2e7034..2e7048`:** skale odniesienia
i pivotu są niezależne: `3fc00000` (1,5) i `40000000` (2). Wysokości sondy
nie zastępują tych skal; API udostępnia selektor wysokości liczbowo, bez
wymyślania nazw postaw. Wyniki sond pozostają **UNKNOWN**.

**CONFIRMED — `2eac38..2eae4c`:** przed filtrowaniem zapisywane jest
`+0 = +144 + O`. Promień `S=+320` korzysta z `1eb5c0` (bieżący `|S|`, cel
`+348 - +512`), potem elewacja i podpisany azymut z `1eb6a8`. Kąty bazują na
`1ff7c8` (asin): cel elewacji `asin((O·up)/(+348-+512))`, bieżąca elewacja
`asin((S·up)/nowy_promień)`; azymut to `π/2 - asin(Oxy·Sxy/(|Oxy||Sxy|))`
ze sprężyny startującej od 0, znak `+1` gdy `(up × Sxy)·Oxy >= 0`. Poziome `S`
obracane jest o krok azymutu wokół up, potem o pełną nową elewację wokół
`S' × up`, i skalowane do nowego promienia. Prędkości to `+344/+340/+336`.

**CONFIRMED — `2eae50..2eaefc`:** gdy `S` leży bliżej niż `3e860a92` (15°) od
osi up (`166f40`), obrót wokół `up × S` o `15° - kąt` koryguje `S` i zeruje
trzy prędkości. Oko `slot+48 = (fokus + 2*up) + S` składa `2eaef8` **po** tej
korekcie (argument `a2 = +320`); poprzednia wersja notatki twierdziła
odwrotnie. Test `test_original_pole_correction_precedes_eye` to sprawdza.

**CONFIRMED — `2ea560..2ea638`:** kierunek poziomy pochodzi z poziomej
różnicy `+128 - outer_eye`; poniżej `3d4ccccd` (0,05) pozostaje poprzedni
forward. **INFERRED — zwykły tryb 0 i zerowy timer przejścia:** odniesienie
`+128` to pozycja gracza plus 1,5 up, zatem jego część XY jest pozycją gracza.

**CONFIRMED — `2ea5dc`, `2ea6c0..2ea750`:** `s1` to `sp+30`, pozioma różnica
`H = poziom(+128) - poziom(oko)`. W slocie opóźnienia `2ea6e4` liczone jest
`f20 = +40 - +36` (nowe wartości sprężyn wysokości), a łańcuch odejmowań daje
`V = H + up*((+320·up) + (+40 - +36))`, czyli wysokość oka nad punktem
odniesienia. Przy wejściu `V_z ≈ 0 + 2,0 - 1,5 = 0,5`, więc widok patrzy
w dół o około `atan(0,5/4,64) ≈ 6,2°` na punkt `gracz + 1,5*up`. Poprzednia
wersja notatki pomijała `+320·up - +36` (wynik 23°); test pierwszego widoku
sprawdza teraz `forward.z ≈ -0,5/√(4,64²+0,5²)`. Gdy `|V|=0`, `2ea768` pomija
obrót pitch.

**CONFIRMED — `2ea7c0..2ea884`:** bias look-pitch zależy od kąta `O`
podzielonego przez 40°. Jest zerowy dla stosunku ≥ −0,1 (`bdcccccd`);
poniżej tej granicy moduł powyżej 0,5 jest zastępowany `1-moduł`, a cel
wynosi `2*moduł*15°`. Bias używa sprężyny k=0,005. Różnica facing pitch
i bias jest zawijana i ograniczona do ±`3f9c61aa` (70°) w `2ea888..2ea8cc`,
po czym forward obraca się o tę różnicę wokół `left = normalize(up × forward)`
(dodatnia różnica pochyla widok w dół). Na końcu `2ea950..2ea984` liczy
`left = normalize(up × forward)` i up widoku `forward × left`. Fokus i
rendererowy target są odrębnymi wielkościami.

### Zależność dystansu od ruchu `2ea9c8`

**CONFIRMED — `1ecf60..1ecfe0`:** pozycja gracza minus poprzednia pozycja
tworzy poziomy kierunek `166f90` i długość `166fb4`. **CONFIRMED —
`2eaa14..2eaab8`:** wydłużenie jest wybierane dla ruchu >`38d1b717`
(0,0001) i `dot(movement_direction,previous_forward) <= -0,3`; przy
niezerowym `+548` i zerowej długości ruchu porównywany jest forward Moby.
Model przyjmuje aktualny yaw gracza jako opcjonalne wejście; brak wartości
zachowuje yaw inicjalizacji lub ostatniej aktualizacji.

**CONFIRMED — `2eaac8..2eab48`:** waga to
`min(length * 4164923a, 1)` (mnożnik około 14,2857). Dla bajtu `1414f4!=1`
kandydat dystansu interpoluje domyślne 4,64 do `40c00000` (6); dla 1 cel
alternatywny to `40800000` (4), wykluczony z domeny zwykłego modelu.
Zapamiętywany jest największy kandydat w `+552`. `+548` jest ustawiane
przez `1feed0(120)`; w tym przebiegu istotna jest wyłącznie niezerowość,
którą przechowuje `movement_distance_active`. Nie odtwarzamy wartości
czasowej tego słowa ani nie wprowadzamy wymyślonego wygasania.

**CONFIRMED — `2eab4c..2eab7c`:** sprężyna promienia interpoluje k=0,02
do `3d23d70a` (0,04), d=0,2 do `3e99999a` (0,3). `2e9900(0)` ustawia
override celu; `2e72e8` stosuje jego sprężynę k=0,003 po złożeniu widoku.
**CONFIRMED — `2eac50..2eac78`:** bieżący cel promienia jest odczytany
przed `2ea9c8`, dlatego wydłużenie działa od następnej aktualizacji.
Po niespełnieniu strażnika `2eab80..2eab94` zeruje aktywność i wraca do
domyślnego dystansu. Test obejmuje opóźnienie, współczynniki, kontynuację
przy zatrzymaniu i powrót. Limit 4,64 dotyczy gałęzi domyślnej, nie całej gry.

## Pierwsza klatka po wejściu do poziomu

**CONFIRMED — `2422d8`, miejsca `2445b8..2445d0`:** po admission Moby
kolejno wykonywane są `205278`, `205598`, `1ed6d8`, dokładnie jedna
aktualizacja `1ed428`; patrz `LEVEL_ENTER_SOURCE_V1.md` §1. Init kamery
przechodzi przez `1ed600/1eb848` do `2e8210`.

**CONFIRMED — `205278`, `2053c4`, `212ed8`:** źródłem transformu jest
pierwsza Moby klasy 0, lecz sonda `25a6d0` może zmienić jej Z. `212e70`
zwiększa `13f75c` do 1 przed inicjalizacją kamery. Zatem poprzednie
uproszczenie #31 o gałęzi `13f75c==0` nie potwierdza faktycznego wejścia.
Niezerowa gałąź `2e5770` sonduje pion i może zmienić pozycję inicjalnego
fokusu; `2e7b68` sonduje pozycję oka.

**CONFIRMED — `gameplay-705.bin`, rekord 0, offset `7a730`:** jedyna Moby
klasy 0 ma XYZ `4304170a 42e6f5c3 41fb70a4`
(132,08999633789062; 115,4800033569336; 31,43000030517578) i rotację
`00000000 00000000 3f29a6ce`, yaw 0,6627014875411987 rad.

**CONFIRMED — `2e58e0`, `2e7c1c..2e7c40`, `2e8500..2e85b8`:** init
rozdziela fokus, pivot i odniesienie; od pozycji oka odejmuje pivot i kopiuje
offset do `+304/+320`, `+0 = oko`, zeruje prędkości oraz ustawia wysokości
2 i 1,5. `2e7b68(1)` składa oko jako `cel + 1ff680((-+348, 0, +352), Moby+c0)`
(`x*row0 + y*row1 + z*row2`), a bazę jako forward do `cel + 1,5*up`,
`left = up × forward`, `up = forward × left` (`2e7fe0..2e8064`).
**INFERRED — wariant zwykły bez trafień sond:** dla rozstrzygniętej pozycji
`P` i yaw wiersze Moby to `(cos,sin,0)`, `(-sin,cos,0)`, `(0,0,1)`, więc oko to
`P - 4,64*(cos(yaw),sin(yaw),0) + 2*up`, offset względny do pivotu ma Z=0,
fokus=P, `+512≈0`.

**INFERRED — kontrakt `rac_gameplay_camera_level_enter_state_v1`:** wykonuje
taką inicjalizację i jeden update, z przekazanym wejściem drążka.
Test wykorzystuje rzeczywiste bity spawnu Veldinu, sprawdza oko w pobliżu
(128,43207; 112,62503; 33,43000), kierunek patrzenia i dokładnie jedną
aktualizację. To test warunkowego modelu, nie wyrocznia rzeczywistego entry.
**UNKNOWN — rzeczywista pierwsza klatka:** brak wyników `25a6d0`, sond
`2e5770/2e7b68`, aktywnych override, próbki pada i dumpu obiektu kamery.
`entry_probes_modeled=false` pozostaje jawne.

## Adapter dla istniejącego renderera

**CONFIRMED — repo, `ThirdPersonCameraViewV1`,
`d3d11_renderer.cpp::make_camera_projection`:** renderer wyznacza forward
z `target-eye`, right przez `cross(forward,up)`, up przez `cross(right,forward)`;
przyjmuje tangens pionowy z FOV i poziomy z iloczynu z aspect. Depth D3D
jest w [0,1]. API deweloperskiej kamery nie jest do tego rozszerzane.

**INFERRED — adapter z dowodów `2ea4c0`, `1f7d00`, `1fd268`:**
`rac_gameplay_camera_renderer_view_v1` zwraca oko oraz `target=eye+forward`
(suma binary64), up bazy, `vertical_fov=2*atan(vertical_tangent)`,
`aspect=horizontal_tangent/vertical_tangent`, near/far podzielone przez
skalę świata. `camera.view()` używa ścieżki PAL 512×448 i skali 1024.
Aspect nie jest stosunkiem pikseli 512/448; ma zachować oba tangensy źródła.
Zmiana rozmiaru okna nie może automatycznie nadpisać tego kontraktu.

**CONFIRMED — test `test_original_renderer_adapter_matrices`:** w układzie
neutralnym yaw=0 daje right=(0,−1,0); baza jest ortogonalna. Test składa
macierz widoku z wierszy right/up/forward i niezależną projekcję D3D,
sprawdzając środek obrazu oraz near→0 i far→1. Nie importuje macierzy
głębi GS ani mnożnika `cafffbe0` do D3D. Stan wyłącznie fokusowy jest
odrzucany przez adapter.

## Kwalifikacja numeryczna i weryfikacja

**CONFIRMED — implementacja, `1ff7c8/160820`, `1ff860/1c27a0`:**
sprężyny i skalarne wielomiany asin/atan2 są obliczane hostowym binary32
z zapisanymi bitami współczynników. Źródłowe asin ma niezerowy błąd przy
argumencie zero; mały pionowy residual pierwszego update nie jest usuwany.

**INFERRED — zamiennik `260c80` i źródłowych VU:** obroty idą ścieżką
kwaternionową `25e228/1ffe18` w kolejności operacji źródła, ale z hostowym
`sin/cos` zamiast mikroprogramów VU0 (`1ff798=cos`, `1ff7b0=sin`, rozpoznane
z `1ed900`); nie odtworzono tych mikroprogramów. Pierwiastek w asin bierze
`|1-|x||` jak VU SQRT, bez dodatkowego clampu. Dzielenie i normalizacja są
hostowe; dzielenie przez zero EE (wynik ±max) nie jest modelowane — takie
stany model odrzuca wyjątkiem. `ee_bit_exact=false` obejmuje cały model.

**CONFIRMED — testy repo:** istniejące testy deweloperskie pozostają;
nowe pokrywają trzy tempa yaw, inwersje, granicę dead-zone, kroki pitch,
oba clampy 70°, limit domyślnego offsetu, obie granice pi, spawn Veldinu
(oko, pochylenie widoku o ~6,2°, długość offsetu i `+512`), gałąź dystansu
zależną od ruchu, korektę bieguna przed złożeniem oka, adapter oraz 1000
identycznych aktualizacji stanu i widoku. Determinizm dotyczy powtórzenia na tym samym hoście;
**UNKNOWN:** zgodność między bibliotekami trygonometrycznymi i względem EE.

## Do podłączenia w runtime

1. **UNKNOWN — wyniki `25a6d0`, `2e5770`, `2e7b68`:** kontrakt `25a6d0`
   jest już znany (F49: odcinek `(X,Y,Z+0,5)→(X,Y,0,01)`, zapis Z tylko przy
   `R>0`), ale wynik zależy od trafienia `1efff0` w świat Veldinu. Runtime ma
   podać do modelu rozstrzygniętą pozycję po `205278`; sondy fokusa
   (`2e5770`, 0,2 w górę / 30 w dół) i oka (`2e7c68`, `2e7cc4`) pozostają
   pominięte. Surowy spawn nie jest dowodem braku korekty Z ani przeszkód.
2. **UNKNOWN — `1efff0`, wywołania `2e70c8/2e7140`, `2e7b68`:** brak
   pełnego kontraktu query kamery, flag (`15f058`, 148/150), trafień i
   aktualizacji niedoboru `+512` przez kolizje. Potrzebne są argumenty,
   bufory wyniku (`173f60`) i ścieżki wyboru trafienia.
   **UNKNOWN — `2e91d0`:** brak przełożenia iteracji Moby, filtrów i korekty
   offsetu na neutralny świat. Dlatego `collision_modeled=false`.
3. **Wykluczone jawnie — `2e5b68`, `2e74b0`, `2e9a40`, `2e6be0`, `2e5de0`:**
   warunki gałęzi są CONFIRMED (tabela w sekcji domeny), ale ich efekty
   (platforma `13f590`, tryby 1–11, nadpisania celów i sprężyn) nie są
   modelowane. **UNKNOWN:** wartości słów `1414d4/1414dc/1414f4/1416d4/
   13f590` w konkretnych klatkach Veldinu — potrzebny dump pamięci gracza.
   Domyślne `2e6be0` aktualizuje tylko osobny `+208` (sprężyna 0,01/0,2 do
   `cel + 1,5*up`), nieużywany przez oko typu 0.
4. **UNKNOWN — `2e9e60`, `2eaf50`, `2ea068`:** właściciele auto-look
   `+452/+456/+460`, flag `+440` (bity 0/1 z `2e9b30`) i strażnika `+520`
   nie są odzyskani; rola przebiegu siatki `2eaf50` (`15f080`, wpisy typu 5)
   jest UNKNOWN. Domena modelu wymaga zerowych override, bajtu `1414f4=0` i
   up=+Z; caller ma odrzucić inną.
5. **INFERRED — integracja z F16/F22 i `1ed428`:** przekazać floaty prawego
   drążka, trzy ustawienia liczbowe, pozycję i bieżący yaw gracza; wykonać
   update według oryginalnego harmonogramu oraz zastąpić źródło widoku
   wynikiem `RacGameplayCameraV1::view()`. Stan jest własnością sesji poziomu.
   Tej integracji nie wykonano w #37, zgodnie z pasem zadania.
6. **UNKNOWN — ustawienia użytkownika `15eee0/15eedc/15eee4`:** kod
   potwierdza mapowanie (przy wartości 1 prawo na drążku skręca widok w lewo,
   góra podnosi oko), ale nie domyślne wartości z karty pamięci ani nazwy
   opcji menu. Runtime musi przekazać rozstrzygnięte słowa, nie zgadywać
   „normalny/odwrócony”.
7. **INFERRED — adapter a okno:** `view()` zachowuje tangensy PAL
   (aspect ≈1,3228). Okno o innych proporcjach wymaga decyzji runtime
   (letterbox albo świadoma polityka), poza tym pasem.
8. **UNKNOWN — kwalifikacja rzeczywistej kamery:** pozyskać ślad klatek
   z obiektu typu 0 (pozycja, fokus, baza, prędkości, up, pad, override),
   porównać model z nim i dopiero wtedy rozstrzygnąć zgodność rzeczywistego
   wejścia. Wierności nie potwierdza sam smoke z kamerą deweloperską.
