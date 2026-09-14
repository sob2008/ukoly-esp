#include "OtaState.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

namespace OtaState {

const char* kDir = "/ota";
const char* kStatePath = "/ota/state.json";
const char* kStateTmpPath = "/ota/state.json.tmp";

namespace {

struct State {
  String pendingVersion;
  String lastFailedVersion;
  String lastFailedReason;
};

State g_state;

void resetToDefault() {
  g_state = State();
}

bool writeStateToDisk() {
  JsonDocument doc;
  doc["pending_version"] = g_state.pendingVersion;
  doc["last_failed_version"] = g_state.lastFailedVersion;
  doc["last_failed_reason"] = g_state.lastFailedReason;

  File f = LittleFS.open(kStateTmpPath, "w");
  if (!f) {
    Serial.println("[OTA] ERROR: cannot open state tmp file for write");
    return false;
  }
  size_t written = serializeJson(doc, f);
  f.close();
  if (written == 0) {
    Serial.println("[OTA] ERROR: state serialization wrote 0 bytes");
    LittleFS.remove(kStateTmpPath);
    return false;
  }

  // Zapis nejdriv do docasneho souboru a az pak prejmenovat - vypadek
  // napajeni behem serializeJson() tak necha puvodni state.json netknuty.
  // LittleFS.rename() cilovy soubor pokud existuje atomicky prepise sam,
  // takze se NEMAZE predem - kdybychom ho smazali a rename pak selhal,
  // zustali bychom bez jakehokoliv state.json.
  if (!LittleFS.rename(kStateTmpPath, kStatePath)) {
    Serial.println("[OTA] ERROR: state rename failed");
    return false;
  }
  return true;
}

void readStateFromDisk() {
  resetToDefault();
  if (!LittleFS.exists(kStatePath)) {
    return; // prvni spusteni - vychozi cisty stav je spravne chovani
  }
  File f = LittleFS.open(kStatePath, "r");
  if (!f) {
    Serial.println("[OTA] WARNING: state.json exists but cannot be opened, using defaults");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.print("[OTA] WARNING: state.json corrupt (");
    Serial.print(err.c_str());
    Serial.println("), using defaults");
    resetToDefault();
    return;
  }
  g_state.pendingVersion = String((const char*)(doc["pending_version"] | ""));
  g_state.lastFailedVersion = String((const char*)(doc["last_failed_version"] | ""));
  g_state.lastFailedReason = String((const char*)(doc["last_failed_reason"] | ""));
}

} // namespace

bool begin() {
  // LittleFS.begin() sama o sobe naformatuje FS pri prvnim/poskozenem
  // pripojeni (autoFormat je vychozi true) a je bezpecne ji volat opakovane -
  // pokud uz je FS pripojeny (typicky main.cpp jej pripoji jako prvni krok
  // v setup()), jen to zaloguje varovani a vrati true beze zmeny.
  if (!LittleFS.begin()) {
    Serial.println("[OTA] ERROR: LittleFS unavailable, OTA state persistence disabled");
    resetToDefault();
    return false;
  }
  if (!LittleFS.exists(kDir)) {
    LittleFS.mkdir(kDir);
  }
  readStateFromDisk();
  return true;
}

String pendingVersionValue() { return g_state.pendingVersion; }
String lastFailedVersion() { return g_state.lastFailedVersion; }

bool isVersionMarkedFailed(const String& version) {
  return g_state.lastFailedVersion.length() > 0 && g_state.lastFailedVersion == version;
}

void beginPendingValidation(const String& newVersion) {
  g_state.pendingVersion = newVersion;
  writeStateToDisk();
}

void clearPending() {
  g_state.pendingVersion = "";
  writeStateToDisk();
}

void clearStalePending() {
  g_state.pendingVersion = "";
  writeStateToDisk();
}

void markUpdateFailed(const String& version, const String& reason) {
  g_state.lastFailedVersion = version;
  g_state.lastFailedReason = reason;
  g_state.pendingVersion = "";
  writeStateToDisk();

  Serial.print("[OTA] Firmware marked as failed: ");
  Serial.print(version);
  Serial.print(" (");
  Serial.print(reason);
  Serial.println(")");
}

} // namespace OtaState
