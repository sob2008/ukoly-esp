#pragma once
// Persistentni stav OTA systemu pro ESP32, ulozeny v LittleFS (/ota/state.json).
//
// Na rozdil od puvodni ESP8266 verze (https://github.com/sob2008/esp-ota)
// tu NENI potreba drzet vlastni zalohu firmware (candidate.bin/last_good.bin) -
// ESP32 ma nativni A/B OTA partition (app0/app1, viz platformio.ini ->
// board_build.partitions default.csv) a nativni rollback na urovni
// bootloaderu (esp_ota_mark_app_valid_cancel_rollback /
// ESP_OTA_IMG_PENDING_VERIFY, viz OtaManager.cpp a README.md).
//
// Tento modul tedy drzi jen to, co ESP-IDF samo o sobe nevi:
//   - kterou verzi jsme naposledy instalovali (aby OtaManager po pripadnem
//     automatickem rollbacku poznal, KTERA verze selhala)
//   - kterou verzi jiz drive OTA selhala (ochrana proti nekonecne smycce)
//
// Zapis probiha pres docasny soubor + rename (stejny vzor jako v puvodnim
// balicku), aby vypadek napajeni uprostred zapisu nezanechal poskozeny stav.

#include <Arduino.h>

namespace OtaState {

extern const char* kDir;
extern const char* kStatePath;
extern const char* kStateTmpPath;

// Pripoji LittleFS a nacte stav z /ota/state.json. Bezpecne volat, i kdyz je
// LittleFS uz pripojeny jinde v projektu (LittleFS.begin() na ESP32 detekuje
// jiz pripojeny FS a vrati true beze zmeny). Musi byt zavolano jednou
// v setup(), pred pripojenim WiFi.
bool begin();

// --- Dotazy na aktualni stav ---
String pendingVersionValue();
String lastFailedVersion();

// Vrati true, pokud "version" presne odpovida verzi, ktera jiz drive
// selhala pri validaci po OTA (ochrana proti nekonecne OTA smycce).
bool isVersionMarkedFailed(const String& version);

// --- Prechody stavu ---

// Zavolat tesne PRED Update.end() po uspesnem stazeni+overeni noveho
// firmware (viz OtaManager::downloadAndApply). Poradi je dulezite ze stejneho
// duvodu jako v puvodnim balicku: jakmile Update.end() jednou uspesne
// dokonci, ESP-IDF ma novou partition nastavenou jako bootovatelnou bez
// ohledu na to, jestli ESP.restart() skutecne probehne hned (vypadek
// napajeni) - pending_version proto musi byt na disku driv, nez k tomu muze dojit.
void beginPendingValidation(const String& newVersion);

// Zavolat po uspesnem overeni, ze nove nabootovany firmware funguje
// (OtaManager::notifyApplicationHealthy). Vycisti pending_version.
void clearPending();

// Zavolat, kdyz se pending stav ukaze jako "stale" (state.json rika, ze se
// ceka na validaci verze X, ale bootloader hlasi PENDING_VERIFY pro jinou
// bezici FIRMWARE_VERSION) - typicky rucni USB reflash mezi OTA cykly. Jen
// vycisti pending, bez zaznamu selhani (nedoslo k realnemu OTA cyklu, ktery
// by bylo co hodnotit).
void clearStalePending();

// Zavolat, kdyz OtaManager::begin() detekuje, ze bootloader mezitim sam
// automaticky rollbackoval (bezime jinou, jiz potvrzenou verzi, nez byla
// naposledy zapsana jako pending_version) - nebo kdyz stahovani/zapis
// noveho firmware selze pred dokoncenim. Zaznamena verzi jako "failed" (aby
// se nezkousela znovu donekonecna) a vycisti pending.
void markUpdateFailed(const String& version, const String& reason);

} // namespace OtaState
