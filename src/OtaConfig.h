#pragma once
// ============================================================
// OTA KONFIGURACE - jedine misto pro nastaveni OTA systemu.
// ============================================================
// Zadne tajne udaje (GitHub token, hesla, klice) sem NEPATRI - repozitar
// je verejny, OTA pouziva anonymni GitHub REST API.
//
// Zalozeno na https://github.com/sob2008/esp-ota (template/OtaConfig.example.h),
// portovano pro ESP32 - viz OtaManager.h/.cpp a README.md, sekce "OTA aktualizace"
// pro rozdily oproti puvodni ESP8266 verzi.

// --- Verze a identita tohoto firmware ---

// Zvysujte pri kazde zmene, kterou chcete distribuovat pres OTA.
// Pouziva se Semantic Versioning (MAJOR.MINOR.PATCH), viz OtaVersion.h.
// Musi presne odpovidat tagu (vX.Y.Z), ktery se pushne pro vydani release
// (viz scripts/release.ps1 a .github/workflows/release.yml).
#define FIRMWARE_VERSION "1.0.0"

// Identifikuje HW/SW variantu tohoto firmware. OTA odmitne nainstalovat
// release, jehoz firmware.json obsahuje jiny "target" - chrani pred
// nahranim firmware urceneho pro jiny hardware.
#define FIRMWARE_TARGET "esp32dev-ukoly-esp32"

// --- GitHub repozitar s Releases ---
#define GITHUB_OWNER "sob2008"
#define GITHUB_REPOSITORY "ukoly-esp"

// Nazvy ocekavanych assetu v GitHub Release.
// firmware.json je volitelny, ale doporuceny - pokud je pritomen, pouzije
// se pro kontrolu "target" a SHA-256 bez nutnosti samostatneho .sha256 souboru.
#define OTA_ASSET_FIRMWARE "firmware.bin"
#define OTA_ASSET_METADATA "firmware.json"
#define OTA_ASSET_CHECKSUM "firmware.bin.sha256"

// --- Chovani OTA ---

// Globalni vypinac - pri false OtaManager::handle() nic nedela.
#define OTA_ENABLED true

// Jak casto (ms) se kontroluje GitHub Releases na novou verzi.
// 30 minut je rozumny vychozi interval pro zarizeni bezici bez dohledu.
#define OTA_CHECK_INTERVAL_MS (30UL * 60UL * 1000UL) // 30 minut

// Timeouty sitovych operaci (ms).
#define OTA_CONNECT_TIMEOUT_MS 10000UL
#define OTA_DOWNLOAD_TIMEOUT_MS 120000UL // cely stahovaci cyklus firmware.bin

// Vyzadovat platny SHA-256 checksum, jinak OTA zrusit (fail-closed).
// Vychozi true - HTTPS spojeni pro OTA pouziva WiFiClientSecure::setInsecure()
// (zadne cert pinning), takze tento checksum je skutecnou, ne jen
// volitelnou pojistkou integrity stazeneho firmware.
#define OTA_REQUIRE_CHECKSUM true

// Poznamka k OTA_MAX_BOOT_ATTEMPTS (byl v puvodni ESP8266 verzi): na ESP32
// s aktivnim CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE (overeno pro tento build
// frameworku) povoluje bootloader firmware v needy stavu "PENDING_VERIFY"
// prezit presne JEDEN boot bez potvrzeni - pokud se do dalsiho restartu
// nezavola esp_ota_mark_app_valid_cancel_rollback(), bootloader pri druhem
// bootu firmware sam oznaci jako ABORTED a vrati se na predchozi (funkcni)
// partition. Vlastni pocitadlo pokusu tedy neni potreba ani konfigurovatelne -
// viz OtaManager::begin()/notifyApplicationHealthy() a README.md.
