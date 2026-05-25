// ============================================================
//  Node_D.ino  —  LoRa Long-Range Gateway
//  Adaptive Wireless Mesh Communication System v2.0
//
//  Role: Receives LoRa packets from the mesh boundary,
//        parses structured JSON payloads, logs signal quality
//        (RSSI/SNR), and bridges data to the dashboard via
//        USB Serial in a format the Python dashboard reads.
//
//  This is the edge-of-mesh gateway — handles communication
//  beyond ESP-NOW's ~200m range via LoRa (up to 2km LOS).
//
//  Wiring (SPI):
//    LoRa VCC  → 3.3V
//    LoRa GND  → GND
//    LoRa SCK  → GPIO 18
//    LoRa MISO → GPIO 19
//    LoRa MOSI → GPIO 23
//    LoRa NSS  → GPIO 5
//    LoRa RST  → GPIO 14
//    LoRa DIO0 → GPIO 2
//
//  Libraries required:
//    - LoRa  by Sandeep Mistry
//    - ArduinoJson  by Benoit Blanchon  (v6.x)
// ============================================================

#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include "config.h"

// ── Gateway state ─────────────────────────────────────────────
struct GatewayState {
  unsigned long bootTime        = 0;
  uint32_t      packetsReceived = 0;
  uint32_t      parseErrors     = 0;
  uint32_t      alertsReceived  = 0;
  uint32_t      heartbeats      = 0;
  long          lastRSSI        = 0;
  float         lastSNR         = 0.0f;
  // Running RSSI average (exponential moving average)
  float         avgRSSI         = 0.0f;
};

static GatewayState gw;

// ─────────────────────────────────────────────────────────────
//  LoRa packet handler
// ─────────────────────────────────────────────────────────────

void handlePacket(int packetSize) {
  if (packetSize == 0) return;

  // Read raw bytes
  char buf[250];
  int  idx = 0;
  while (LoRa.available() && idx < 249) {
    buf[idx++] = (char)LoRa.read();
  }
  buf[idx] = '\0';

  // Signal quality
  long  rssi = LoRa.packetRssi();
  float snr  = LoRa.packetSnr();
  gw.lastRSSI = rssi;
  gw.lastSNR  = snr;
  gw.packetsReceived++;

  // Exponential moving average RSSI (alpha=0.2)
  if (gw.avgRSSI == 0) gw.avgRSSI = rssi;
  else gw.avgRSSI = 0.8f * gw.avgRSSI + 0.2f * rssi;

  Serial.printf("\n[LORA] Packet #%u | len=%d | RSSI=%ld dBm | SNR=%.1f dB\n",
                gw.packetsReceived, packetSize, rssi, snr);

  // Parse JSON
  StaticJsonDocument<300> doc;
  DeserializationError err = deserializeJson(doc, buf);
  if (err) {
    gw.parseErrors++;
    Serial.printf("[WARN] JSON parse error: %s | raw=%s\n", err.c_str(), buf);
    return;
  }

  const char* node    = doc["node"]   | "?";
  const char* type    = doc["type"]   | "?";
  long        ts      = doc["ts"]     | 0L;
  const char* ver     = doc["ver"]    | "?";

  // Inject RF metadata into the document before forwarding
  doc["rssi"]      = rssi;
  doc["snr"]       = snr;
  doc["avg_rssi"]  = (int)gw.avgRSSI;
  doc["gw_ts"]     = millis() - gw.bootTime;

  // ── Handle by message type ───────────────────────────────
  if (strcmp(type, MSG_ALERT) == 0) {
    gw.alertsReceived++;
    const char* sensor   = doc["sensor"]   | "UNKNOWN";
    const char* severity = doc["severity"] | "UNKNOWN";
    float       value    = doc["value"]    | 0.0f;

    Serial.printf("[ALERT] node=%-2s sensor=%-10s severity=%-8s value=%.1f\n",
                  node, sensor, severity, value);

    // Signal quality warning for weak links
    if (rssi < -110) {
      Serial.printf("[WARN] Weak signal (RSSI=%ld) — packet may be unreliable\n", rssi);
    }

  } else if (strcmp(type, MSG_HEARTBEAT) == 0) {
    gw.heartbeats++;
    long uptime = doc["uptime_ms"] | 0L;
    Serial.printf("[BEAT] node=%-2s uptime=%lums tx_ok=%u tx_fail=%u\n",
                  node, uptime,
                  (uint32_t)(doc["tx_ok"]   | 0),
                  (uint32_t)(doc["tx_fail"] | 0));

  } else if (strcmp(type, MSG_STATUS) == 0) {
    const char* offlineNode = doc["offline_node"] | "?";
    long silentMs           = doc["silent_ms"]    | 0L;
    Serial.printf("[STATUS] Node %s offline — silent for %ldms\n",
                  offlineNode, silentMs);
  }

  // ── Forward enriched JSON to dashboard via Serial ────────
  // Dashboard reads lines prefixed with "DATA:" on the serial port
  Serial.print("DATA:");
  serializeJson(doc, Serial);
  Serial.println();

  // Visual feedback
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  digitalWrite(LED_PIN, LOW);
}

