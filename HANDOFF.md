# OpenRC — przekazanie pracy

## Najnowszy checkpoint runtime — instalacja stanu Veldinu 2026-10-03

Pełny zwykły smoke `new-game-sequence` na nowym pakiecie v14 PASS, exit0,
PID27044 zakończony: `local/forensics/frontend-transition/v14-normal-sequence-oct3-v1.log`
i `.err`. Intro → menu/dialog → New Game → rzeczywiste prepare →3 karty,
3 filmy616/1348/415 klatek i7 fade → ukończone ładowanie/cleanup → rzeczywiste
`level/admit-prepared-sections`. Ten ostatni konsument zachowuje kanoniczną
sesję, odbiera załadowane dane dokładnie raz do trwałego właściciela i wykonuje
3457 zapisów; revision1194→1195, ostatnia klatka niezmieniona.

**Następna granica to `level/enter`.** Log rozdziela `level_state_installed=1`
od `level_admitted=0 world_entered=0 level_playable=0`. Nie utworzono jeszcze
obiektów świata ani nie włączono rozgrywki. Zwykłe uruchomienie zatrzymuje się
na brakującym entry; smoke potwierdza wyłącznie wykonany zakres. Poprzedni
checkpoint v13 poniżej pozostaje historią, w tym jego brak instalacji.

Nowy neutralny `StateInstallationV1` i `GameSessionV1::apply_state_installation`
stosują istniejącą atomową transakcję zapisów. Walidowane są poziom, digest
schematu, revision i cały batch, z zachowaniem aliasów oraz kolejności. Brak
źródłowych adresów lub kodu w zasobie runtime. Szczegóły: `docs/STATE_INSTALLATION_V1.md`.
Wspólny zasób `new-game/level-installation` podnosi liczbę zasobów shared do58.
Trzy nowe kanoniczne obszary lokalne startują od prawdziwych obrazów boot;
schema ma361 buforów/467 widoków/61131 B. Instalacja stosuje powiązania fazy
poziomu, nie nadpisuje semantycznie innych pól frontendu pod dawnymi adresami.
267 właścicieli postępu i zapisany selektor obrazu pozostają bez zmian.

Porównanie z rzeczywistym oryginalnym copierem PASS:
`local/forensics/frontend-level-installation-compare-v1.log`. Po zatruciu
kanonicznego wejścia sprawdzono wszystkie3457 bajtów,354 pozostałe niezmienione
obszary, zachowanie alokacji sesji i pojedynczy przyrost revision. Wykonanie
komponentu zatrzymuje się przed2465f8, z jawnymi fixture wcześniejszego powrotu
i zakończenia usług kernela; nie jest pełnym źródłowym New Game ani capture PS2.
Sonda SHA256: `7f81239543d664d1c95622409b8048c969fc0daa8067b6c65a90a2ac2da8978a`.

Build CMake/static runtime i audyty PE PASS. Testy state-installation oraz
native-game-prepare PASS, w tym99 odrzuconych brakujących/uszkodzonych profili.
Stary literal „50 resources” w wyjściu tego testu nie opisuje v14; asercje
obejmują58 zasobów shared/51 zasobów kontynuacji. Pięć regresji PASS:
`local/forensics/level-installation-regressions-v1-summary.log` (GameWorld,
SessionState, StateIO, FrontendInput i FrontendSequence). Runtime SHA256:
`b2030594a42237e826f3748f327601d478a583219feb7d8fbb5fa94454618fd8`.

Przygotowanie i osobna dokładna walidacja wszystkich19 poziomów PASS:
`local/prepared-milestone1-v14`, profil `0.1.0-native-eight-resource-v14-level-installation`.
Logi `frontend-transition/prepare-v14.log` i `validate-v14.log`; manifest
SHA256 `d9249c74834a1423cb6b3bdfdb5b5812991cc634521ce54b73323dc583237fd9`,
shared SHA256 `950a7c6488402e082a7b537033319c848ba8b22b878e1c1e3008fa696091bb01`.
Brak nowej publikacji paczki użytkownika przez `build-portable.ps1`.
Wszystkie7 otwarć audio testu użyły Oculus index1 tylko w procesie potomnym;
NAME/REQUIRED/INDEX w Process/User/Machine oraz lokalny `audio-output.txt`
pozostają nieustawione. Uruchomienia użytkownika zachowują główne wyjście.

Najbliższa implementacja: uporządkowany katalog obiektów w istniejących
`next_session`/`LoadedStateV1.world`, z faktycznym brakiem encji dla odrzuconego
rekordu. Obecny loader tworzy wszystkie definicje; samo `disabled` nie wystarcza.
Admission → konstruktor → shared stores → post/spatial → color/reference musi
zakończyć się przed następnym rekordem. Pierwsza luka to połączenie rzeczywistego
d18 dla niezerowej rotacji z istniejącym post i właścicielem przestrzennym;
helper frontendu obsługuje tylko zerowe rotacje. Nie odtwarzać rozwiązanej już
luki drugiej sesji ani pól color/reference, które parser zachowuje. Oryginalne
1f7bc8/1f7d00 wymagają też powiązania projekcji; deweloperska kamera hosta nie
potwierdza oryginalnego entry. Cel pozostaje aktywny i nieukończony.

## Najnowszy checkpoint runtime — zwykłe New Game i wszystkie filmy 2026-10-03

`--smoke-stage new-game-sequence` PASS, exit0, PID20996 zakończony:
`local/forensics/frontend-transition/v13-normal-sequence-oct3-v1.log` i `.err`.
Ta próba wprowadza wejście menu przez istniejący test i wykonuje zwykłych
konsumentów sekwencji; nie używa osobnej diagnostyki biblioteki filmów.
Przebieg: intro363 klatki → menu141/dialog196 → zatwierdzone New Game/revision633
→ rzeczywiste wyjście frontendu → `transition/prepare` →3 karty/3 filmy/7 fade
→ zakończenie ładowania Veldinu i cleanup. Filmy mają616/1348/415 wyświetlonych
i zdekodowanych klatek, bez skip. Po pierwszych dwóch filmach audio jest
zatrzymywane przez właściwy tail (`audio_retired=1`, `audio_drained=0`), trzeci
osiąga też naturalny koniec audio. Nie utożsamiać resetu buforów z ich odtworzeniem.

`src/runtime/windows_main.cpp` rejestruje rzeczywiste prepare dla ograniczonego
świeżego profilu PAL v13. Sprawdza zgodne źródła/provenance25 zasobów audio,
powiązania do banków, programu, streamów i gain tables, wspólne intro/state/input/
sequence oraz rzeczywisty świeży stan sesji. Byte13de60 to istniejący owner
`progress/level/0/field-3001/bytes[0]`. Nie ma nowego stanu zastępującego oryginał.
Kanoniczny evaluator ustawia prefix; stop-all otrzymuje3 terminalne potwierdzenia
ambientu, zwalnia oba banki i kończy worker. GPU drain i porównanie ostatniej
klatki muszą przejść przed potwierdzeniem prepare. Osobny test audio-retirement
pozostaje osobnym diagnostycznym zakresem, pomijając rejestrację prepare tylko tam.

Nowa granica zwykłego przebiegu: **`level/admit-prepared-sections`**, potem
`level/enter`. Oba nadal niezaimplementowane. Log jawnie podaje
`level_admitted=0 level_playable=0`; PASS dotyczy prezentacji i realnego ładowania,
nie grywalności ani całego celu. Zwykłe uruchomienie bez smoke zgłasza ten brak
zamiast potwierdzać nieistniejącą implementację. Nie oznaczać celu jako ukończonego.

CMake/static runtime oraz jawny audyt PE PASS. Runtime SHA256:
`3d876c50d5120c5c2eb0ab702626e8978f459a836b8b5aef2829e1cc87f21475`.
Siedem otwarć audio użyło Oculus index1 wyłącznie w środowisku procesu testu.
Sprawdzono brak lokalnego `audio-output.txt` i brak trwałych zmiennych NAME/
REQUIRED/INDEX użytkownika i maszyny. Zwykłe uruchomienia używają głównego
wyjścia Windows. Brak nowej publikacji paczki przez build-portable.ps1.

Najbliższy krok: neutralne przyjęcie już wczytanego poziomu, zachowujące
kanoniczną sesję, potem właściwy entry. Istnieją `RuntimeGameplaySessionOptionsV1`
z `frontend_session` i `D3d11Renderer::set_gameplay_scene`, więc nie tworzyć drugiego
okna, renderera ani sesji. Samo połączenie deweloperskiej ścieżki `--level` nie
kwalifikuje oryginalnego entry: jej obecny zestaw encji/logika są częściowe.
`RuntimePlayerAnimationV1` pożycza dane rig/clip, więc przyszły owner poziomu
musi przeżyć animację i całą pętlę gry. Zachować rozdzielenie siedmiosekcyjnej
instalacji (`frontend-cleanup-install-source-evidence.md`, wykonana) od pełnego
entry/preloader/admission (`veldin-entry-prefix-source-evidence.md`, częściowe).
Poniższe starsze opisy niezarejestrowanego prepare są historyczne.

## Najnowszy checkpoint źródłowy — przygotowanie audio i obrazu 2026-10-03

`--sound-live-prepare-prefix` PASS, exit0:
`local/forensics/frontend-sound-live-prepare-prefix-v2.log`. Na zachowanej
historii trzech cykli menu wykonano prawdziwy prefiks233308 w kolejności
RPC50→18→34→36→06→08: wyłączenie efektów, StopAll, sprawdzenie czterech
slotów strumieni, zakończenie odczytów, zwolnienie banku i rozwiązanie referencji.
2078 instrukcji EE i11851 IOP; osiem oryginalnych wywołań, jeden rzeczywisty
free banku, jeden reset callbacka CD i jeden reset efektów. Nie poprzedzano
tego prefiksu osobnym wywołaniem StopAll. Wszystkie4200 RPC wykonane i odpowiedzi
dostarczone, brak kolejnych poleceń. EE zachowuje ostatnią dostarczoną odpowiedź08
do późniejszego potwierdzenia: `delivered_unacknowledged=1`, nie brak wykonania
unload. Pełne bajty tej odpowiedzi i jej adres sprawdzone; brak hostowej pompy EE.

Po83 jawnych tickach oryginalny worker zatwierdza oba KOFF, a model kończy
ostatnią obwiednię: zero aktywnych głosów, bank zwolniony, brak nowych próbek
niezerowych. Następnie52 rzeczywiste instrukcje233464→233574 wykonują siedem
zapisów wejść mgły, ustawienie czarnego RGBA i wyliczenie PAL fade5. Całe576 B
właściciela grafiki porównano z oczekiwanymi siedmioma zmianami; brak przebudowy
projekcji lub rysowania. Zachowano80 B ramki prepare,176 B frontendu i odpowiedź EE.

To wykonanie komponentu na zachowanej historii, nadal nie źródłowo osiągnięte
New Game ani cała funkcja233308: `normal_new_game_history=0 prepare_complete=0`.
Pozostałe ograniczenia zewnętrznej pętli wejścia/wyświetlania i modelu urządzenia
pozostają. Samo następne potwierdzenie EE opisuje
`frontend-sound-prepare-next-pump-oct3.md`; nie wymaga to dodawania źródłowych
dekoderów do runtime. Karty mają FGE=0, filmy używają neutralnych RGBA;
runtime zachowuje ostatni obraz przez istniejący renderer bez nowego stanu mgły.

