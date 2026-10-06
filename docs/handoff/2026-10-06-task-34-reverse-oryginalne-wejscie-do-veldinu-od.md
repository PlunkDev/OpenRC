# Task #34: Reverse: oryginalne wejście do Veldinu — od entry 2465f8 do pierwszej klatki ze sterowaniem Ratchetem (mapa właścicieli i stan początkowy)

- Date: 2026-10-06
- Kind: reverse
- Agent: claude (claude-opus-5-5)
- Reviewer: claude
- Paths: docs/LEVEL_ENTER_SOURCE_V1.md, local/forensics/level-enter-source

## Summary
Sprawdziłem pracę z F33. Pełne SHA-256 ISO (0f18a6c8…ef73260), ELF z `prepared-stage1` i ELF wyciągniętego w tym zadaniu (17f8a846…b122b) zgadzają się z F33, więc żadna teza mapy się nie zmienia. Fakty F33 o profilu v14 dotyczą pakietu, którego statyczna mapa nie używa. Przy okazji ponownie odczytałem gałąź prologu 246770..2467ac i znalazłem błąd w dokumencie dowodowym: kierunek gałęzi dla poziomu 0 był odwrócony. Poprawny jest taki: przy niezerowym bajcie 0x13d498 wywoływane jest 2666a8, przy zerowym 266670(0). Poprawiłem P14 w dowodzie, a w `docs/LEVEL_ENTER_SOURCE_V1.md` doprecyzowałem wiersz 10 i dopisałem, że wiersze 9–11 należą jeszcze do prologu (koniec w 2468a4, pętla od 2468a8). Commit 64810bb.

## Handoff
## Ta sesja (wznowienie #34, F33)
- **F33 nie podważa mapy.** `Get-FileHash -Algorithm SHA256` dał takie wyniki:
  - `local/ratchet-and-clank.iso`: `0F18A6C8…EF73260`;
  - `prepared-stage1/SCES-50916/0f18a6c8…/files/SCES_509.16` i `local/forensics/level-enter-source/SCES_509.16.elf`: oba `17F8A846…B122B`.

  Wszystko jest identyczne z F33. Fakty o profilu v14 (58 zasobów shared, 19 poziomów po 8 zasobów) nie dotyczą statycznej mapy. W dokumencie dowodowym §0 dopisałem notę z pełnymi hashami.
- **Korekta merytoryczna.** Ponownie odczytałem 246770..2467ac. `s0` ma wartość 1 (ustawiane w 2466e4). Dla poziomu 1 przy `0x13de4b==0` wywoływane jest 2666a8. Dla poziomu 0 przy `0x13d498!=0` też 2666a8, bo w 246790 gałąź nie jest brana i wykonanie przechodzi dalej. W pozostałych przypadkach wywoływane jest 266670(0) w 2467a8. Wcześniej P14 w dowodzie miał odwrócony kierunek dla poziomu 0; teraz jest poprawiony.
- **Docs.** Wiersz 10 tabeli prologu podaje teraz kierunek gałęzi, a dodane zdanie mówi, że wiersze 9–11 to wciąż jednorazowy prolog (koniec w 2468a4, pętla od 2468a8). To odpowiada na nit recenzenta.

## Bez zmian
Kolejność właścicieli, brak fazy nieinteraktywnej na poziomie trybów na Veldinie (0 klatek), stan Ratcheta i kolejność klatki trybu 0 są jak w cce5798/ab94712.

## Pułapki
- Baza to b0aa062. Linki do `RAC_MOBY_POST_V1.md` i komponenty `rac_new_game_flow`, `state_installation` i `rac_moby_post` istnieją tylko na main. Przed scaleniem trzeba zrobić rebase albo scalić z main.
- Pliki w `local/` zapisuj w UTF-8 bez BOM. Sprawdziłem, że po tej sesji plik dowodowy nie ma BOM ani mojibake.
- Ponieważ 2666a8 wywołuje się przy niezerowym 0x13d498, wynik P14 zależy od tego, czy callback klasy 834 (2db278, zapis w 2db35c) wykona się w przebiegu 2657b8. Nadal jest to UNKNOWN.

## Verification (agent)
Zmieniłem tylko dokumentację, więc builda ani ctest nie uruchamiałem; kod jest bez zmian. Get-FileHash SHA256 dla ISO, ELF z prepared-stage1 i ELF z katalogu zadania dał wartości zgodne z F33. Kierunek gałęzi P14 sprawdziłem ponownie w `lvl.asm` (246770..2467ac oraz `li s0, 1` w 2466e4). Plik dowodowy jest w UTF-8 bez BOM i bez sekwencji mojibake. Commit 64810bb; plik dowodowy jest w ignorowanym `local/`.

## Next steps
- Implementacja: oryginalny harmonogram klatki i słowo trybu. Obejmuje dispatch 9 ramion, wybór trybu przez 2901a8 (poziom 0 → tryb 0), licznik klatek w trybie, jedną aktualizację na prezentację PAL z dogonieniem oraz bramkę pauzy po 8 klatkach.
- Implementacja: transakcja wejścia przeplatana per rekord według 2422d8, na komponentach v14 (po rebase na main), z podłączeniem RacMobyPostV1 do właściciela przestrzennego.
- Reverse: sonda 25a6d0 i dane wejściowe instancji klasy 834 na Veldinie. Te ostatnie rozstrzygają, czy w New Game 0x13d498 ma wartość 1 (wtedy 2666a8) i czy startuje automatyczna scenka.
- Implementacja: inicjalizacja bohatera przez 205278/205598 i kolejność podsystemów trybu 0 (moby przed bohaterem). Zależy od #29, #30 i #31.
- Reverse: właściciel doczepienia Clanka oraz role 2666a8/266670 i innych nienazwanych właścicieli prologu i klatki.
