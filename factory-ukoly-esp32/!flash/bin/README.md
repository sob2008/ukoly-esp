Sem patří zkompilovaný `<sketch>.ino.bin` (+ `.ino.bootloader.bin`/`.ino.partitions.bin`
z `pio run`, přejmenované podle Arduino IDE konvence – viz `../../README.md`) –
`flash.py`/`flash.sh` v rodičovské složce ho odsud vezmou a nahrají do zařízení.

Aktuálně obsahuje hotové binárky finální verze továrního firmware,
commitnuté do gitu záměrně (na rozdíl od obvyklé konvence „vždy buildovat
čerstvě“) – aby šlo zařízení flashnout i bez PlatformIO na počítači. Pokud se
`factory-ukoly-esp32/src/` znovu změní, je potřeba je ručně přebuildovat
(`pio run` v `factory-ukoly-esp32/`) a nahradit.
