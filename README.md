# ukoly-esp32

Lokální webový úkolovník běžící přímo na ESP32, čistě na domácí WiFi – bez
cloudu a bez závislosti na internetu. Přístupný z libovolného zařízení v síti
(telefon, PC, tablet) přes prohlížeč, a navíc instalovatelný na telefonu jako
PWA, aby fungoval i offline mimo domov.

## Konfigurace před nahráním

V souboru [`src/main.cpp`](src/main.cpp) na začátku uprav konstanty:

```cpp
const char *WIFI_SSID = "TVOJE_WIFI_SSID";
const char *WIFI_PASSWORD = "TVOJE_WIFI_HESLO";
const char *MDNS_NAME = "ukoly";   // zařízení bude dostupné jako http://ukoly.local
const int STATUS_LED = 2;          // uprav podle konkrétní desky, pokud GPIO2 nesedí
```

## Nahrání na zařízení

Firmware i data (frontend) se nahrávají zvlášť:

```bash
# 1) nahraje obsah data/ (frontend) na LittleFS
pio run --target uploadfs

# 2) nahraje samotný firmware
pio run --target upload
```

Pořadí není striktně nutné, ale doporučuje se nejdřív nahrát souborový systém.

## Jak zařízení najít v síti

- `http://ukoly.local` (díky mDNS – funguje ve většině domácích sítí a na
  většině zařízení; na některých starších Android telefonech mDNS nemusí
  fungovat)
- nebo IP adresa vypsaná do sériové konzole po startu (`pio device monitor`,
  rychlost 115200 baud)

## Stavová LED

- **trvale svítí** – WiFi je připojeno a server běží
- **pomalu bliká (500 ms)** – WiFi spadlo
- **rychle bliká donekonečna** – nepodařilo se připojit LittleFS (kritická chyba)

## Přidání appky na plochu telefonu

Appka je instalovatelná jako PWA. Při **první instalaci musí být telefon
připojený k WiFi a otevřít appku přímo na ESP32** (např. `http://ukoly.local`),
aby si service worker stihl nacachovat shell appky pro pozdější offline provoz.

**iPhone (Safari):**
1. Otevři `http://ukoly.local` v Safari.
2. Ťukni na ikonu Sdílet.
3. Zvol „Přidat na plochu“.

**Android (Chrome):**
1. Otevři `http://ukoly.local` v Chrome.
2. Otevři nabídku (tři tečky).
3. Zvol „Přidat na plochu“ / „Nainstalovat aplikaci“.

## Otevření v prohlížeči na PC bez instalace

Appku lze úplně stejně jen otevřít v libovolném prohlížeči na `http://ukoly.local`
(nebo na IP adrese zařízení) bez jakékoliv instalace – funguje to stejně, jen
bez možnosti offline provozu mimo domácí síť.

## Jak funguje synchronizace

- Appka vždy čte a zobrazuje data z `localStorage` v telefonu/PC – to je zdroj
  pravdy pro UI, ať appka běží online nebo offline.
- Pokud appku otevíráš **přímo na ESP32** (doma, přes prohlížeč nebo
  nainstalovanou appku připojenou k domácí WiFi), žádná ruční synchronizace
  neprobíhá – appka pracuje přímo s daty na zařízení přes relativní URL
  `/api/...`.
- Synchronizace mezi appkou a ESP32 se řeší jen tehdy, když appku (nainstalovanou
  jako PWA) otevřeš **mimo domácí síť** a chceš si vynutit sloučení dat po
  návratu domů. V takovém případě v sekci **Nastavení** vyplň adresu zařízení
  (např. `http://ukoly.local` nebo IP adresu) a použij tlačítko
  „Synchronizovat teď“, případně se sync spustí automaticky při návratu appky
  do popředí.
- Při konfliktu vyhrává záznam s novějším `updatedAt`.

## OTA aktualizace

Zařízení si samo na pozadí kontroluje [GitHub Releases](https://github.com/sob2008/ukoly-esp/releases)
tohoto repozitáře (`src/OtaManager.cpp`, `handle()` v `loop()`, výchozí interval
30 minut – `OTA_CHECK_INTERVAL_MS` v `src/OtaConfig.h`). Když najde novější tag
`vX.Y.Z`, bezpečně stáhne `firmware.bin`, ověří SHA-256 checksum (povinný) a
target (`firmware.json`), a nainstaluje ho do neaktivní OTA partition (`app1`,
zatímco běží `app0`, a naopak) – běžící firmware se nikdy nepřepisuje. Pokud
se nová verze po restartu neprokáže jako funkční, ESP32 bootloader ji sám
zneplatní a vrátí se na předchozí verzi.

Systém je port [`sob2008/esp-ota`](https://github.com/sob2008/esp-ota)
(navrženého a testovaného pro ESP8266) na nativní ESP32 A/B OTA partition –
proto tu chybí `factory-template/` (tovární/provisioning firmware pro
výrobu/expedici kusů zákazníkům) i `OTA_MAX_BOOT_ATTEMPTS`: tenhle projekt je
jedno domácí zařízení s pevně zadrátovaným WiFi heslem, ne produkt s vlastní
provisioning fází, a ESP32 bootloader řeší nepotvrzený boot nativně (přesně
jeden pokus, žádné vlastní počítadlo netřeba – viz komentáře v
`src/OtaManager.cpp`/`src/OtaConfig.h`).

**Vydání nové verze:**
1. Uprav kód, otestuj lokálně (`pio run`).
2. `./scripts/release.ps1 -Version 1.1.0` – nastaví `FIRMWARE_VERSION` v
   `src/OtaConfig.h`, commitne, vytvoří tag `v1.1.0` a po potvrzení ho pushne.
3. Push tagu spustí `.github/workflows/release.yml` – zkompiluje firmware
   (PlatformIO), spočítá SHA-256 a vytvoří GitHub Release s `firmware.bin`,
   `firmware.json` a `firmware.bin.sha256`.
4. Zařízení si aktualizaci najde samo do 30 minut (nebo po restartu).

OTA aktualizuje **jen firmware** (partitions `app0`/`app1`), ne obsah
`data/` na LittleFS – změny frontendu se pořád nahrávají ručně přes
`pio run --target uploadfs`.

**Testy:** `test_host/test_ota_version.cpp` a `test_host/test_sha256.cpp` jsou
host-side testy (běžný `g++`, žádný ESP32 toolchain potřeba):
```bash
g++ -std=c++17 -Wall -Wextra -o test_ota_version.exe test_host/test_ota_version.cpp src/OtaVersion.cpp
g++ -std=c++17 -Wall -Wextra -o test_sha256.exe test_host/test_sha256.cpp src/Sha256.cpp
```
`src/OtaState.cpp`/`src/OtaManager.cpp` (závislé na LittleFS/WiFi/HTTPClient)
lze ověřit jen reálnou kompilací (`pio run`) – co nelze ověřit bez fyzického
zařízení: skutečný stažení+flash cyklus, chování bootloaderu při
nepotvrzeném bootu, a celý rollback cyklus (vyžaduje záměrně vadný Release).
