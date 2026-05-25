# Adaptive Wireless Mesh Communication System for Disaster Management

> **Real-time, internet-independent sensor network for disaster response.**  
> Built on ESP32 + LoRa. No router. No cloud. No single point of failure.

[![Firmware](https://img.shields.io/badge/firmware-Arduino%20%2F%20ESP32-blue)]()
[![Dashboard](https://img.shields.io/badge/dashboard-Streamlit-red)]()
[![Protocol](https://img.shields.io/badge/protocol-ESP--NOW%20%2B%20LoRa-green)]()
[![Version](https://img.shields.io/badge/version-2.0.0-orange)]()

---

## What it does

When a disaster strikes — earthquake, gas leak, flood — conventional infrastructure fails first. This system operates without internet, without a central server, and without any existing network. Sensors detect hazards, alerts propagate across a self-healing mesh, and a live dashboard visualises everything over USB/LoRa.

**System overview:**

```
  [Node C]  ──ESP-NOW──>  [Node A]  ──ESP-NOW──>  [Node D (LoRa GW)]
 Sensor node             Relay node              Long-range gateway
 Vibration                                            │
 Gas (MQ-2)                                      USB Serial
 Flood                                               │
                                             [Dashboard PC]
                                          Streamlit web interface
```

---

## Architecture

### Communication layers

| Layer | Protocol | Range | Use |
|-------|----------|-------|-----|
| Short-range mesh | ESP-NOW (Wi-Fi 2.4GHz) | ~200m LOS | Node-to-node relay |
| Long-range backbone | LoRa 433/868/915 MHz | ~2km LOS | Gateway to base station |
| Dashboard bridge | USB Serial @ 115200 baud | — | Node D → PC |

### Node roles

| Node | Role | Sensors | Hardware |
|------|------|---------|----------|
| **A** | Mesh relay + network monitor | — | ESP32 dev board |
| **C** | Multi-sensor alert node | Vibration (SW-420), Gas (MQ-2), Flood (analog) | ESP32 + sensors |
| **D** | LoRa long-range gateway | — | ESP32 + LoRa SX1278 |

### Message format (JSON over ESP-NOW / LoRa)

All messages use a structured JSON schema. Example alert:

```json
{
  "node":     "C",
  "type":     "ALERT",
  "ts":       12345,
  "ver":      "2.0.0",
  "sensor":   "GAS",
  "value":    3200,
  "severity": "CRITICAL",
  "desc":     "Gas concentration elevated — ADC=3200 (baseline=450)",
  "uptime":   60000
}
```

Example heartbeat:

```json
{
  "node":       "A",
  "type":       "HEARTBEAT",
  "ts":         12345,
  "uptime_ms":  60000,
  "tx_ok":      24,
  "tx_fail":    1,
  "peers": { "A": "ONLINE", "B": "UNKNOWN", "C": "ONLINE" }
}
```

---

## Hardware

### Bill of materials

| Component | Qty | ~Cost (INR) | Notes |
|-----------|-----|-------------|-------|
| ESP32 Dev Board (38-pin) | 3 | ₹350 each | Nodes A, C, D |
| LoRa SX1278 module (433MHz) | 1 | ₹350 | Node D only |
| SW-420 Vibration Sensor | 1 | ₹30 | Node C |
| MQ-2 Gas Sensor | 1 | ₹120 | Node C |
| Analog Water Level Sensor | 1 | ₹50 | Node C |
| Micro-USB cables | 3 | ₹80 each | Power + flashing |
| Breadboard + jumper wires | — | ₹100 | |
| **Total** | | **~₹1,400** | |

### Node C wiring

```
ESP32 GPIO 4  → SW-420 DO   (vibration, digital)
ESP32 GPIO 34 → MQ-2 AO    (gas, analog — use ADC1 only)
ESP32 GPIO 35 → Water AO   (flood, analog — use ADC1 only)
ESP32 3.3V    → Sensor VCC
ESP32 GND     → Sensor GND
```

### Node D (LoRa) wiring

```
ESP32 GPIO 18 → LoRa SCK
ESP32 GPIO 19 → LoRa MISO
ESP32 GPIO 23 → LoRa MOSI
ESP32 GPIO 5  → LoRa NSS
ESP32 GPIO 14 → LoRa RST
ESP32 GPIO 2  → LoRa DIO0
ESP32 3.3V    → LoRa VCC   (NOT 5V — module is 3.3V only)
ESP32 GND     → LoRa GND
```

---

## Setup and flashing

### 1. Install Arduino IDE dependencies

Open Arduino IDE → Library Manager → install:

- `ArduinoJson` by Benoit Blanchon (v6.x)
- `LoRa` by Sandeep Mistry

### 2. Configure each node

All configuration lives in `firmware/config.h`. Only one file to edit:

```cpp
// Set this before flashing each board:
#define NODE_NAME  "A"   // Change to "C" for sensor node, "D" uses separate firmware
```

Thresholds, timing, and LoRa parameters are all in `config.h` — no digging through code.

### 3. Flash order

1. Flash **Node D** first (LoRa gateway) — connect via USB, open Serial Monitor to verify boot
2. Flash **Node A** (relay) — verify it receives heartbeats from D
3. Flash **Node C** (sensors) — verify alerts appear on Node A's serial output

### 4. Run the dashboard

```bash
cd dashboard
pip install -r requirements.txt

# With hardware (Node D connected via USB):
streamlit run dashboard.py

# Without hardware (simulation mode):
streamlit run dashboard.py -- --simulate

# Specify serial port manually:
streamlit run dashboard.py -- --port /dev/ttyUSB0
```

Dashboard opens at `http://localhost:8501`

---

## Dashboard features

- **Live node status** — online/offline with last-seen timestamp per node
- **Alert log** — real-time alert feed with severity, sensor type, RSSI, timestamp
- **RSSI chart** — signal strength over time with weak-signal threshold lines
- **SNR gauge** — link quality indicator
- **Sensor readings** — live gas and flood ADC values with threshold markers
- **Network metrics** — TX success rate, packet counts, parse errors
- **Simulation mode** — runs without hardware for demos and testing

---

## Key engineering decisions

**Why ESP-NOW over Wi-Fi?**  
Wi-Fi requires a router. In disaster zones, routers fail. ESP-NOW is a peer-to-peer protocol built into the ESP32 — zero infrastructure dependency, ~1ms latency, 250-byte payload.

**Why JSON payloads?**  
Raw string messages (v1.0) made the dashboard impossible to build reliably. JSON gives a typed schema, extensibility, and makes every node's output parseable by any tool.

**Why per-sensor cooldowns?**  
A vibration sensor triggers continuously during an earthquake. Without a cooldown, the mesh floods with hundreds of duplicate alerts per second, saturating the channel. 3-second cooldown per sensor prevents this without missing real events.

**Why heartbeats?**  
Silent nodes are ambiguous — did the node go offline, or is it just not detecting hazards? Heartbeats resolve this: if a node misses 3 heartbeats (15s), it is declared OFFLINE and the dashboard logs the event.

**Why severity classification?**  
Not all gas readings are equally dangerous. A reading 300 ADC units above baseline is MEDIUM. 1500 above is CRITICAL. Severity lets response teams triage without reading raw ADC values.

---

## Project structure

```
.
├── firmware/
│   ├── config.h          # All configuration — edit this only
│   ├── Node_A/
│   │   └── Node_A.ino    # Relay node + network health monitor
│   ├── Node_C/
│   │   └── Node_C.ino    # Multi-sensor alert node
│   └── Node_D/
│       └── Node_D.ino    # LoRa long-range gateway
├── dashboard/
│   ├── dashboard.py      # Streamlit dashboard
│   └── requirements.txt
└── README.md
```

---

## What I would add next

- **Encryption** — ESP-NOW supports AES-128; adding it prevents packet spoofing in adversarial environments
- **OLED display per node** — local status without needing a laptop
- **SD card logging on Node D** — persist alert history across power cycles
- **GPS tagging** — attach coordinates to alerts for map-based dashboard view
- **Two-way ACK** — gateway acknowledges critical alerts; unacknowledged alerts retry

---

## Author

**Praghna M.K**  
Electronics and Communication Engineering, GSSSIETW  
[linkedin.com/in/praghnamk](https://linkedin.com/in/praghnamk) · [github.com/praghnaMK](https://github.com/praghnaMK)
