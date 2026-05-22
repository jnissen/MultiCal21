# Chat-Historie – wM-Bus (Multical21) auf Heltec WiFi LoRa 32 V3

Stand: 2026-05-22  
Repo: `MultiCal21_Tasmota_fork`  
Ziel-Hardware: Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262 + SSD1306 OLED)

---

## 1. Auftrag

Tasmota-Fork um einen modularen Wireless-M-Bus-Empfänger erweitern, der per
SX1262 (RadioLib) Mode C1 empfängt, Multical21-Frames dekodiert und auf
einem OLED-HUD anzeigt. Anschließend Build- und Flash-Pipeline für die
Heltec V3 stabilisieren.

---

## 2. Implementierte Komponenten

| Slot      | Datei                                              | Aufgabe                                         |
| --------- | -------------------------------------------------- | ----------------------------------------------- |
| Lib       | `lib/lib_rf/wmbus/src/radio_interface.h`           | Abstraktes `IRadio` Interface                   |
| Lib       | `lib/lib_rf/wmbus/src/radio_sx1262.{h,cpp}`        | RadioLib-Backend (Mode C1, 868,95 MHz, 100 kbps)|
| XDRV_128  | `tasmota/tasmota_xdrv_driver/xdrv_128_wmbus_radio.ino` | wM-Bus Radio-Treiber + Cmd `WMBusInfo`     |
| XDRV_129  | `tasmota/tasmota_xdrv_driver/xdrv_129_wmbus_display.ino` | OLED-HUD (SSD1306 128×64)                |
| XSNS_121  | `tasmota/tasmota_xsns_sensor/xsns_121_multical21.ino` | Multical21 Decoder + Callback-Pfad         |
| Header    | `include/wmbus_display_snapshot.h`                 | Shared `M21DisplaySnapshot` Struct              |
| PIO env   | `platformio_tasmota_env32.ini` → `tasmota32s3-heltec-wmbus` | Build-Konfiguration                  |

### Aktive Build-Flags der Env

```
-DFIRMWARE_TASMOTA32
-DUSE_WMBUS_RADIO
-DUSE_WMBUS_OLED
-DUSE_MULTICAL21
upload_speed = 115200
```

`USE_SPI` / `USE_SPI_LORA` werden bereits in `tasmota_configurations_ESP32.h`
gesetzt – nicht doppelt in der Env definieren.

### Heltec V3 Pinout (per Tasmota-Template gesetzt)

| Funktion   | GPIO |
| ---------- | ---- |
| SPI CLK    | 9    |
| SPI MISO   | 11   |
| SPI MOSI   | 10   |
| LoRa CS    | 8    |
| LoRa RST   | 12   |
| LoRa BUSY  | 13   |
| LoRa DIO1  | 14   |
| OLED SDA   | 17   |
| OLED SCL   | 18   |
| OLED Vext  | 36 (active-low) |

---

## 3. Erledigte Probleme

1. **Typedef-Kollision** zwischen `M21DisplaySnapshot` und Treiber-Headern.  
   → Gemeinsame Struktur in `include/` (Projekt-Root, nicht `tasmota/include/`),
     Cross-File-Signaturen mit `void *`.
2. **Arduino-Auto-Prototyper** erzeugte fehlerhafte Forward-Declarations
   für statische Funktionen über `.ino`-Grenzen hinweg → `void *` API.
3. **PIO Cache-Korruption** nach Abbruch / Pfadproblemen → `Remove-Item -Recurse
   .pio\build\<env>` + Neubau.
4. **Upload-Baudrate 2 Mbaud instabil** → fest auf 115 200 gesetzt
   (empfohlen: 921 600 wenn Treiber zuverlässig).
5. **Module 0 vs. Template** verwirrt → Template per `Backlog Template {...}; Module 0;`
   gesetzt, Status zeigt `Heltec WiFi LoRa 32 V3 wMBus`.
6. **`WMBusInfo` Diagnostik** um `Spi`, `Pins{...}`, `InitStep`, `InitStatus`,
   `Radio:"init-fail"` Status erweitert.

---

## 4. Aktuelles offenes Problem

Gerät bootet, MQTT läuft, Template aktiv. Aber:

```jsonc
{"WMBusInfo":{
  "Radio":"none","Rx":0,"Dropped":0,"Rssi":-32768,
  "Spi":3,
  "Pins":{"CS":8,"RST":12,"BUSY":13,"DI1":14,"CLK":9,"MOSI":10,"MISO":11}
}}
```

- `Spi:3` = `SPI_MOSI_MISO` ⇒ SPI ist aktiv.
- Alle LoRa-Pins zugewiesen.
- Trotzdem wird das Radio nicht ready.

