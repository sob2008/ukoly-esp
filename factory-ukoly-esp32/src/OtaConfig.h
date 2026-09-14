#pragma once
// ============================================================
// OTA KONFIGURACE PRO TOVARNI (PROVISIONING) FIRMWARE.
// ============================================================
// DULEZITE: FIRMWARE_TARGET, GITHUB_OWNER, GITHUB_REPOSITORY a OTA_ASSET_*
// MUSI byt IDENTICKE s ../../src/OtaConfig.h (ostry firmware) - jinak by
// tovarni firmware nenaslo/neoverilo spravny Release. FIRMWARE_VERSION a
// OTA_CHECK_INTERVAL_MS jsou zamerne JINE, viz komentare nize.
//
// Zalozeno na https://github.com/sob2008/esp-ota (factory-template/OtaConfig.example.h).

// --- Identita "verze" tovarniho firmware ---

// Umyslne "0.0.0" - musi byt vzdy nizsi nez jakakoliv realne vydana verze,
// aby tovarni firmware pri prvnim pripojeni k WiFi okamzite naslo a
// nainstalovalo nejnovejsi dostupny Release (OtaManager porovnava verze
// vzdy jako "vysledek > 0", takze se OTA vzdy spusti).
#define FIRMWARE_VERSION "0.0.0"

// Musi presne odpovidat FIRMWARE_TARGET v ../../src/OtaConfig.h.
#define FIRMWARE_TARGET "esp32dev-ukoly-esp32"

// --- GitHub repozitar s Releases (musi odpovidat ostremu firmware) ---
#define GITHUB_OWNER "sob2008"
#define GITHUB_REPOSITORY "ukoly-esp"

// Nazvy ocekavanych assetu v GitHub Release - stejne jako v ostrem firmware.
#define OTA_ASSET_FIRMWARE "firmware.bin"
#define OTA_ASSET_METADATA "firmware.json"
#define OTA_ASSET_CHECKSUM "firmware.bin.sha256"

// --- Chovani OTA ---

#define OTA_ENABLED true

// U tovarniho firmware umyslne KRATSI interval nez v ostrem firmware
// (tam 30 min) - kdo zarizeni provizuje, ceka u nej a sleduje Serial
// Monitor, chceme rychle opakovani pri docasnem vypadku WiFi/GitHubu,
// ne cekat 30 minut.
#define OTA_CHECK_INTERVAL_MS (20UL * 1000UL) // 20 sekund

#define OTA_CONNECT_TIMEOUT_MS 10000UL
#define OTA_DOWNLOAD_TIMEOUT_MS 120000UL

// Stejna bezpecnostni ocekavani jako ostry firmware - tovarni priprava
// neni duvod checksum vyzadovat mene prisne.
#define OTA_REQUIRE_CHECKSUM true
