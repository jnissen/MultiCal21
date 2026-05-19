# Kamstrup Multical21 / FlowIQ 2200 – Tasmota Integration

## Overview

This Tasmota driver (`xsns_121_multical21.ino`) wirelessly reads the Kamstrup **Multical21** or **FlowIQ 2200** water meter via the wM-Bus protocol (C1, 868 MHz). A **TI CC1101** sub-GHz transceiver on hardware SPI serves as the radio receiver.

---

## Hardware

### Required Components

| Component | Description |
|---|---|
| ESP32 / ESP8266 | e.g. ESP32-C3 Super Mini, Wemos D1 Mini |
| CC1101 868 MHz module | e.g. from eBay/AliExpress with antenna |

### Wiring

| CC1101 Pin | Tasmota GPIO Function |
|---|---|
| VCC | 3V3 |
| GND | GND |
| CSN | SPI CS |
| MOSI | SPI MOSI |
| MISO | SPI MISO |
| SCK | SPI CLK |
| GDO0 | CC1101 GDO0 (interrupt, any free GPIO) |
| GDO2 | not connected |

**Example ESP32-C3 Super Mini:**
CSN=GPIO7, MOSI=GPIO6, MISO=GPIO5, SCK=GPIO4, GDO0=GPIO10.

---

## Build / Firmware

### Prerequisites

- PlatformIO with the Tasmota fork
- Build variant: `tasmota-sensors` (or custom with the define)

### Activation

In `tasmota/user_config_override.h`:

```c
#ifdef USE_MULTICAL21
  #undef USE_MULTICAL21
#endif
#define USE_MULTICAL21
```

Or as a build flag in `platformio_override.ini`:

```ini
build_flags = -DUSE_MULTICAL21
```

### Compile & Flash

```bash
pio run --target upload
```

---

## Configuration (Tasmota Console)

After flashing, use the Tasmota console (Web UI, MQTT, or Serial):

| Command | Argument | Description |
|---|---|---|
| `M21Key` | `<32 hex chars>` | AES-128 decryption key (request from your water utility) |
| `M21Id` | `<8 hex chars>` | Meter serial number (printed on the meter) |
| `M21Type` | `0` or `1` | `0` = Multical21 (default), `1` = FlowIQ 2200 |
| `M21Period` | `<seconds>` | Minimum telemetry push interval, `0` = TelePeriod only (default) |
| `M21Info` | – | Show driver status |

Issued without arguments, any setter prints the current (masked) value.

**Note:** Configuration is persisted in the filesystem (Mem1..Mem4) — no re-flash required.

---

## MQTT Telemetry

### Sensor Topic

```
tele/<topic>/SENSOR
```

### JSON Payload

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

| JSON Key | Description | Unit |
|---|---|---|
| `Volume.Value` | Current meter reading | m³ |
| `VolumeTarget.Value` | Target date meter reading | m³ |
| `FlowPerMin.Value` | Flow in the last 60 seconds | Liters |
| `RSSI.Value` | wM-Bus signal strength | dBm |
| `Flow.Temperature` | Water temperature (flow) | °C |
| `Ambient.Temperature` | Ambient temperature at the meter | °C |

---

## Home Assistant Integration

### Automatic Discovery

The Tasmota integration detects the device automatically via Tasmota Discovery (`SetOption19 0`).

### Resulting Entities

| Entity ID (example) | Description | Unit |
|---|---|---|
| `sensor.w100_volume_value` | Meter reading | – (customize: m³) |
| `sensor.w100_volumetarget_value` | Target date reading | – (customize: m³) |
| `sensor.w100_flowpermin_value` | Flow/min | – (customize: L) |
| `sensor.w100_rssi_value` | wM-Bus signal | – (customize: dBm) |
| `sensor.w100_flow_temperature` | Flow temperature | °C (auto) |
| `sensor.w100_ambient_temperature` | Ambient temperature | °C (auto) |

### Template Sensor for Energy Dashboard (Water)

In `configuration.yaml`:

```yaml
template:
  - sensor:
      - name: "Water Meter Total"
        unique_id: multical21_water_total
        state: "{{ states('sensor.w100_volume_value') | float(0) }}"
        unit_of_measurement: "m³"
        device_class: water
        state_class: total_increasing

      - name: "Water Meter Target"
        unique_id: multical21_water_target
        state: "{{ states('sensor.w100_volumetarget_value') | float(0) }}"
        unit_of_measurement: "m³"
        device_class: water
```

This allows `sensor.water_meter_total` to be used directly as a **water source** in the Energy Dashboard.

### Re-trigger Discovery (after firmware update)

```
SetOption19 1
SetOption19 0
Restart 1
```

Then in HA: Settings → Devices & Services → Tasmota → ⋮ → "Reload".

---

## Web UI (Tasmota)

The following values are shown on the Tasmota main page:

| Display | Value |
|---|---|
| Volume | Meter reading in m³ |
| Volume Target | Target date volume in m³ |
| Flow/min | Flow last minute in L |
| Flow Temperatur | Flow temperature in °C |
| Ambient Temperatur | Ambient temperature in °C |
| RSSI | Signal strength in dBm |
| Frames | Valid / Total frames |

Values are displayed immediately after boot (showing 0) even before the first wM-Bus frame has been received.

---

## How It Works

1. The CC1101 is initialised in wM-Bus Mode C1 (868.95 MHz).
2. Approximately every 16 seconds, the Multical21 transmits an encrypted telegram.
3. The driver decodes the wM-Bus frame, verifies CRC, and decrypts with AES-128.
4. Meter reading, target volume, temperatures, and status are extracted.
5. A 60-second ring buffer calculates the per-minute flow (difference × 1000 = liters).
6. Values are published via MQTT and displayed on the Web UI.

