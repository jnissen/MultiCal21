# Kamstrup Multical21 / FlowIQ 2200 – Tasmota Integration

## Übersicht

Dieser Tasmota-Treiber (`xsns_121_multical21.ino`) liest drahtlos den Kamstrup **Multical21** oder **FlowIQ 2200** Wasserzähler über das wM-Bus-Protokoll (C1, 868 MHz) aus. Als Funkempfänger dient ein **TI CC1101** Sub-GHz-Transceiver am Hardware-SPI.

---

## Hardware

### Benötigte Bauteile

| Bauteil | Beschreibung |
|---|---|
| ESP32 / ESP8266 | z. B. ESP32-C3 Super Mini, Wemos D1 Mini |
| CC1101 868 MHz Modul | z. B. von eBay/AliExpress mit Antenne |

### Verdrahtung

| CC1101 Pin | Tasmota GPIO-Funktion |
|---|---|
| VCC | 3V3 |
| GND | GND |
| CSN | SPI CS |
| MOSI | SPI MOSI |
| MISO | SPI MISO |
| SCK | SPI CLK |
| GDO0 | CC1101 GDO0 (Interrupt, beliebiger freier GPIO) |
| GDO2 | nicht verbunden |

**Beispiel ESP32-C3 Super Mini:**
CSN=GPIO7, MOSI=GPIO6, MISO=GPIO5, SCK=GPIO4, GDO0=GPIO10.

---

## Build / Firmware

### Voraussetzungen

- PlatformIO mit dem Tasmota-Fork
- Build-Variante: `tasmota-sensors` (oder eigene mit dem Define)

### Aktivierung

In `tasmota/user_config_override.h`:

```c
#ifdef USE_MULTICAL21
  #undef USE_MULTICAL21
#endif
#define USE_MULTICAL21
```

Oder als Build-Flag in `platformio_override.ini`:

```ini
build_flags = -DUSE_MULTICAL21
```

### Kompilieren & Flashen

```bash
pio run --target upload
```

---

## Konfiguration (Tasmota-Konsole)

Nach dem Flashen in der Tasmota-Konsole (Web-UI, MQTT oder Serial):

| Befehl | Argument | Beschreibung |
|---|---|---|
| `M21Key` | `<32 Hex-Zeichen>` | AES-128 Schlüssel (vom Wasserversorger erfragen) |
| `M21Id` | `<8 Hex-Zeichen>` | Seriennummer des Zählers (aufgedruckt) |
| `M21Type` | `0` oder `1` | `0` = Multical21 (Default), `1` = FlowIQ 2200 |
| `M21Period` | `<Sekunden>` | Mindestabstand Telemetrie-Pushes, `0` = nur TelePeriod (Default) |
| `M21Info` | – | Zeigt Treiber-Status |

Ohne Argument zeigt jeder Setter den aktuellen (maskierten) Wert an.

**Hinweis:** Die Konfiguration wird im Filesystem persistiert (Mem1..Mem4) — kein Re-Flash nötig.

---

## MQTT-Telemetrie

### Sensor-Topic

```
tele/<topic>/SENSOR
```

### JSON-Payload

```json
{
  "Time": "2026-05-19T07:00:00",
  "Volume": {"Value": 2584.117},
  "VolumeTarget": {"Value": 2564.163},
  "FlowPerMin": {"Value": 3.2},
  "RSSI": {"Value": -76},
  "Flow": {"Temperature": 12},
  "Ambient": {"Temperature": 22}
}
```

| JSON-Key | Beschreibung | Einheit |
|---|---|---|
| `Volume.Value` | Aktueller Zählerstand | m³ |
| `VolumeTarget.Value` | Stichtagszählerstand | m³ |
| `FlowPerMin.Value` | Durchfluss letzte 60 Sekunden | Liter |
| `RSSI.Value` | Empfangsstärke wM-Bus | dBm |
| `Flow.Temperature` | Wassertemperatur (Vorlauf) | °C |
| `Ambient.Temperature` | Umgebungstemperatur am Zähler | °C |

---

## Home Assistant Integration

### Automatische Discovery

Die Tasmota-Integration erkennt das Gerät automatisch über Tasmota-Discovery (`SetOption19 0`).

### Resultierende Entitäten

| Entity ID (Beispiel) | Beschreibung | Einheit |
|---|---|---|
| `sensor.w100_volume_value` | Zählerstand | – (customize: m³) |
| `sensor.w100_volumetarget_value` | Stichtag | – (customize: m³) |
| `sensor.w100_flowpermin_value` | Durchfluss/min | – (customize: L) |
| `sensor.w100_rssi_value` | wM-Bus Empfang | – (customize: dBm) |
| `sensor.w100_flow_temperature` | Vorlauftemperatur | °C (auto) |
| `sensor.w100_ambient_temperature` | Umgebungstemperatur | °C (auto) |

### Template-Sensor für Energie-Dashboard (Wasser)

In `configuration.yaml`:

```yaml
template:
  - sensor:
      - name: "Wasserzähler Total"
        unique_id: multical21_water_total
        state: "{{ states('sensor.w100_volume_value') | float(0) }}"
        unit_of_measurement: "m³"
        device_class: water
        state_class: total_increasing

      - name: "Wasserzähler Stichtag"
        unique_id: multical21_water_target
        state: "{{ states('sensor.w100_volumetarget_value') | float(0) }}"
        unit_of_measurement: "m³"
        device_class: water
```

Damit kann `sensor.wasserzahler_total` direkt als **Wasserquelle** im Energie-Dashboard eingebunden werden.

### Discovery neu auslösen (nach Firmware-Update)

```
SetOption19 1
SetOption19 0
Restart 1
```

Danach in HA: Einstellungen → Geräte & Dienste → Tasmota → ⋮ → „Neu laden".

---

## Web-UI (Tasmota)

Auf der Tasmota-Startseite werden folgende Werte angezeigt:

| Anzeige | Wert |
|---|---|
| Volume | Zählerstand in m³ |
| Volume Target | Stichtagsvolumen in m³ |
| Flow/min | Durchfluss letzte Minute in L |
| Flow Temperatur | Vorlauftemperatur in °C |
| Ambient Temperatur | Umgebungstemperatur in °C |
| RSSI | Empfangsstärke in dBm |
| Frames | Gültige / Gesamtframes |

Die Werte werden sofort nach Boot angezeigt (mit 0), auch bevor der erste wM-Bus-Frame empfangen wurde.

---

## Funktionsweise

1. Der CC1101 wird im wM-Bus Mode C1 (868,95 MHz) initialisiert.
2. Alle ~16 Sekunden sendet der Multical21 ein verschlüsseltes Telegramm.
3. Der Treiber dekodiert das wM-Bus-Frame, prüft CRC und entschlüsselt mit AES-128.
4. Zählerstand, Stichtag, Temperaturen und Status werden extrahiert.
5. Ein 60-Sekunden-Ringpuffer berechnet den Minutendurchfluss (Differenz × 1000 = Liter).
6. Die Werte werden per MQTT publiziert und auf der Web-UI angezeigt.

---

## Quellen

- [pthalin/esp32-multical21](https://github.com/pthalin/esp32-multical21) (GPL-3.0)
- Original: chester4444@wolke7.net
