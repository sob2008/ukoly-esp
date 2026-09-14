# ukoly-esp32

Lokální webový úkolovník běžící přímo na ESP32, čistě na domácí WiFi – bez
cloudu a bez závislosti na internetu. Přístupný z libovolného zařízení v síti
(telefon, PC, tablet) přes prohlížeč, a navíc instalovatelný na telefonu jako
PWA, aby fungoval i offline mimo domov.

## Konfigurace před nahráním

WiFi se nezadává do kódu – zařízení se konfiguruje přes captive portal
([WiFiManager](https://github.com/tzapu/WiFiManager)), viz "První připojení
k WiFi" níže. V souboru [`src/main.cpp`](src/main.cpp) lze podle potřeby
upravit jen:

```cpp
const char *WIFI_SETUP_AP_NAME = "Ukoly_Setup"; // název sítě pro první nastavení
const char *MDNS_NAME = "ukoly";   // zařízení bude dostupné jako http://ukoly.local
const int STATUS_LED = 2;          // uprav podle konkrétní desky, pokud GPIO2 nesedí
```

## Nahrání na zařízení

Firmware i data (frontend) se nahrávají zvlášť:

```bash
# 1) nahraje obsah data/ (frontend) na LittleFS
pio run --target uploadfs

# 2) nahraje samotný firmware
pio run --target upload
```

Pořadí není striktně nutné, ale doporučuje se nejdřív nahrát souborový systém.
Obojí je potřeba nahrát vždy přes USB – zařízení se samo neaktualizuje.

## První připojení k WiFi

Po prvním nahrání (nebo kdykoliv zařízení nemá uložené funkční WiFi) vytvoří
síť **`Ukoly_Setup`**:

1. Připoj se na ni telefonem/PC.
2. V prohlížeči se buď sama otevře portál, nebo jdi na `192.168.4.1`.
3. Vyber domácí WiFi síť a zadej heslo.

Zařízení se pak připojí a přihlašovací údaje si uloží (persistují v NVS na
flash, přežijí restart). Portál čeká 180 sekund – pokud vyprší, zařízení
pokračuje bez WiFi (LED viz níže) a při dalším restartu to zkusí znovu.

## Jak zařízení najít v síti

- `http://ukoly.local` (díky mDNS – funguje ve většině domácích sítí a na
  většině zařízení; na některých starších Android telefonech mDNS nemusí
  fungovat)
- nebo IP adresa vypsaná do sériové konzole po startu (`pio device monitor`,
  rychlost 115200 baud)

## Stavová LED

- **trvale svítí** – WiFi je připojeno a server běží
- **zhasnutá** – čeká se na nastavení WiFi přes portál `Ukoly_Setup` (viz výše)
- **pomalu bliká (500 ms)** – WiFi spadlo / portál vypršel bez nastavení
- **rychle bliká donekonečna** – nepodařilo se připojit LittleFS (kritická chyba)

## Přidání appky na plochu telefonu

Appka je instalovatelná jako PWA. Při **první instalaci musí být telefon
připojený k WiFi a otevřít appku přímo na ESP32** (např. `http://ukoly.local`),
aby si service worker stihl nacachovat shell appky pro pozdější offline provoz.

**iPhone (Safari):**
1. Otevři `http://ukoly.local` v Safari.
2. Ťukni na ikonu Sdílet.
3. Zvol „Přidat na plochu“.

**Android (Chrome):**
1. Otevři `http://ukoly.local` v Chrome.
2. Otevři nabídku (tři tečky).
3. Zvol „Přidat na plochu“ / „Nainstalovat aplikaci“.

## Otevření v prohlížeči na PC bez instalace

Appku lze úplně stejně jen otevřít v libovolném prohlížeči na `http://ukoly.local`
(nebo na IP adrese zařízení) bez jakékoliv instalace – funguje to stejně, jen
bez možnosti offline provozu mimo domácí síť.

## Rozhraní appky

Appka je plně responzivní – na mobilu spodní navigace + plovoucí tlačítko
„+“ vpravo dole, na širší obrazovce (PC/tablet) se navigace přesune do
levého panelu. Čtyři sekce:

- **Úkoly** – jen seznam (přidávání přes „+“, otevře formulář jako spodní
  „sheet“ na mobilu / vycentrované okno na PC).
- **Kategorie** – seznam kategorií s počtem nedokončených úkolů; kliknutím
  na kategorii detail s jejími úkoly a možností kategorii smazat (úkoly v ní
  zůstanou, jen se přeřadí na „Nezařazeno“ – kategorie samotné se nedají
  editovat, jen vytvořit/smazat).
- **Kalendář** – měsíční přehled, dny s úkoly mají tečku barvenou podle
  nejvyšší priority daného dne; kliknutím na den se dole zobrazí jeho úkoly.
- **Nastavení** – adresa ESP32 pro ruční sync a stav synchronizace.

Vpravo nahoře je tečka indikující stav synchronizace (klik na ni otevře
Nastavení): **zelená** = přímo na zařízení, nebo nedávno synchronizováno;
**žlutá** = synchronizováno, ale ne v poslední hodině; **červená** = víc
než den bez synchronizace (nebo nikdy).

## Jak funguje synchronizace

- Appka vždy čte a zobrazuje data z `localStorage` v telefonu/PC – to je zdroj
  pravdy pro UI, ať appka běží online nebo offline.
- Pokud appku otevíráš **přímo na ESP32** (doma, přes prohlížeč nebo
  nainstalovanou appku připojenou k domácí WiFi), žádná ruční synchronizace
  neprobíhá – appka pracuje přímo s daty na zařízení přes relativní URL
  `/api/...`.
- Synchronizace mezi appkou a ESP32 se řeší jen tehdy, když appku (nainstalovanou
  jako PWA) otevřeš **mimo domácí síť** a chceš si vynutit sloučení dat po
  návratu domů. V takovém případě v sekci **Nastavení** vyplň adresu zařízení
  (např. `http://ukoly.local` nebo IP adresu) a použij tlačítko
  „Synchronizovat teď“, případně se sync spustí automaticky při návratu appky
  do popředí.
- Při konfliktu vyhrává záznam s novějším `updatedAt`.