CMake/static runtime i jawny audyt PE PASS. EXE SHA256:
`e9c7496a38defee63111019893d786a72fcaa5593bad5a313780176f82c53e3d`.
Wcześniejszy audio-prefix-v1 także PASS; nie zawiera jeszcze52 instrukcji grafiki
ani dodatkowego porównania bajtów dostarczonej odpowiedzi. Osobny komponent
`frontend-sound-live-stream-v1.log` PASS sprawdził216d88 na zachowanej historii;
został następnie objęty pełnym prefiksem. Żadna z tych sond nie otwiera playback.
Poniższy checkpoint StopAll i jego „najbliższy krok” są już historyczne.

## Najnowszy checkpoint źródłowy — StopAll i rzeczywisty koniec modelu 2026-10-03

`--sound-live-stop` PASS, exit0: `local/forensics/frontend-sound-live-stop-v3.log`.
Po tej samej historii3 cykli/4194 aktualizacji wykonano oryginalne EE22efe8,
rzeczywisty RPC18 i IOP CF3C. Przed wywołaniem:3 handlery,1 fizyczny właściciel
i1 działający głos modelu. Po powrocie: źródłowe listy puste,98 zapisów wyciszenia
pokrywających96 rejestrów L/R,1 callback źródłowy i oba pending KOFF=00ffffff.
Model nadal ma1 głos; czas i ENVX nie są sztucznie przesuwane przez StopAll.
Zachowano176 B ramki frontendu, SP wrócił przez prawdziwy epilog; dokładny zakres
czyszczenia cache/30 slotów EE sprawdzony bez hostowych zapisów właścicieli.

Następny krok oryginalnego workera zatwierdził oba KOFF przez1547c. Jawna kontynuacja czasu
urządzenia przez83 ticki/16600 klatek kończy ostatnią obwiednię bez nowego KON,
bez niezerowych próbek po wyciszeniu i bez dalszych odczytów ENVX. Końcowo:
20214 ticków,4042800 klatek modelu,266 startów,254 końce wejścia/12 obwiedni,
0 działających głosów. Ten sam worker pozostaje zaparkowany, bez zaległych
wybudzeń; nie jest to pomiar fizycznego SPU ani czasu konsoli.

Istotne zachowanie oryginalnego direct RPC18: przy pustym payloadzie source
12e820 nie zmienia znacznika15ed9c ani przeciwnej kolejki. Pump12ddc0 ponownie
wykonuje3 stare callbacki22f0f0. Most zachowuje ostatni rzeczywiście zakończony
batch i porównuje pełne bajty poleceń, odpowiedzi i rekordów callbacków oraz
wszystkie wskaźniki przed tymi wywołaniami. Oryginalny JALR/argumenty/zapis
wyniku nadal obowiązują. Te3 powtórzenia mają osobny licznik; liczba nowych
admissions/callbacks pozostaje9/9. Wcześniejszy v1 zatrzymał się na brakującym
kontrakcie powtórzeń, v2 na brakującym zakresie oryginalnego callbacka;
oba nie są wynikami PASS. Nie usunięto callbacków ani nie zmieniono sourceflow.

Implementacja w istniejącym celu CMake: `frontend_sound_live_stop_source_probe.hpp`
oraz rozszerzenie istniejącego live RPC/runnera. CMake, static runtime i jawny
audyt PE PASS. EXE SHA256:
`5339e3ab0cdf70bc8ead7d26584890fc908a656f68e6a72f31f54e966727c951`.
Brak playback, publikacji paczki lub zmian głównego wyjścia użytkownika.

To jawne wywołanie komponentu na zachowanym stanie, nie źródłowo osiągnięte
New Game: `component_invocation=1 normal_new_game_history=0 prepare_complete=0`.
Bank nadal załadowany. Najbliższy krok: oryginalny216d88 (stream stop/drain,
RPC34 i36) na tej samej historii, potem pozostałe efekty/unload w poprawnej
kolejności. Pełny prefiks233308 i `transition/prepare` nadal otwarte.
Sprostowania w `frontend-sound-live-prepare-gap-oct3.md`:1e9730 jest tylko
diagnostycznym zapisem argumentów, nie bankwait; RPC06/08 są asynchroniczne,
więc samo osiągnięcie233464 nie dowodzi opróżnienia kolejki ani zwolnienia banku.

## Najnowszy checkpoint źródłowy — rzeczywiste zgłoszenia audio 2026-10-03

Rozszerzenie `--sound-live-clock` PASS, exit0:
`local/forensics/frontend-sound-live-clock-v2.log`. Na tej samej historii3 cykle
tła /4194 aktualizacje wykonują4194 prawdziwe batch RPC,9 admissions/callbacks,
14151 updates,6 query,6 stop i25164 ustawienia grup głośności. Zachowany worker
6db8 oraz callback6df0 wykonują20131 ticków; source19770 rośnie oryginalnie,
worker wraca do prawdziwego SleepThread z zapisanym kontekstem i osobnym
obrazem stosu. Bez resetu IOP, banku, RNG lub handlerów. Wszystkie kredyty
wybudzeń rozliczone; semafor nasyca się zgodnie z max1, nie udaje kolejnych
sukcesów. Ograniczona wcześniejsza próba2 ticków także PASS (`*-clock-v1.log`).

Jawny model urządzenia korzysta z istniejącego AudioVoicePlayerV1, rzeczywistych
zapisów SSA/ADSR/pitch/gain i KON/KOFF. Przetworzył4026200 klatek48kHz:
266 startów,254 końce wejścia,11 końców obwiedni,1 nadal aktywny głos;
47193 odczyty ENVX,45904 niezerowe,0 odczytów nigdy nieuruchomionego głosu.
Nie kasuje właścicieli źródłowych ani nie uruchamia drugiego schedulera.
To model bez playback i bez fizycznego captureSPU. Rytm24/5 ticka na aktualizację
i początkowa faza0 są jawnym regularnym wejściem PAL; pełna zewnętrzna pętla
karty/wejścia/wyświetlania i rzeczywista faza od startupu nadal nie wykonane.
CMake i jawny audyt PE PASS; EXE SHA256
`57e2f091882a440b52960c86b76b5269f4ab48d7ad372bd59adc6338e0db2cf8`.
Ówczesny kolejny krok obejmował stop/stream-drain/unload. Nowszy checkpoint
powyżej potwierdza sam StopAll oraz koniec modelu; pełne prepare nadal otwarte.

`--sound-live-rpc` PASS: `local/forensics/frontend-sound-live-rpc-v3.log`, exit0.
Po zachowanej historii intro/init/bank/postload pierwsza aktualizacja tła
wykonuje prawdziwą kolejkę EE, batch9 poleceń i dispatcher IOP16c8. Sześć
poleceń9 ustawia rzeczywiste grupy głośności; trzy11 zwracają uchwyty
`85000002/85010002/85020002`. Oryginalny JALR12de78 i callback22f0a8 zapisują
je w rzeczywistych właścicielach EE13e6c0/13e730/13e7a0. Brak ręcznego
wywołania callbacków lub przydziału zastępczych handlerów. IOP8788 instrukcji;
źródłowe DE60/DE7C, przydział tonów i libsd Note2Pitch wykonane bez skrótów.

To **pierwsza aktualizacja, bez ticków audio**, nie trzy cykle żywych dźwięków.
ENVX i nieznane importy urządzenia są jawnie odrzucane; późniejszy odczyt38
wymaga jeszcze połączenia postępu urządzenia z oryginalną pompą. Transport
kończy się przy zadeklarowanej granicy cache, bez pomiaru latencji konsoli.
Pełna poprzednia próba3 cykli nadal PASS po dodaniu zachowywanego kontekstu
IOP (`frontend-sound-resumable-regression-v1.log`); nadal ma stare odpowiedzi
audio sondy. CMake, static runtime i jawny audyt PE PASS; EXE obu nowych
prób SHA256 `797b9a402ba510e9dcda0d57484f185b6c7e9baa07952816010ed3d912e8f6e4`.
Powyższe rozszerzenie zamyka następny etap timera/workera oraz modelu ENVX
przez `frontend_sound_live_timer_source_probe.hpp` i
`frontend_sound_live_voice_device_source_probe.hpp`.
Żadnego playback, zmiany wyjścia użytkownika ani publikacji paczki.
`transition/prepare` nadal niezarejestrowane.

## Najnowszy checkpoint runtime — zakończenie audio 2026-10-03

`--smoke-stage frontend-audio-retirement` PASS na istniejącym pakiecie v13.
Próba przechodzi rzeczywiste intro363 klatki, menu141, dialog196, zatwierdzone
New Game/revision633 oraz zwolnienie sceny. Następnie sprawdza nowy komponent
`retire_frontend_audio`: oba finite dźwięki zakończyły się naturalnie;
przed stopem ambient ma2 fizyczne/3 logiczne głosy, otrzymano3 terminalne
potwierdzenia, wszystkie7 uruchomionych głosów fizycznych zwolnione. Stop
programów, zwolnienie urządzeń, dołączenie workera i usunięcie obu dekodowanych
banków sprawdzone; zamrożony obraz i rewizja wspólnej sesji nie zmieniły się.
Pomocnik zachowuje właścicieli przy błędzie, umożliwiając ponowienie zwolnienia
urządzenia również po wcześniejszym potwierdzeniu stopu przez worker.

To osobna diagnostyka komponentu po zwykłym wyjściu ze sceny.
`transition/prepare` pozostaje **niezarejestrowane**, a player pozostaje
`incomplete`; log jawnie zawiera `normal_sequence_barriers_executed=0`.
Nie dodano zastępczych właścicieli efektów/odczytów ani nowych pól stanu.

CMake runtime oraz jawny audyt PE PASS. Runtime SHA256
`c8291da85620cdcffca3f7ec87a31f7820043284991c5fbd971e9e407078e29a`.
Logi: `local/forensics/frontend-transition/v13-audio-retirement-oct3-v2.log`
i `.err`; PID24524 zakończony exit0. Wszystkie4 otwarcia audio użyły Oculus
index1, wyłącznie przez zmienne procesu. Plik `audio-output.txt` nie istnieje;
User/Machine NAME i REQUIRED są puste. Wcześniejsza próba `*-oct3.log`/PID7768
zakończyła się przed playback, bo otrzymała względną ścieżkę prepared-root;
nie jest wynikiem diagnostyki audio. Nie zmieniono publikacji pakietu.

Stan implementacji sprawdzany 2026-09-26 w `E:\projekty\OpenRC`.
Kontynuujemy istniejący projekt i lokalną pracę. Użytkownik wznowił główny cel
2026-09-26 po zapisaniu innego wyjścia audio; status celu `active` sprawdzony.
Prace kontynuujemy z wyjściem Oculus wyłącznie dla testów agenta, jak poniżej.
Nie wykonano commita, pusha, resetu ani usuwania wcześniejszej pracy.

## Najnowszy checkpoint źródłowy — ciągłość frontendu 2026-10-03

