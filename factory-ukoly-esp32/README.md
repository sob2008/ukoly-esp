# factory-ukoly-esp32

Tovární (provisioning) firmware pro `ukoly-esp32`. Samostatný PlatformIO
projekt – **ne** součást ostrého firmware v `../src`. Adaptováno z
[`sob2008/esp-ota`](https://github.com/sob2008/esp-ota) (`factory-template/`).

## K čemu to je

Nahrává se přes USB na nové/vrácené/resetované zařízení **místo** ostrého
firmware. Po zapnutí:

1. Vytvoří WiFi síť `Ukoly_Provisioning` – připoj se na ni telefonem/PC,
   otevři `192.168.4.1` a vyber domácí WiFi (WiFiManager captive portal).
2. Jakmile se připojí, během pár sekund (interval 20 s, viz `src/OtaConfig.h`)
   si samo najde, stáhne a nainstaluje nejnovější GitHub Release ostrého
   firmware ([`sob2008/ukoly-esp`](https://github.com/sob2008/ukoly-esp)) –
   stejným OTA klientem jako ostrý firmware (`FIRMWARE_VERSION "0.0.0"` je
   vždy nižší než reálný release, takže se OTA spustí okamžitě).
3. Po úspěšné instalaci se zařízení samo restartuje a dál běží jako ostrý
   firmware – tenhle sketch se tím přepíše a už nikdy neběží znovu (dokud by
   ho někdo znovu nenahrál přes USB).

**Nutná podmínka:** repozitář `sob2008/ukoly-esp` musí mít alespoň jeden
publikovaný Release (viz `../README.md`, sekce "OTA aktualizace" –
`scripts/release.ps1`), jinak tovární firmware jen donekonečna zkouší a nic
nenajde.

**Důležité:** ostrý firmware (`../src/main.cpp`) má WiFi SSID/heslo pevně
zadrátované v kódu (`WIFI_SSID`/`WIFI_PASSWORD`), ne přes WiFiManager. WiFi,
kterou zadáš do portálu tady, slouží jen k tomu, aby si TOHLE tovární
firmware mohlo stáhnout release – aby se po restartu připojil i ostrý
firmware, musí mít build, který si stáhne, ve `WIFI_SSID`/`WIFI_PASSWORD`
už při kompilaci nastavenou tvoji síť.

## Sestavení a nahrání

```bash
# v tomhle adresáři (factory-ukoly-esp32/)
pio run                    # zkompiluje tovární firmware
pio run --target upload    # nahraje přes USB (vyžaduje PlatformIO/pio na PC)
pio device monitor         # sériová konzole, 115200 baud
```

### Nebo bez PlatformIO – přes `!flash/`

`!flash/` obsahuje univerzální flash skript (ESP32/ESP8266, nezávislý na
projektu – nezakázáno editovat) z `sob2008/esp-ota`. Očekává soubory
pojmenované podle Arduino IDE konvence (`<sketch>.ino.bin` apod.), zatímco
PlatformIO builduje jako `firmware.bin`/`bootloader.bin`/`partitions.bin` –
proto je po `pio run` nutné je zkopírovat a přejmenovat do `!flash/bin/`:

```powershell
Copy-Item .pio\build\esp32dev\firmware.bin      "!flash\bin\factory-ukoly-esp32.ino.bin"
Copy-Item .pio\build\esp32dev\bootloader.bin    "!flash\bin\factory-ukoly-esp32.ino.bootloader.bin"
Copy-Item .pio\build\esp32dev\partitions.bin    "!flash\bin\factory-ukoly-esp32.ino.partitions.bin"
```

Pak z `!flash/`:
- Windows: `python flash.py`
- Linux/macOS: `./flash.sh`

Skript sám zkontroluje/nabídne doinstalovat `esptool`/`pyserial`, najde USB
zařízení, smaže flash a nahraje firmware. `--monitor` otevře sériový monitor,
`--info` zobrazí informace o čipu, `--erase` jen smaže flash.

## Rozdíly oproti `sob2008/esp-ota/factory-template`

- `OtaVersion`, `Sha256`, `OtaState`, `OtaManager` jsou stejné ESP32-portované
  soubory jako v `../src` (nativní A/B OTA partition, žádná vlastní záloha
  firmware) – viz `../README.md`, sekce "OTA aktualizace".
- Žádné vlastní `WiFiManagerParameter` navíc – ostrý firmware žádný
  doplňkový údaj od uživatele při prvním nastavení nepotřebuje.
- Žádný displej/LED status – `otaStatusCallback` jen loguje na Serial.
