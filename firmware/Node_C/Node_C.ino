// ============================================================
//  Node_C.ino  —  Multi-Sensor Alert Node
//  Adaptive Wireless Mesh Communication System v2.0
//
//  Role: Reads vibration, gas, and flood sensors. Sends
//        structured JSON alerts over ESP-NOW mesh when
//        thresholds are breached. Includes per-sensor
//        cooldown, severity classification, and ACK tracking.
//
//  Sensors wired:
//    SW-420 vibration  → GPIO 4  (digital)
//    MQ-2  gas sensor  → GPIO 34 (analog, ADC1)
//    Water level       → GPIO 35 (analog, ADC1)
//
//  Libraries required:
//    - ArduinoJson  by Benoit Blanchon  (v6.x)
// ============================================================

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include "config.h"

// ── Override node name for this file ─────────────────────────
#undef  NODE_NAME
#define NODE_NAME "C"

// ── Broadcast address ────────────────────────────────────────
static const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ── Sensor state ─────────────────────────────────────────────
struct SensorState {
  // Vibration
  unsigned long lastVibAlert  = 0;
  uint32_t      vibCount      = 0;   // Total vibration events detected

  // Gas
  unsigned long lastGasAlert  = 0;
  int           gasBaseline   = 0;   // Calibrated at boot
  uint32_t      gasAlerts     = 0;

  // Flood
  unsigned long lastFloodAlert = 0;
  uint32_t      floodAlerts    = 0;
};

// ── Node state ────────────────────────────────────────────────
struct NodeState {
  unsigned long lastHeartbeat = 0;
  unsigned long bootTime      = 0;
  uint32_t      packetsSent   = 0;
  uint32_t      sendFailures  = 0;
  uint32_t      packetsRx     = 0;
};

static SensorState sensors;
static NodeState   state;
static esp_now_peer_info_t broadcastPeer;

// ── Forward declarations ──────────────────────────────────────
void    sendAlert(const char* sensor, float value, const char* severity, const char* desc);
void    sendHeartbeat();
void    sendJson(const String& type, JsonDocument& doc);
void    calibrateGas();
const char* classifySeverity(const char* sensorType, float value);
void    blinkLED(int times, int onMs, int offMs);

// ─────────────────────────────────────────────────────────────
//  ESP-NOW callbacks
// ─────────────────────────────────────────────────────────────

void IRAM_ATTR onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) state.sendFailures++;
}

void onDataRecv(const esp_now_recv_info* info, const uint8_t* data, int len) {
  state.packetsRx++;

  char buf[250];
  int  copyLen = min(len, 249);
  memcpy(buf, data, copyLen);
  buf[copyLen] = '\0';

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, buf) != DeserializationError::Ok) return;

  const char* type = doc["type"] | "";

  // On receiving an ACK addressed to us, log it
  if (strcmp(type, MSG_ACK) == 0 && doc["to"] == NODE_NAME) {
    Serial.printf("[ACK ] Delivery confirmed by Node %s\n",
                  (const char*)doc["node"]);
    blinkLED(1, 50, 0);
  }
}

// ─────────────────────────────────────────────────────────────
//  Gas sensor calibration (runs at boot)
// ─────────────────────────────────────────────────────────────

void calibrateGas() {
  Serial.print("[INFO] Calibrating gas sensor");
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(GAS_PIN);
    delay(50);
    if (i % 5 == 0) Serial.print(".");
  }
  sensors.gasBaseline = sum / 20;
  Serial.printf("\n[INFO] Gas baseline = %d ADC units\n", sensors.gasBaseline);
}

// ─────────────────────────────────────────────────────────────
//  Severity classifier
// ─────────────────────────────────────────────────────────────

const char* classifySeverity(const char* sensorType, float value) {
  if (strcmp(sensorType, ALERT_GAS) == 0) {
    if (value > sensors.gasBaseline + 1500) return "CRITICAL";
    if (value > sensors.gasBaseline + 800)  return "HIGH";
    return "MEDIUM";
  }
  if (strcmp(sensorType, ALERT_FLOOD) == 0) {
    if (value > 3500) return "CRITICAL";
    if (value > 2500) return "HIGH";
    return "MEDIUM";
  }
  // Vibration is binary — digital sensor
  return "HIGH";
}

// ─────────────────────────────────────────────────────────────
//  JSON send helper with retry
// ─────────────────────────────────────────────────────────────

void sendJson(const String& type, JsonDocument& doc) {
  doc["node"] = NODE_NAME;
  doc["type"] = type;
  doc["ts"]   = millis();
  doc["ver"]  = NODE_VERSION;

  char buf[300];
  size_t len = serializeJson(doc, buf);

  for (int attempt = 1; attempt <= MAX_RETRY_COUNT; attempt++) {
    esp_err_t result = esp_now_send(BROADCAST_MAC, (const uint8_t*)buf, len);
    if (result == ESP_OK) {
      state.packetsSent++;
      return;
    }
    Serial.printf("[WARN] Send attempt %d/%d failed\n", attempt, MAX_RETRY_COUNT);
    delay(RETRY_DELAY_MS);
  }

  Serial.println("[ERROR] Packet dropped after max retries");
  state.sendFailures++;
}

// ─────────────────────────────────────────────────────────────
//  Alert sender
// ─────────────────────────────────────────────────────────────