Log `local/forensics/frontend-background-transpose-v1.log` (exit0) potwierdza na wspólnym
stanie EE/IOP: oryginalny prolog frontendu, przygotowanie grafiki i heap,
intro, pełne wygaszenie/obraz praw autorskich po intro, bank audio,
inicjalizację danych, ładowanie tła oraz dane UI po tym ładowaniu. Następnie
wykonuje3 pełne pętle tła:4194 aktualizacje,20970 aktualizacji aktorów,
9 zgłoszeń cue,6 stopów i45 kolejnych dekodowań chunków. Aktualizacje
wykonały21202548 instrukcji, cała historia EE28005213. To PASS ograniczonej
próby `--sound-prepare`, nadal nie całej interaktywnej historii menu ani E2E.

Ważne potwierdzone szczegóły:
- Prolog `1e99d8→201e88`:13 instrukcji, rzeczywista ramka128 B i zapis
  `15f6c8=1`. Jego wcześniejsze pominięcie błędnie dopuszczało przestrzenną
  logikę audio z pustym wskaźnikiem kolizji; naprawiono producenta stanu,
  bez zastępowania wyniku kolizji. Ograniczony collision helper pozostaje
  materiałem diagnostycznym i nie jest potrzebny w tej poprawnej ścieżce.
- Rzeczywiste bufory filmu `552640/812640`, bez wcześniejszego host seed
  `800000`. Postintro:12 wygaszeń,15 granic VBlank,13 łańcuchów DMA i13
  oryginalnych callbacków; dokładne pokrycie wszystkich917504 bajtów obrazu.
  Cache efektów i menedżer strumieni zachowane,113402 instrukcje PASS.
- Init:21 rekordów/267 kopiowań/57355 sprawdzonych bajtów; rzeczywisty
  transport przez oryginalne workery IOP. Tło:156 obiektów geometrii,
  296 TIE +569 shrub,32600 kolorów porównanych z niezależną referencją.
  Dwa odczyty tła:162 odczyty CD,4933632 bajty danych SIF. Pięć aktorów,
  czas pętli1398. Postload:6 odczytów/258048 B i2 dekodowania UI PASS.
- Oba oryginalne uploady VIF0 pozostają na wspólnej historii. Stos sondy
  przeniesiono do rzeczywistego zakresu deklarowanego przez CRT, poniżej
  `02000000`, a pakiet diagnostyczny SIF do `01ff8000`; stare adresy
  kolidowały z oryginalnymi danymi. Naprawiono też lifetime kopiowanych
  hooków EE/IOP przy zagnieżdżonej zmianie adaptera.
- Shared scalar112 przypadków PASS (`frontend-ei-packed-source-v1.log`),
  w tym EI i PSRLW. Math1280 przypadków +32 cosine/32 sine bridges PASS
  (`frontend-pool-math-oct3-integrated.log`). Normalizer `1f9dc0`:160
  niezależnych przypadków/2560 instrukcji/pełne361 słów stanu PASS
  (`frontend-pool-normalize-oct3-integrated.log`). Camera960 przypadków PASS
  (`frontend-pool-camera-oct3-integrated.log`). Dot `1f9c78` i transpose
  `1fa4a0` wykonują oryginalne instrukcje przez istniejący adapter camera,
  bez nowego loweringu arytmetyki; ich dodanie zamknęło pętlę aktualizacji.

CMake i jawny audyt PE PASS; EXE ostatniej próby SHA256
`6e10442162fd8c865dcee8851467d2f0760ec68431c244e6dfb2cc351a347c9c`.
Wszystkie powyższe próby bez playback. Startup module loader, zewnętrzna
pętla wyświetlania/wejścia/karty i kompletne menu history nadal poza pełnym
wykonaniem; ukończenia DMA/OS są jawnymi wejściami diagnostycznymi, nie
pomiarem fizycznej konsoli. Log jawnie raportuje `cue_outer_register_fixture=1`
i `display_input_outer_loop_executed=0`. EE census komend audio nadal korzysta
z jawnych odpowiedzi RPC sondy; nie zaliczać go jako wykonania żywych
zgłoszeń programów przez IOP. Kompozycja tych programów ma osobne dowody.
Szczegóły: `frontend-post-intro-history-gap.md`,
`frontend-outer-prefix-history-gap.md`, `frontend-input-cold-source-gap.md`
i źródłowe notatki pod `local/forensics`. `transition/prepare` nadal
niezarejestrowane. Najbliższy konkretny krok: skierować rzeczywiste RPC `0x11`
z cue do zachowanego właściciela IOP/banku `0xc4040` i przekazać jego prawdziwy
wynik/uchwyt do callbacka `22f0a8`. Obecny hook `12e688/12e820` dla tego RPC
rejestruje dane i zwraca zero; `accepted_admissions=9` oznacza przyjęcie przez
kolejkę EE, nie przez IOP. Dopiero rzeczywisty most pozwoli sprawdzić dalszy
stop-all/unload w tej samej historii. Osobnym nadal otwartym odcinkiem jest
zewnętrzna pętla wyświetlania/karty/wejścia `1ebd68→1ebdbc` i jej wcześniejsi
producenci; nie zastępować ich gotowymi statusami ani rozszerzać zakresu PASS.
Nie wykonano ponownej publikacji paczki ani nowego testu z playback.

Poniższe wpisy z września i zachowany snapshot v10 w `FIRST_PLAYABLE.md` są
historyczne tam, gdzie opisują menu jako niepodłączone lub zatrzymują próbę
na instrukcji SUB. Aktualny checkpoint dodano też do `FIRST_PLAYABLE.md`
i `MEDIA_CLIP_V1.md`. Bieżący stan runtime opisuje pierwszy checkpoint tego
pliku; bieżący stan sondy opisuje niniejszy checkpoint.

## Poprzedni checkpoint źródłowy — bank audio 2026-09-26

Ograniczony `--sound-bank-load` PASS na tym samym stanie EE/IOP po intro.
Oryginalne EE `12db68` wiąże dwa klienty RPC i wyprowadza argument fn0 `137c00`;
IOP `25c` zapisuje `18edc` przed `6918`. Naprawia to wcześniejsze pominięcie fn0.
EE `1e9d58→22ea20→12e060` otrzymuje rzeczywisty uchwyt banku `0xc4040` z fn3,
a oba fn8 wykonują osobne rozwiązanie referencji. Dziewięć iteracji oryginalnych
workerów odczytu i zakończenia transferu dostarcza 153600 B; przesłanie do SPU
147680 B, 10 deskryptorów / 124 rekordy, 12 transferów statusu SIF DMA oraz
blokujące fn36 sprawdzone. Poziomy wyjścia efektów i ich właściciele
pozostają niezmienione. Żadnego playback ani zmiany publicznych pakietów.

Szczegóły: `local/forensics/frontend-sound-bank-load-source-evidence.md`;
Końcowy `frontend-sound-bank-load-source-v5.log` PASS, exit0, jawny audyt PE PASS.
EXE SHA256 `affc75e1adff453f28f1593d3095a927919b998e164f42a6a08de23fe6dd2379`.
Kolejka CMake i envprobe wolne; żaden proces tej próby nie pozostał aktywny.
Helper podpięty przed ładowaniem tła w ciągłej próbie historii efektów.
Po tym checkpointcie złożona próba zatrzymuje się na brakującej instrukcji
EE SUB `1f9974/00441022` we wspólnym executorze (obszar neutral_audio),
log `frontend-sound-prepare-bank-integrated-v1.log`. Prepare NIE odblokowano.
Ta ograniczona próba banku nie potwierdza jeszcze całej historii sceny/menu.

Poprawiono także opis starej próby dysku: import `175ac` / ID37 usuwa callback CD,
nie wykonuje CdSync; log teraz nazywa licznik `cd_callback_resets`.
Źródłowe oczekiwanie na trzy pending owners nadal jest wykonane i sprawdzone.

## Wyjście audio — bieżące ustawienie 2026-09-26

Użytkownik doprecyzował: **Oculus tylko podczas testów agenta; jego ręczne
uruchomienia mają korzystać z głównego/domyślnego wyjścia Windows**. Root usunął
wcześniej utworzony `%LOCALAPPDATA%\PlunkDev\OpenRC\audio-output.txt` po
sprawdzeniu, że zawierał dokładnie wybór Oculus. Brak trwałych zmiennych audio
w środowisku User/Machine. Nie zmieniono globalnego wyjścia Windows.

Każdy test agenta odtwarzający dźwięk musi ustawić WYŁĄCZNIE we własnym procesie:
`OPENRC_AUDIO_DEVICE_NAME=Słuchawki (Oculus Virtual Audio` oraz
`OPENRC_AUDIO_DEVICE_REQUIRED=1`. Dokładna nazwa WinMM jest skrócona przez
Windows, obecnie indeks1. Nie zapisywać tego wyboru ponownie w konfiguracji
ani globalnym środowisku. Sam REQUIRED bez NAME teraz celowo odrzuci odtwarzanie.
Source-only probes bez playback nie potrzebują żadnego ustawienia audio.

Bezgłośne sprawdzenie po zmianie PASS: zwykłe uruchomienie wybiera
`Windows default output`, explicit0/index4294967295; proces testowy z powyższym
NAME+REQUIRED wybiera Oculus, explicit1/index1. Obie próby playback_opened0.
Wspólny `windows_audio_device` obejmuje filmy/klipy i PCM streaming. Pozostaje
obsługa jawnego wyboru z env lub pliku; niedostępny jawny wybór nie przechodzi
na domyślny. Dawny `audio-output-selection.log` dokumentuje wcześniejszy
wybór z pliku i nie opisuje już aktywnej konfiguracji.

Po wznowieniu `v12-movie-owner-oculus.log` PASS, exit0: nowy `PreparedMovieOwner`
obsłużył intro, trzy pełne filmy (616/1348/415 klatek), trzy karty, siedem
wygaszeń, rzeczywiste ładowanie Veldinu i przerwany film0 (112 klatek), z
potwierdzonym zwolnieniem audio/dekodera/GPU. Trzecia karta trwała200 klatek,
bo odczyt już się zakończył; wcześniejsze `v12-async-level.log` dowodziło jej
wydłużenia do220 podczas rzeczywistego oczekiwania. Diagnostyka nadal jawnie
raportuje `normal_sequence_barriers_executed=0`.

Także zwykłe START→INTRO→MENU→DIALOG→NEW GAME→zwolnienie sceny menu PASS,
exit0 (`v12-owner-menu-oculus.log`), rewizja633/target0, oba finite dźwięki
zakończone naturalnie. Runtime obu prób SHA256
`dd4690807d3dc79f024340d856b913ff9216bf63daaee605805b4b57b6595d4f`.
Poprzednie dwa przerwania pierwszego filmu nie powtórzyły się na Oculus;
ich przyczyna pozostaje nieustalona, nie twierdzić, że sam refactor był błędny.
Runtime wypisuje stdout/clog bez buforowania i niezależnie zapisuje wyjątek do
`%LOCALAPPDATA%\PlunkDev\OpenRC\logs\runtime-last-error.log`. Zapis sprawdzono
celowo nieistniejącym prepared-root (PID23128, oczekiwany exit1); ten zapis
ostatniego błędu nie oznacza niepowodzenia powyższych udanych przebiegów.

## Integracja właścicieli ambient — wznowiona 2026-09-26

