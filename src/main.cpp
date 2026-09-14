// ukoly-esp32 – lokální webový úkolovník na ESP32
// Firmware: WiFi + NTP + LittleFS + REST API (ESPAsyncWebServer, ArduinoJson v7)

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <time.h>

#include "OtaConfig.h"
#include "OtaState.h"
#include "OtaManager.h"

// ============================================================
// KONFIGURACE – uprav podle vlastní sítě a desky
// ============================================================
const char *WIFI_SSID = "TVOJE_WIFI_SSID";
const char *WIFI_PASSWORD = "TVOJE_WIFI_HESLO";
const char *MDNS_NAME = "ukoly";       // zařízení dostupné jako http://ukoly.local
const int STATUS_LED = 2;              // uprav podle konkrétní desky

// NTP / časová zóna Praha (CET/CEST, automatický letní čas)
const char *NTP_SERVER = "pool.ntp.org";
const char *TZ_PRAGUE = "CET-1CEST,M3.5.0,M10.5.0/3";

// Datové soubory na LittleFS
const char *CATEGORIES_FILE = "/categories.json";
const char *TASKS_FILE = "/tasks.json";

// Smazané záznamy starší než tohle (v sekundách) se při čištění fyzicky odstraní
const long PURGE_AGE_SECONDS = 30L * 24 * 60 * 60; // 30 dní
const unsigned long PURGE_INTERVAL_MS = 24UL * 60 * 60 * 1000; // jednou denně

// ============================================================
// STAV
// ============================================================
AsyncWebServer server(80);

enum LedMode
{
    LED_OFF,
    LED_SOLID,
    LED_SLOW_BLINK,
    LED_FAST_BLINK
};
LedMode ledMode = LED_OFF;
unsigned long ledLastToggle = 0;
bool ledState = false;
unsigned long lastPurgeMs = 0;

// ============================================================
// POMOCNÉ FUNKCE
// ============================================================

// Náhodné id (millis + random -> hex string)
String generateId()
{
    uint32_t a = millis();
    uint32_t b = (uint32_t)esp_random();
    char buf[17];
    snprintf(buf, sizeof(buf), "%08x%08x", (unsigned int)a, (unsigned int)b);
    return String(buf);
}

time_t nowTimestamp()
{
    time_t now;
    time(&now);
    return now;
}

// Načte JSON pole ze souboru; pokud soubor neexistuje nebo je poškozený, vrátí prázdné pole
bool loadJsonArray(const char *path, JsonDocument &doc)
{
    if (!LittleFS.exists(path))
    {
        doc.to<JsonArray>();
        return true;
    }
    File f = LittleFS.open(path, "r");
    if (!f)
    {
        doc.to<JsonArray>();
        return false;
    }
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err)
    {
        Serial.printf("Chyba parsovani %s: %s\n", path, err.c_str());
        doc.clear();
        doc.to<JsonArray>();
    }
    return true;
}

bool saveJsonArray(const char *path, JsonDocument &doc)
{
    File f = LittleFS.open(path, "w");
    if (!f)
    {
        Serial.printf("Chyba: nelze zapsat %s\n", path);
        return false;
    }
    serializeJson(doc, f);
    f.close();
    return true;
}

void sendJson(AsyncWebServerRequest *request, int code, JsonDocument &doc)
{
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    response->setCode(code);
    request->send(response);
}

void sendError(AsyncWebServerRequest *request, int code, const char *message)
{
    JsonDocument doc;
    doc["error"] = message;
    sendJson(request, code, doc);
}