// ─────────────────────────────────────────────────────────────
//  Periodic gateway status report
// ─────────────────────────────────────────────────────────────

unsigned long lastStatusPrint = 0;

void printGatewayStatus() {
  if (millis() - lastStatusPrint < 30000) return;  // Every 30s
  lastStatusPrint = millis();

  Serial.println("\n── Gateway Status ─────────────────────");
  Serial.printf("   Uptime       : %lu s\n",   (millis() - gw.bootTime) / 1000);
  Serial.printf("   Packets RX   : %u\n",       gw.packetsReceived);
  Serial.printf("   Alerts       : %u\n",       gw.alertsReceived);
  Serial.printf("   Heartbeats   : %u\n",       gw.heartbeats);
  Serial.printf("   Parse errors : %u\n",       gw.parseErrors);
  Serial.printf("   Last RSSI    : %ld dBm\n",  gw.lastRSSI);
  Serial.printf("   Avg RSSI     : %d dBm\n",   (int)gw.avgRSSI);
  Serial.printf("   Last SNR     : %.1f dB\n",  gw.lastSNR);
  Serial.println("───────────────────────────────────────\n");
}

// ─────────────────────────────────────────────────────────────
//  Setup
// ─────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);

  Serial.println("\n========================================");
  Serial.printf("  Mesh Node D  |  Firmware v%s\n", NODE_VERSION);
  Serial.println("  Role: LoRa Long-Range Gateway");
  Serial.printf("  Freq: %.0f MHz\n", LORA_FREQ / 1E6);
  Serial.println("========================================");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("[ERROR] LoRa init failed — check wiring and frequency");
    Serial.println("[ERROR] Halting.");
    while (true) {
      digitalWrite(LED_PIN, HIGH); delay(100);
      digitalWrite(LED_PIN, LOW);  delay(100);
    }
  }

  // Must match transmitter settings exactly
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.disableCrc();  // CRC handled at application layer via JSON integrity

  gw.bootTime = millis();

  // Ready signal — 3 quick blinks
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH); delay(150);
    digitalWrite(LED_PIN, LOW);  delay(150);
  }

  Serial.printf("[INFO] Gateway ready — SF=%d BW=%.0fkHz sync=0x%02X\n",
                LORA_SF, LORA_BW / 1000.0f, LORA_SYNC_WORD);
  Serial.println("[INFO] Listening for LoRa packets...");
  Serial.println("========================================\n");
}

// ─────────────────────────────────────────────────────────────
//  Main loop
// ─────────────────────────────────────────────────────────────

void loop() {
  // Non-blocking LoRa receive
  int packetSize = LoRa.parsePacket();
  if (packetSize > 0) {
    handlePacket(packetSize);
  }

  // Periodic self-status
  printGatewayStatus();
}