Nowe neutralne `AudioVoicePlayerV1` i `AudioVoiceMixerV1` łączą sprawdzone
stream/envelope/read-ahead: końcowa zerowa klatka obwiedni rozwija fazę,
EOF nie rozwija obwiedni, release zachowuje poziom, fizyczny slot pozostaje
aż do jawnego retire. Mikser sumuje signed PCM przed końcową saturacją.
Review wykrył i poprawiono atomowość kontroli dla małego watermark:
maksymalny ruch to min(refill,watermark+1), a nie samo refill. Przypadek
refill16/watermark3/phase44,64,0 jest regresją w teście.

Wspólny batch2016 + jawne audyty PE i wykonanie PASS: audio-program lifecycle,
audio-voice, audio-voice-mixer, audio-voice-bank (`*-integrated-tests.log` w
`local/forensics`). AudioGainTable codec/shared immutable states:4 grupy PASS
(`audio-gain-table-tests.log`). Wszystkie nowe pliki core i testy zarejestrowane
w CMake. `rac_frontend_sound_bank.cpp` również zarejestrowany po tym batchu.
To komponenty, jeszcze nie podłączony żywy ambient ani `transition/prepare`.

Źródłowa kwalifikacja gain znalazła dzielnik32766 zamiast32767 w krzywej
głośności. Poprawka compiler-side:30240 przypadków/4095195 oryginalnych
instrukcji PASS; wszystkie5 dotychczasowych finite payloadów zachowały hashe.
Envelope:9 par/45 release trajektorii/4944892 klatek PASS. Finite EOF:
16 bloków×5 zmiennych pitch patterns,80/1104088 kroków PASS. Pitch:
31 tonów/11 strojeń plus positive-center,786432 wywołania libsd i44385525
instrukcji PASS,9 saturacji base+LFO PASS (`audio-pitch-source.log`).

Retirement7 grup/377881 oryginalnych instrukcji PASS
(`audio-retirement-source-v3.log`). Tag41 od razu usuwa logiczne ownership,
ale fizyczny release tail trwa; tag38 czeka tylko na logiczne głosy. Normalny
tag43 kończy graph i pomija tail; tail jest wykonywany przez jawny stop.
Naturalne on-state wymaga4 kolejnych obserwacji ENVXzero; detached off-state
zwalnia slot na pierwszym zerze. Pending start pomija obserwację tylko do
rzeczywistego commit; NIE stosować stałej liczby skip od momentu admission.
Kolejność:observe→program→release commit→start commit→modulation, na trwałej
siatce200 klatek PCM przy48kHz. Stopped voice obserwuje się jakozero nawet
jeśli jego diagnostyczny snapshot zachował dawny envelope.level.

Najbliższa integracja: bank13 streamów/4 tablic gain/26 bindings/5 programów,
pełne wykonane kwalifikacje source binding i EE pan, potem host/controller
na neutralnych bankach i workerze PCM, zachowujący powyższy rzeczywisty cykl.
Nie publikować nowego profilu zanim źródłowe walidacje banku przejdą.

## Bank ambient i Windows host — kolejna integracja 2026-09-26

`AudioVoiceBankPlayerV1` zarejestrowany w core/testach, audyty PE i testy PASS.
Łączy trwały zegar/RNG, pending commits, naturalne4zero, detached tails i
fizyczne retire. Konstruktor odrzuca opóźnione/cykliczne/wait-owned/voice-start
stop-paths; `stop_all` nie udaje wykonania odroczonego tail. Source-qualified
v7:9grup/381461 instrukcji; pre-KOFF obserwacja detached counter1 nie zmienia
PCM, ownership ani callbacków, więc neutralny skip pozostaje równoważny.

Pełne5programów:12000+320ticków,49681 kompletnych stanów,357events,334callbacks,
wszystkie250słów RNG na tick,8843713 oryginalnych instrukcji PASS. Program4
naturalnie zapętla tag38→tag36; jego normalny tag43 jest nieosiągalny, więc
wcześniejsza asercja naturalnego zakończenia4 była błędem fixture. Wszystkie
7gałęzi4,118blocked-wait i76restartów zakwalifikowane. Kompozycja rzeczywistego
banku:2640200PCMframes,271starts/268completions/271retirements,max7physical,
codec/nieregularne bufory/readmission/hard-stop silence PASS. Logi:
`audio-full-program-source.log`, `audio-bank-player-composition.log`.

Nowe neutralne `AudioProgramCuesV1` + codec/evaluator/testy PASS; source cue v4:
102boundarycases,16callbacks,8validitystates,8EE-stopstates,4194updates
(3×1398),818783instr PASS. Rzeczywisty ownerclock to18cc54, NIE19cc54
(błąd dawnej notatki i testowego adresu). Timeline.samples[n] przechowuje
wizualny postclock(n+1)%1398; cue scan czyta preclock n, więc używać INDEXn.
Source scan→modeupdate→dialog leadingpump→common pump. State7 queued jest
nieważny; pump przed RPC zmienia7→1(valid), callback nonzero1→2; zero→0.
Stop1/2→4 jest natychmiast invalid, stop7 kasuje do0.

`WindowsAudioProgramBankV1` działa przez istniejący PCM worker, oddziela
zewnętrzny token/queue receipt od worker admission i terminal callback.
Stop bez release zachowuje logical owner aż do rzeczywistego retirement;
stop przed przyszłym admissionframe odrzucany synchronicznie. Rezerwuje oba
eventy; checked stop-all wymaga bank ack + native close/join. Test rzeczywisty
`startup-media/audio-program-bank-device-v3.log` PASS:24800frames byte-exact
z neutralnym referencyjnym playerem,13events,6physicalstarts,explicit
stop/replacement/futurecancel/unstartedstop/generic retained owner/native
writefailure. Wszystkie4otwarcia wyłącznie Oculus index1. Media probe SHA256
`cf9e704bdd362604c74a9848b94030e38b7a6bff09d3c91c6438523ab24d36d2`.

Windows main podłączony: dla paczek z ambient bank worker zaczyna przed intro
i czeka na pierwsze submitted PCM. Zachowuje zegar podczas intro→menu;
cue mirror skanuje przed mode evaluate, przyjmuje/obserwuje eventy tylko przy
pump; dwa pumpy dialogu nie dublują przyjęcia. Runtime i media probe zbudowane
i audytowane. Nowe źródło hosta w CMake runtime oraz ignored media probe.
Szczegóły: `docs/WINDOWS_AUDIO_PROGRAM_BANK_V1.md`. Zwykły v12 regression PASS,
exit0 (`frontend-transition/v12-ambient-host-regression.log`): intro363,
menu141/dialog196,canonical request633,target0,oba finite clips natural,
GPU scene retired/frozen image. Runtime SHA256
`0bf8a7b6d3040f9255d383bffc5fcb6bf86b2db60ab201fed5be31a4527596c4`.
Ta próba zachowuje zgodność v12; żywe cue z v13 wymagają odrębnej próby.
Przed próbą v13 root poprawił runtime validation: źródłowe pierwsze3cue
mają last_scene_sample99999999 i legalnie obejmują całą pętlę1398. Kontrola
wymaga osiągalnego first_sample, a nie last_sample wewnątrz pętli. Bank+cues
muszą występować razem; częściowy zestaw nie jest już pomijany jak stary profil.
Runtime po obu poprawkach build+PEaudit PASS, SHA256
`598b92dd850d8bc948ae50266de8099d75f1bce6377e3a9804c9e04901599ff0`.

Diagnostic neutral LevelPackage20resources gotowy:
`local/forensics/frontend-transition/ambient-resources/ambient-audio.levelpkg`,
15454856B,SHA256e8240b0cb6e8a9ee87a04004c72acb8ca41303efad24435668f235e5940b1793.
To samo20payloadów, manifest id/type/path/hash obok; cue255B. Native v13
integracja57sharedresources/limits32provenance,512total PASS: syntetyczny test
50flowresources i94missing/tampered przypadki (15nowychaudio), a rzeczywisty
merge v12shared+20audio w pamięci przeszedł strict admission. Logi
`audio-v13-native-profile-tests.log` i `audio-v13-actual-profile.log`.
CLI również64/32/512 i zwiększony aggregate budget o shared320MiB.
Pełne19-level prepare i `validate-native-game` v13 PASS, exit0; sesja77611
zakończona. Logi `frontend-transition/prepare-v13.log`, `validate-v13.log`.
Compiler `0.1.0-native-eight-resource-v13-frontend-ambient`, suma1631802213B,
shared57resources/223322234B,SHA256
`3ece00eaea9063fbfe0a777b2aa8a68221c949cec8c29284a29925fa79ad934f`.
Manifest1622B,SHA256
`e0dcf52a185d386423ab907363288895691813fda81674ee8bfd27e18467e452`.
CLI build+PEaudit+frontend-soundtests PASS; CLI SHA256
`4bc425553d50b16274b9836f621aaadba76f6d246d2d4cb98625db75b23668d2`.
V12 pozostaje nietknięte; to prepared data, nie końcowa publikacja portable.

Rzeczywisty v13 START→INTRO→MENU→DIALOG→NEWGAME→frontend exit PASS, exit0
(`frontend-transition/v13-ambient-menu.log` i stderr, sesja1269/PID3640
zakończone). Persistent worker potwierdza pierwsze PCM przedintro; cues0/1/2
przyjęły programy2/3/8, przy request programtick6114,started5,physical2,
logical3,played_frames1221274. Main update141/dialog196,revision633,target0,
obydwa finite clips natural; renderer scene retired/frozen image preserved.
Ten wczesny NewGame nie dochodzi do późniejszych sceniccue4/9; wszystkie5
mają osobne source/composition/device kwalifikacje wyżej. Runtime SHA256
`598b92dd850d8bc948ae50266de8099d75f1bce6377e3a9804c9e04901599ff0`.
Test ustawił Oculus WYŁĄCZNIE w swoim procesie; normalna konfiguracja użytkownika
pozostała na Windows default. `transition/prepare` nadal nie wykonane.

Nowy `--smoke-stage new-game-sequence` kontynuuje tę samą normalną sekwencję
po zatwierdzeniu New Game. Rzeczywista próba v13 (`v13-normal-sequence.log`
i `.err` pod `local/forensics/frontend-transition`) osiągnęła menu141,
dialog196, revision633 i rzeczywiste zwolnienie sceny, po czym zakończyła się
oczekiwanym exit1 dokładnie na `transition/prepare`. To dowód obecnej granicy,
nie zaliczenie całej sekwencji. Runtime SHA256
`b94edce3cf2059a1b16172778163df553887deeec7e2adb4d6719f34cab8b415`,
build i PE audit PASS; wszystkie cztery otwarcia audio tylko Oculus index1
ustawione procesowo. Ostatni log błędu runtime może teraz zawierać ten
oczekiwany brak consumera zamiast dawnej próby nieistniejącego prepared-root.

`transition/prepare` nadal NIEzarejestrowane. Finite+ambient stop/native close
oraz zwolnienie banków mają rzeczywiste API, ale pozostaje źródłowa kwalifikacja
wet-effect routingu i reached-path disc streams/discI/O.13AudioStreams są
rezydentnymi tonami, nie dowodem braku źródłowych strumieni dyskowych. Pcm_device
sprawdza historię effect state, neutral_audio reached-path stream/disc.
Wykonany routingproof `audio-prepare-routing-source-v1.log` PASS:
31tones,248routingcases,2effectzero cases,19537instr. Wszystkie31flags0
wyłączają wet send na obu rdzeniach; selectedcore1 zeroing działa, a pending
effect owner poprawnie odracza depth writes. Fixture zakładało initial type0,
więc nie dowodzi historycznego wet tail ani stanu IOP disc streams.
Compiler guard odrzuca ambient tone z inną flagą. Nie zaliczać gate na
podstawie samych pustych list.