**Hypothese**: `RadioSX1262::begin()` (RadioLib) gibt still einen Fehlercode
zurück. Die ursprüngliche Implementierung verwarf den Fehler und löschte das
Radio-Objekt, sodass `WMBusInfo` keine Diagnose mehr ausgeben konnte.

### Letzte Code-Änderung (in diesem Chat)

1. `IRadio` erweitert um:
   - `virtual int16_t lastStatus() const`
   - `virtual uint8_t initStep() const`
2. `RadioSX1262::begin()` speichert nach jedem RadioLib-Aufruf
   `init_step_` (1..6) und `last_status_`.  
   Schritte: 1=ctor, 2=beginFSK, 3=setEncoding, 4=setCRC, 5=setSyncWord,
   6=variablePacketLengthMode.
3. `xdrv_128`:
   - `WMBusState` um `radio_ready`, `init_step`, `init_status` erweitert.
   - Bei `begin()`-Fehler wird das Radio-Objekt **nicht** mehr gelöscht,
     damit die Diagnose-Felder erhalten bleiben.
   - `WMBusCmndInfo` JSON enthält jetzt `"InitStep":N,"InitStatus":N`
     und `"Radio":"init-fail"` bei Init-Failure.
   - `WMBusRadioReady()` prüft jetzt `radio_ready`-Flag.
   - Logging zeigt Step + Status bei Fehlschlag.

### Build-Stand

Zwei aufeinanderfolgende Build-Versuche schlugen mit
`[FAILED]` ohne sichtbaren Compile-Fehler fehl, einmal mit Hinweis:
```
*** [.pio\build\tasmota32s3-heltec-wmbus\libaf4\Wire] ...
   Das System kann den angegebenen Pfad nicht finden
```
Vermutlich Windows-Pfadlängenproblem + leerer Stale-Cache nach `Remove-Item`.

**Nächste Schritte (offen)**:

1. PIO Cache komplett verwerfen: `Remove-Item -Recurse -Force .pio` und neu bauen.
2. Long-Path-Support unter Windows aktivieren:
   ```powershell
   Set-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' `
     -Name LongPathsEnabled -Value 1
   ```
3. Build mit voller Ausgabe (`-v`) um echten Fehler zu sehen.
4. Wenn Build durchläuft → flashen → `WMBusInfo` ausführen → `InitStep`/`InitStatus`
   ablesen → mit RadioLib-Statuscodes (`RADIOLIB_ERR_*` in
   `lib/lib_rf/RadioLib/src/TypeDef.h`) abgleichen.

### Wahrscheinliche RadioLib-Ursachen (sobald Code sichtbar)

| Verdacht                          | Maßnahme                                          |
| --------------------------------- | ------------------------------------------------- |
| TCXO-Spannung falsch              | Heltec V3 = 1.8 V (bereits gesetzt)               |
| `SPI.begin()` Konflikt mit RadioLib `Module::begin()` | RadioLib SPI-Handle prüfen      |
| Sync-Word-Länge / Alignment       | RadioLib-Versionscheck (`setSyncWord`)            |
| `variablePacketLengthMode` Reihenfolge | ggf. nach `setPacketType` aufrufen           |
| Reset-Timing / BUSY hängt         | Hardware-Verdrahtung prüfen                       |

---

## 5. Erkenntnisse für Memory

Gespeichert in `/memories/repo/tasmota-build.md`:

- Shared Types müssen im **Projekt-Root** `include/` liegen, nicht `tasmota/include/`.
- Arduino-Auto-Prototyper erstellt Forward-Decls auch für `static`-Funktionen
  über `.ino`-Grenzen → bei Typen aus Headern, die nicht überall sichtbar sind,
  Cross-File-API mit `void *` halten.
- Heltec V3 OLED: GPIO36 Vext **active-low**, SDA=17, SCL=18.
- `SPI_MOSI_MISO == 3` (in `tasmota/include/tasmota.h`, enum `SpiInterfaces`).
- WMB_LOG benutzt `AddLog` mit Prefix `WMB:`; sichtbar ab `weblog 2` (INFO).

---

## 6. Datei-Übersicht der Änderungen in dieser Session

```
lib/lib_rf/wmbus/src/radio_interface.h    +6  Zeilen   (lastStatus, initStep)
lib/lib_rf/wmbus/src/radio_sx1262.h       +4  Zeilen   (Members + Overrides)
lib/lib_rf/wmbus/src/radio_sx1262.cpp     ~25 Zeilen   (Step-Tracking in begin())
tasmota/tasmota_xdrv_driver/xdrv_128_wmbus_radio.ino
                                          ~40 Zeilen   (State, Init, Cmd-JSON)
```
