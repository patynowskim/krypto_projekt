# Rozszerzenie protokołu łączności satelitarnej o elementy kryptograficzne

**Technologia bazowa:** ESP32, ESP-NOW, CCSDS Space Packet Protocol (133.0-B-2)
**Warstwa bezpieczeństwa:** model inspirowany CCSDS SDLS (355.0-B-2), AES-256-GCM
**Dokument koncepcyjny** — dyskusja z prowadzącym: 19 listopada 2026
**Prezentacja śródokresowa:** 17 grudnia 2026
**Prezentacja końcowa:** 28 stycznia 2027

| Rola | Osoba |
| --- | --- |
| Lider zespołu, architektura ramek, integracja, dokumentacja | Mikołaj Patynowski |
| Inżynier ds. kryptografii | Ada Lenardt |
| Programista ESP32 / warstwa transmisji | Dawid Jaworski |
| Inżynier ds. testów i symulacji kanału | Oskar Czenczek |

---

## 1. Streszczenie

Projekt dodaje warstwę poufności, integralności i ochrony przed replayem do bezpołączeniowego protokołu ESP-NOW, tak aby ramki przenoszone w paśmie ISM 2,4 GHz mogły pełnić rolę uproszczonego łącza satelitarnego (CubeSat ↔ stacja naziemna / mikrosatelita ↔ mikrosatelita).

Ładunek ESP-NOW nie jest dowolnym buforem bajtów: jest formatowany jako **pakiet CCSDS Space Packet Protocol** (CCSDS 133.0-B-2). Ochronę kryptograficzną nakładamy według modelu **Space Data Link Security (SDLS)** (CCSDS 355.0-B-2): nagłówek bezpieczeństwa (SPI + IV/licznik), szyfrogram pola danych oraz znacznik uwierzytelniający AES-GCM.

ESP-NOW ma wbudowane szyfrowanie CCMP (PMK/LMK). Nie używamy go jako warstwy misyjnej, bo jest specyficzne dla Espressif, nie mapuje się na CCSDS i nie daje jawnego, audytowalnego anti-replay zgodnego z łączem o opóźnieniu i stratach. Własna warstwa AEAD pozwala zmierzyć narzut, porównać go ze standardem kosmicznym i zademonstrować ataki, które CCMP w ESP-NOW nie adresuje w sposób zgodny z CCSDS.

Środowisko demonstracyjne: dwa (lub trzy) ESP32. Kanał satelitarny jest emulowany programowo (opóźnienie, strata pakietów, błędy bitowe) oraz — w miarę możliwości — fizycznie (odległość, zakłócenia ISM).

---

## 2. Problem i motywacja

Łączność CubeSat w paśmie amatorskim/ISM jest często realizowana jako krótkie, bezpołączeniowe ramki o małym MTU. Typowe zagrożenia:

1. **Podsłuch** telemetrii (pozycja, stan podsystemów, dane ładunku).
2. **Fałszowanie telekomend** — wstrzyknięcie ramki resetu, detumble, otwarcia panelu.
3. **Replay** — ponowne nadanie legalnej telekomendy zarejestrowanej wcześniej; przy opóźnieniu orbitalnym naiwny znacznik czasu nie wystarcza.
4. **Modyfikacja w kanale** — przekłamanie bitów wyglądające jak szum, ale skierowane w pole komendy.

CCSDS Space Packet Protocol sam z siebie **nie zapewnia poufności ani autentyczności**. Służy do multipleksowania strumieni (APID), numerowania sekwencyjnego i oddzielenia TM od TC. Bezpieczeństwo w rodzinie CCSDS jest dodawane osobno, przede wszystkim przez SDLS na warstwie łącza danych.

ESP-NOW jest dobrym nośnikiem laboratoryjnym, bo:

- działa w warstwie 2, bez IP, DHCP i asocjacji Wi-Fi (analogia do łącza proximity / ISM),
- jest bezpołączeniowy; używamy ESP-NOW **v2.0** (payload do 1470 B, ESP-IDF ≥ 5.4),
- da się go uruchomić na tanich ESP32, które zespół już ma,
- ma sprzętowy AES, więc AES-GCM jest realistyczny na mikrokontrolerze.