### Końcowe sprzątanie prezentacji — 2026-09-26

Native `transition/cleanup` ma teraz rzeczywistą operację: po sprawdzeniu
zakończenia filmu, kart, frontend audio i odczytu poziomu czeka na najnowszą
pracę GPU i zwalnia jej obiekt zakończenia. `try_retire_submission_drain`
odrzuca niezaobserwowane/stare tokeny; późniejsze upload/draw/resize wymagają
nowego drain. Zachowuje obraz, urządzenie, swap chain i kanoniczną sesję.
Pozostaje za niezarejestrowanym `transition/prepare`, więc normalny przebieg
NIE przeszedł jeszcze tego consumera.

Build runtime/GPU-tests/envprobe oraz jawne PE audits PASS. Test rzeczywistego
GPU `transition-cleanup-d3d-tests.log` PASS, exit0: nieaktualne tokeny,
ponawianie po nowej pracy, zwolnienie query, identyczny obraz i dalsze użycie
tego samego renderera. Runtime SHA256
`33fbae71a8ceb997e5f129e464a20f789635cf055262d0f64e46e9848c91643b`.

`frontend-transition/v13-cleanup-media-library.log` i `.err` PASS, exit0
(PID29212/sesja79101 zakończone): intro363, pełne filmy616/1348/415,
karty200/150/213, siedem wygaszeń, rzeczywisty odczyt Veldinu, końcowe
zwolnienie GPU query z identycznym framebufferem i przerwany film112 klatek.
213 klatek trzeciej karty wynikało z rzeczywistego oczekiwania na odczyt.
Każde otwarcie audio Oculus index1, wyłącznie procesowo.
Diagnostyka jawnie zachowuje `normal_sequence_barriers_executed=0`.
Regresja normalnej ścieżki na tym samym runtime również osiągnęła New Game
revision633/target0 i zwolnienie sceny. `v13-cleanup-normal-sequence.log/.err`
zakończyły się oczekiwanym exit1 dokładnie na nadal brakującym
`transition/prepare` (PID25592/sesja76164 zakończone); nowy consumer cleanup
nie omija tej granicy. Wszystkie cztery otwarcia audio również tylko Oculus.

Niezależne źródłowe kwalifikacje helperów sceny: math1280cases/24080instr
oraz camera960cases/49920instr PASS. Każdy przypadek porównuje pełne361
słów stanu; cosine ma dodatkowo32przypadki przez istniejący VU executor.
Szczegóły `local/forensics/frontend-pool-math-source-evidence.md`.
Ciągły source replay init→intro→ładowanie/menu nadal w toku; nie utożsamiać
tych helperów z zaliczeniem całej historii audio/streamów.

## Cel i decyzje

Bieżący priorytet: **START → oryginalne INTRO → oryginalne MENU → NEW GAME →
pełna wymagana sekwencja cutscenek → grywalny VELDIN (level 0)**.
Novalis (level 1) służy teraz wyłącznie regresji wspólnych mechanizmów. Szerszy
Milestone 1 zachowuje późniejsze oryginalne przejście Veldin → Novalis.
Nie wracać do historycznego pytania o kolejność planet ani animacji.

Obowiązują AGENTS.md: jeden launcher/runtime/pipeline, neutralne pakiety w
runtime, wszystkie dekodery RAC/PS2 po stronie kompilatora, dane źródłowe tylko
w ignorowanym `local`. Windows EXE wyłącznie przez CMake, statyczny runtime MinGW,
audyt PE; uruchamiać tylko zweryfikowane EXE pod `build-portable`.
Pakiet dla użytkownika publikuje `scripts/build-portable.ps1`. Konfiguracja:
`%APPDATA%\PlunkDev\OpenRC`, cache/logi: `%LOCALAPPDATA%\PlunkDev\OpenRC`.

Pełne aktualne polecenie jest w
`C:\Users\Plunk\.codex\attachments\d4bcfd27-ece8-42e9-9d0f-5ba570b6c500\pasted-text.txt`.
Pierwotne 33 kryteria: attachment `e935f5fc-4f8b-43a6-8788-1d42694ff578`.
Aktualny zakres i zweryfikowany kod mają pierwszeństwo nad historycznymi pytaniami.

## Bieżąca integracja 2026-09-26 — v12 i rzeczywiste wyjście z menu

Profil `0.1.0-native-eight-resource-v12-frontend-transition` został przygotowany
dla wszystkich 19 poziomów w `local/prepared-milestone1-v12`. Dokładna walidacja
profilu PASS, exit 0 (`prepare-v12.log`, `validate-v12.log` pod
`local/forensics/frontend-transition`). Manifest 1625 B, SHA256
`7f55a7a4cd399b20157d0ae27303ac81fdda1952965f8bd0f0241bac6530cfdc`.
Shared ma 37 zasobów, 207864219 B, SHA256
`1ed216139d7e0859ad10c9c9f535a418a814cce88c850c05b0853f0b323e516d`.
19 poziomów zajmuje 1408479979 B; suma CLI ze shared to 1616344198 B.
Schemat: 358 buforów, 461 widoków, 57690 B i 267 kopii resetu. Sekwencja ma
39 cue: usunięto dwa dodatkowe oczekiwania po zapisach bieżącego poziomu,
zachowując same oryginalne zapisy oraz późniejszy rzeczywisty start I/O.

Rzeczywisty `--smoke-stage frontend-exit` PASS, exit 0 (`v12-exit.log`):
**START → INTRO → MENU → DIALOG → zatwierdzone NEW GAME → zwolniona scena menu**.
Intro: 363 klatki, 640976 próbek stereo, audio/decoder/GPU rzeczywiście
zakończone. Menu update 141, dialog update 196, żądanie rewizja 633/target 0.
Przyjęto pięć oryginalnych klipów; warianty 3 i 0 rzeczywiście wystartowały
i zakończyły się naturalnie. Wyjście zachowało ostatni obraz i zwolniło scenę
CPU/GPU po potwierdzonym zakończeniu pracy GPU. Ponowne rysowanie/resize podczas
czekania wymusza nowy fence, zamiast uznać starszy za wystarczający.
Tło i dialog mają identyczne hashe jak zweryfikowane v11 poniżej.
Audyt PE obu uruchamianych plików PASS. Runtime tego testu SHA256
`695ee92e70fe9cfb1f67f1e4cbf669f70e65295690ba766f6ea8862de6dcbb74`;
CLI przygotowania i walidacji SHA256
`b6e35d1a097eb968f775faaa6d51355890f9cdeb330b33058346f8f1ac7ec442`.

Pierwsza niezaimplementowana operacja normalnej kontynuacji to teraz
`transition/prepare`. Pięć jednorazowych dźwięków nie stanowi pełnego banku:
brakuje odtwarzania oryginalnych zapętlonych programów ambient, ich modulacji
i rzeczywistego zakończenia właścicieli audio. Neutralne AudioStream i Windows
PCM streaming są w trakcie integracji/testów; nie zaliczają jeszcze tego
przejścia. Pracujemy nad źródłowo potwierdzonym schedulerem i przyjęciem tych
właścicieli. Przygotowane trzy filmy działają w osobnym diagnostycznym przebiegu
opisanym poniżej, ale pełne New Game → cutscenki → grywalny Veldin nie działa.
Pełna końcowa publikacja przez `scripts/build-portable.ps1` nadal niewykonana.

Po powyższym checkpointcie wspólny host wygaszania został użyty zarówno przez
post-intro, jak i przygotowane cue New Game. Każda klatka musi być naprawdę
przedstawiona, a końcowe potwierdzenie czeka również na tail. Normalne
START→wyjście ponownie PASS (`v12-feedback.log`); wszystkie piksele 12 klatek
wygaszania zgodne z tablicami z tolerancją kanału ≤1. Runtime tego testu:
`fade8229bbca38bcbdfc7e6ff3aeab3fc3cb54d5fdd2025cfa2e7ca786bb572d`.
`v12-presentations.log` PASS, exit 0: na jednym urządzeniu wszystkie trzy karty
(200/150/200 klatek), siedem wygaszeń (5/2/4/2/4/2/4), trzy pełne filmy
(616/1348/415 klatek) i przerwany pierwszy film (112 klatek po żądaniu przy100).
Każde audio/decoder/GPU jawnie zakończone; trzy karty sprawdzone z framebufferem,
pierwsza także obejrzana (`v12-card-0.png`). Runtime tego testu:
`70c8d6568ec724487f4c41322da062a59afdd99d5126f4552592498b746b60ae`.
To rozszerzony diagnostyczny `media-library`, który jawnie raportuje
`normal_sequence_barriers_executed=0`; nie dowodzi pełnego New Game.
Normalne loading consumers zachowują ten sam backend karty pamięci po menu.
Trwa podłączanie rzeczywistego asynchronicznego odczytu neutralnego poziomu
przed trzecią kartą; wcześniejszy `transition/prepare` nadal jest zamknięty.

AudioStream ma siedem zaliczonych grup testów granic/liczb/kodeka
(`local/forensics/audio-stream-reviewed-tests.log`). WindowsPcmStream zaliczył
prawdziwe mono/stereo po37544 klatki, kolejkę, zegar, anulowanie oraz11 błędów
API z retry (`startup-media/pcm-stream-device.log`). Naprawiono retry zamknięcia
eventu po już zamkniętym urządzeniu. Nowy neutralny AudioProgram jest źródłowo
porównany przez12000 ticków:2472819 instrukcji,36003 stanów ośmiu skalarów,
104 zdarzenia i wszystkie250 słów RNG (`audio-program-source.log`). Starszy
probe używał nieprawidłowych uchwytów IOP i nie dowodził modulacji pitch bend;
poprawiony używa rzeczywistych uchwytów typu5 i wymaga niezerowego zapisu bend.
Nie jest to jeszcze kwalifikacja końcowego DSP/miksowania ani żywy ambient.

## Poprzedni checkpoint 2026-09-19 — dane v11 i rzeczywiste menu

Profil `0.1.0-native-eight-resource-v11-frontend-flow` ma29 wspólnych zasobów:
dotychczasowe7 oraz13 zasobów main/dialog,6 kart/filmów New Game i3 zasoby
kanonicznego stanu/wejścia/sekwencji. Wszystkie19 poziomów przygotowano w
`local/prepared-milestone1-v11`; exact native validation PASS, exit0
(`prepare-v11.log`, `validate-v11.log` pod `local/forensics/frontend-transition`).
Manifest1619B SHA256`519c2ae3758a3c24f374908cf907bf9961094d615c707182a5842a79da2806ff`.
Shared207478542B SHA256`70cca5e13b90fb9fad848d6dcf82bc0b214060c1bea7d4819fc9e3da37017900`.
19 plików poziomów nadal1408479979B; CLI suma1615958521B obejmuje shared.

