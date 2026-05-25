// ============================================================
//  config.h  —  Shared configuration for all mesh nodes
//  Adaptive Wireless Mesh Communication System
//  Edit ONLY this file when deploying to a new node.
// ============================================================

#ifndef CONFIG_H
#define CONFIG_H

// ── Node Identity ───────────────────────────────────────────
// Set this to "A", "B", or "C" before flashing each board.
// Node D is LoRa gateway — uses Node_D firmware separately.
#define NODE_NAME        "A"
#define NODE_VERSION     "2.0.0"

// ── ESP-NOW / Wi-Fi ─────────────────────────────────────────
#define WIFI_CHANNEL     1          // All mesh nodes must share the same channel
#define TX_POWER         WIFI_POWER_19_5dBm

// ── Timing ──────────────────────────────────────────────────
#define HEARTBEAT_INTERVAL_MS     5000   // Send heartbeat every 5 seconds
#define ALERT_COOLDOWN_MS         3000   // Minimum gap between same-type alerts
#define NODE_OFFLINE_TIMEOUT_MS  15000   // Node marked OFFLINE after 15s silence
#define MAX_RETRY_COUNT               3  // Retransmit failed packets up to 3 times
#define RETRY_DELAY_MS              200  // Wait 200ms between retries

// ── Sensors (Node C) ─────────────────────────────────────────
#define VIB_PIN          4    // SW-420 vibration sensor → GPIO 4
#define GAS_PIN         34    // MQ-2 gas sensor analog → GPIO 34
#define FLOOD_PIN       35    // Water level sensor analog → GPIO 35
#define GAS_THRESHOLD  2000   // ADC units (0–4095) above which gas alert fires
#define FLOOD_THRESHOLD 1800  // ADC units above which flood alert fires

// ── LoRa (Node D gateway) ───────────────────────────────────
#define LORA_SCK        18
#define LORA_MISO       19
#define LORA_MOSI       23
#define LORA_SS          5
#define LORA_RST        14
#define LORA_DIO0        2
#define LORA_FREQ       433E6   // Match your module: 433E6 / 868E6 / 915E6
#define LORA_SF         12      // Spreading factor — higher = longer range, slower
#define LORA_BW         125E3   // Bandwidth
#define LORA_CR          5      // Coding rate 4/5
#define LORA_SYNC_WORD  0x34    // Private network sync word

// ── LED ──────────────────────────────────────────────────────
#define LED_PIN          2    // Onboard LED on most ESP32 dev boards

// ── Serial ───────────────────────────────────────────────────
#define SERIAL_BAUD    115200

// ── Message Types ────────────────────────────────────────────
#define MSG_HEARTBEAT  "HEARTBEAT"
#define MSG_ALERT      "ALERT"
#define MSG_ACK        "ACK"
#define MSG_STATUS     "STATUS"

// ── Alert Subtypes ───────────────────────────────────────────
#define ALERT_VIBRATION  "VIBRATION"
#define ALERT_GAS        "GAS"
#define ALERT_FLOOD      "FLOOD"

#endif // CONFIG_H
