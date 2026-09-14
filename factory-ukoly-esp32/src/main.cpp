// Tovarni (provisioning) firmware pro ukoly-esp32.
//
// Nahravat POUZE pres USB na nove/vracene kusy, MISTO ostreho firmware
// (../src). Jediny ucel: pripojit zarizeni k domaci WiFi (pres captive
// portal WiFiManager) a hned poté si samo stahnout a nainstalovat
// nejnovejsi ostry firmware z GitHub Releases - pouziva presne stejny OTA
// klient (OtaManager/OtaState/OtaVersion/Sha256) jako ostry firmware, jen
// zkopirovany do teto slozky (PlatformIO/Arduino kompiluje kazdy projekt/
// sketch zvlast, cross-projektovy #include neni mozny).
//
// Po uspesne instalaci se zarizeni samo restartuje a dal uz bezi jako
// ostry firmware - tento firmware se tim prepise a jiz nikdy nebezi znovu
// (dokud by nekdo rucne nenahral factory firmware pres USB podruhe, napr.
// pri reklamaci/resetu).
//
// Adaptovano z https://github.com/sob2008/esp-ota (factory-template/factory-sw.ino).
// POZOR: ostry firmware (../src/main.cpp) pouziva pevne zadratovane
// WIFI_SSID/WIFI_PASSWORD (viz jeho zdrojak), NE WiFiManager - domaci WiFi,
// kterou tady zadas do portalu, je tedy jen pro TOTO tovarni stahovani.
// Aby se po restartu ostry firmware pripojil, musi mit build (a tedy i
// GitHub Release), ktery si tovarni firmware stahne, spravne nastavene
// WIFI_SSID/WIFI_PASSWORD pro tvoji sit uz pri kompilaci.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "OtaConfig.h"
#include "OtaState.h"
#include "OtaManager.h"

// Nazev WiFi site, kterou zarizeni docasne vytvori pro prvni nastaveni
// (pripojis se na ni telefonem/PC a vybereš domaci WiFi).
static const char *AP_NAME = "Ukoly_Provisioning";

// Vychozi implementace jen loguje na Serial - tento projekt nema displej.
void otaStatusCallback(const String &line1, const String &line2)
{
    Serial.print("[OTA-STATUS] ");
    Serial.print(line1);
    Serial.print(" ");
    Serial.println(line2);
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("================================================");
    Serial.println("   TOVARNI (PROVISIONING) FIRMWARE - ukoly-esp32");
    Serial.println("================================================");
    Serial.println("Ucel: pripoji zarizeni k WiFi a rovnou nainstaluje");
    Serial.println("aktualni ostry firmware z GitHub Releases.");
    Serial.print("Cilova platforma (FIRMWARE_TARGET): ");
    Serial.println(FIRMWARE_TARGET);
    Serial.println();

    // LittleFS + perzistentni OTA stav. Na zcela novem/prave smazanem
    // zarizeni (plny erase_flash pred zapisem tovarniho firmware) je tohle
    // vzdy cisty start.
    OtaState::begin();
    OtaManager::setStatusCallback(otaStatusCallback);
    OtaManager::begin();

    Serial.print("KROK 1: Pripojte se na WiFi sit: ");
    Serial.println(AP_NAME);
    Serial.println("        Pak v prohlizeci otevrete: 192.168.4.1");
    Serial.println("        a vyberte domaci WiFi sit.");
    Serial.println();

    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    if (!wm.autoConnect(AP_NAME))
    {
        Serial.println("Konfiguracni portal WiFi vyprsel (180s), restartuji...");
        delay(2000);
        ESP.restart();
    }

    Serial.print("WiFi pripojeno. IP adresa: ");
    Serial.println(WiFi.localIP());
    Serial.println();

    Serial.println("KROK 2: Stahuji a instaluji aktualni verzi softwaru...");
    Serial.println("        (podrobny prubeh viz [OTA] hlasky nize)");
    Serial.println();
}

void loop()
{
    // OtaManager::handle() pri uspesne instalaci sam zavola ESP.restart() -
    // od tohoto okamziku uz dal bezi ostry firmware a tento sketch se
    // nevraci. Pokud se sem loop() vrati, aktualizace se (zatim) nepovedla
    // (napr. GitHub docasne nedostupny, nebo zadny kompatibilni Release
    // jeste neexistuje) - OTA_CHECK_INTERVAL_MS v OtaConfig.h je umyslne
    // kratky (20s), takze se to zkusi znovu za chvili. Presny duvod vidis
    // v Serial Monitoru (hlasky "[OTA] ...").
    OtaManager::handle();
    delay(200);
}