Normalny runtime używa tej samej sesji, renderera, oryginalnych modeli/list
main i warstw dialogu. Rzeczywisty test `v11-flow.log` potwierdził pełne intro,
copyright, Press Start i menu (update141, entry12,273 submissions aktorów menu).
Zrzut `v11-flow.menu.png` przedstawia New Game / Load Game / Options.
Zrzut tła update100 jest identyczny z v10: PPM SHA256
`fbe965818bcc975492b65407942b2617a43b085c91466574f0ad7b4e4a2bab7d`.
Ten pierwszy test zakończył się exit1 przy oczekiwaniu na dialog. Przyczyna:
backend podawał wynik−1 (zmiana karty), który celowo pozostawia busy2.
Oryginalny brak urządzenia zwraca−11; adapter przekazuje teraz jawny wynik
zakończonego zapytania. Ponowny pełny smoke `v11-card-flow.log` PASS, exit0:
dialog panel/prompts przy update196, New Game zatwierdzone w tej samej sesji
przy rewizji632, target0, końcowa klatka wysłana. To potwierdza
**START → INTRO → MENU → DIALOG → żądanie NEW GAME**; nie jego dalsze cutscenki.
Regresja `input-v11-card-source.log`:1741 przypadków/98 resetów,864 title,
56 izolowanych card/menu,288 prezentacji,17 main oraz5 sekwencyjnych flow /
725 zapisów /71999 wykonanych instrukcji źródła PASS. Kontrola−1 nadal czeka,
właściwe−11 zwalnia busy i pokazuje panel; nie pominięto oryginalnej bramki.

Schemat ma347 buforów/448 widoków/57663B,267 kopii resetu. Dodane cztery
pola należą do oryginalnego wygaszania tytułu;864 aktualizacje zgodne z referencją.
Końcowy nieruchomy stan14 aktorów main potwierdzono64 kolejnymi aktualizacjami
źródła/896 stanami. Nie dotyczy to nadal brakujących dekoracji menu.
Dokładna walidacja nowej części profilu:22 zasoby strukturalne przyjęte,
43 przypadki brakujących/zmienionych danych odrzucone. GPU:1179648 pikseli
overlay,720896 pikseli warstw i786432 piksele sceny PASS, również pierwsze
przyjęcie rzeczywistego modelu gracza do tego samego renderera po mediach.

Po zatwierdzeniu New Game kod zachowuje tę samą kanoniczną sesję i przed
przejściem rzeczywiście wysyła końcową klatkę dialogu. Przygotowana kontynuacja
ma41 cue i niezależnie sprawdza sześć kart/filmów. Jej brakujące operacje
wykonawcze pozostają jawne; nie ma jeszcze odtwarzania wszystkich filmów
w normalnym flow ani wejścia do grywalnego Veldinu. Pierwsza niepodłączona
operacja to `frontend/exit-and-video-restore`, następna `transition/prepare`.
Źródłowe wymagania wyjścia/audio: `local/forensics/rac-new-game-prepare-barriers-evidence.md`,
`local/forensics/rac-frontend-sound-bank-evidence.md` oraz
`docs/RAC_MOVIE_LIFETIME_V1.md`. Oryginalny bank dźwięków frontendu musi zostać
przyjęty i rzeczywiście zwolniony; brak implementacji dźwięków nie oznacza
pustego oryginalnego banku. Zakończenie filmu wymaga jawnego zatrzymania i
zwolnienia audio, a nie wyłącznie oczekiwania na naturalny koniec całej kolejki.

Najbliższy krok: podłączyć rzeczywiste operacje wyjścia i przygotowania New Game,
w tym przyjęcie i zakończenie oryginalnych dźwięków menu.
Pełna końcowa publikacja przez `scripts/build-portable.ps1` nadal niewykonana.
Poniższe v10/v9/v8 są historią; hashe ich EXE nie opisują bieżących binariów.

Po tym checkpointcie wspólna funkcja `run_prepared_movie` zastąpiła osobną
pętlę intro. Test `v11-movies.log` (`--smoke-stage media-library`) PASS, exit0:
intro363 i wszystkie trzy rzeczywiste filmy616/1348/415 klatek, wszystkie
przechwyty GPU961 punktów z błędem kanału≤1, każde audio jawnie zakończone
i zwolnione, każde GPU zakończone. Przerwanie po100 klatkach pierwszego filmu
oddało jeszcze12 klatek dekodera, następnie rzeczywiste audio/GPU retirement.
Test nie wykonuje menu/kart/całej sekwencji i nie zalicza E2E. Właściwe zatrzymanie
może pozostawić `audio_drained=0`, gdy kolejka nie skończyła się naturalnie;
`audio_retired=1` oznacza sprawdzone Reset→Unprepare→Close. Osobna regresja
waveOut z oryginalnym PCM: stop przed startem,3 aktywne zatrzymania, naturalny
koniec, zachowany licznik próbek i odrzucenie restartu PASS. Po zmianie odtwarzacza
pełne START→menu→dialog→żądanie New Game ponownie PASS (`v11-media-menu.log`).
SHA256 EXE tych dwóch testów:
`1484c5957b75ed6497d5d2f7b2b155d49a6e851229ab46a437af8cf015f93261`.

Trwa następna partia v12: jawne bootstrap/exit/transition-prefix stanu,
źródłowe tablice sprzężeniowego wygaszania, neutralne klipy audio menu i
zwalnianie zasobów sceny po rzeczywistym GPU fence. Nie przygotowano jeszcze
danych v12; nowe pola nie mogą być wymagane od istniejącej paczki v11.

## Poprzedni checkpoint — dane v10 zweryfikowane

Kod profilu ma teraz `0.1.0-native-eight-resource-v10-frontend-environment`.
Dodany siódmy wspólny zasób `frontend/background/geometry` łączy cztery
oryginalne warstwy nieba i teren; normalny runtime ładuje go do tego samego
renderera przed aktorami. Przygotowano wszystkie19 poziomów w
`local/prepared-milestone1-v10`; exact native validation PASS. Manifest1626B,
SHA256`9eaaf95dad80a139aa0dd1fe9b99df6c6f94a036e1c73556ef237aef5ab35a77`.
Shared44193250B, SHA256`3cc4950023cdd81d9eeb9375031e81e933722bd2e4089c0149f5934d1e5736f6`.
Suma samych19 poziomów1408479979B; CLI suma1452673229B obejmuje shared.
Logi`prepare-v10.log` i`validate-v10.log` w`local/forensics/frontend-transition`.
**Normalny runtime v10: cały intro→copyright→1398 aktualizacji otoczenia PASS,
kod procesu0.** `v10-verified.log`:363 decoded frames,364 submissions,
640976 stereo samples/audio_drained1, GPU drained1, fade12,5 aktorów/6990
submissions. Zrzut update100 ma517997 nieczarnych pikseli; identyczny bajtowo
z testem GPU opisanym niżej. Obraz`v10-verified.frontend.png` w tym samym
katalogu. SHA256 zweryfikowanego runtime EXE:
`c0c0cda664d5a94f481b32d5ea39c31ef24aa9939163177224bcf0a9c1334e85`.
Test aplikacji GUI musi utrzymywać rurę (`2>&1 | Out-File`), aby czekać na
rzeczywisty kod procesu i zachować log. Pierwszy start bez tej rury dał tylko
obrazy; dowodem pełnego przebiegu jest powtórzenie`v10-verified`, nie`v10-flow`.

Teren: wszystkie156 rekordów kończą się oryginalnym E i kompletnym strumieniem
GS;10962 wierzchołki,9036 trójkątów,51 tekstur. Poprawka wspólnego executora
zachowuje wspólne znane bity wszystkich możliwych adresów odczytu, dzięki czemu
martwy odczyt wyprzedzający nie zatrzymuje końca listy. Nie zgaduje wartości;
zapisy, sterowanie i XGKICK nadal wymagają znanego adresu. Testy pięciu rodzin
odczytów i niepewnych skutków ubocznych PASS. Niebo:4 warstwy,90 klastrów,
1465 wierzchołków,1472 trójkąty,4 tekstury.

Renderer wykonuje kodowane mieszanie /128, interpolację afiniczną, warstwy
niezależne od translacji kamery i źródłową regułę alpha GE96/RGB_ONLY terenu:
nieudany test zachowuje kolor i test głębokości, lecz nie zapisuje głębokości.
Nowe pole neutralne `alpha_failure` ma domyślne `discard`, więc stare bajty
pozostają bez zmian. GPU:1179648 pikseli overlay +786432 piksele sceny PASS;
granica95/96/97, zapis koloru, blokowanie głębokości i poprzednia bliższa
powierzchnia PASS. Rzeczywisty zrzut z neutralnych zasobów v9 plus odzyskane
niebo/teren: `local/forensics/frontend-transition/neutral/environment-gpu.png`.
Widać otoczenie, aktorów i logo/Press Start. To test renderera, jeszcze nie pełny
normalny przebieg v10. Pełny przebieg później sprawdzono oddzielnie powyżej.
Brakuje TIE/shrub, proceduralnych sprite'ów nieba i mgły.
Źródłowe, zależne od kamery mieszanie zgrubnych punktów terenu też pozostaje
niezakwalifikowane; obecna geometria pochodzi z dotychczasowej ścieżki odzysku.
Nie określać obecnego otoczenia jako kompletnego oryginalnego frontendu.

Renderer dekoduje kolory do zakresu bajtowego przed interpolacją i interpoluje
je afinicznie; polityka perspektywy dotyczy UV. Naprawia to spadanie stałej
alpha128 do127. Dokładny test gradientów przy różnych W, alpha-failure,
przecinających się prostokątów i zdarzenia zakończenia GPU PASS. Kompilator
dowodzi pełnej alpha128 dla44 materiałów/8578 trójkątów terenu. Zrzuty
1280x720 z mieszaniem i po optymalizacji są identyczne, również po ograniczeniu
kopiowania framebuffer do konserwatywnych prostokątów trójkątów:
SHA256 PPM`fbe965818bcc975492b65407942b2617a43b085c91466574f0ad7b4e4a2bab7d`.

Oświetlenie początkowe TIE/shrub:865 oryginalnych instancji,32600 kolorów,
4673 różne RGBA zgodne z wykonaniem instrukcji źródłowych; log
`local/forensics/frontend-transition/lighting-source-20260919.log`. RSQRT ma
wspólny całkowitoliczbowy model SQRT→DIV, potwierdzony12 publicznymi wynikami
autora i testami dokładnych potęg/znaków. To nie nowy fizyczny pomiar PS2.

Neutralny input:1741 porównań z oryginałem i98 resetów PASS. Rzeczywisty
schemat sesji ma343 nazwane bufory,444 widoki,57647 bajtów i267 kopii
resetu; roundtrip trzech zasobów i pełny reset z zachowaniem bieżących wartości
PASS. Sześć zasobów trzech kart/filmów New Game skompilowano z ISO i sprawdzono
roundtrip;62 rastry512x448 zgodne z oryginalnym rysowaniem. Porównanie kolejności
New Game:10 przypadków/70 oryginalnych wywołań PASS. Te zasoby i wejście nadal
nie są podłączone do normalnego menu. Ścieżka braku karty prowadzi przez osobny
dialog3/mode4; nie wolno udawać obecnej karty dla uruchomienia224728.

