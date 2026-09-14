# ukoly-esp32

Lokální správce projektů/úkolů/poznámek/nápadů běžící přímo na ESP32, čistě
na domácí WiFi – bez cloudu a bez závislosti na internetu. Přístupný
z libovolného zařízení v síti (telefon, PC, tablet) přes prohlížeč, a navíc
instalovatelný na telefonu na plochu jako appka.

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

**Uložené úkoly/projekty/poznámky přitom zůstanou netknuté** – žijí na
samostatné flash partition `userdata`, oddělené od partition se statickými
soubory appky (`data/`), kterou `uploadfs` přepisuje. Viz `partitions.csv`
a sekce "Architektura ukládání dat" níže.

Jediná výjimka je změna `partitions.csv` samotného (rozložení flash) – to
vyžaduje kompletní `erase_flash` a nové nahrání všeho od nuly, včetně ztráty
uložených dat i WiFi hesla. Za normálního vývoje (úpravy `src/main.cpp` nebo
`data/`) k tomu nedochází.

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

Appka jde přidat na plochu telefonu (funguje jako appka na celou obrazovku,
bez adresního řádku prohlížeče).

**iPhone (Safari):**
1. Otevři `http://ukoly.local` v Safari (musíš být na domácí WiFi).
2. Ťukni na ikonu Sdílet.
3. Zvol „Přidat na plochu“.

**Android (Chrome):**
1. Otevři `http://ukoly.local` v Chrome (musíš být na domácí WiFi).
2. Otevři nabídku (tři tečky).
3. Zvol „Přidat na plochu“ / „Nainstalovat aplikaci“.

**Důležité omezení – appka NEFUNGUJE offline od "studeného startu":**
appka běží na obyčejném HTTP (`http://ukoly.local`), ne HTTPS. Prohlížeče
(Chrome, Safari) z bezpečnostních důvodů dovolují service workery (a tedy
stránkové cachování pro offline použití) jen na HTTPS nebo `localhost` – na
`http://ukoly.local` to technicky nejde. V praxi to znamená:
- Appka funguje offline, **dokud zůstává běžet na pozadí** telefonu (čte si
  vlastní data z `localStorage`, síť k tomu nepotřebuje).
- Pokud appku úplně zavřeš/vypneš telefon a spustíš ji znovu **mimo domácí
  WiFi**, zobrazí se prázdná/chybová stránka – prohlížeč se musí nejdřív
  spojit se zařízením, aby appku vůbec stáhnul.
- Přidání HTTPS na ESP32 by tohle vyřešilo, ale je to zásadní zásah
  (vlastní certifikát, který by sis musel ručně nainstalovat a "vyzdvihnout"
  jako důvěryhodný na každém telefonu) – zatím se nedělalo, appka se
  primárně používá doma na WiFi.

## Otevření v prohlížeči na PC bez instalace

Appku lze úplně stejně jen otevřít v libovolném prohlížeči na `http://ukoly.local`
(nebo na IP adrese zařízení) bez jakékoliv instalace.

## Rozhraní appky

Appka je plně responzivní – na mobilu spodní navigace + plovoucí tlačítko
„+“ vpravo dole, na širší obrazovce (PC/tablet) se navigace přesune do
levého panelu. Pět sekcí:

- **Úkoly** – plochý přehled VŠECH nesplněných/splněných úkolů napříč
  projekty i nápady, seřazený podle priority a termínu. Rychlý pohled na to,
  co je potřeba udělat, bez nutnosti procházet jednotlivé projekty.
- **Projekty** – seznam projektů (např. "Zahrada", "Rekonstrukce koupelny").
  Kliknutím na projekt detail se dvěma taby:
  - **Úkoly** – checklist položek k udělání v rámci projektu.
  - **Poznámky** – volné textové poznámky/nápady k projektu (bez termínu
    a checkboxu, jen text + čas přidání).

  V detailu jde projekt tlačítkem s ikonou koše smazat (úkoly v něm zůstanou,
  jen se přeřadí na „Nezařazeno“). Projekty se needitují (nejde přejmenovat),
  jen vytvářejí a mažou.
- **Nápady** – strukturně úplně stejné jako Projekty (vlastní seznam, každý
  nápad má svoje úkoly i poznámky) – jen oddělená kategorie pro věci, které
  ještě nejsou plnohodnotný projekt.
- **Kalendář** – měsíční přehled, dny s úkoly mají tečku barvenou podle
  nejvyšší priority daného dne; kliknutím na den se dole zobrazí jeho úkoly.
- **Nastavení** – adresa ESP32 pro ruční sync a stav synchronizace.

Tlačítko „+“ (FAB, vpravo dole) je kontextové podle toho, kde zrovna jsi:
na Úkolech/Kalendáři přidá úkol, na Projektech/Nápadech založí nový
projekt/nápad, v detailu projektu přidá úkol nebo poznámku podle toho, který
tab je aktivní.

Vpravo nahoře je tečka indikující stav synchronizace (klik na ni otevře
Nastavení): **zelená** = přímo na zařízení, nebo nedávno synchronizováno;
**žlutá** = synchronizováno, ale ne v poslední hodině; **červená** = víc
než den bez synchronizace (nebo nikdy).

## Architektura ukládání dat

Zařízení má na flash dvě oddělené LittleFS partition (viz `partitions.csv`):
- **`spiffs`** – statické soubory appky (`data/` – HTML/JS/CSS/ikony).
  Tuhle partition přepisuje `pio run --target uploadfs` při každé aktualizaci
  frontendu.
- **`userdata`** – uložené úkoly/projekty/nápady/poznámky
  (`/tasks.json`, `/categories.json`, `/notes.json`). Firmware ji montuje
  jako druhou, nezávislou LittleFS instanci (`UserFs` v `src/main.cpp`) –
  `uploadfs` se jí vůbec nedotkne.

Tohle oddělení je záměrné: bez něj by každá aktualizace frontendu (běžná
věc) nenávratně smazala všechna uložená data.

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
