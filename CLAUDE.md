# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Co to je

`ukoly-esp32` je lokální správce projektů/úkolů/poznámek/nápadů běžící přímo
na ESP32 (PlatformIO, Arduino framework). Žádný cloud, žádná závislost na
internetu — jen domácí WiFi. Skládá se ze dvou nezávislých částí, které se
nahrávají zvlášť:

- **Firmware** (`src/main.cpp`) — WiFi (WiFiManager), NTP čas, dvě oddělené
  LittleFS partition (viz "Architektura ukládání dat" níže), REST API
  (ESPAsyncWebServer), mDNS (`http://ukoly.local`).
- **Frontend** (`data/`) — vanilla JS appka nahraná na LittleFS, kterou
  firmware servíruje jako statické soubory. Žádný build krok, žádné CDN
  závislosti — vše musí fungovat i bez internetu.

Zařízení se neaktualizuje samo (žádné OTA) — firmware i frontend se vždy
nahrávají ručně přes USB.

**Appka NENÍ plně offline-schopná (service worker) a nikdy nebude, dokud
běží na obyčejném HTTP** — `navigator.serviceWorker` je v prohlížečích
dostupné jen v "secure context" (HTTPS nebo `localhost`), `http://ukoly.local`
tuhle podmínku nesplňuje (ověřeno přímo: `window.isSecureContext === false`
na zařízení). `data/sw.js` existuje a registruje se, ale reálně nikdy
neproběhne (`"serviceWorker" in navigator` je `false`), takže appka po
úplném zavření mimo domácí WiFi nenaběhne (studený start vyžaduje síťové
spojení na stažení HTML/JS). V `localStorage` běžící appka (na pozadí)
offline funguje normálně. Neřeš to jako "bug k opravení" bez explicitního
zadání — přidání HTTPS na ESP32 je zásadní zásah (vlastní CA cert,
instalace/trust na každém telefonu), viz historie konverzace/README.md.

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
stačí `uploadfs`, po úpravě jen `src/main.cpp` stačí `upload`. **Uložená
data (úkoly/projekty/poznámky) tím nejsou dotčená** — viz partition
architektura níže. Výjimka: změna `partitions.csv` vyžaduje
`esptool erase_flash` + nahrání všeho od nuly (ztráta dat i WiFi hesla) —
neměň bez vážného důvodu a bez upozornění uživatele.

Testovací framework ani linter v projektu není; ověření je kompilace (`pio run`)
a ruční test v prohlížeči po nahrání na zařízení (ideálně přímo na fyzickém
zařízení přes `pio run --target upload/uploadfs`, ne jen `pio run`).

## Architektura

### Ukládání dat: dvě oddělené LittleFS partition (klíčový koncept)

`partitions.csv` (vlastní tabulka, `board_build.partitions` v
`platformio.ini`) definuje:
- `factory` (app, ~2.1 MB) — firmware. Bez OTA/dual-partition, jen jedna app
  partition (výrazná rezerva oproti dřívějším ~1.25 MB, kdy tu byl ještě
  OTA systém — ten byl odstraněn, viz git historie).
- `spiffs` (~1.6 MB) — statické soubory appky (`data/`). `LittleFS` (globální
  instance) je namountovaná sem, `pio run --target uploadfs` ji CELOU
  přepisuje při každé aktualizaci frontendu.
- `userdata` (128 KB, subtype `0x82` — schválně NE `spiffs`, aby si ji
  PlatformIO uploadfs/buildfs tooling nespletlo s tou výše) — `tasks.json`,
  `categories.json`, `notes.json`. Firmware ji montuje jako **druhou,
  nezávislou** `fs::LittleFSFS` instanci (`UserFs` v `src/main.cpp`,
  `UserFs.begin(true, "/userdata", 10, "userdata")`). `loadJsonArray`/
  `saveJsonArray` v `src/main.cpp` pracují vždy přes `UserFs`, nikdy přes
  `LittleFS`.

**Historie/proč:** původně žily runtime soubory ve stejné partition jako
`data/`, takže každý `uploadfs` (běžná věc při vývoji frontendu) nenávratně
smazal všechna uložená data uživatele — reálně se to stalo a způsobilo
i duplicitní záznamy (viz níže). Oddělení na vlastní partition je trvalá
oprava, ne workaround — nikdy nerušit, nikdy nevracet runtime data do
partition `spiffs`.

### Datový model a synchronizace

Aplikace běží paralelně na dvou místech s vlastní kopií dat:

