# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Co to je

`ukoly-esp32` je lokální webový úkolovník běžící přímo na ESP32 (PlatformIO,
Arduino framework). Žádný cloud, žádná závislost na internetu — jen domácí
WiFi. Skládá se ze dvou nezávislých částí, které se nahrávají zvlášť:

- **Firmware** (`src/main.cpp`) — WiFi (WiFiManager), NTP čas, LittleFS
  úložiště, REST API (ESPAsyncWebServer), mDNS (`http://ukoly.local`).
- **Frontend** (`data/`) — vanilla JS PWA nahraná na LittleFS, kterou firmware
  servíruje jako statické soubory. Žádný build krok, žádné CDN závislosti —
  vše musí fungovat i bez internetu.

Zařízení se neaktualizuje samo (žádné OTA) — firmware i frontend se vždy
nahrávají ručně přes USB.

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
v `src/main.cpp`). Kategorie se dají jen vytvářet/přejmenovávat, ne mazat —
ani přes API, ani přes frontend (podle původního zadání).

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
LittleFS mount (kritická chyba = rychlé blikání LED navždy) → WiFi
(`WiFiManager::autoConnect()` — zkusí v NVS uložené údaje z posledního
úspěšného připojení, jinak otevře blokující captive portal `Ukoly_Setup` na
`192.168.4.1`, viz `README.md` "První připojení k WiFi") → NTP
(`configTzTime` s časovou zónou Prahy) → mDNS → počáteční purge → routy →
`server.serveStatic("/", ...)` s `index.html` jako default file →
`server.begin()`.

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