Narzut kryptograficzny 36 B na ramce v2 to 2,4 % (przy pełnym payloadzie), więc zostaje miejsce na housekeeping i większe SDU, nadal z twardym MTU typowym dla łącza CubeSat.

---

## 3. Cele

### 3.1 Cel główny

Zaimplementować i zmierzyć warstwę kryptograficzną dla ESP-NOW, przenoszącą pakiety CCSDS, zapewniającą:

- poufność (szyfrowanie pola danych),
- integralność i uwierzytelnienie (tag AEAD),
- ochronę przed replayem (monotoniczny licznik / nonce + okno odbioru).

### 3.2 Cele szczegółowe

| ID | Cel | Kryterium sukcesu |
| --- | --- | --- |
| C1 | Zgodny z 133.0-B-2 nagłówek pakietu (6 oktetów) + APID TM/TC | Wireshark/hex dump pokazuje poprawne pola; Idle Packet = APID `0x7FF` |
| C2 | AES-256-GCM zgodnie z profilem SDLS Annex E | 12 B IV, 16 B tag, SPI 2 B; deszyfracja tylko przy poprawnym tagu |
| C3 | Anti-replay | Odrzucenie ramki z IV ≤ ostatni zaakceptowany (z oknem na stratę i reordering) |
| C4 | Emulator kanału | Konfigurowalne opóźnienie, P(drop), BER; logi CSV |
| C5 | Metryki | Narzut bajtowy, RTT, PDR, CPU/heap, prąd (jeśli sprzęt pozwoli) |
| C6 | Negatywne testy bezpieczeństwa | Podszywanie, bit-flip, replay, nonce reuse — wszystkie wykryte |

### 3.3 Poza zakresem (świadomie)

- Pełny stos TM/TC/AOS/USLP Transfer Frame (poza zakresem semestru; v2 i tak nie mieści pełnej ramki TM 1115 B + SDLS w każdym wariancie misji).
- Dynamiczna wymiana kluczy w locie (SDLS Extended Procedures). Start: klucze wgrane na urządzenie (pre-shared), z miejscem na późniejszy key wrap.
- Prawdziwe pasmo UHF/S i protokół radioamatorski.
- Formalny dowód bezpieczeństwa GCM — opieramy się na NIST/CCSDS.

---

## 4. Analiza protokołów bazowych

### 4.1 ESP-NOW (warstwa nośna)

ESP-NOW to vendor-specific Action Frame 802.11 (Category 127, OUI Espressif `0x18FE34`). Domyślny bitrate 1 Mb/s.

| Wersja | Max. payload | Uwaga |
| --- | --- | --- |
| v1.0 | **250 B** | Kompatybilność wsteczna, jeden element vendor-specific |
| v2.0 | 1470 B | Wymaga nowszego ESP-IDF (≥ 5.4); v1 nie przyjmie dłuższych ramek w całości |

**Decyzja:** payload **≤ 1470 B** (ESP-NOW v2, `ESP_NOW_MAX_DATA_LEN_V2`). Wymaga ESP-IDF ≥ 5.4 na obu węzłach. v1 nie przyjmie ramek > 250 B w całości, więc mieszany firmware jest wykluczony.

Wbudowane szyfrowanie ESP-NOW:

- CCMP (AES-CCM) z PMK 16 B i per-peer LMK 16 B,
- pole „Random Values” (4 B) w Action Frame — ochrona przed prostym relayingiem ramki 802.11, **nie** jest to misyjny anti-replay CCSDS.

Dlatego CCMP zostawiamy wyłączone albo traktujemy jako obronę PHY/MAC, a bezpieczeństwo misji realizujemy **nad** ESP-NOW, w ładunku.

### 4.2 CCSDS 133.0-B-2 — Space Packet Protocol

PDU = Space Packet:

```
┌──────────────────────────────── Space Packet (7 … 65542 oktety) ────────────────────────────────┐
│ Packet Primary Header (6 B, obowiązkowy) │ Packet Data Field (1 … 65536 B)                       │
└─────────────────────────────────────────────────────────────────────────────────────────────────┘
```

**Packet Primary Header (48 bitów):**

