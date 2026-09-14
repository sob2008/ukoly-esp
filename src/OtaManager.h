#pragma once
// Jadro OTA systemu (ESP32 port https://github.com/sob2008/esp-ota). Vse
// potrebne pro spravne pouziti:
//
//   setup():
//     OtaState::begin();       // pripojit LittleFS + nacist stav (viz OtaState.h)
//     OtaManager::begin();     // rozhodnout o pripadnem rollbacku z minuleho OTA
//     ... pripojit WiFi, provest prvni cyklus hlavni funkce zarizeni ...
//     OtaManager::notifyApplicationHealthy(); // potvrdit, ze firmware funguje
//
//   loop():
//     OtaManager::handle();    // neblokujici mimo aktivni OTA cyklus; kontroluje
//                               // GitHub Releases jednou za OTA_CHECK_INTERVAL_MS
//
// OtaManager pri aktivnim stahovani/flashovani BLOKUJE hlavni smycku - to je
// vedomy kompromis (jednoduchost > asynchronost), stejne jako v puvodnim
// balicku. Pro tento projekt (domaci ukolovnik, zadne rizeni v realnem case)
// je to v poradku - HTTP server bezi dal, protoze ESPAsyncWebServer obsluhuje
// pozadavky mimo hlavni loop().

#include <Arduino.h>

namespace OtaManager {

// Volitelny callback pro zobrazeni stavu OTA (napr. na displeji) behem
// stahovani/flashovani. Volano prilezitostne (ne kazdy chunk) z prubehu OTA
// cyklu. Tento projekt nema displej, takze zustava nevyuzity (bezpecne, no-op).
typedef void (*StatusCallback)(const String& line1, const String& line2);
void setStatusCallback(StatusCallback cb);

// Zavolat jednou v setup(), po OtaState::begin() a PRED pripojenim WiFi.
// Zjisti (z esp_ota_get_state_partition), jestli bezici firmware ceka na
// potvrzeni (ESP_OTA_IMG_PENDING_VERIFY), a podle toho aktualizuje state.json
// (viz OtaState.h). Samotne rozhodnuti o pripadnem rollbacku uz v tomto
// bode provedl bootloader - viz README.md, sekce "OTA aktualizace".
void begin();

// Zavolat jednou v setup(), jakmile hlavni aplikace prokaze, ze bezi
// (v tomto projektu: az po uspesnem server.begin() - HTTP server skutecne
// obsluhuje pozadavky). Bezpecne volat i kdyz zadne OTA neprobehlo (no-op).
void notifyApplicationHealthy();

// Zavolat v kazdem loop(). Neblokujici mimo aktivni kontrolu/update cyklus.
void handle();

} // namespace OtaManager
