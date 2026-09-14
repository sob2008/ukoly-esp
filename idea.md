Vytvoř kompletní PlatformIO projekt pro ESP32 s názvem "ukoly-esp32" –
lokální webový úkolovník běžící čistě na domácí WiFi, bez cloudu a bez
závislosti na internetu. Přístupný z libovolného zařízení v síti
(telefon, PC, tablet) přes prohlížeč, a navíc instalovatelný na telefonu
jako PWA, aby fungoval i offline mimo domov.

============================================================
ČÁST 1 – FIRMWARE (ESP32)
============================================================

TECH STACK
- framework = arduino, board = esp32dev
- ESPAsyncWebServer + AsyncTCP (zkus ESP32Async/ESPAsyncWebServer a
  ESP32Async/AsyncTCP; pokud PlatformIO nenajde, použij starší jména
  me-no-dev/ESPAsyncWebServer a me-no-dev/AsyncTCP)
- ArduinoJson v7 (JsonDocument, ne DynamicJsonDocument)
- LittleFS jako souborový systém (board_build.filesystem = littlefs)
- ESPmDNS -> zařízení dostupné jako http://ukoly.local
- WiFi + NTP (configTime) pro reálný čas

KONFIGURACE (na začátku main.cpp jako konstanty, snadno upravitelné)
- WIFI_SSID, WIFI_PASSWORD
- MDNS_NAME = "ukoly"
- STATUS_LED pin (default GPIO2, uprav podle konkrétní desky)

NTP ČAS
- Po připojení k WiFi zavolej configTime() s pool.ntp.org a časovou
  zónou pro Prahu (CET/CEST). Počkej krátce na první synchronizaci
  (timeout ~10s); pokud se nepovede, pokračuj dál, ale over to.
- Přidej endpoint GET /api/time -> vrátí aktuální unix timestamp
  zařízení jako JSON {"time": 1234567890}

DATOVÝ MODEL (ukládaný jako JSON pole na LittleFS)
- /categories.json: pole objektů { id (string), name (string),
  updatedAt (unix timestamp), deleted (bool) }
- /tasks.json: pole objektů { id (string), title, description,
  categoryId (string nebo prázdný = nezařazeno), priority
  ("nizka"|"stredni"|"vysoka"), deadline ("YYYY-MM-DD" nebo ""),
  done (bool), updatedAt (unix timestamp), deleted (bool) }
- id generuj na serveru jako náhodný string (např. z millis() + random()
  převedené na hex), protože appka může posílat nová id z klienta
  (uuid) – server je akceptuje tak, jak přijdou, ale při vlastním
  vytváření (pokud by šlo) generuje vlastní
- smazání = nastav deleted=true a updatedAt=now, NEmaž fyzicky ze
  souboru (kvůli synchronizaci s appkou)
- při startu (nebo jednou denně) fyzicky odeber záznamy s deleted=true
  starší než 30 dní

REST API
- GET  /api/categories                 -> pole kategorií (včetně
  smazaných, appka si je sama odfiltruje)
- POST /api/categories                 -> přidá/aktualizuje kategorii
  (tělo: {id, name}) – pokud id existuje, jen aktualizuje name a
  updatedAt; pokud ne, vytvoří novou
- GET  /api/tasks                      -> pole všech úkolů (včetně
  smazaných)
- GET  /api/tasks?category=ID          -> úkoly jen z jedné kategorie
- POST /api/tasks                      -> přidá úkol (tělo: id, title,
  description, categoryId, priority, deadline); pokud kategorie s
  daným categoryId neexistuje a v těle přijde i categoryName, rovnou
  ji založ
- POST /api/tasks/update               -> upraví úkol podle id (tělo:
  id + libovolná měněná pole, nastav updatedAt=now)
- POST /api/tasks/delete?id=X          -> nastaví deleted=true
- GET  /api/time                       -> {"time": unix_timestamp}

Všechny POST endpointy přijímající JSON tělo použij
AsyncCallbackJsonWebHandler. Endpoint na smazání zůstává jako
jednoduchý GET parametr přes HTTP_POST.

STAVOVÁ LED
- trvale svítí = WiFi připojeno a server běží
- pomalu bliká (500 ms) = WiFi spadlo
- rychle bliká donekonečna = selhal mount LittleFS

STATICKÉ SOUBORY
- server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html")
- ověř, že se správně servírují i manifest.json, sw.js a soubory ve
  složce /icons (MIME typy podle přípony by měl zvládnout
  ESPAsyncWebServer automaticky, ale over to)

============================================================
ČÁST 2 – FRONTEND (PWA, ve složce data/, nahrává se na LittleFS)
============================================================

SOUBORY
- index.html – shell appky
- app.js – veškerá logika (vanilla JS, žádný build krok, žádné externí
  CDN závislosti – vše musí fungovat i bez internetu)
- style.css – styly
- manifest.json – název appky "Úkolovník", ikony, start_url "/",
  display: "standalone", tmavé theme_color a background_color
- sw.js – service worker: cache-first pro shell appky (index.html,
  app.js, style.css, manifest.json, ikony), aby appka po prvním
  navštívení fungovala i úplně offline
