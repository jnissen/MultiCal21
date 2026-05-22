# Tasmota Fork – Multical21 / wM-Bus Empfänger

> Fork von [arendst/Tasmota](https://github.com/arendst/Tasmota) – Upstream‑Dokumentation, Release‑Notes und allgemeine Tasmota‑Features siehe [README_UPSTREAM.md](README_UPSTREAM.md) bzw. <https://tasmota.github.io/docs/>.

Dieser Fork erweitert Tasmota um einen modularen **wireless M‑Bus (wM-Bus, OMS) Empfänger**
für ESP32-Boards. Damit lassen sich z. B. Kamstrup **Multical21 / FlowIQ 2200** Wasserzähler
direkt per Funk in Home Assistant, openHAB, ioBroker oder MQTT-Backends einbinden.

Implementiert sind drei kooperierende Tasmota-Treiber:

| Slot | Datei | Zweck |
|------|-------|-------|
| `XDRV_128` | [xdrv_128_wmbus_radio.ino](tasmota/tasmota_xdrv_driver/xdrv_128_wmbus_radio.ino) | Radio-Frontend (SX1262, CC1101 geplant), Frame-Dispatcher |
| `XDRV_129` | [xdrv_129_wmbus_display.ino](tasmota/tasmota_xdrv_driver/xdrv_129_wmbus_display.ino) | SSD1306 OLED HUD für Heltec V3 |
| `XSNS_121` | [xsns_121_multical21.ino](tasmota/tasmota_xsns_sensor/xsns_121_multical21.ino) | Kamstrup‑Decoder (AES‑128‑CTR, Compact + Long Frame) |

---

## Inhalt

- [Unterstützte Hardware](#unterstützte-hardware)
- [Build & Flash](#build--flash)
- [1. Heltec WiFi LoRa 32 V3 (SX1262)](#1-heltec-wifi-lora-32-v3-sx1262)
- [2. ESP32 + CC1101 Funkmodul](#2-esp32--cc1101-funkmodul)
- [3. Gemeinsame Konfiguration (alle Boards)](#3-gemeinsame-konfiguration-alle-boards)
- [Backlog‑Snippets](#backlog-snippets)
- [Debugging](#debugging)
- [MQTT Output](#mqtt-output)
- [Bekannte Einschränkungen](#bekannte-einschränkungen)

---

## Unterstützte Hardware

| Board / Modul | Funk-Chip | Status |
|---|---|---|
| Heltec WiFi LoRa 32 V3 (ESP32‑S3) | Semtech SX1262 (on‑board) | ✅ unterstützt (MVP) |
| Heltec WiFi LoRa 32 V2 (ESP32) | Semtech SX1276 | ⚠️ nicht getestet (SX1276‑Backend folgt) |
| beliebiges ESP32/ESP8266 + CC1101‑Modul | TI CC1101 | 🛠️ Pin‑Konfiguration vorbereitet, Backend in Arbeit |

> **Hinweis CC1101:** Das CC1101‑Backend ist in [xdrv_128_wmbus_radio.ino](tasmota/tasmota_xdrv_driver/xdrv_128_wmbus_radio.ino) bereits als zweiter Pfad eingeplant. Bis zur Fertigstellung lässt sich CC1101 nur mit dem klassischen Treiber aus [MULTICAL21.md](MULTICAL21.md) (Legacy‑Pfad) betreiben.

---

## Build & Flash

```pwsh
git clone https://github.com/<dein-user>/MultiCal21_Tasmota_fork.git
cd MultiCal21_Tasmota_fork

# Bevorzugt VS Code + PlatformIO-Extension öffnen, dann Env wählen:
#   tasmota32s3-heltec-wmbus

# Oder per CLI:
pio run -e tasmota32s3-heltec-wmbus -t upload
pio device monitor -e tasmota32s3-heltec-wmbus
```

Build‑Env-Definition: [platformio_tasmota_env32.ini](platformio_tasmota_env32.ini#L413)

Aktive Build‑Flags:

```
-DFIRMWARE_TASMOTA32
-DUSE_WMBUS_RADIO       ; aktiviert xdrv_128 + Radio-Backend
-DUSE_WMBUS_OLED        ; aktiviert xdrv_129 OLED HUD
-DUSE_MULTICAL21        ; aktiviert xsns_121 Decoder
```

Eigene Anpassungen gehören in [platformio_override.ini](platformio_override.ini) – `platformio_tasmota_env32.ini` bleibt unangetastet, damit Upstream‑Merges sauber laufen.

---

## 1. Heltec WiFi LoRa 32 V3 (SX1262)

Das Heltec V3 hat **SX1262 + SSD1306 OLED + Vext‑Power** bereits on-board verdrahtet. Keine externe Verkabelung nötig.

### Pinout (fest verdrahtet)

| Funktion | GPIO | Tasmota Template-Eintrag |
|---|---|---|
| SPI SCK   | 9  | `SPI CLK`  |
| SPI MISO  | 11 | `SPI MISO` |
| SPI MOSI  | 10 | `SPI MOSI` |
| LoRa CS   | 8  | `LoRa CS`  |
| LoRa RST  | 12 | `LoRa RST` |
| LoRa BUSY | 13 | `LoRa BUSY`|
| LoRa DIO1 | 14 | `LoRa DI1` |
| OLED SDA  | 17 | `I2C SDA`  |
| OLED SCL  | 18 | `I2C SCL`  |
| OLED RST  | 21 | (Auto‑Reset im Treiber, kein Template‑Eintrag nötig) |
| Vext EN   | 36 | (vom Treiber direkt gesteuert) |

### Konfiguration nach dem Flashen

1. **Erstes Boot** → Hotspot `tasmota-xxxx` → WLAN konfigurieren.
2. **Template setzen** (in der Web‑UI unter *Configuration → Configure Other → Template* einfügen):

```json
{"NAME":"Heltec V3 wM-Bus","GPIO":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,608,640,0,0,0,5728,0,0,0,0,0,0,5792,5824,5856,5920,5888,0,0,0,0,0,0,0,5760,0,0,0,0,0],"FLAG":0,"BASE":1}
```

3. Modul aktivieren: *Configuration → Configure Module → Module type = User Configured*, **Save**.
4. Sobald das Modul mit gesetztem Template läuft, erscheinen im Log Zeilen wie:
   ```
   WMBUS: SX1262 ready (868.95 MHz, 100 kbps)
   WOLED: SSD1306 ready at 0x3C (Vext pin=36 RST pin=21)
   ```

### Heltec‑spezifische Konsolen‑Befehle

```
; OLED komplett aus
OledOff

; OLED wieder an (deaktiviert auch den Boot-Auto-Off)
OledOn

; Helligkeit (0..255)
OledDim 200

; Seite fixieren (0=Network, 1=Water, 2=Debug, -1=Auto-Rotate)
OledPage 1
```

> Default: Das OLED schaltet sich **5 Minuten nach Boot** automatisch ab (Stromspar‑Auto‑Off). Override per Build‑Flag `-DWMBUS_OLED_AUTO_OFF_SEC=<sek>` in [platformio_override.ini](platformio_override.ini); `0` = nie.

---

## 2. ESP32 + CC1101 Funkmodul

Empfohlen für Setups **ohne LoRa‑Onboard‑Chip**, z. B. Wemos D1 Mini, ESP32‑C3 SuperMini, NodeMCU.

> **Status:** Im neuen `xdrv_128`-Stack ist das CC1101‑Backend vorbereitet, aber noch nicht aktiv. Bis dahin: Hardware bereits jetzt verdrahten und mit dem klassischen Pfad aus [MULTICAL21.md](MULTICAL21.md) betreiben.

### 2.1 Wemos D1 Mini (ESP8266) – getestet

Klassische Verdrahtung wie im Originalprojekt `pthalin/esp32-multical21`:

| CC1101 Pin | Funktion | D1 Mini Pin | GPIO |
|---|---|---|---|
| VCC  | 3.3 V    | 3V3 | – |
| GND  | GND      | GND | – |
| SCK  | SPI CLK  | D5 | GPIO 14 |
| MISO | SPI MISO | D6 | GPIO 12 |
| MOSI | SPI MOSI | D7 | GPIO 13 |
| CSN  | SPI CS   | D8 | GPIO 15 |
| GDO0 | IRQ      | D2 | GPIO 4  |
| GDO2 | (optional) | – | – |

**Template (Tasmota Web‑UI → Configuration → Configure Other → Template):**

```json
{"NAME":"Multical 21 D1Mini","ARCH":"ESP8266","GPIO":[0,0,0,0,4544,0,0,0,672,704,736,768,0,0],"FLAG":0,"BASE":18}
```

Anschließend per Konsole aktivieren:

```text
Backlog Module 0; Restart 1
```

Code‑Mapping des Templates (BASE 18 = Wemos D1 R2 & Mini):

| GPIO | Code | Funktion |
|---|---|---|
| GPIO 4  | `4544` | `CC1101 GDO0` |
| GPIO 12 | `672`  | `SPI MISO` |
| GPIO 13 | `704`  | `SPI MOSI` |
| GPIO 14 | `736`  | `SPI CLK`  |
| GPIO 15 | `768`  | `SPI CS`   |

### 2.2 ESP32‑C3 SuperMini – Beispiel‑Verdrahtung

| CC1101 Pin | Funktion | ESP32‑C3 GPIO | Tasmota‑GPIO‑Label |
|---|---|---|---|
| VCC  | 3.3 V    | 3V3 | – |
| GND  | GND      | GND | – |
| SCK  | SPI CLK  | GPIO 4 | `SPI CLK` |
| MISO | SPI MISO | GPIO 5 | `SPI MISO` |
| MOSI | SPI MOSI | GPIO 6 | `SPI MOSI` |
| CSN  | SPI CS   | GPIO 7 | `SPI CS` |
| GDO0 | IRQ      | GPIO 8 | `CC1101 GDO0` |
| GDO2 | (optional) | – | – |

**Template (experimentell, BASE 1 = generic ESP32‑C3):**

```json
{"NAME":"Multical 21 C3","GPIO":[0,0,0,0,736,672,704,768,4544,0,0,0,0,0,0,0,0,0,0,0,0,0],"FLAG":0,"BASE":1}
```

Aktivieren:

```text
Backlog Module 0; Restart 1
```

> Funktioniert exakt diese Belegung bei deinem Board nicht out‑of‑the‑box, empfiehlt sich der manuelle Weg: *Configure Module → Module type = ESP32‑C3 Generic*, dann pro Pin oben aufgeführtes Label aus dem Dropdown wählen.

ANT‑Pin am CC1101: 17.4 cm Draht (λ/4 für 868 MHz) oder eine 868 MHz SMA‑Antenne.

### 2.3 Build‑Flag

In [platformio_override.ini](platformio_override.ini) ein eigenes Env anlegen, Beispiel:

```ini
[env:tasmota32c3-cc1101-wmbus]
extends     = env:tasmota32c3
build_flags = ${env:tasmota32c3.build_flags}
              -DUSE_WMBUS_RADIO
              -DUSE_MULTICAL21
              ; -DUSE_WMBUS_OLED   ; (nur falls SSD1306 vorhanden)
```

### 2.4 CC1101 vs. SX1262

Der Decoder (`xsns_121`) und alle `M21*`‑Befehle sind **funkchip‑agnostisch**. Sobald das passende Backend instanziert ist, gelten exakt dieselben Backlogs wie unter Heltec.

---

## 3. Gemeinsame Konfiguration (alle Boards)

Nach Template + Reboot ist nur noch der **AES‑Schlüssel** und die **Meter‑ID** nötig. Beides bekommt man bei Kamstrup‑Multical21 üblicherweise vom Wasserversorger; die ID steht **lesbar** auf dem Display des Zählers (8‑stellige Dezimalzahl).

### Erstinbetriebnahme per Backlog

```text
Backlog M21Id 0; M21Key 00112233445566778899AABBCCDDEEFF; M21Type 0; M21Period 60; SetOption4 1; TelePeriod 300
```

- `M21Id 0` → **Promiscuous Mode**: alle Multical21‑Telegramme im Funkbereich werden angenommen → die echte ID erscheint im Log unter `M21: RX mfr=KAM id=XXXXXXXX`.
- Sobald die eigene ID bekannt ist, festschreiben:

```text
Backlog M21Id 75714832; SaveData 1
```

### Alle M21‑Befehle

| Befehl | Werte | Wirkung |
|---|---|---|
| `M21Key <32-hex>`     | 16 Byte AES‑Key als Hex‑String                       | AES‑128‑CTR Decryption Key setzen. Antwort maskiert (`set`/`missing`). |
| `M21Id <8-hex>`       | 8 Hex‑Zeichen (=4 Byte LE Meter‑ID) **oder** `0`    | `0` = Wildcard (alles annehmen). |
| `M21Type <0\|1>`      | `0` = Multical21, `1` = FlowIQ 2200                 | Steuert Decoder‑Varianten. |
| `M21Period <s>`       | `0…3600`                                            | Mindestabstand zwischen MQTT‑Publishes; `0` = jeder Frame. |
| `M21Info`             | –                                                   | JSON‑Status: HW, Konfig, Frame‑Counter, RSSI. |

### Allgemeine Tasmota‑Befehle, die wir empfehlen

```text
Backlog Topic W101; FullTopic %prefix%/%topic%/; TelePeriod 300; SetOption4 1; SetOption65 1; Hostname multical21-%06X; Restart 1
```

- `Topic W101` setzt den MQTT‑Topic auf etwas Sprechendes (z. B. „Wasserzähler 101“).
- `SetOption4 1` → MQTT‑Topics in Klein‑/Großbuchstabentrennung sauber.
- `SetOption65 1` → schnellere WiFi‑Reconnects.
- `TelePeriod 300` → reguläres `tele/W101/SENSOR` alle 5 min. Bei jedem **decodierten Multical‑Frame** (~16 s) wird zusätzlich sofort ein SENSOR‑Telegramm gepostet (per `MqttPublishTeleSensor()` im Decoder).

---

## Backlog‑Snippets

### A) Vollständige Erstinstallation (Heltec V3)

```text
Backlog Hostname multical21-%06X; Topic W101; FullTopic %prefix%/%topic%/; MqttHost 192.168.1.10; MqttPort 1883; MqttUser tasmota; MqttPassword secret; TelePeriod 300; SetOption4 1; SetOption65 1; Restart 1
```

Nach dem Reboot:

```text
Backlog M21Id 0; M21Key 00112233445566778899AABBCCDDEEFF; M21Type 0; M21Period 60; M21Info
```

Sobald die eigene ID im Log auftaucht (`M21: RX mfr=KAM id=XXXXXXXX`):

```text
Backlog M21Id XXXXXXXX; SaveData 1; M21Info
```

### B) Display ständig an / nie ausschalten

```text
Backlog OledOn; OledDim 200
```

> Persistent über Builds: `-DWMBUS_OLED_AUTO_OFF_SEC=0` in [platformio_override.ini](platformio_override.ini).

### C) Reset & Defaults

```text
Backlog Reset 5; Restart 1     ; löscht alle Settings, behält WLAN
Backlog Reset 1; Restart 1     ; Werksreset (auch WLAN weg)
```

### D) Diagnose‑Dump (einmaliger Statusabruf)

```text
Backlog M21Info; Status 0; Status 5; Status 11; State
```

---

## Debugging

### Live‑Log per serieller Konsole

```pwsh
pio device monitor -e tasmota32s3-heltec-wmbus
```

Wahlweise auch über die Tasmota Web‑UI unter *Consoles → Console* (Auto‑Refresh).

### Log‑Level setzen

```text
SerialLog 4       ; Detaillevel für UART (0..4)
WebLog 4          ; Detaillevel für Web-Konsole
MqttLog 0         ; Log nach MQTT (0=off, 4=DEBUG)
TelePeriod 30     ; SENSOR-Daten alle 30 s (zum Testen)
```

> Loglevel 4 (DEBUG) aktiviert die wichtigen Hex‑Dumps:
> - `M21: RX  mfr=KAM id=...  len=... rssi=...`
> - `M21: RAW <hex>` – rohe Frame‑Bytes nach Sync‑Strip
> - `M21: HDR <hex>` – wM-Bus‑Header
> - `M21: DEC <hex>` – entschlüsselter Plaintext
> - `M21: CRC ok len=64 CI=79` / `CRC mismatch ... (continuing)`
> - `WMBUS: SX1262 ready (868.95 MHz, 100 kbps)`
> - `WOLED: SSD1306 ready at 0x3C ...`

### Status abrufen

```text
M21Info       ; JSON: {"M21Info":{"Hw":"ok","Configured":"ok","Id":"75714832","Type":0,"Period":60,"Frames":42,"Valid":40,"Rssi":-72}}
Status 11     ; allgemeiner Tasmota State + Sensorblock
```

### Live MQTT‑Trace mitschneiden

```pwsh
mosquitto_sub -h <broker> -v -t 'tele/W101/#' -t 'stat/W101/#'
```

### Häufige Probleme

| Symptom | Diagnose | Lösung |
|---|---|---|
| `Frames`>0, `Valid`=0 | AES‑Key falsch oder Meter‑ID‑Filter aktiv | `M21Key …` neu setzen, `M21Id 0` zum Sniffen |
| Immer dieselbe falsche Hersteller‑ID | Sync/L‑Field nicht gestrippt | Aktuellen Stand ziehen – Fix in [radio_sx1262.cpp](lib/lib_rf/wmbus/src/radio_sx1262.cpp) |
| OLED bleibt schwarz | RST‑Pulse fehlt | sicherstellen, dass `WMBUS_OLED_RST_PIN=21` nicht überschrieben ist |
| OLED geht nach 5 min aus | Auto‑Off aktiv (gewollt) | `OledOn` oder `-DWMBUS_OLED_AUTO_OFF_SEC=0` |
| Keine Frames empfangen | Antenne, Distanz, Wandstärke | mit `M21Id 0` testen, RSSI im Log prüfen |
| `M21: CRC mismatch … (continuing)` | erwartetes Verhalten bei Compact‑Frames | nur Warnung – Werte sind gültig |

---

## MQTT Output

Beispiel `tele/W101/SENSOR` direkt nach einem decodierten Frame:

```json
{
  "Time": "2026-05-22T18:42:11",
  "Multical21": {
    "Id": "75714832",
    "Manufacturer": "KAM",
    "Volume":       {"Value": 2587.221, "Unit": "m3"},
    "VolumeTarget": {"Value": 2564.163, "Unit": "m3"},
    "Frames": 42,
    "Valid":  40,
    "Rssi":  -72
  }
}
```

Home‑Assistant‑Snippet (MQTT‑Sensor):

```yaml
mqtt:
  sensor:
    - name: "Wasserzähler Gesamt"
      state_topic: "tele/W101/SENSOR"
      unit_of_measurement: "m³"
      device_class: water
      state_class: total_increasing
      value_template: "{{ value_json.Multical21.Volume.Value }}"
    - name: "Wasserzähler Stichtag"
      state_topic: "tele/W101/SENSOR"
      unit_of_measurement: "m³"
      value_template: "{{ value_json.Multical21.VolumeTarget.Value }}"
    - name: "Wasserzähler RSSI"
      state_topic: "tele/W101/SENSOR"
      unit_of_measurement: "dBm"
      value_template: "{{ value_json.Multical21.Rssi }}"
```

---

## Bekannte Einschränkungen

- **Nur Mode C1** (868.95 MHz, 100 kbps). T1 / S1 sind nicht implementiert.
- **CC1101‑Backend** ist in `xdrv_128` vorbereitet, aber noch nicht aktiv.
- **Compact‑Frame‑CRC** wird im `xsns_121` als Warnung degradiert (`continuing`) – die EN13757‑CRC umspannt nicht das `0x2F`‑Padding.
- Temperaturen werden nur in Long‑Frames (~täglich) gesendet; in Compact‑Frames bleibt `Flow.Temperature` / `Ambient.Temperature` bei `0`.
- Settings (AES‑Key, Meter‑ID) liegen im Tasmota‑Settings‑Block; nach `Reset 5/6` müssen sie neu gesetzt werden.

---

## Lizenz & Mitwirken

Dieser Fork bleibt unter der originalen Tasmota‑Lizenz (GPLv3). Eigene Treiber‑Dateien stehen unter **Apache‑2.0** (siehe Kopf der jeweiligen `.ino`). Pull Requests willkommen – bitte vorher gegen `tasmota32s3-heltec-wmbus` bauen und ein `M21Info`‑Log beilegen.
