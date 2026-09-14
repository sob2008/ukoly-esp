#include "OtaManager.h"
#include "OtaConfig.h"
#include "OtaState.h"
#include "OtaVersion.h"
#include "Sha256.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <string.h>
#include <ctype.h>

namespace OtaManager {

namespace {

const size_t kChunkSize = 512;
const char* kUserAgent = "ESP32-OTA-" FIRMWARE_TARGET;

StatusCallback g_statusCb = nullptr;
unsigned long g_lastCheckMs = 0;
bool g_firstCheckDone = false;
bool g_healthyNotified = false;

void reportStatus(const String& l1, const String& l2) {
  if (g_statusCb != nullptr) {
    g_statusCb(l1, l2);
  }
}

// Spolecny zacatek HTTP pozadavku - vsechna mista, ktera OTA pouziva (GitHub
// API, firmware.json, firmware.bin.sha256, firmware.bin), musi nasledovat
// presmerovani: "browser_download_url" GitHub Release assetu je VZDY 302
// redirect na CDN (objects.githubusercontent.com), bez ohledu na to, jestli
// jde o firmware.bin nebo maly .json/.sha256 soubor - viz README.md
// v https://github.com/sob2008/esp-ota, "Poucni z praxe" v AGENT_PROMPT.md.
bool httpBeginCommon(HTTPClient& http, WiFiClientSecure& client, const String& url,
                      bool acceptJson) {
  if (!http.begin(client, url)) {
    Serial.println("[OTA] ERROR: HTTP begin failed");
    return false;
  }
  http.addHeader("User-Agent", kUserAgent);
  if (acceptJson) {
    http.addHeader("Accept", "application/vnd.github+json");
  }
  http.setTimeout(OTA_CONNECT_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  return true;
}

// --- GitHub Release JSON (filtrovane, aby se zbytecne velke JSON odpovedi
//     nedeserializovaly cele do RAM) ---

bool fetchJson(WiFiClientSecure& client, HTTPClient& http, const String& url,
               JsonDocument& doc, const JsonDocument* filter) {
  if (!httpBeginCommon(http, client, url, true)) {
    return false;
  }

  int code = http.GET();
  if (code != 200) {
    Serial.print("[OTA] HTTP GET failed, code=");
    Serial.println(code);
    http.end();
    return false;
  }

  DeserializationError err = (filter != nullptr)
      ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
      : deserializeJson(doc, http.getStream());
  http.end();

  if (err) {
    Serial.print("[OTA] JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }
  return true;
}

bool fetchLatestRelease(WiFiClientSecure& client, HTTPClient& http,
                         String& tagName, String& firmwareUrl, size_t& firmwareSize,
                         String& metadataUrl, String& checksumUrl) {
  JsonDocument filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  filter["assets"][0]["size"] = true;

  String url = String("https://api.github.com/repos/") + GITHUB_OWNER + "/" +
               GITHUB_REPOSITORY + "/releases/latest";

  JsonDocument doc;
  if (!fetchJson(client, http, url, doc, &filter)) {
    return false;
  }

  const char* tag = doc["tag_name"] | "";
  if (strlen(tag) == 0) {
    Serial.println("[OTA] Release not found");
    return false;
  }
  tagName = String(tag);
  firmwareUrl = "";
  metadataUrl = "";
  checksumUrl = "";
  firmwareSize = 0;

  if (doc["assets"].is<JsonArray>()) {
    for (JsonObject asset : doc["assets"].as<JsonArray>()) {
      const char* name = asset["name"] | "";
      if (strcmp(name, OTA_ASSET_FIRMWARE) == 0) {
        firmwareUrl = String((const char*)(asset["browser_download_url"] | ""));
        firmwareSize = asset["size"] | 0;
      } else if (strcmp(name, OTA_ASSET_METADATA) == 0) {
        metadataUrl = String((const char*)(asset["browser_download_url"] | ""));
      } else if (strcmp(name, OTA_ASSET_CHECKSUM) == 0) {
        checksumUrl = String((const char*)(asset["browser_download_url"] | ""));
      }
    }
  }
  return true;
}

struct Metadata {
  String target;
  size_t firmwareSize = 0;
  String sha256;
  bool valid = false;
};

Metadata fetchMetadata(WiFiClientSecure& client, HTTPClient& http, const String& url) {
  Metadata m;
  if (url.length() == 0) return m;

  JsonDocument doc;
  if (!fetchJson(client, http, url, doc, nullptr)) {
    return m;
  }
  m.target = String((const char*)(doc["target"] | ""));
  m.firmwareSize = doc["firmware_size"] | 0;
  m.sha256 = String((const char*)(doc["sha256"] | ""));
  m.valid = m.target.length() > 0;
  return m;
}

// Nacte a naparsuje "firmware.bin.sha256" - bezny format je bud samotny
// 64znakovy hex retezec, nebo "hex  nazev_souboru" (format sha256sum).
// Vraci prvnich 64 po sobe jdoucich hex znaku, nebo prazdny retezec.
String fetchChecksumFile(WiFiClientSecure& client, HTTPClient& http, const String& url) {
  if (url.length() == 0) return "";
  if (!httpBeginCommon(http, client, url, false)) return "";

  int code = http.GET();
  if (code != 200) {
    http.end();
    return "";
  }
  String payload = http.getString();
  http.end();

  String hex = "";
  for (size_t i = 0; i < payload.length() && hex.length() < 64; i++) {
    char c = payload[i];
    if (isxdigit(static_cast<unsigned char>(c))) {
      hex += c;
    } else if (hex.length() > 0) {
      break;
    }
  }
  return (hex.length() == 64) ? hex : String("");
}

enum class ApplyResult { Success, Cancelled, Failed };

// Stahne firmware.bin primo do neaktivni OTA partition (Update.begin s
// U_FLASH na ESP32 sam interne vybere esp_ota_get_next_update_partition() -
// tedy tu partition, ktera PRAVE NEBEZI - viz Updater.cpp ve frameworku),
// pocita SHA-256 za behu a na konci overi checksum. Update.end() (ktery na
// ESP32 zapise otadata a oznaci partition jako bootovatelnou pro pristi
// restart) se vola az PO uspesnem overeni - do te doby zustava bezici
// firmware zcela netknuty.
ApplyResult downloadAndApply(WiFiClientSecure& client, HTTPClient& http,
                              const String& url, size_t declaredSize,
                              const String& expectedSha256, const String& versionForPending) {
  size_t ceiling = ESP.getFreeSketchSpace();

  Serial.print("[OTA] Firmware size: ");
  Serial.print(declaredSize);
  Serial.println(" bytes");
  Serial.print("[OTA] OTA partition size: ");
  Serial.print(ceiling);
  Serial.println(" bytes");

  if (declaredSize > 0 && declaredSize > ceiling) {
    Serial.println("[OTA] ERROR: Firmware too large");
    Serial.println("[OTA] Update cancelled");
    return ApplyResult::Cancelled;
  }
  Serial.println("[OTA] Size check: OK");

  if (!httpBeginCommon(http, client, url, false)) {
    return ApplyResult::Failed;
  }

  Serial.println("[OTA] Downloading firmware...");
  int code = http.GET();
  if (code != 200) {
    Serial.print("[OTA] Download failed, HTTP code=");
    Serial.println(code);
    http.end();
    return ApplyResult::Failed;
  }

  int contentLength = http.getSize();
  if (contentLength > 0 && size_t(contentLength) > ceiling) {
    Serial.println("[OTA] ERROR: Firmware too large (Content-Length)");
    Serial.println("[OTA] Update cancelled");
    http.end();
    return ApplyResult::Cancelled;
  }

  // size predany do Update.begin() je zamerne konzervativni HORNI MEZ (cela
  // volna OTA oblast), ne presna ocekavana velikost - Content-Length se
  // nepovazuje za plne duveryhodny. Skutecna presnost se overuje jinak:
  // (1) Update.write() sam odmitne zapsat vic, nez kolik zbyva mista - viz
  // kontrola "written != n" nize, (2) SHA-256 na konci.
  if (!Update.begin(ceiling, U_FLASH)) {
    Serial.print("[OTA] ERROR: Update.begin failed: ");
    Serial.println(Update.errorString());
    http.end();
    return ApplyResult::Failed;
  }

  Sha256 sha;
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[kChunkSize];
  size_t total = 0;
  unsigned long startMs = millis();
  bool sizeExceeded = false;
  bool writeFailed = false;
  bool timedOut = false;
  int lastLoggedPercent = -1;

  while (http.connected() && (contentLength <= 0 || total < size_t(contentLength))) {
    if (millis() - startMs > OTA_DOWNLOAD_TIMEOUT_MS) {
      timedOut = true;
      break;
    }
    int availInt = stream->available();
    if (availInt <= 0) {
      delay(1);
      continue;
    }
    size_t toRead = size_t(availInt) > sizeof(buf) ? sizeof(buf) : size_t(availInt);
    size_t n = stream->readBytes(buf, toRead);
    if (n == 0) {
      continue;
    }

    sha.update(buf, n);

    size_t written = Update.write(buf, n);
    if (written != n) {
      writeFailed = true;
      if (Update.getError() == UPDATE_ERROR_SPACE) {
        sizeExceeded = true;
      }
      break;
    }

    total += n;
    if (contentLength > 0) {
      int percent = int((total * 100UL) / size_t(contentLength));
      if (percent >= lastLoggedPercent + 10) {
        Serial.print("[OTA] Download progress: ");
        Serial.print(percent);
        Serial.println("%");
        lastLoggedPercent = percent;
        char l2[24];
        snprintf(l2, sizeof(l2), "%d %%", percent);
        reportStatus("Aktualizace FW", String(l2));
      }
    }
    delay(0);
  }

  http.end();

  if (timedOut) {
    Serial.println("[OTA] ERROR: Download timed out");
    Update.abort();
    Serial.println("[OTA] Update cancelled");
    return ApplyResult::Failed;
  }
  if (writeFailed) {
    if (sizeExceeded) {
      Serial.println("[OTA] ERROR: Firmware too large (exceeded OTA partition during write)");
    } else {
      Serial.print("[OTA] ERROR: OTA write failed: ");
      Serial.println(Update.errorString());
    }
    Serial.println("[OTA] Update cancelled");
    return ApplyResult::Failed;
  }
  if (contentLength > 0 && total != size_t(contentLength)) {
    Serial.println("[OTA] ERROR: Download incomplete (connection closed early)");
    Update.abort();
    Serial.println("[OTA] Update cancelled");
    return ApplyResult::Failed;
  }
  if (declaredSize > 0 && total != declaredSize) {
    Serial.println("[OTA] ERROR: Downloaded size does not match declared firmware size");
    Update.abort();
    Serial.println("[OTA] Update cancelled");
    return ApplyResult::Failed;
  }

  uint8_t digest[32];
  sha.finish(digest);
  char hex[65];
  Sha256::toHex(digest, hex);

  if (expectedSha256.length() == 0) {
    if (OTA_REQUIRE_CHECKSUM) {
      Serial.println("[OTA] ERROR: No checksum available (required)");
      Update.abort();
      Serial.println("[OTA] Update cancelled");
      return ApplyResult::Cancelled;
    }
    Serial.println("[OTA] WARNING: proceeding without checksum verification (OTA_REQUIRE_CHECKSUM=false)");
  } else {
    Serial.print("[OTA] Verifying SHA-256... ");
    Serial.println(hex);
    if (!Sha256::hexEquals(digest, expectedSha256.c_str())) {
      Serial.println("[OTA] ERROR: Checksum mismatch");
      Update.abort();
      Serial.println("[OTA] Update cancelled");
      return ApplyResult::Failed;
    }
    Serial.println("[OTA] Verification successful");
  }

  reportStatus("Aktualizace FW", "Zapisuji...");
  Serial.println("[OTA] Starting OTA...");

  // Ulozit "pending" stav JESTE PRED Update.end(), ne az po nem - viz
  // OtaState.h. Duvod: jakmile Update.end(true) jednou uspesne dokonci, ma
  // ESP-IDF novou partition nastavenou jako bootovatelnou bez ohledu na to,
  // jestli ESP.restart() skutecne probehne hned (vypadek napajeni). Kdybychom
  // pending_version zapsali az PO Update.end(), existovalo by okno, kdy
  // bootloader pri dalsim bootu novy firmware zvoli, ale nase sledovani o tom
  // nebude vedet.
  OtaState::beginPendingValidation(versionForPending);

  if (!Update.end(true)) {
    Serial.print("[OTA] ERROR: OTA finalize failed: ");
    Serial.println(Update.errorString());
    Serial.println("[OTA] Update cancelled");
    // Update.end() selhal => otadata NENI zmenena, soucasny firmware zustava
    // aktivni. Nas "pending" zapis byl tedy predcasny - vycistit ho hned.
    OtaState::clearStalePending();
    return ApplyResult::Failed;
  }

  Serial.println("[OTA] OTA completed");
  return ApplyResult::Success;
}

void checkAndUpdate() {
  Serial.println("\n[OTA] Checking for updates...");
  Serial.print("[OTA] Current version: ");
  Serial.println(FIRMWARE_VERSION);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(OTA_CONNECT_TIMEOUT_MS);
  HTTPClient http;

  String tagName, firmwareUrl, metadataUrl, checksumUrl;
  size_t firmwareSizeFromRelease = 0;

  if (!fetchLatestRelease(client, http, tagName, firmwareUrl, firmwareSizeFromRelease, metadataUrl, checksumUrl)) {
    Serial.println("[OTA] GitHub unavailable");
    return;
  }

  Serial.print("[OTA] Latest version: ");
  Serial.println(tagName);
  Serial.print("[OTA] Target: ");
  Serial.println(FIRMWARE_TARGET);

  bool cmpOk = false;
  int cmp = compareOtaVersionStrings(tagName.c_str(), FIRMWARE_VERSION, &cmpOk);
  if (!cmpOk) {
    Serial.println("[OTA] ERROR: cannot parse release version tag, skipping");
    return;
  }
  if (cmp <= 0) {
    Serial.println("[OTA] No update available");
    return;
  }

  if (OtaState::isVersionMarkedFailed(tagName)) {
    Serial.println("[OTA] Firmware previously failed");
    return;
  }

  if (firmwareUrl.length() == 0) {
    Serial.println("[OTA] Firmware asset not found");
    return;
  }

  Metadata meta = fetchMetadata(client, http, metadataUrl);
  size_t declaredSize = firmwareSizeFromRelease;
  String expectedSha256 = "";

  if (meta.valid) {
    if (meta.target != FIRMWARE_TARGET) {
      Serial.print("[OTA] ERROR: Incompatible firmware (release target=");
      Serial.print(meta.target);
      Serial.println(")");
      Serial.println("[OTA] Update cancelled");
      return;
    }
    if (meta.firmwareSize > 0) declaredSize = meta.firmwareSize;
    if (meta.sha256.length() > 0) expectedSha256 = meta.sha256;
  } else {
    Serial.println("[OTA] WARNING: firmware.json not found, skipping target verification");
  }

  if (expectedSha256.length() == 0) {
    expectedSha256 = fetchChecksumFile(client, http, checksumUrl);
  }

  if (expectedSha256.length() == 0 && OTA_REQUIRE_CHECKSUM) {
    Serial.println("[OTA] ERROR: No checksum available (required)");
    Serial.println("[OTA] Update cancelled");
    return;
  }

  reportStatus("Aktualizace FW", "Zjistuji...");

  ApplyResult result = downloadAndApply(client, http, firmwareUrl, declaredSize, expectedSha256, tagName);

  if (result == ApplyResult::Success) {
    reportStatus("Aktualizace FW", "Restart...");
    Serial.println("[OTA] Rebooting...");
    delay(200);
    ESP.restart();
  } else {
    reportStatus("Aktualizace FW", "Chyba, pokracuji");
    delay(1000);
  }
}

} // namespace

void setStatusCallback(StatusCallback cb) {
  g_statusCb = cb;
}

void begin() {
  g_healthyNotified = false;
  if (!OTA_ENABLED) return;

  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t otaImgState = ESP_OTA_IMG_UNDEFINED;
  esp_ota_get_state_partition(running, &otaImgState);

  String pending = OtaState::pendingVersionValue();

  if (otaImgState == ESP_OTA_IMG_PENDING_VERIFY) {
    // Toto je jedina prilezitost potvrdit novy firmware jako funkcni - pokud
    // tento boot selze (crash/reset) driv, nez zavolame
    // notifyApplicationHealthy(), bootloader pri PRISTIM bootu SAM (bez
    // naseho zasahu) prepne zpet na predchozi partition a tuto oznaci jako
    // ABORTED - viz esp_ota_ops.h, ESP_OTA_IMG_PENDING_VERIFY, a README.md.
    if (pending.length() > 0 && pending != String(FIRMWARE_VERSION)) {
      // state.json rika, ze se ceka na jinou verzi, nez jaka skutecne bezi
      // jako "pending verify" - typicky rucni USB reflash mezi OTA cykly.
      // Neni co rollbackovat/potvrzovat vuci puvodne planovane verzi.
      Serial.println("[OTA] Stale pending-validation state (firmware version mismatch), clearing");
      OtaState::clearStalePending();
    } else {
      Serial.print("[OTA] Boot pending validation for version ");
      Serial.println(FIRMWARE_VERSION);
    }
  } else if (pending.length() > 0 && pending != String(FIRMWARE_VERSION)) {
    // Bezime jinou (jiz VALID/UNDEFINED, tedy potvrzenou) verzi, nez byla
    // naposledy zapsana jako "pending" - bootloader mezitim sam provedl
    // automaticky rollback, protoze novy firmware se nestihl/nedokazal
    // potvrdit pred dalsim restartem. Zaznamenat jako failed, aby ho pristi
    // kontrola GitHub Releases nezkousela znovu donekonecna.
    Serial.print("[OTA] Detected automatic rollback from v");
    Serial.println(pending);
    OtaState::markUpdateFailed(pending, "boot_rollback");
  }
}

void notifyApplicationHealthy() {
  if (g_healthyNotified) return;
  g_healthyNotified = true;

  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t otaImgState = ESP_OTA_IMG_UNDEFINED;
  esp_ota_get_state_partition(running, &otaImgState);

  if (otaImgState == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
      Serial.println("[OTA] Application healthy - firmware marked as valid");
    } else {
      Serial.printf("[OTA] WARNING: esp_ota_mark_app_valid_cancel_rollback failed: %s\n", esp_err_to_name(err));
    }
  }

  if (OtaState::pendingVersionValue().length() > 0) {
    OtaState::clearPending();
  }
}

void handle() {
  if (!OTA_ENABLED) return;

  unsigned long now = millis();
  if (g_firstCheckDone && (now - g_lastCheckMs) < OTA_CHECK_INTERVAL_MS) {
    return;
  }
  g_lastCheckMs = now;
  g_firstCheckDone = true;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[OTA] Skipping check: WiFi not connected");
    return;
  }

  checkAndUpdate();
}

} // namespace OtaManager