1. **Na ESP32** — `/tasks.json`, `/categories.json`, `/notes.json` na
   partition `userdata` (pole objektů, viz konstanty `TASKS_FILE`/
   `CATEGORIES_FILE`/`NOTES_FILE` v `src/main.cpp`).
2. **V prohlížeči/appce** — `localStorage` (klíče `ukoly:tasks`,
   `ukoly:categories`, `ukoly:notes` v `data/app.js`).

Frontend **vždy čte a vykresluje data z localStorage** (zdroj pravdy pro UI).
Server (ESP32) do UI nikdy nezasahuje přímo.

Smazání je vždy měkké: nastaví se `deleted: true` a `updatedAt`, fyzicky se
nic nemaže (kvůli sync algoritmu). Fyzické čištění starých smazaných záznamů
(>30 dní, všechny tři soubory) běží na ESP32 při startu a pak jednou denně
(`purgeOldDeleted()` v `src/main.cpp`).

Merge algoritmus (`mergeCollections()` v `data/app.js`, generický přes
libovolnou kolekci s `id`/`updatedAt`) je **last-write-wins**: novější
lokální záznam se pošle na server, novější serverový přepíše lokální;
jednostranné záznamy se zkopírují na druhou stranu. Push endpointy
(`/api/tasks`, `/api/categories`, `/api/notes` — vše "create") **musí**
kontrolovat, jestli záznam s daným `id` už neexistuje, a pokud ano, update
místo slepého appendu (`arr.add<JsonObject>()`) — bez týhle kontroly
opakovaný/zpožděný push stejného id vytvoří duplicitní záznam (přesně tenhle
bug reálně nastal, když se runtime data ještě ztrácela při `uploadfs` a
localStorage v prohlížeči je pak při dalším syncu "obnovilo" vícekrát po
sobě). Kategorie mají navíc pole `type` (`"projekt"` | `"napad"`, chybějící
= `"projekt"` kvůli zpětné kompatibilitě) — `POST /api/categories` typ při
update nepřepisuje, pokud nepřijde v těle.

Když měníš datový model (přidáváš pole), musíš synchronně upravit: schéma
v `src/main.cpp` (příslušné endpointy), odpovídající `create*` funkci v
`data/app.js`, a merge/push logiku, pokud pole ovlivňuje konflikt.

Appka pozná, jestli běží přímo na ESP32, podle toho, jestli je v Nastavení
vyplněná ruční adresa serveru (`ukoly:serverUrl`) — prázdná hodnota =
relativní `/api/...` URL přímo na zařízení a sync se přeskakuje
(`isRunningOnDevice()` v `data/app.js`). Sync se spouští jen když appka běží
jako nainstalovaná appka mimo síť: automaticky při načtení a při
`visibilitychange` na `visible`, nebo ručně tlačítkem v Nastavení.
**Vytvoření/smazání záznamu se propíše na server až při příštím syncu**,
ne okamžitě — při ručním testování přes curl po UI akci je potřeba appku
nejdřív reloadnout (nebo počkat na `visibilitychange`), jinak server ještě
nemá nejnovější stav.

### Firmware (`src/main.cpp`)