// Fyzicky odstraní záznamy s deleted=true starší než PURGE_AGE_SECONDS
void purgeOldDeleted()
{
    time_t now = nowTimestamp();
    const char *files[] = {CATEGORIES_FILE, TASKS_FILE};
    for (int i = 0; i < 2; i++)
    {
        JsonDocument doc;
        loadJsonArray(files[i], doc);
        JsonArray arr = doc.as<JsonArray>();

        JsonDocument kept;
        JsonArray keptArr = kept.to<JsonArray>();
        for (JsonObject item : arr)
        {
            bool deleted = item["deleted"] | false;
            long updatedAt = item["updatedAt"] | 0L;
            if (deleted && (long)now - updatedAt > PURGE_AGE_SECONDS)
            {
                continue; // starý smazaný záznam -> nekopíruj, tím ho fyzicky odstraníme
            }
            keptArr.add(item);
        }
        saveJsonArray(files[i], kept);
    }
    Serial.println("Cisteni starych smazanych zaznamu dokonceno.");
}

// ============================================================
// STAVOVÁ LED
// ============================================================
void updateLed()
{
    unsigned long nowMs = millis();
    switch (ledMode)
    {
    case LED_OFF:
        digitalWrite(STATUS_LED, LOW);
        break;
    case LED_SOLID:
        digitalWrite(STATUS_LED, HIGH);
        break;
    case LED_SLOW_BLINK:
        if (nowMs - ledLastToggle >= 500)
        {
            ledLastToggle = nowMs;
            ledState = !ledState;
            digitalWrite(STATUS_LED, ledState ? HIGH : LOW);
        }
        break;
    case LED_FAST_BLINK:
        if (nowMs - ledLastToggle >= 100)
        {
            ledLastToggle = nowMs;
            ledState = !ledState;
            digitalWrite(STATUS_LED, ledState ? HIGH : LOW);
        }
        break;
    }
}