Aktualny input corpus dodatkowo:56 źródłowych przypadków braku karty/menu,
288 przypadków prezentacji dialogu i17 przypadków wejścia main/4 zakończone
bramki PASS. Czas wejścia12 aktualizacji jest źródłową stałą; faktyczne zasoby
14 aktorów muszą istnieć przed startem bramki. Nowy helper
`rac_frontend_menu_resources` kompiluje pierwsze4 neutralne zasoby main:
14 instancji,13 próbek,20 klatek list i10 rasterów PASS. Dekoracje/dialogi
są dalej integrowane przez agenta; zasoby nie są jeszcze opublikowane w v10.
Przekazanie istniejącego GameSession do pierwszego poziomu zachowuje tę samą
alokację kanonicznych buforów, ich bajty i rewizję; test pierwszego ticka,
nieaktualnej rewizji i odrzucenia podwójnej inicjalizacji PASS.

D3D11 ma rzeczywiste zdarzenie zakończenia wcześniejszych poleceń GPU
(`begin_submission_drain`/`submission_drain_completed`), już używane po intro.
To nie zastępuje wszystkich pozostałych barier New Game. Adapter MPEG ujawnia
osobno koniec wejścia i rzeczywisty drain oraz pozwala zakończyć podawanie
pakietów. Faktyczny pierwszy film New Game:616 klatek w całości; po stop przy100
oddaje jeszcze12 klatek, następnie drain PASS. Sandbox blokuje aktywację MPEG
kodem c004f011; zatwierdzone uruchomienie poza sandboxem PASS. Nie oznacza to
jeszcze pełnego odtwarzania wszystkich trzech filmów z audio w normalnym flow.

Aktualne testy profilu v10, nowych kodeków, palet, TIE i wejścia PASS.
Pełna późniejsza regresja i publikacja przez `build-portable.ps1` pozostają
do wykonania. Najbliższa integracja: dołączyć istniejące neutralne zasoby main,
kanoniczny stan/wejście oraz karty/filmy New Game do wspólnego pakietu i runtime.

## Ostatni pełny checkpoint danych v9 (zastępuje stan v8 opisany niżej)

Przygotowano od nowa wszystkie 19 poziomów z profilem
`0.1.0-native-eight-resource-v9-acc-frontend` w `local/prepared-milestone1-v9`.
Exact native validation PASS. Manifest SHA-256:
`db7ca7895a022a87bec39fe82524c4c69ebe07be9eeee6820ecfc699f863b768`.
Shared: 40253504 B, SHA-256
`838ad93d02c1116fe2bccdd946812fec95d1f4d68de8ba59a89b19987ecd1297`.
19 plików poziomów: 1400537715 B. CLI pole „Level package bytes” obejmuje
również shared; starsze sumy poniżej nie są samą sumą poziomów.

Ten sam runtime wykonuje teraz **całe intro z dźwiękiem → fade12 → oryginalny
copyright → oryginalne logo/Press Start i 1398 aktualizacji pięciu aktorów tła**.
Rzeczywisty hidden smoke v9 exit0: 363 decoded frames, 640976 audio samples,
audio_drained1, 6990 actor submissions. Log `local/forensics/frontend-transition/v9-flow.log`.
Przechwyt GPU `v9-flow.frontend.png` ujawnił jednak **brak statycznego otoczenia**:
tylko Moby actors i overlay. Nie zaliczać tego jako kompletnego frontendu.
Ich mały rozmiar przy dolnej krawędzi jest zgodny z oryginalną projekcją238d90:
Ratchet root(68,410), ship root(332,405) w512x448. Nie powiększać aktorów ani
nie zmieniać kamery dla maskowania braku TIE/shrub/terrain/skybox.
Normalny runtime zapętla ten ekran; **wejście Press Start/menu/New Game nie jest
jeszcze zintegrowane**, pełnego E2E i oryginalnej grywalności Veldinu nie zaliczono.

Shared zawiera sześć neutralnych zasobów: `startup/intro`, `startup/post-intro`,
`frontend/background/actors`, `/animation`, `/timeline`, `frontend/title`.
Runtime nie czyta RAC/PS2. Neutralne kontrakty `SceneTimelineV1`,
`ImagePresentationV1` i `ScreenOverlayV1` są opisane w dokumentach o tych nazwach.
Compilerowe dokumenty: `RAC_FRONTEND_SCENE_COMPILE_V1.md`,
`RAC_FRONTEND_TITLE_V1.md`, `RAC_FRONTEND_OBJECT_V1.md`, `RAC_FRONTEND_LOADING_V1.md`.

Arytmetyka nie jest już w stanie opisanym dla v8 poniżej: wspólny VU executor
wykonuje ordered MADD/MSUB/ACC z osobną znaną/nieznaną flagą overflow ACC i
integer DIV; helper SQRT wykonuje model redundant radix2. Nieznany latch nie
oznacza false. Porównanie z publicznymi danymi autora modelu: 72 MADD/MSUB,
18 grup flag i36 DIV, 0 różnic; to nie fizyczny capture konsoli. Pozostałe
nieobsługiwane instrukcje i ogólne spatial tails nadal wymagają pracy.
Niedyadyczny timed-color jest wykonany: corpus96cases/1879effects/345layouts/
632glyphs i65536half-mix PASS, bez dawnych11pending. List draw192cases/3694quads PASS.
Kamera259sourcecases, bounds projection512 i source menu projection PASS.

Po New Game oryginał odtwarza trzy **rzeczywiste PSS**, przez tę samą źródłową
funkcję co intro: TOC1998/19a0/19a8. To nie zastąpienie realtime cutscenki filmem.
Wszystkie trzy razy pięć kanałów0/2/3/4/5 przeszły kompilację i neutralny roundtrip;
pełny decode obrazu616/1348/415frames PASS. Loading cards/clock/poll/IO gates
mają wykonany source corpus256STQ/432clocks/213draws. Sequencing, pełne menu,
publikacja tych filmów i ich runtime playback pozostają do integracji.
Main menu:14objects/13entryupdates i15original list rasters (pięć języków) PASS;
decor2250b8:512sourcecases PASS. Same listy nie zastępują modeli/dekoracji menu.

Regresja aktualnej partii: **158/158 CTest PASS**, 12.90s,
`local/forensics/frontend-transition/ctest-regression-v9.log`.
Późniejsze nowe cele main compile, decoration i loading raster: build/PE/test PASS.
Ten sam D3D11 compositor: 1179648 dokładnych pikseli i przejście media→textured
static scene→media PASS. Sampling title/STQ ma jawną kwalifikację publicznego
modelu czterobitowego; nie twierdzi się zgodności z fizycznym GS DDA.
CLI/runtime pod `build-portable` są audytowanymi przyrostowymi wynikami CMake;
**pełnej końcowej publikacji v9 przez build-portable.ps1 jeszcze nie wykonano**.
Hashe EXE w historycznym v8 niżej nie opisują bieżących CLI/runtime.

Najbliższy krok: dodać brakujące źródłowe otoczenie do istniejącego RenderScene
compiler i tego samego renderera, zweryfikować obraz, następnie połączyć
już wykonane modele/listy/input menu oraz karty i trzy filmy z przejściem do gry.
Nie przeplanowywać projektu i nie wracać do historycznych pytań.

## Historyczny checkpoint v8 — osiągnięty wówczas przebieg

**Uruchomienie tego samego runtime → cały oryginalny film intro PAL z obrazem
D3D11 i dźwiękiem PCM → koniec intro.** Normalny runtime dalej zgłasza jawnie
brak zintegrowanego oryginalnego frontendu. Intro smoke kończy się kodem 0 dopiero
po końcu obrazu i opróżnieniu dźwięku. Nie ma zastępczego menu ani skoku do gry.
Launcher Play przekazuje teraz tylko prepared-root; `--level` pozostaje jawnym
deweloperskim ładowaniem poziomu. Nie zaliczono ręcznego pełnego Play/E2E.

| Etap | Stan |
| --- | --- |
| Wybrany oryginalny film intro PAL | PASS: pełne odtwarzanie w runtime; nie oznacza kompletnego boot/prelude |
| Oryginalny ekran tytułowy, animowane tło i menu | Odzyskane kolejne komponenty; brak integracji ekranu w runtime |
| New Game | Wykonywane i porównane oryginalne fragmenty input/dialog/reset; brak pełnej ścieżki użytkownika |
| Cutscenki | Neutralne animacje aktora oraz próbki kamery/root i timing; brak pełnego scenicznego odtwarzania/audio/sequencing |
| Veldin | Deweloperski load, package smoke, render i ograniczona interakcja; nie pełna oryginalna grywalność |
| E2E i osobna zgodność z oryginałem | NIEZALICZONE |

Brak zewnętrznej przeszkody wymagającej decyzji użytkownika. Pozostałe braki są
konkretną pracą implementacyjną, nie powodem do zadawania ponownie pytań o zgodę.

## Nowa implementacja

- `MediaClipV1` i compiler `rac_pss`: neutralne pakiety elementary MPEG-2 z
  zachowanym decode order i PTS/DTS, PCM16 stereo. Compiler usuwa PES/PSS oraz
  SShd/SSbd, deinterleaves PS ADPCM z zachowaniem historii kanałów.
- `rac_startup`: oryginalny wybór filmu z region byte ISO sector289+33. Faktyczny
  `P` wybiera TOC1800: LBA44148, 9879556 B. Alternatywne TOC17f8 nie jest drugim
  filmem do odtworzenia. Source movie mode -1 wyłącza skip kontrolerem.
- Ten sam Windows runtime: Media Foundation MPEG-2, format changes i drain,
  waveOut/sample clock, D3D11 i letterbox według oryginalnego logicznego rastra
  512x512 (PAL). Coded image to 512x416; fizycznego stretch/overscan PS2 nie zmierzono.
  Naprawiono przedwczesne pokazanie pierwszej klatki, czekanie na dekodowanie
  następnej przed prezentacją bieżącej i podwójne gamma encoding z shadera sceny.
  Wideo używa istniejącego pasującego shadera `ps_textured`.
- `PreparedGameV2` ma opcjonalny wspólny pakiet, poza listą planet, z rezerwacją
  UINT32_MAX jako shared namespace. Stare manifesty bez feature bitu zachowują
  format. Walidacja obejmuje kolizje ścieżek, hash, tożsamość i transakcję publikacji.
- Profil native: **`0.1.0-native-eight-resource-v8-startup-media`**.
  `shared.orlevel` zawiera `startup/intro`, typ `openrc.media-clip`, schema1.
  Dokładnie zweryfikowany v7 można migrować bez rekompilacji danych poziomów;
  starsze profile nie są uprawnione do tego reuse. Nadal 19 poziomów po 8 zasobów.
- Frontend text callback: timer 1f98c0 wykonuje źródłowe operacje przy unit scale,
  timed-color wykonuje clamp i dokładne fazy dyadyczne. Niedyadyczny DIV/MADD
  pozostaje jawnie pending, podobnie ogólny ACC; brak zastąpienia go MUL+ADD.
- `rac_frontend_input`: rzeczywisty Press Start219e60, action4→dialog1fbc80,
  ready-card arm 1fd3e8, no-save 224728 i reset 209dc0→209ce8→20bd70 z oryginalnym
  szablonem i267 kopiami. Wyniki to uporządkowane źródłowe efekty, nie nowy emulator
  pamięci ani zintegrowane menu. Nieobsługiwane card-save/lifecycle nie są pomijane.
- `rac_scene_animation_compile`: scene header+12 FF nie jest liczbą triggerów.
  Oryginalne ścieżki szkieletowe trafiają do istniejącego ActorAnimationBank/player;
  identity aktora to ordinal, nie sama klasa. Uporządkowane MUL/MUL/ADD dla root.
  Odd-update BNEL 29a670 ma delay slot phase 1 wyłącznie na nieparzystym ticku.
