# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Co to je

`ukoly-esp32` je lokální webový úkolovník běžící přímo na ESP32 (PlatformIO,
Arduino framework). Žádný cloud, žádná závislost na internetu — jen domácí
WiFi. Skládá se z několika nezávislých částí:

- **Firmware** (`src/main.cpp`) — WiFi, NTP čas, LittleFS úložiště, REST API
  (ESPAsyncWebServer), mDNS (`http://ukoly.local`).
- **Frontend** (`data/`) — vanilla JS PWA nahraná na LittleFS, kterou firmware
  servíruje jako statické soubory. Žádný build krok, žádné CDN závislosti —
  vše musí fungovat i bez internetu.
- **OTA** (`src/Ota*.{h,cpp}`) — samoaktualizace firmware z GitHub Releases,
  port [`sob2008/esp-ota`](https://github.com/sob2008/esp-ota) na nativní
  ESP32 A/B partition. Viz sekce "OTA systém" níže a `README.md`.
- **Tovární firmware** (`factory-ukoly-esp32/`) — samostatný PlatformIO
  projekt pro prvotní uvedení zařízení do provozu přes USB + WiFi captive
  portal. Viz sekce "Tovární firmware" níže.

Firmware, frontend a tovární firmware se nahrávají/buildují nezávisle na
sobě.

Kompletní původní zadání je v `idea.md`.

## Příkazy

PlatformIO CLI není v PATH; je nutné volat plnou cestu
`%USERPROFILE%\.platformio\penv\Scripts\platformio.exe` (zkráceně `pio` níže).

```bash
pio run                      # zkompiluje firmware (src/main.cpp)
pio run --target buildfs     # sestaví LittleFS image z data/ (bez nahrání)
pio run --target uploadfs    # nahraje data/ (frontend) na zařízení
pio run --target upload      # nahraje firmware na zařízení
pio device monitor           # sériová konzole, 115200 baud
```

`factory-ukoly-esp32/` je vlastní PlatformIO projekt — stejné příkazy
(`pio run`, `pio run --target upload`, `pio device monitor`) se spouští
z tohoto podadresáře, ne z kořene repozitáře; nemá `--target uploadfs`
(žádný `data/`).

Frontend a firmware se nahrávají a mění nezávisle — po úpravě jen v `data/`
stačí `uploadfs`, po úpravě jen `src/main.cpp` stačí `upload`.

Testovací framework ani linter v projektu není; ověření je kompilace (`pio run`)
a ruční test v prohlížeči po nahrání na zařízení.

## Architektura

### Datový model a synchronizace (klíčový koncept)

Aplikace běží paralelně na dvou místech s vlastní kopií dat:

1. **Na ESP32** — soubory `/categories.json` a `/tasks.json` na LittleFS
   (pole objektů, viz `src/main.cpp` u konstant `CATEGORIES_FILE`/`TASKS_FILE`).
2. **V prohlížeči/PWA** — `localStorage` (klíče `ukoly:tasks`,
   `ukoly:categories` v `data/app.js`).

Frontend **vždy čte a vykresluje data z localStorage** (zdroj pravdy pro UI),
ať appka běží přímo na ESP32, nebo offline jako nainstalovaná PWA mimo domácí
síť. Server (ESP32) do UI nikdy nezasahuje přímo.

Smazání je vždy měkké: nastaví se `deleted: true` a `updatedAt`, fyzicky se
nic nemaže (kvůli sync algoritmu). Fyzické čištění starých smazaných záznamů
(>30 dní) běží na ESP32 při startu a pak jednou denně (`purgeOldDeleted()`
v `src/main.cpp`).

Merge algoritmus (`mergeCollections()` v `data/app.js`) je **last-write-wins**
podle `updatedAt`: novější lokální záznam se pošle na server, novější serverový
přepíše lokální; jednostranné záznamy se zkopírují na druhou stranu. Když měníš
datový model (přidáváš pole na úkol/kategorii), musíš synchronně upravit:
- schéma v `src/main.cpp` (endpointy `/api/tasks*`, `/api/categories*`)
- `createTask`/`createCategory` v `data/app.js`
- merge logiku, pokud pole ovlivňuje konflikt

Appka pozná, jestli běží přímo na ESP32, podle toho, jestli je v Nastavení
vyplněná ruční adresa serveru (`ukoly:serverUrl`) — prázdná hodnota =
relativní `/api/...` URL přímo na zařízení a sync se přeskakuje
(`isRunningOnDevice()` v `data/app.js`). Sync se spouští jen když appka běží
jako nainstalovaná PWA mimo síť: automaticky při načtení a při
`visibilitychange` na `visible`, nebo ručně tlačítkem v Nastavení.

### Firmware (`src/main.cpp`)

Jeden soubor, žádné vlastní hlavičky. Struktura shora dolů:
konfigurační konstanty → pomocné funkce nad JSON soubory (`loadJsonArray`/
`saveJsonArray` pracují vždy s celým polem najednou, žádná stránkovaná
persistence) → stavová LED (neblokující, řízená v `loop()` přes `millis()`) →
`setupRoutes()` registruje všechny REST endpointy → `setup()` postupně:
LittleFS mount (kritická chyba = rychlé blikání LED navždy) → WiFi connect →
NTP (`configTzTime` s časovou zónou Prahy) → mDNS → počáteční purge → routy →
`server.serveStatic("/", ...)` s `index.html` jako default file.

Všechny POST endpointy s JSON tělem (`/api/categories`, `/api/tasks`,
`/api/tasks/update`) jsou registrované přes `AsyncCallbackJsonWebHandler`
(vyžaduje `<AsyncJson.h>`). Mazání (`/api/tasks/delete`) jde přes obyčejný
`HTTP_POST` s query parametrem `id`, ne JSON tělo.

ArduinoJson v7 — používej `JsonDocument` (dynamický), nikdy
`DynamicJsonDocument`/`StaticJsonDocument` (v6 API).

### Frontend (`data/app.js`)

Jeden IIFE soubor bez modulů/importů. Sekce v pořadí: localStorage
helpers → in-memory `state` (`tasks`, `categories`) → navigace mezi views
(`switchView`) → render funkce (`renderTasks`, `renderCategories`,
`renderCategoryDetail`, `renderSettings`) → CRUD operace nad `state` (vždy
hned zapisují do localStorage přes `persistTasks`/`persistCategories` a pak
ručně volají odpovídající render) → sync (`attemptSync`, `mergeCollections`,
`runSync`) → registrace service workera → start (počáteční render + `runSync`).

Tři views v `index.html`/`style.css`: Úkoly (`#view-tasks`), Kategorie
(`#view-categories` seznam → `#view-category-detail` detail), Nastavení
(`#view-settings`). Přepínání je čistě přes CSS třídu `active`, bez reloadu.

`sw.js` cachuje jen shell appky (cache-first) a explicitně ignoruje všechny
`/api/*` požadavky — ty appka řeší sama přes `fetch` s timeoutem, ne přes
service worker.

### OTA systém (`src/Ota*.{h,cpp}`, `src/OtaConfig.h`)

Port [`sob2008/esp-ota`](https://github.com/sob2008/esp-ota) (navrženého pro
ESP8266) na ESP32. `OtaVersion.{h,cpp}` a `Sha256.{h,cpp}` jsou beze změny
(platformově nezávislé, testované `test_host/`). `OtaState`/`OtaManager` jsou
přepsané: ESP32 má nativní A/B OTA partition (`app0`/`app1`, viz
`platformio.ini` → `default.csv`) a nativní rollback na úrovni bootloaderu
(`esp_ota_mark_app_valid_cancel_rollback`, `ESP_OTA_IMG_PENDING_VERIFY`),
takže tu na rozdíl od originálu **není** vlastní záloha firmware
(`candidate.bin`/`last_good.bin`) ani počítadlo pokusů o boot
(`OTA_MAX_BOOT_ATTEMPTS`) — bootloader povolí přesně jeden nepotvrzený boot a
sám se vrátí na předchozí partition, pokud se do dalšího restartu nezavolá
`OtaManager::notifyApplicationHealthy()`. `src/OtaState.cpp` drží už jen
`pending_version`/`last_failed_version` v `/ota/state.json` (LittleFS) —
detaily a zdůvodnění viz komentáře v `OtaManager.cpp::begin()` a
`README.md`, sekce "OTA aktualizace".

Integrace v `src/main.cpp`: `OtaState::begin()` + `OtaManager::begin()` hned
po mountu LittleFS a **před** WiFi; `OtaManager::notifyApplicationHealthy()`
až po `server.begin()` (signál "firmware funguje"); `OtaManager::handle()`
na začátku `loop()`. Pořadí je důležité, neměň ho bez přečtení komentářů v
`OtaManager.h`.

Vydávání verzí: `scripts/release.ps1 -Version X.Y.Z` (nastaví
`FIRMWARE_VERSION` v `src/OtaConfig.h`, commit, tag, push) →
`.github/workflows/release.yml` (spouští se jen na tag `vX.Y.Z`, kompiluje
přes PlatformIO, publikuje `firmware.bin`/`firmware.json`/`firmware.bin.sha256`
jako GitHub Release). Nikdy nespouštět build/release automaticky na běžný
push do `main`.

### Tovární firmware (`factory-ukoly-esp32/`)

Samostatný PlatformIO projekt (vlastní `platformio.ini` + `src/`), ne součást
`src/` výše — PlatformIO/Arduino kompiluje každý projekt/sketch zvlášť,
cross-projektový `#include` není možný, proto má vlastní kopie
`Ota*.{h,cpp}` (identické s `src/`) a vlastní `OtaConfig.h`
(`FIRMWARE_VERSION "0.0.0"`, krátký `OTA_CHECK_INTERVAL_MS` — jinak identická
`FIRMWARE_TARGET`/`GITHUB_OWNER`/`GITHUB_REPOSITORY`, musí zůstat v souladu
s `src/OtaConfig.h`). Nahrazuje `WIFI_SSID`/`WIFI_PASSWORD` konstanty
WiFiManager captive portálem (`tzapu/WiFiManager`) — po připojení rovnou
stáhne a nainstaluje nejnovější GitHub Release stejným OTA klientem a
restartuje se do něj; sám sebe už nikdy znovu nespustí. `!flash/` je
univerzální flash skript z `sob2008/esp-ota` (needituj ho) — očekává Arduino
IDE pojmenování (`*.ino.bin`), takže PlatformIO výstup je před použitím
nutné přejmenovat (viz `factory-ukoly-esp32/README.md`).