| Pole | Bity | Wartość w projekcie |
| --- | ---: | --- |
| Packet Version Number | 3 | `000` (SPP) |
| Packet Type | 1 | `0` = TM (raport), `1` = TC (telekomenda) |
| Secondary Header Flag | 1 | `1` gdy jest Secondary Header (znacznik czasu) |
| APID | 11 | np. `0x001` TM housekeeping, `0x010` TC, `0x7FF` Idle |
| Sequence Flags | 2 | `11` = unsegmented (cały pakiet w jednej ramce ESP-NOW) |
| Packet Sequence Count | 14 | licznik modulo 16384, **osobny per APID** |
| Packet Data Length | 16 | `długość_pola_danych − 1` |

Packet Sequence Count **nie zastępuje** nonce kryptograficznego: jest 14-bitowy, resetowalny i nie jest związany z kluczem. Służy do detekcji strat/kolejności na poziomie SPP (Packet Loss Indicator). Anti-replay budujemy na IV w Security Header.

**Packet Secondary Header (opcjonalny):** w projekcie 8 B czasu misji (CCSDS Unsegmented Time Code, uproszczony: 32 b sekundy + 16 b subsekundy + 16 b reserved / P-field). Używany jako metadane, nie jako jedyne zabezpieczenie replayu — zegary ESP32 się rozjeżdżają, a kanał satelitarny ma opóźnienie.

### 4.3 CCSDS 355.0-B-2 — SDLS (wzorzec bezpieczeństwa)

SDLS dodaje do ramki łącza:

- **Security Header:** SPI (Security Parameter Index) + IV,
- **Security Trailer:** MAC/tag,

szyfruje wyłącznie pole danych, a uwierzytelnia nagłówek ramki + Security Header + dane (AAD + ciphertext).

**Profil bazowy Annex E (TM/AOS/USLP):** AES-256-GCM, IV 96 bitów, tag 128 bitów, nagłówek bezpieczeństwa 14 B (SPI 2 B + IV 12 B).

To jest nasz profil kryptograficzny. Nie implementujemy pełnych Transfer Frame TM/TC — przenosimy **semantykę SDLS na ładunek ESP-NOW**, bo ESP-NOW pełni tu rolę Space Data Link.

Powiązane dokumenty (literatura, nie do implementacji w Etapie 2):

- CCSDS 352.0-B-2 — *CCSDS Cryptographic Algorithms* (AES-GCM jako algorytm rekomendowany),
- CCSDS 355.1-B-1 — SDLS Extended Procedures (wymiana SA; poza zakresem).

---

## 5. Architektura

### 5.1 Stos protokołów

```
  Aplikacja misji (TM housekeeping / TC komendy)
            │
            ▼
  CCSDS Space Packet Protocol          133.0-B-2
  (Primary Header, APID, Sequence Count)
            │
            ▼
  Warstwa SDLS-lite                    355.0-B-2 Annex E
  AES-256-GCM, SPI, IV/counter, tag
            │
            ▼
  ESP-NOW v2                           L2, connectionless, ≤ 1470 B
            │
            ▼
  802.11 PHY 2,4 GHz                   analog ISM CubeSat / proximity
```

### 5.2 Role węzłów

```
  ┌─────────────────────┐                              ┌─────────────────────┐
  │  ESP32 „CubeSat”    │  ─────────────────────────►  │  ESP32 „GCS”        │
  │  APID TM source     │         ESP-NOW              │  APID TM sink       │
  │  APID TC sink       │  ◄─────────────────────────  │  APID TC source     │
  │  COM3 / UART log    │                              │  UART log           │
  └──────────┬──────────┘                              └──────────┬──────────┘
             │ host PC                                            │ host PC
             ▼                                                    ▼
        metryki CSV, kluczowanie, sterowanie emulatorem kanału
```

Opcjonalnie trzeci ESP32 jako **zakłócacz / obserwator** (sniffer 802.11) do testów podsłuchu i wstrzykiwania.

### 5.3 Architektura oprogramowania (moduły)