Jeden soubor, žádné vlastní hlavičky. Struktura shora dolů: konfigurační
konstanty → `UserFs` instance → pomocné funkce nad JSON soubory
(`loadJsonArray`/`saveJsonArray`, vždy celé pole najednou) → stavová LED
(neblokující, `loop()` přes `millis()`) → `setupRoutes()` registruje
všechny REST endpointy → `setup()` postupně: LittleFS (`spiffs`) mount →
LittleFS (`userdata`, `UserFs`) mount (obojí kritická chyba = rychlé
blikání LED navždy) → WiFi (`WiFiManager::autoConnect()` — zkusí v NVS
uložené údaje z posledního úspěšného připojení, jinak otevře blokující
captive portal `Ukoly_Setup` na `192.168.4.1`, viz `README.md` "První
připojení k WiFi") → NTP (`configTzTime` s časovou zónou Prahy) → mDNS →
počáteční purge → routy → `server.serveStatic("/", LittleFS, "/")` s
`index.html` jako default file → `server.begin()`.

Endpointy: `/api/tasks`, `/api/categories`, `/api/notes` (GET vrací pole
včetně smazaných; volitelný `?category=ID` filtr u tasks/notes), `POST`
create (s dedup-podle-id kontrolou, viz výše), `POST .../update` (merguje
libovolná pole z těla kromě `id`), `POST .../delete?id=X` (měkké smazání,
query param, ne JSON tělo). Všechny POST endpointy s JSON tělem přes
`AsyncCallbackJsonWebHandler` (`<AsyncJson.h>`).

ArduinoJson v7 — používej `JsonDocument` (dynamický), nikdy
`DynamicJsonDocument`/`StaticJsonDocument` (v6 API).

### Frontend (`data/app.js`)

Jeden IIFE soubor bez modulů/importů. Sekce v pořadí: localStorage helpers
→ in-memory `state` (`tasks`, `categories`, `notes`) → navigace mezi views
(`switchView`, řídí i viditelnost FAB přes `updateFabVisibility`) → render
funkce (`renderTasks`, `renderProjects`/`renderIdeas` — sdílí
`categoryRowHtml`, `renderCategoryDetail` — vykresluje OBA taby najednou,
viditelnost řeší CSS, `renderCalendar`, `renderSettings`) → modály
(`openTaskModal`/`openCategoryModal`/`openNoteModal` + jejich `close*`) →
CRUD operace nad `state` (vždy hned zapisují do localStorage přes
`persistTasks`/`persistCategories`/`persistNotes` a pak ručně volají
odpovídající render) → indikátor synchronizace
(`computeSyncStatus`/`renderSyncIndicator`) → sync (`attemptSync`,
`mergeCollections`, `runSync`) → registrace service workera → start
(počáteční render + `runSync`).

Šest views v `index.html`/`style.css`: Úkoly (`#view-tasks`, plochý dashboard
napříč vším), Projekty (`#view-projects`), Nápady (`#view-ideas`, stejná
struktura jako Projekty — `categoryType()`/`visibleProjects()`/
`visibleIdeas()` filtrují stejnou `state.categories` podle pole `type`),
sdílený detail (`#view-category-detail` — segmented control Úkoly/Poznámky,
`currentCategoryOrigin` pamatuje odkud se otevřel detail pro tlačítko Zpět),
Kalendář (`#view-calendar`), Nastavení (`#view-settings`). Přepínání je
čistě přes CSS třídu `active`, bez reloadu. Přidávání úkolů/projektů/
nápadů/poznámek jde jen přes FAB (`#fab-add`, kontextové podle `currentView`
a u detailu i podle aktivního tabu) + modál (bottom-sheet na mobilu,
vycentrované okno na desktopu přes media query `min-width: 860px`) — žádné
inline formuláře ve views.

**Past bug (pozor při dalších modálech/overlayích):** `.modal-overlay` má
`display: flex` v CSS a JS přepíná viditelnost přes DOM atribut `hidden`
(`overlay.hidden = true/false`) — bez explicitního `.modal-overlay[hidden]
{ display: none; }` by `display: flex` z třídy přebilo UA styl `[hidden]`
a modál by zůstal vidět i se staveným atributem. Stejný vzor (třída s
vlastním `display` + `[hidden]` override) dodržuj u každého nového overlaye.

**Past bug (mobilní zoom):** `input[type=text|date], select, textarea`
musí mít `font-size: 16px` explicitně (viz `style.css`) — pod 16px iOS/
Android při fokusu automaticky přiblíží celý viewport, i kdyby to jinde
vypadalo konzistentně s `body`'s 15px.

Mazání projektu/nápadu (`deleteCategory` v `data/app.js` + `POST
/api/categories/delete?id=X`) je měkké — úkoly/poznámky v něm zůstávají,
jen `categoryName()` pro smazanou kategorii vrací "Nezařazeno". Kategorie
(projekty i nápady) se needitují (žádný rename endpoint/UI), jen vytváří
a mažou. Poznámky (`createNote`/`deleteNote`, `POST /api/notes*`) nemají
prioritu/termín/checkbox — jen text + `categoryId` + `updatedAt`, žádost
o "úpravu textu poznámky" by šla přes existující `/api/notes/update`
endpoint (backend to podporuje, frontend zatím jen create/delete).

`sw.js` cachuje shell appky (cache-first) a explicitně ignoruje všechny
`/api/*` požadavky — ale viz sekce "Co to je" výše: na `http://` (ne HTTPS)
se service worker v prohlížeči vůbec nezaregistruje, takže `sw.js` reálně
nikdy neběží. Necachuje soubory paralelně (`caches.addAll`, "vše nebo nic"),
ale postupně (`for...await cache.add()`) — ESP32 zvládá jen omezený počet
současných spojení, hromadný paralelní fetch při instalaci SW dřív občas
selhal a nenacachovalo se nic.