// ============================================================
// REST API
// ============================================================
void setupRoutes()
{
    // GET /api/time
    server.on("/api/time", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        JsonDocument doc;
        doc["time"] = (long)nowTimestamp();
        sendJson(request, 200, doc); });

    // GET /api/categories
    server.on("/api/categories", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        JsonDocument doc;
        loadJsonArray(CATEGORIES_FILE, doc);
        sendJson(request, 200, doc); });

    // POST /api/categories  { id?, name }
    auto categoriesHandler = new AsyncCallbackJsonWebHandler(
        "/api/categories",
        [](AsyncWebServerRequest *request, JsonVariant &json)
        {
            JsonObject body = json.as<JsonObject>();
            String name = body["name"] | "";
            if (name.length() == 0)
            {
                sendError(request, 400, "name je povinne");
                return;
            }
            String id = body["id"] | "";

            JsonDocument doc;
            loadJsonArray(CATEGORIES_FILE, doc);
            JsonArray arr = doc.as<JsonArray>();
            time_t now = nowTimestamp();

            JsonObject target;
            bool found = false;
            if (id.length())
            {
                for (JsonObject cat : arr)
                {
                    const char *catId = cat["id"] | "";
                    if (id == catId)
                    {
                        cat["name"] = name;
                        cat["updatedAt"] = (long)now;
                        cat["deleted"] = false;
                        target = cat;
                        found = true;
                        break;
                    }
                }
            }
            if (!found)
            {
                JsonObject newCat = arr.add<JsonObject>();
                newCat["id"] = id.length() ? id : generateId();
                newCat["name"] = name;
                newCat["updatedAt"] = (long)now;
                newCat["deleted"] = false;
                target = newCat;
            }

            saveJsonArray(CATEGORIES_FILE, doc);
            JsonDocument response;
            response.set(target);
            sendJson(request, 200, response);
        });
    server.addHandler(categoriesHandler);

    // GET /api/tasks  a  GET /api/tasks?category=ID
    server.on("/api/tasks", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        JsonDocument doc;
        loadJsonArray(TASKS_FILE, doc);
        JsonArray arr = doc.as<JsonArray>();

        if (request->hasParam("category")) {
            String category = request->getParam("category")->value();
            JsonDocument filtered;
            JsonArray filteredArr = filtered.to<JsonArray>();
            for (JsonObject t : arr) {
                const char* cid = t["categoryId"] | "";
                if (category == cid) {
                    filteredArr.add(t);
                }
            }
            sendJson(request, 200, filtered);
            return;
        }

        sendJson(request, 200, doc); });

    // POST /api/tasks  { id?, title, description, categoryId, categoryName?, priority, deadline }
    auto tasksCreateHandler = new AsyncCallbackJsonWebHandler(
        "/api/tasks",
        [](AsyncWebServerRequest *request, JsonVariant &json)
        {
            JsonObject body = json.as<JsonObject>();
            String title = body["title"] | "";
            if (title.length() == 0)
            {
                sendError(request, 400, "title je povinny");
                return;
            }
            String categoryId = body["categoryId"] | "";
            String categoryName = body["categoryName"] | "";
            time_t now = nowTimestamp();

            // pokud kategorie s danym ID neexistuje a prislo i jmeno, rovnou ji zaloz
            if (categoryId.length() && categoryName.length())
            {
                JsonDocument catDoc;
                loadJsonArray(CATEGORIES_FILE, catDoc);
                JsonArray catArr = catDoc.as<JsonArray>();
                bool exists = false;
                for (JsonObject c : catArr)
                {
                    const char *cid = c["id"] | "";
                    if (categoryId == cid)
                    {
                        exists = true;
                        break;
                    }
                }
                if (!exists)
                {
                    JsonObject newCat = catArr.add<JsonObject>();
                    newCat["id"] = categoryId;
                    newCat["name"] = categoryName;
                    newCat["updatedAt"] = (long)now;
                    newCat["deleted"] = false;
                    saveJsonArray(CATEGORIES_FILE, catDoc);
                }
            }

            JsonDocument doc;
            loadJsonArray(TASKS_FILE, doc);
            JsonArray arr = doc.as<JsonArray>();

            JsonObject newTask = arr.add<JsonObject>();
            String id = body["id"] | "";
            newTask["id"] = id.length() ? id : generateId();
            newTask["title"] = title;
            newTask["description"] = body["description"] | "";
            newTask["categoryId"] = categoryId;
            newTask["priority"] = body["priority"] | "stredni";
            newTask["deadline"] = body["deadline"] | "";
            newTask["done"] = false;
            newTask["updatedAt"] = (long)now;
            newTask["deleted"] = false;

            saveJsonArray(TASKS_FILE, doc);
            JsonDocument response;
            response.set(newTask);
            sendJson(request, 200, response);
        });
    server.addHandler(tasksCreateHandler);

    // POST /api/tasks/update  { id, ...zmenena pole }
    auto tasksUpdateHandler = new AsyncCallbackJsonWebHandler(
        "/api/tasks/update",
        [](AsyncWebServerRequest *request, JsonVariant &json)
        {
            JsonObject body = json.as<JsonObject>();
            String id = body["id"] | "";
            if (id.length() == 0)
            {
                sendError(request, 400, "id je povinne");
                return;
            }

            JsonDocument doc;
            loadJsonArray(TASKS_FILE, doc);
            JsonArray arr = doc.as<JsonArray>();

            for (JsonObject t : arr)
            {
                const char *tid = t["id"] | "";
                if (id == tid)
                {
                    for (JsonPair kv : body)
                    {
                        if (strcmp(kv.key().c_str(), "id") == 0)
                            continue;
                        t[kv.key()] = kv.value();
                    }
                    t["updatedAt"] = (long)nowTimestamp();
                    saveJsonArray(TASKS_FILE, doc);
                    JsonDocument response;
                    response.set(t);
                    sendJson(request, 200, response);
                    return;
                }
            }
            sendError(request, 404, "ukol nenalezen");
        });
    server.addHandler(tasksUpdateHandler);

    // POST /api/tasks/delete?id=X
    server.on("/api/tasks/delete", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        if (!request->hasParam("id")) {
            sendError(request, 400, "id je povinne");
            return;
        }
        String id = request->getParam("id")->value();

        JsonDocument doc;
        loadJsonArray(TASKS_FILE, doc);
        JsonArray arr = doc.as<JsonArray>();

        for (JsonObject t : arr) {
            const char* tid = t["id"] | "";
            if (id == tid) {
                t["deleted"] = true;
                t["updatedAt"] = (long)nowTimestamp();
                saveJsonArray(TASKS_FILE, doc);
                JsonDocument response;
                response["ok"] = true;
                sendJson(request, 200, response);
                return;
            }
        }
        sendError(request, 404, "ukol nenalezen"); });

    server.onNotFound([](AsyncWebServerRequest *request)
                       {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
            return;
        }
        sendError(request, 404, "not found"); });
}