```
sat_config.h        ← stałe protokołu, APID, limity, kody błędów (Mikołaj)
sat_frame/          ← seal/open ramki, skleja SPP + SDLS (Mikołaj)
app_tm_tc/          ← generowanie TM, parser TC (Dawid)
ccsds_spp/          ← pack/unpack Primary Header, APID, SEQ (Dawid)
espnow_link/        ← init Wi-Fi, peer MAC, send/recv, callback (Dawid)
cli/                ← konsola UART: mac, peer, role, hk, ping, led, stats (Dawid)
sdls_lite/          ← SPI, IV, AES-GCM, anti-replay window (Ada)
channel_emu/        ← delay, drop, bit-flip przed send (Oskar)
metrics/            ← timestamp, heap, cykl AES, log UART CSV (Oskar)
```

Granica: `espnow_link` widzi tylko bufor 1470 B. Nie zna kluczy. `sdls_lite` nie woła ESP-NOW. Dzięki temu emulator kanału można wpiąć między warstwami bez ruszania kryptografii.

### 5.4 Sprzęt

| Element | Rola | Status |
| --- | --- | --- |
| ESP32-WROOM + CH340 (COM3 na PC lidera) | pierwszy węzeł / CubeSat | wykryty na stacji roboczej |
| Drugi ESP32 DevKit | GCS | do skompletowania |
| Kable USB | zasilanie + logi | — |
| Opcjonalnie INA219 / USB tester | prąd TX/RX/idle | Etap 4 |
| Opcjonalnie trzeci ESP32 | sniffer / attacker | testy bezpieczeństwa |

Każdy węzeł loguje po UART (115200) linie CSV: `t_us, dir, apid, seq, iv, len, status, rssi`.

---

## 6. Format ramki (ładunek ESP-NOW)

Jedna ramka ESP-NOW = jeden niepodzielony Space Packet + otoka SDLS-lite.

```
 Offset   Rozmiar   Pole                         Enc  Auth
 0        6 B       CCSDS Packet Primary Header  nie  tak (AAD)
 6        2 B       SPI                          nie  tak (AAD)
 8        12 B      IV (nonce / frame counter)   nie  tak (AAD)
 20       N B       User Data Field (ciphertext) tak  tak
 20+N     16 B      GCM Authentication Tag       —    —
```

`N = 1470 − 6 − 2 − 12 − 16 = 1434` bajtów maksymalnie na dane użytkownika.

Jeśli Secondary Header (8 B) jest włączony, leży na początku User Data **przed** szyfrowaniem albo — lepiej — jest częścią AAD obok Primary Header. **Rekomendacja:** Secondary Header (czas) w AAD, niezaszyfrowany, żeby GCS mógł odrzucić ramkę po czasie jeszcze przed GCM (oszczędność CPU przy floodzie). Integralność czasu i tak chroni tag.

### 6.1 IV / nonce (12 oktetów)

Zgodne z SDLS: 96-bit IV, unikalne dla pary (klucz, SPI).

```
  0        4 B    SA sequence / epoch   (na start = 0)
  4        8 B    Frame Counter         (monotoniczny, big-endian)
```

Nadawca inkrementuje 64-bitowy licznik **przed** każdym GCM. Odbiorca trzyma `highest_accepted` oraz bitmapę okna (np. 64 ramki) na out-of-order i straty.

**Zakaz:** powtórzenie IV przy tym samym kluczu. Po wyczerpaniu licznika — rekey (w v1: błąd fatalny i zatrzymanie TX; w raporcie opisujemy to jako limit SA).

Packet Sequence Count (14 bit) zostaje w Primary Header niezależnie od IV. SPP nadal umie zgłosić lukę w sekwencji, nawet gdy GCM przeszedł.

### 6.2 SPI

`SPI = 0x0001` — jedyna Security Association w demonstracji:

| Parametr SA | Wartość |
| --- | --- |
| Algorytm | AES-256-GCM |
| Klucz | 32 B, pre-shared, w NVS / flash (nie w repo) |
| IV length | 96 bit |
| Tag | 128 bit |
| Kierunek | osobny licznik TX dla CubeSat→GCS i GCS→CubeSat (dwa IV space’y, ten sam klucz albo dwa klucze — do decyzji 19 XI) |

**Rekomendacja na dyskusję:** dwa klucze kierunkowe (TM key, TC key). Kompromitacja telemetrycznego sniffera nie daje prawa do podrabiania telekomend.

### 6.3 Przykładowe APID

