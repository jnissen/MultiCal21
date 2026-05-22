#!/usr/bin/env python3
"""
Generate Tasmota Template JSON for Heltec WiFi LoRa 32 V3 with SX1262 (wM-Bus).
Parses tasmota/include/tasmota_template.h to resolve GPIO enum values, applies
ESP32-S3 conditional compilation rules, and emits the AGPIO-encoded template.

AGPIO(x) = x << 5  (cf. tasmota_globals.h)
"""
import os, re, json, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE_H = os.path.join(ROOT, "tasmota", "include", "tasmota_template.h")

# ---- 1) Extract the gpio_function enum (everything between "enum ... { GPIO_NONE" and "GPIO_SENSOR_END" or the closing brace) ----
src = open(TEMPLATE_H, encoding="utf-8", errors="replace").read()
m = re.search(r"enum\s+\w+\s*\{\s*(GPIO_NONE.*?)\}\s*;", src, re.DOTALL)
if not m:
    sys.exit("Could not locate GPIO enum block")
block = m.group(1)

# Active defines we care about for ESP32-S3 (positive set; everything not listed is treated as undefined)
DEFINES = {"ESP32", "ESP32S3", "USE_WMBUS_RADIO", "USE_SPI", "USE_I2C"}

# ---- 2) Walk lines, tracking #ifdef stack, collect enum tokens in order ----
def stack_active(stack):
    return all(s for s in stack)

tokens = []
ifstack = []
for raw in block.splitlines():
    line = raw.strip()
    s = line
    if s.startswith("#ifdef"):
        name = s.split()[1] if len(s.split()) > 1 else ""
        ifstack.append(name in DEFINES)
        continue
    if s.startswith("#ifndef"):
        name = s.split()[1] if len(s.split()) > 1 else ""
        ifstack.append(name not in DEFINES)
        continue
    if s.startswith("#if "):
        # naive: assume false for any complex #if (no Heltec pins inside such blocks)
        ifstack.append(False)
        continue
    if s.startswith("#else"):
        if ifstack:
            ifstack[-1] = not ifstack[-1]
        continue
    if s.startswith("#elif"):
        if ifstack:
            ifstack[-1] = False
        continue
    if s.startswith("#endif"):
        if ifstack:
            ifstack.pop()
        continue
    if not stack_active(ifstack):
        continue
    # strip comments
    line = re.sub(r"//.*$", "", line)
    line = re.sub(r"/\*.*?\*/", "", line)
    for tok in line.replace(",", " ").split():
        if re.fullmatch(r"GPIO_[A-Z0-9_]+", tok):
            tokens.append(tok)

# Deduplicate while preserving order (enum members appear once)
seen = set()
ordered = []
for t in tokens:
    if t not in seen:
        seen.add(t)
        ordered.append(t)

idx = {name: i for i, name in enumerate(ordered)}

def agpio(name, opt=0):
    if name not in idx:
        sys.exit(f"GPIO symbol not found in enum: {name}")
    return (idx[name] << 5) | (opt & 0x1F)

# ---- 3) Heltec WiFi LoRa 32 V3 pin map ----
# Reference: official Heltec V3 schematic (rev 2023+)
# https://resource.heltec.cn/download/WiFi_LoRa_32_V3/HTIT-WB32LA(F)_V3_Schematic_Diagram.pdf
HELTEC_V3 = {
    0:  ("GPIO_KEY1",        0),  # PRG button (active low)
    1:  ("GPIO_ADC_INPUT",   0),  # VBAT measurement (via VBAT_CTRL/GPIO37)
    8:  ("GPIO_LORA_CS",     0),
    9:  ("GPIO_SPI_CLK",     0),
    10: ("GPIO_SPI_MOSI",    0),
    11: ("GPIO_SPI_MISO",    0),
    12: ("GPIO_LORA_RST",    0),
    13: ("GPIO_LORA_BUSY",   0),
    14: ("GPIO_LORA_DI1",    0),
    17: ("GPIO_I2C_SDA",     0),  # OLED SDA (Vext-powered)
    18: ("GPIO_I2C_SCL",     0),  # OLED SCL
    21: ("GPIO_OUTPUT_HI",   0),  # OLED reset – idle high
    35: ("GPIO_LEDLNK",      0),  # white user LED (active high)
    36: ("GPIO_OUTPUT_LO",   0),  # Vext power enable, active LOW = ON (OLED + LoRa periphery)
}

# ESP32-S3 has GPIO0..GPIO48 → Template GPIO array size = 49 (FLAG byte separate)
GPIO_COUNT = 49
arr = [0] * GPIO_COUNT
for pin, (sym, opt) in HELTEC_V3.items():
    arr[pin] = agpio(sym, opt)

# BASE 1 = ESP32 Generic. For ESP32-S3 boards Tasmota expects BASE=1; the variant is selected via the build.
template = {
    "NAME": "Heltec WiFi LoRa 32 V3 wMBus",
    "GPIO": arr,
    "FLAG": 0,
    "BASE": 1,
}

print(json.dumps(template, separators=(",", ":")))
sys.stderr.write("\nResolved AGPIO codes:\n")
for pin in sorted(HELTEC_V3):
    sym, opt = HELTEC_V3[pin]
    sys.stderr.write(f"  GPIO{pin:>2} = {sym:<16} -> AGPIO {arr[pin]} (enum idx {idx[sym]})\n")