// ============================================================
// SETUP / LOOP
// ============================================================
void setup()
{
    Serial.begin(115200);
    delay(200);
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);

    // --- LittleFS ---
    if (!LittleFS.begin(true))
    {
        Serial.println("KRITICKA CHYBA: LittleFS se nepodarilo pripojit!");
        ledMode = LED_FAST_BLINK;
        while (true)
        {
            updateLed();
            delay(10);
        }
    }
    Serial.println("LittleFS pripojen.");

    // --- OTA: nacist stav a rozhodnout o pripadnem rollbacku z minuleho
    // cyklu - musi byt PRED pripojenim WiFi, viz OtaManager.h ---
    OtaState::begin();
    OtaManager::begin();

    // --- WiFi ---
    ledMode = LED_SLOW_BLINK;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Pripojuji k WiFi");
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 20000)
    {
        updateLed();
        delay(50);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print("WiFi pripojeno, IP: ");
        Serial.println(WiFi.localIP());
        ledMode = LED_SOLID;
    }
    else
    {
        Serial.println("WiFi se nepodarilo pripojit, pokracuji (LED pomalu bliká).");
        ledMode = LED_SLOW_BLINK;
    }

    // --- NTP cas ---
    configTzTime(TZ_PRAGUE, NTP_SERVER);
    Serial.print("Synchronizuji cas");
    struct tm timeinfo;
    unsigned long ntpStart = millis();
    bool timeOk = false;
    while (millis() - ntpStart < 10000)
    {
        if (getLocalTime(&timeinfo, 100))
        {
            timeOk = true;
            break;
        }
        Serial.print(".");
    }
    Serial.println();
    Serial.println(timeOk ? "Cas synchronizovan." : "Varovani: cas se nepodarilo synchronizovat, pokracuji dal.");

    // --- mDNS ---
    if (WiFi.status() == WL_CONNECTED)
    {
        if (MDNS.begin(MDNS_NAME))
        {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("mDNS spusteno: http://%s.local\n", MDNS_NAME);
        }
        else
        {
            Serial.println("Varovani: mDNS se nepodarilo spustit.");
        }
    }

    // --- pocatecni cisteni starych smazanych zaznamu ---
    purgeOldDeleted();
    lastPurgeMs = millis();

    // --- REST API ---
    setupRoutes();

    // --- staticke soubory PWA (index.html, app.js, style.css, manifest.json, sw.js, icons/) ---
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    server.begin();
    Serial.println("HTTP server bezi.");

    // --- OTA: potvrdit, ze firmware funguje (HTTP server skutecne bezi) -
    // jinak by bootloader pri pristim bootu tuto verzi automaticky
    // rollbackoval, viz OtaManager.h ---
    OtaManager::notifyApplicationHealthy();
}

void loop()
{
    OtaManager::handle();

    if (WiFi.status() != WL_CONNECTED)
    {
        ledMode = LED_SLOW_BLINK;
    }
    else
    {
        ledMode = LED_SOLID;
    }
    updateLed();

    unsigned long nowMs = millis();
    if (nowMs - lastPurgeMs >= PURGE_INTERVAL_MS)
    {
        lastPurgeMs = nowMs;
        purgeOldDeleted();
    }
}