| APID | Typ | Zawartość |
| ---: | --- | --- |
| `0x001` | TM | housekeeping: uptime, RSSI, heap, VCC, licznik TX |
| `0x002` | TM | ping / echo payload |
| `0x010` | TC | komendy: LED, interwał TM, parametry emulatora |
| `0x7FF` | Idle | wypełniacz; Secondary Header Flag = 0; bez szyfrowania albo z szyfrowaniem — **szyfrować**, żeby Idle nie był oracle’em długości |

---

## 7. Warstwa kryptograficzna

### 7.1 Wybór algorytmu

| Kandydat | Zgodność CCSDS | ESP32 | Werdykt |
| --- | --- | --- | --- |
| **AES-256-GCM** | SDLS Annex E, CCSDS 352 | sprzętowy AES, mbedTLS w ESP-IDF | **wybrany** |
| AES-128-GCM | dopuszczalny, słabszy profil | szybszy | wariant testowy narzutu |
| ChaCha20-Poly1305 | IETF IoT, nie baseline CCSDS | tylko software na ESP32 | opcjonalny benchmark porównawczy |
| HMAC-SHA256 + AES-CTR | dwa przebiegi | więcej kodu | nie; AEAD jest krótsze i zgodne z SDLS |
| ESP-NOW CCMP | vendor | wbudowane | nie jako warstwa misyjna |

GCM daje poufność i integralność w jednym przebiegu. Tag 128 bit jest baseline SDLS — nie skracamy go (oszczędność 8 B nie jest warta spadku bezpieczeństwa).

### 7.2 AAD i zakres szyfrowania

Zgodnie z duchem SDLS:

- **AAD (tylko auth):** Primary Header + SPI + IV [+ Secondary Header],
- **Encrypt:** User Data Field,
- **Tag:** 16 B na końcu.

Dzięki temu APID i Sequence Count są czytelne dla routera/logów, ale nie da się ich podmienić bez zepsucia tagu.

### 7.3 Zarządzanie kluczami (v1)

1. Klucz 256 bit generowany offline (`esp_fill_random` na jednym węźle albo host).
2. Wgranie na oba ESP32 przez UART w trybie provisioning albo `nvs_set_blob`.
3. Klucz **nie trafia do Gita**. W repo tylko `secrets.h.example`.
4. Rotacja: zmiana SPI i zerowanie IV; w demonstracji ręczna.

Nie implementujemy PKI ani ECDH w Etapie 3. W raporcie końcowym opisujemy, jak SDLS EP / ECDH na ESP32 mogłyby to zastąpić.

### 7.4 Anti-replay — algorytm odbioru

```
on_recv(spi, iv, ciphertext, tag, aad):
    if spi not in SA_table: DROP (unknown SA)
    if iv.epoch != sa.epoch: DROP
    ctr = iv.counter
    if ctr + WINDOW < sa.highest: DROP          # stare
    if ctr <= sa.highest and bit_set(window, ctr): DROP  # duplikat
    if gcm_decrypt(...) != OK: DROP             # integralność
    accept
    update highest + window bitmap
```

`WINDOW` musi być większe niż maksymalna liczba ramek w locie przy emulowanym opóźnieniu. Start: **64**.

Sam Sequence Count SPP jest dodatkowym sygnałem analitycznym (luka ⇒ strata w kanale), nie bramką bezpieczeństwa.

### 7.5 Co atakujący widzi / czego nie zrobi

| Atak | Skutek oczekiwany |
| --- | --- |
| Sniff 802.11 | widzi nagłówek CCSDS i IV, nie treść TM/TC |
| Bit-flip w ciphertext | tag fail, DROP |
| Podmiana APID w headerze | tag fail (AAD) |
| Replay zarejestrowanej ramki | DROP po oknie IV |
| Truncation | długość / tag fail |
| Nonce reuse (błąd implementacji) | test negatywny — zespół musi wykazać, że TX nigdy nie reuse’uje |

---

## 8. Emulacja kanału satelitarnego

Nie mamy komory antenowej ani orbity. Modelujemy cechy, które psują AEAD i anti-replay:

| Zjawisko | Model | Parametr |
| --- | --- | --- |
| Opóźnienie propagacji / kolejka | `delay_ms ~ Uniform` albo stałe | 0–500 ms (LEO proximity / processing) |
| Strata pakietów | Bernoulliego przed `esp_now_send` | P_drop 0–30 % |
| Błędy bitowe | XOR losowych bitów w ładunku | BER 10⁻⁶ … 10⁻³ |
| Burst errors | k kolejnych ramek drop | burst 1–8 |
| Reordering | kolejka z losowym wyprzedzeniem | związane z delay jitter |

Emulator siedzi **nad** ESP-NOW, **pod** SDLS: psuje ciphertext/AAD tak, jak kanał, a nie omija GCM.

Scenariusze testowe (Oskar):

1. Czysty kanał — baseline latency/PDR.
2. P_drop = 10 % — PDR vs okno anti-replay.
3. BER = 10⁻⁴ — prawie każda ramka z błędem ⇒ tag fail (FER ≈ 1 − (1−BER)^{8·L}).
4. Replay attacker (trzeci węzeł lub pętla hosta).
5. Flood TC zła-SPI / zły-tag — wpływ na CPU GCS.

Wzór narzutu:

```
overhead = (6 + 2 + 12 + 16) / 1470 = 36/1470 ≈ 2,45 %   (przy pełnym payloadzie)
goodput  ≈ PDR × (1434 / T_frame)
```

---

## 9. Metryki i testy

### 9.1 Wydajność

| Metryka | Jak |
| --- | --- |
| Narzut ramki | stały 36 B |
| Czas AES-GCM enc/dec | `esp_timer_get_time()` wokół mbedTLS |
| RTT ping TM/TC | znacznik w Secondary Header albo payload |
| PDR / FER | seq SPP vs IV |
| RSSI | `esp_now_recv_info` |
| Heap min / stack watermark | `esp_get_free_heap_size`, `uxTaskGetStackHighWaterMark` |
| Prąd | INA219 średnia idle / TX burst (opcjonalnie) |

### 9.2 Bezpieczeństwo (checklista)

- [ ] tajny tekst nie pojawia się w sniffie
- [ ] 1-bit flip ⇒ 100 % DROP
- [ ] replay ⇒ 100 % DROP
- [ ] zły SPI ⇒ DROP
- [ ] IV nie wraca do tyłu na TX (asercja)
- [ ] dwa klucze kierunkowe: TM key nie deszyfruje TC (jeśli wybrano ten wariant)

### 9.3 Porównania

1. plaintext SPP-over-ESP-NOW,
2. SPP + AES-256-GCM (docelowy),
3. opcjonalnie ChaCha20-Poly1305,
4. opcjonalnie natywny ESP-NOW CCMP (jako punkt odniesienia, nie CCSDS).

---

## 10. Harmonogram

| Etap | Zakres | Kto | Termin |
| --- | --- | --- | --- |
| **1** | Analiza ESP-NOW, SPP, SDLS; uzgodnienie koncepcji | cały zespół | **19 XI 2026** — dyskusja |
| **2** | Dopiecie formatu ramki, SPI, IV, APID, budżet 1470 B (ESP-NOW v2) | Mikołaj + Ada + Dawid | do końca XI |
| **3** | `ccsds_spp` + `espnow_link` + `sdls_lite` na dwóch ESP32 | Dawid, Ada, integracja Mikołaj | pierwsza połowa XII |
| **4** | Emulator kanału, CSV, testy wydajności i bezpieczeństwa | Oskar + reszta | **17 XII 2026** — demo śródokresowe: ping TM/TC + GCM + replay drop |
| **5** | Analiza wyników, optymalizacja (heap, czas GCM), raport | cały zespół | I 2027 |
| **6** | Prezentacja i live demo | cały zespół | **28 I 2027** |

---

## 11. Podział zadań

**Mikołaj Patynowski** — lider: code review, scalenie PR, kontakt z prowadzącym, raport końcowy, diagram sprzętowy, architektura ramki.

**Ada Lenardt** — kryptografia: mbedTLS AES-GCM, SA/SPI, generowanie IV, okno anti-replay, provisioning kluczy, wektory testowe (NIST GCM), testy negatywne.

**Dawid Jaworski** — ESP32 / transmisja: init ESP-NOW, lista peerów, callback RX, pack/unpack SPP, APID TM/TC, UART log, integracja z CH340/COM.