- icons/icon-192.png a icons/icon-512.png – vygeneruj jednoduché
  placeholder ikony (jedna barva pozadí + jednoduchý tvar/písmeno "Ú")

iOS META TAGY (v <head> index.html, kvůli přidání appky na plochu)
- <meta name="apple-mobile-web-app-capable" content="yes">
- <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
- <link rel="apple-touch-icon" href="/icons/icon-192.png">
- <link rel="manifest" href="/manifest.json">
- <meta name="theme-color" content="...">

NAVIGACE – DVĚ SEKCE + NASTAVENÍ, přepínání čistě v JS (žádný reload)

1) Sekce "Úkoly" (výchozí pohled):
   - rychlý formulář nahoře: název úkolu, priorita (select: nízká/
     střední/vysoká), termín (date input), a výběr kategorie ze
     selectu NEBO "+ nová kategorie…" (odkryje textové pole na název,
     nová kategorie se založí zároveň s úkolem)
   - pod tím seznam VŠECH nesmazaných úkolů, řazený: nedokončené první,
     pak podle priority; každý řádek: checkbox na dokončení, název,
     štítek kategorie, termín (zvýrazněný, pokud je po termínu),
     tlačítko na smazání
   - barevné odlišení priority: tenký barevný levý okraj řádku (vysoká
     = teplá barva, střední = okrová, nízká = tlumená zelená/tyrkysová)
     – ne barevné pilulky

2) Sekce "Kategorie":
   - nahoře textové pole + tlačítko na založení nové kategorie (bez
     nutnosti hned přidávat úkol)
   - seznam kategorií jako klikatelné řádky, u každé počet
     nedokončených úkolů a šipka vpravo
   - klik otevře detail: tlačítko zpět, název kategorie v hlavičce,
     formulář na přidání úkolu SCOPED do této kategorie (bez volby
     kategorie), seznam úkolů jen z této kategorie

3) Sekce "Nastavení":
   - textové pole na adresu ESP32 (výchozí prázdné = appka běží přímo
     na ESP32 a používá relativní URL "/api/...", sync se přeskakuje;
     uživatel ho vyplní jen když appku otevírá jako nainstalovanou PWA
     mimo domácí síť a chce ručně vynutit sync, jinak se sync spouští
     automaticky s relativní URL při běhu přímo na ESP32)
   - řádek "Naposledy synchronizováno: [čas / nikdy]"
   - tlačítko "Synchronizovat teď"

LOKÁLNÍ ÚLOŽIŠTĚ
- localStorage, klíče "ukoly:tasks", "ukoly:categories",
  "ukoly:lastSyncAt", "ukoly:serverUrl"
- appka VŽDY čte a zobrazuje data z localStorage (zdroj pravdy pro UI),
  ať je online (přímo na ESP32) nebo offline (nainstalovaná appka mimo
  domácí síť)
- id nových záznamů generuj přes crypto.randomUUID()
- v UI vždy skrývej záznamy s deleted:true, ale nech je v úložišti

SYNC LOGIKA (app.js)
- funkce attemptSync(baseUrl): fetch(`${baseUrl}/api/time`) s timeoutem
  3s (AbortController); při chybě/timeoutu tiše skonči beze zprávy
  uživateli, appka dál jede z localStorage
- při úspěchu stáhni GET /api/categories a GET /api/tasks (obsahují i
  smazané), projdi podle id a slouč s lokálními daty: kdo má vyšší
  updatedAt vyhrává (novější lokální -> pošli na server přes POST;
  novější serverový -> přepiš lokální záznam v localStorage);
  jednostranné záznamy zkopíruj na druhou stranu
- po úspěšném syncu ulož Date.now() do "ukoly:lastSyncAt" a zobraz v
  Nastavení
- spouštěj sync automaticky: při načtení appky (s baseUrl = relativní
  "" pokud běží přímo na ESP32, jinak hodnota z "ukoly:serverUrl"), a
  při "visibilitychange" na "visible"; navíc ruční tlačítko v Nastavení

STYL
- tmavé, technické, funkční ladění – žádné SaaS-card klišé, žádné
  zbytečné efekty
- konzistentní barvy/mezery/typografie na jednom místě v style.css

============================================================
ČÁST 3 – DOKUMENTACE
============================================================

README.md musí obsahovat:
- úprava WiFi SSID/hesla a případně pinu LED v main.cpp
- postup nahrání: `pio run --target uploadfs` (frontend), pak
  `pio run --target upload` (firmware)
- jak zařízení najít v síti: http://ukoly.local (nebo IP ze sériového
  monitoru)
- jak appku přidat na plochu iPhonu (Safari -> Sdílet -> Přidat na
  plochu) a Androidu (Chrome -> nabídka -> Přidat na plochu), a že
  první instalace musí proběhnout, když je telefon připojený k ESP32
  (ať má service worker co nacachovat)
- jak appku prostě jen otevřít v prohlížeči na PC bez instalace
- poznámka, že sync mezi appkou a ESP32 se řeší jen při odchodu/návratu
  mimo domácí síť; při přímém prohlížení na ESP32 (PC, telefon doma)
  žádný sync neprobíhá, appka pracuje přímo s daty na zařízení

Po vytvoření zkus zkompilovat (`pio run`) a oprav případné chyby.