void sendAlert(const char* sensor, float value, const char* severity, const char* desc) {
  StaticJsonDocument<300> doc;
  doc["sensor"]   = sensor;
  doc["value"]    = value;
  doc["severity"] = severity;
  doc["desc"]     = desc;
  doc["uptime"]   = millis() - state.bootTime;

  sendJson(MSG_ALERT, doc);

  Serial.printf("[ALERT] %-10s | severity=%-8s | value=%.1f | %s\n",
                sensor, severity, value, desc);

  // Visual feedback — blink pattern by severity
  if (strcmp(severity, "CRITICAL") == 0) blinkLED(5, 80,  60);
  else if (strcmp(severity, "HIGH") == 0) blinkLED(3, 120, 80);
  else                                     blinkLED(2, 200, 100);
}

// ─────────────────────────────────────────────────────────────
//  Heartbeat
// ─────────────────────────────────────────────────────────────

void sendHeartbeat() {
  StaticJsonDocument<256> doc;
  doc["uptime_ms"]    = millis() - state.bootTime;
  doc["tx_ok"]        = state.packetsSent;
  doc["tx_fail"]      = state.sendFailures;
  doc["vib_events"]   = sensors.vibCount;
  doc["gas_alerts"]   = sensors.gasAlerts;
  doc["flood_alerts"] = sensors.floodAlerts;
  doc["gas_now"]      = analogRead(GAS_PIN);
  doc["flood_now"]    = analogRead(FLOOD_PIN);

  sendJson(MSG_HEARTBEAT, doc);
  Serial.printf("[BEAT] Heartbeat — uptime=%lums vib=%u gas=%u flood=%u\n",
                millis() - state.bootTime,
                sensors.vibCount, sensors.gasAlerts, sensors.floodAlerts);
}

// ─────────────────────────────────────────────────────────────
//  Sensor polling
// ─────────────────────────────────────────────────────────────

void pollVibration() {
  if (digitalRead(VIB_PIN) == HIGH) {
    sensors.vibCount++;
    unsigned long now = millis();
    if (now - sensors.lastVibAlert > ALERT_COOLDOWN_MS) {
      sensors.lastVibAlert = now;
      sendAlert(ALERT_VIBRATION, 1.0f, "HIGH",
                "Seismic vibration detected at Node C");
    }
  }
}

void pollGas() {
  int raw = analogRead(GAS_PIN);
  if (raw > GAS_THRESHOLD) {
    unsigned long now = millis();
    if (now - sensors.lastGasAlert > ALERT_COOLDOWN_MS) {
      sensors.lastGasAlert = now;
      sensors.gasAlerts++;
      const char* sev = classifySeverity(ALERT_GAS, raw);
      char desc[64];
      snprintf(desc, sizeof(desc),
               "Gas concentration elevated — ADC=%d (baseline=%d)",
               raw, sensors.gasBaseline);
      sendAlert(ALERT_GAS, raw, sev, desc);
    }
  }
}

void pollFlood() {
  int raw = analogRead(FLOOD_PIN);
  if (raw > FLOOD_THRESHOLD) {
    unsigned long now = millis();
    if (now - sensors.lastFloodAlert > ALERT_COOLDOWN_MS) {
      sensors.lastFloodAlert = now;
      sensors.floodAlerts++;
      const char* sev = classifySeverity(ALERT_FLOOD, raw);
      char desc[48];
      snprintf(desc, sizeof(desc), "Water level critical — ADC=%d", raw);
      sendAlert(ALERT_FLOOD, raw, sev, desc);
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  Utility
// ─────────────────────────────────────────────────────────────

void blinkLED(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH); delay(onMs);
    digitalWrite(LED_PIN, LOW);  delay(offMs);
  }
}

// ─────────────────────────────────────────────────────────────
//  Setup
// ─────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);

  Serial.println("\n========================================");
  Serial.printf("  Mesh Node %s  |  Firmware v%s\n", NODE_NAME, NODE_VERSION);
  Serial.println("  Role: Multi-Sensor Alert Node");
  Serial.println("========================================");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Sensor pins
  pinMode(VIB_PIN,   INPUT);
  pinMode(GAS_PIN,   INPUT);
  pinMode(FLOOD_PIN, INPUT);

  // Wi-Fi + ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW init failed — halting");
    while (true) blinkLED(5, 50, 50);
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  memset(&broadcastPeer, 0, sizeof(broadcastPeer));
  memcpy(broadcastPeer.peer_addr, BROADCAST_MAC, 6);
  broadcastPeer.channel = WIFI_CHANNEL;
  broadcastPeer.encrypt = false;

  if (esp_now_add_peer(&broadcastPeer) != ESP_OK) {
    Serial.println("[ERROR] Failed to add broadcast peer — halting");
    while (true) blinkLED(5, 50, 50);
  }

  // Calibrate gas sensor against fresh air baseline
  calibrateGas();

  state.bootTime = millis();
  blinkLED(3, 200, 200);
  Serial.printf("[INFO] Node %s ready — monitoring vibration, gas, flood\n", NODE_NAME);
  Serial.println("========================================\n");
}

// ─────────────────────────────────────────────────────────────
//  Main loop
// ─────────────────────────────────────────────────────────────

void loop() {
  unsigned long now = millis();

  // Poll all sensors every loop iteration (fast — 10ms cycle)
  pollVibration();
  pollGas();
  pollFlood();

  // Periodic heartbeat
  if (now - state.lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
    sendHeartbeat();
    state.lastHeartbeat = now;
  }

  delay(10);  // 10ms loop — fast enough for vibration sensor
}