**Oskar Czenczek** — testy: emulator delay/drop/BER, skrypty hosta (Python: CSV → wykresy), stanowisko, BER/PDR/RTT, (opcjonalnie) pomiar prądu, scenariusz atakującego.

Integracja: nic nie commituje „na gotowo” bez przejścia testu `clean_channel_ping` i `replay_rejected`.

---

## 12. Ryzyka

| Ryzyko | Wpływ | Mitygacja |
| --- | --- | --- |
| Tylko jeden ESP32 na starcie | brak łącza | drugi DevKit jak najszybciej; do tego czasu unit testy GCM/SPP na hostie lub loopback |
| ESP-NOW nie paruje się (kanał Wi-Fi, MAC) | blokada Etapu 3 | stały kanał 1/6/11, peer MAC wpisany na sztywno, ten sam PHY rate |
| Narzut 36 B za duży na TM | mało miejsca na housekeeping | TM packed struct ≤ 32 B; Secondary Header wyłączalny |
| Zegary ESP32 rozjechane | zły anti-replay na timestamp | **licznik, nie zegar**, jako źródło prawdy |
| Nonce reuse przy restarcie | złamanie GCM | IV epoch w NVS; po reboocie epoch++ |
| GCM za wolny na klasycznym ESP32 | zły RTT | zmierzyć; w razie czego AES-128-GCM jako wariant, 256 zostaje w raporcie |
| Zakres „za bardzo CCSDS” vs prowadzący chce tylko ESP-NOW | przerost | SPP 6 B + SDLS 30 B to świadomy, mały podzbiór; Transfer Frame odkładamy |

---

## 13. Struktura repozytorium (aktualna)

Repozytorium: https://github.com/patynowskim/krypto_projekt

```
firmware/
  CMakeLists.txt / platformio.ini / sdkconfig.defaults
  main/
    main.c                  # NVS epoch, init SA/ESP-NOW, task RX + CLI [gotowe]
    sat_config.h            # stałe ramki, APID, kody błędów [gotowe]
    sat_frame.{c,h}         # seal/open (SPP + SDLS + AAD) [gotowe]
    ccsds_spp.{c,h}         # pack/unpack Primary Header [gotowe]
    espnow_link.{c,h}       # ESP-NOW v2, peer MAC, kolejka RX [gotowe]
    app_tm_tc.{c,h}         # role cubesat/gcs, HK, PING, LED, replay [gotowe]
    cli.{c,h}               # CLI po UART 115200 [gotowe]
    sdls_lite.{c,h}         # AES-256-GCM + anti-replay [nagłówek + stub TODO]
    channel_emu.{c,h}       # delay/drop/BER [nagłówek + stub TODO]
    metrics.{c,h}           # liczniki + CSV po UART [nagłówek + stub TODO]
    secrets.example.h       # klucze labowe (kopiować do secrets.h)
host/
  satlink.py                # SppPrimary [gotowe], SecurityAssociation [stub TODO]
  test_protocol.py          # testy SPP [gotowe], testy GCM/replay [skip TODO]
  provision_key.py          # generator kluczy secrets.h [stub TODO]
  plot_metrics.py           # parser CSV + wykresy [stub TODO]
  replay_attack.py          # test replay offline [stub TODO]
Opis v1.pdf                 # opis wstępny projektu
WRITEUP.md                  # ten dokument
README.md
```

---

## 15. Literatura

1. CCSDS 133.0-B-2, *Space Packet Protocol*, Blue Book, June 2020. https://ccsds.org/Pubs/133x0b2e2.pdf
2. CCSDS 355.0-B-2, *Space Data Link Security Protocol*, Blue Book, August 2022. https://ccsds.org/Pubs/355x0b2.pdf
3. CCSDS 352.0-B-2, *CCSDS Cryptographic Algorithms*.
4. Espressif, *ESP-NOW*, ESP-IDF Programming Guide (payload v1 = 250 B, v2 = 1470 B, CCMP/PMK/LMK).
5. NIST SP 800-38D, *Recommendation for Block Cipher Modes of Operation: Galois/Counter Mode (GCM)*.
6. IEEE Std 802.11 — vendor-specific action frames (nośnik ESP-NOW).