- Oryginalne tło frontendu:15 chunks w WAD, 1398 ticks, threshold 96 także PAL,
  pierwsza próbka 1, reload próbka 0. Osobny rzeczywisty consumer ignoruje camera
  control byte i wymusza projection bits 3f2147ae. Nie utożsamiać go z general
  cutscene samplerem. Zdekodowany katalog i zegar są po stronie kompilatora.

Zachowana wcześniejsza praca: faktycznie współdzielony świat sesji i materializera,
integer ADD/SUB i ordered MUL/MULA, constructor/allocator/sequence/post-step.
Pełne MADD/MSUB/ACC, bounds, spatial tail z live-token binding, oryginalny
movement/camera/AI/weapon/HUD/events/progression nadal nie są domknięte.
Post source/native ma historyczne 1008 cases / 4023 checkpoints i 287 pending spatial
tails; nie jest dowodem pełnej arytmetyki ani fizycznym capture PS2.

## Bieżące dane i dowody

Root v8: `local/prepared-milestone1-v8`.
Manifest SHA-256: `e4ba256d64c17275a2575dc18f84dea71b4ef07ecc45b49579f4a80e66cb1baf`.
Shared SHA-256: `77d5b72608d9d1c02c07338100c91caa2d4fac5f7a839dbae2da784be17f4e35`.
Shared bytes 11693064; manifest 1618 B; suma poziomów 1412230779 B.
**Wszystkie 19 plików poziomów v7/v8 są byte-identical według SHA-256.**
Walidacja exact v8, rzeczywista migracja i ponowne Prepare „already prepared” PASS.
Końcowy reuse dodatkowo odtwarza mały wspólny pakiet z rzeczywistego filmu ISO
i porównuje kompletne kanoniczne bajty przez SHA-256. Usuwa to wykryte pominięcie:
sam zgodny strukturalnie locator/zakres i niezerowy hash nie dowodziły pochodzenia.
Poprawny v8 ponownie dał „already prepared”, exit 0. Negatywna próba zmieniła
offset filmu o 2048 i przeliczyła wszystkie hashe kontenerów; Prepare odrzucił ją
na granicy source movie, exit 3, bez zmiany manifestu. Logi:
`reuse-v8-source-verified.log`, `reuse-mutated-provenance.log`.
V7 zachowane osobno: `local/prepared-milestone1-v7`, manifest
`44ee1d82572e899820de8a3af5441d10ecd30f85bd2c0a2eeba782124c72478f`.

- Końcowy portable Release: **149/149 PASS**, CTest 17,91 s,
  `portable-release-final-20260912.log`; pełna publikacja zakończona kodem 0.
  Wszystkie audyty PE przeszły, brak ostrzeżeń kompilatora i błędów w tym logu.
  Obejmuje także poprawkę sprawdzania źródła filmu podczas reuse.
  Zmieniony skrypt sprawdzono ponownie: `portable-incremental-verified.log`,
  149/149 PASS w 5,18 s, 0 rekompilowanych jednostek, publikacja exit 0.
  SHA-256 wszystkich trzech EXE pozostały identyczne; wcześniejsze runtime
  testy nadal dotyczą dokładnie tych opublikowanych bajtów.
- Intro runtime: 363 decoded frames, 364 submissions (w tym capture), 640976
  stereo sample frames, audio_drained 1, exit 0. Faktyczny framebuffer frame 199:
  961 próbek przeciw pikselom dekodera, max channel error 1/255. PPM/PNG pozostają
  w `local/forensics/startup-media`, nie trafiają do repo.
  Końcowy test opublikowanego EXE: `final-intro.log`, exit 0, ten sam wynik;
  zrzut `final-intro.png`. Pierwszy capture frame149 był czarną klatką samego
  oryginału; frame199 zawiera obraz i nadaje się do sprawdzania koloru.
- Niezależny pełny decode: PAL 363 klatki, alternatywa NTSC 436; ffprobe PAL 363.
  NTSC nie jest osobno przygotowanym normalnym profilem użytkownika.
- V8 package smoke level 0/1 exit 0. Replay Veldin `11091236615096623899`,
  Novalis `1540951510915972271`; takie same jak potwierdzony v7.
- Końcowe hidden graphical smoke opublikowanego runtime dla v8 level 0/1:
  oba exit 0; logi `final-graphical-v8-{0,1}.log` i pliki `-error.log`.
- Frontend text source/native: 96 cases, 1879 effects, 345 layouts, 632 symbolic
  glyph draws; 262 timery, 80 wykonanych timed-colors, 11 nie-dyadycznych obserwacji.
  Dodatkowo 65536 source/native par mixu 0.5; permanentne 589824 bounded color cases.
- Frontend input source/native: 13 action stores, 280 fresh stores, 267 copies;
  5 grup permanentnych. Raw-source dialog 24 cases plus Press Start 5 stores.
  Source trace resetu wykonuje 3681604 instrukcje, oryginalny checksum i tag search.
- Integer glyph `1f6668→1f5800`: 384 przypadki i 7318 quadów z pełnymi bajtami
  pakietów; main-list `21c1b0`: 192 przypadki i 4102 quady, w tym 25 font retries.
  Oba cele przeszły CMake/PE audit i niezależny source/native corpus. Test listy
  skorygował syntetyczne metryki control 8..15 na zero, zgodnie z oryginalnym
  fontem; source width liczy advance nawet dla control, glyph dispatch je pomija.
- Scene actor probe: payload 661/686, 90 frames, 176 sampled poses, 19536 joints.
- Background probe: 15 chunks, 715 frames, 2796 samples, 310356 joints, 30 reloads,
  2 pełne pętle. Próbkowanie neutralnego aktora 0 i source camera/root działa;
  to nie render wszystkich 5 aktorów ani gotowe menu.

Logi tej partii: `local/forensics/startup-media/`.
Fixture input: `local/forensics/rac-frontend-input-source.bin`, SHA
`a4dc372355cbc9f490bfbf36fe90b1b8f05100e3d8ce62bef40311ff41756357`.
Background źródła/noty: `local/forensics/frontend-transition/`.

Publiczne EXE (rozmiar i SHA-256 zgodne z audytowanymi wynikami builda):

| EXE | Bajty | SHA-256 |
| --- | ---: | --- |
| openrc-cli.exe | 4784640 | `1e9569b3049a9818b8fe973642269d70fbaa94aaddc5161a23a4f91fe160877a` |
| openrc-launcher.exe | 4070912 | `f28e86e598598eb878f4ab9b010ef23b7540a9855aac52c4bc231166bebc90b3` |
| openrc-runtime.exe | 3102720 | `fe8b09a5515f2fa7c26cd8995496fdef7fd9eb064df91c6594e56ddb9ae59974` |

## Repozytorium i uruchamianie

Gałąź main, HEAD i lokalny origin/main nadal
`b0aa062b0268df5450969444215ea2665b4d8d46`. Nie odświeżano remote.
Index pusty; liczne wcześniejsze i nowe zmiany pozostają w working tree.
Nie uznawać całego diff względem HEAD za wyłącznie bieżącą sesję.

Źródło ISO:
`local/disc/Ratchet & Clank (Europe) (En,Fr,De,Es,It) (v2.00).iso`, 4214784000 B,
SHA `0f18a6c84cd8d727ec8c21000a236ed5ce2f279cbb8d0682fa9747199ef73260`.
ELF: `local/prepared-stage1/SCES-50916/<ISO-SHA>/files/SCES_509.16`,
SHA `17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b`.

```powershell
.\scripts\build-portable.ps1 -Configuration Release -ParallelJobs 6
& .\build-portable\openrc-cli.exe validate-native-game E:\projekty\OpenRC\local\prepared-milestone1-v8
& .\build-portable\openrc-cli.exe prepared-native-level-smoke E:\projekty\OpenRC\local\prepared-milestone1-v8 0
```

Intro graphical smoke: runtime arguments `--prepared-root <absolute-v8-root>
--smoke-test --smoke-capture <absolute-local.ppm>`. Uruchamiać Start-Process
z `-WindowStyle Hidden`, stderr/stdout do local; oczekiwany czas około 15 s.
Deweloperski graphical smoke dodatkowo `--level 0` lub 1, bez smoke-capture.
Nie uruchamiać równolegle publikacji paczki i jej starych EXE: może zablokować cleanup.

Pozostał ignorowany katalog
`build-portable.previous-ee0dc230805a4e89b7c4e5efea6ea13c` po publikacji wykonanej
podczas starego Prepare. Nowy pakiet został opublikowany, ale usunięcie starego
CLI było zablokowane jego procesem. Późniejszy bezpośredni cleanup został dwukrotnie
odrzucony przez automatyczną kontrolę („blocked by policy”, bez szczegółów).
Nie jest to błąd testów ani uszkodzenie aktywnego pakietu; nie obchodzić odmowy.
Skrypt publikuje teraz poprawnie nawet przy błędzie późniejszego cleanup,
zgłaszając zatrzymany backup oddzielnie. Zachowuje też cache CMake dla budowania
przyrostowego, nadal wymuszając właściwy toolchain, testy i wszystkie audyty.

CMake dla istniejących lokalnych sond: `local/pvar-sweep/build`,
`local/pvar-sweep/inject.cmake`; output pod build-portable, static runtime i PE audit.
Testy i runtime w tym cache są teraz ON. Publikacja usuwa sondy z aktywnej paczki;
ponowne uruchomienie wymaga odbudowania konkretnego celu, nie kopiowania starych EXE.

## Dokumentacja i najbliższy konkretny krok

Czytać `docs/FIRST_PLAYABLE.md`, `docs/MEDIA_CLIP_V1.md`,
`docs/PREPARED_GAME_V2.md`, `docs/RAC_FRONTEND_TEXT_NODE_V1.md`,
`docs/RAC_FRONTEND_LIST_V1.md`,
`docs/RAC_SCENE_ANIMATION_COMPILE_V1.md`. Dla wcześniejszej pracy:
`docs/ENTITY_SCENE_V1.md`, `docs/SESSION_STATE_V1.md`,
`docs/SOURCE_MULTIPLIER_REFERENCE_V1.md`, `docs/SOURCE_NUMERIC_RECOVERY_V1.md`,
`docs/RAC_MOBY_POST_V1.md`. Roadmap jest indeksem zakresów, nie zaliczeniem E2E.

Najwcześniejsza brakująca ścieżka: **oryginalne przejście po filmie do ekranu
frontendu**, obejmujące fade 12 / bootbitmap 201af0, asset init 1eabe8, title/background
1eb458 i dopiero Press Start→219e60→main screen1d4948. Menu nie jest trzema
samymi napisami. Owner219e90 tworzy 14 animowanych obiektów przez 226d50/213d28;
21a610 używa camera 219c08 i projection 238d90, a rzeczywista lista to21c1b0→
integer glyph 1f6668 (odrębny od floating heading 21b298).

Następny krok implementacyjny: domknąć oryginalne admission/animation i projekcję
tych frontend objects, aby przygotować ich pełną neutralną prezentację wraz z
już odzyskanym tłem i listami, a następnie podłączyć ją po intro w tym samym runtime.
Nie publikować częściowego atlasu z wymyślonymi pozycjami jako „gotowego menu”.