---

## Sources

- [pthalin/esp32-multical21](https://github.com/pthalin/esp32-multical21) (GPL-3.0)
- Original code: chester4444@wolke7.net

---

## Setup Guide (Step by Step)

### 1. Initial Connection via Captive Hotspot

On first boot (or when no known WiFi network is reachable), Tasmota automatically opens its own WiFi hotspot:

| Property | Value |
|---|---|
| SSID | `tasmota-XXXXXX` (XXXXXX = part of the MAC address) |
| Password | none (open) |
| Device IP | `192.168.4.1` |

**Steps:**

1. Connect your PC/phone to the WiFi network `tasmota-XXXXXX`.
2. A **captive portal** (login page) usually opens automatically. If not: open a browser and navigate to `http://192.168.4.1`.
3. On the page, select your home WiFi network:
   - **AP1 SSId** → Enter your home WiFi name (or select from the list)
   - **AP1 Password** → Enter your WiFi password
   - Optional: configure a second WiFi as fallback under AP2
4. Click **Save** → the device restarts and connects to your home WiFi.
5. Find the new IP address in your router (DHCP lease list) or read it from the serial monitor.
6. Open `http://<new-IP>` in a browser → the Tasmota Web UI is accessible.

> **Tip:** If the connection fails (wrong password, etc.), Tasmota will re-open the hotspot after ~60 seconds. Reconnect and correct the settings.

### 2. Import Template

In the Tasmota console (or via Web UI → Configuration → Configure Other → Template):

```
{"NAME":"Multical 21","ARCH":"ESP8266","GPIO":[0,0,0,0,4544,0,0,0,672,704,736,768,0,0],"FLAG":0,"BASE":18}
```

This template automatically configures the SPI pins and the CC1101 GDO0 interrupt.

Then activate **Module 0 (Template)**:

```
Module 0
```

The device will restart.

### 3. Configure WiFi & MQTT

If not already done, via the Tasmota Web UI:

1. **Configuration → Configure WiFi** → Enter SSID and password
2. **Configuration → Configure MQTT** → Broker IP, port, user, password, topic

### 4. Configure the Meter

Set all meter parameters in a single command:

```
Backlog M21Key <YOUR-32-CHAR-HEX-KEY>; M21Id <YOUR-8-CHAR-METER-ID>; M21Type 0; M21Period 30; Restart 1
```

**Replace the placeholders:**

| Parameter | Description | Example Format |
|---|---|---|
| `<YOUR-32-CHAR-HEX-KEY>` | AES-128 key (request from your water utility) | `0123456789ABCDEF0123456789ABCDEF` |
| `<YOUR-8-CHAR-METER-ID>` | Serial number (printed on the meter) | `12345678` |

**Parameter explanation:**

| Parameter | Value | Meaning |
|---|---|---|
| `M21Key` | 32 hex chars | AES key for decrypting wM-Bus frames |
| `M21Id` | 8 hex chars | Meter serial number (filters only this meter) |
| `M21Type` | `0` | Meter type: `0` = Multical21, `1` = FlowIQ 2200 |
| `M21Period` | `30` | Publish telemetry every 30 seconds |
| `Restart 1` | – | Restart to apply all settings |

### 5. Verify Operation

After restart, in the console:

```
M21Info
```

Expected output:

```json
{"M21Info":{"Hw":"ok","Configured":"ok","Id":"12345678","Type":0,"Period":30,"Frames":1,"Valid":1,"Rssi":-75}}
```

| Field | Expected Value | Meaning |
|---|---|---|
| `Hw` | `ok` | CC1101 hardware detected |
| `Configured` | `ok` | Key and ID are set |
| `Frames` | > 0 | Received frames (increases every ~16 sec) |
| `Valid` | > 0 | Successfully decrypted frames |
| `Rssi` | -50 to -90 | Signal strength (closer to 0 = better) |

### 6. Check Tasmota Web UI

On the device's main page (http://<IP>) the following values should appear:

- **Volume** — Current meter reading in m³
- **Volume Target** — Target date volume in m³
- **Flow/min** — Flow of the last minute in liters
- **Flow Temperatur** — Water temperature in °C
- **Ambient Temperatur** — Ambient temperature in °C
- **RSSI** — Signal strength in dBm
- **Frames** — Valid / Total frames

### 7. Connect to Home Assistant

Prerequisites: Tasmota integration installed in HA and MQTT connected.

1. Ensure discovery is active:
   ```
   SetOption19 0
   ```

2. In HA: **Settings → Devices & Services → Tasmota** — the device should appear automatically.

3. Optional: Create a template sensor for the Energy Dashboard (see section above).

### 8. Troubleshooting

| Problem | Solution |
|---|---|
| `Hw: fail` | Check SPI pins, re-import template, verify wiring |
| `Configured: no` | Set `M21Key` and `M21Id` again |
| Frames received but `Valid: 0` | Wrong key → request the correct one from your water utility |
| No frames | Meter out of range, check antenna on CC1101 |
| HA shows no sensors | `SetOption19 1` → `SetOption19 0` → `Restart 1`, then reload HA integration |

---

## Changed Files (compared to upstream)

| File | Description |
|---|---|
| `tasmota/tasmota_xsns_sensor/xsns_121_multical21.ino` | Driver: JSON restructure for HA discovery, flow/min sensor, Web UI adjustments |
| `tasmota/user_config_override.h` | `HOME_ASSISTANT_DISCOVERY_ENABLE false` (SetOption19 0 as default) |
| `platformio_override.ini` | Build variant set to `tasmota-sensors` |
| `MULTICAL21.md` | Documentation (German) |
| `MULTICAL21_EN.md` | Documentation (English) |
