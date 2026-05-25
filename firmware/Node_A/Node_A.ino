// ============================================================
//  Node_A.ino  —  Mesh Relay Node (ESP-NOW Broadcast)
//  Adaptive Wireless Mesh Communication System v2.0
//
//  Role: Relays messages across the mesh, monitors network
//        health, and forwards alerts to the LoRa gateway.
//
//  Libraries required (install via Arduino Library Manager):
//    - ArduinoJson  by Benoit Blanchon  (v6.x)
// ============================================================

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include "config.h"

// ── Broadcast address (sends to all ESP-NOW peers in range) ──
static const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ── Node state ───────────────────────────────────────────────
struct NodeState {
  unsigned long lastHeartbeatSent = 0;
  unsigned long bootTime          = 0;
  uint32_t      packetsSent       = 0;
  uint32_t      packetsReceived   = 0;
  uint32_t      sendFailures      = 0;
  // Last-seen table: tracks when each peer node last sent a packet
  // Index 0=A, 1=B, 2=C — we track others, not ourselves
  unsigned long peerLastSeen[3]   = {0, 0, 0};
  String        peerStatus[3]     = {"UNKNOWN","UNKNOWN","UNKNOWN"};
};

static NodeState state;
static esp_now_peer_info_t broadcastPeer;

// ── Forward declarations ──────────────────────────────────────
void sendJson(const String& type, const JsonDocument& payload);
void processIncoming(const String& raw, const uint8_t* senderMAC);
void checkPeerHealth();
void blinkLED(int times, int onMs, int offMs);
int  peerIndex(const String& name);

// ─────────────────────────────────────────────────────────────
//  ESP-NOW callbacks
// ─────────────────────────────────────────────────────────────

void IRAM_ATTR onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
  // Called from ISR context — keep minimal
  if (status != ESP_NOW_SEND_SUCCESS) {
    state.sendFailures++;
  }
}

void onDataRecv(const esp_now_recv_info* info, const uint8_t* data, int len) {
  state.packetsReceived++;

  // Null-terminate and parse
  char buf[250];
  int  copyLen = min(len, 249);
  memcpy(buf, data, copyLen);
  buf[copyLen] = '\0';

  String raw(buf);
  processIncoming(raw, info->src_addr);

  // Brief LED blink on receive (non-blocking)
  digitalWrite(LED_PIN, HIGH);
  delayMicroseconds(5000);   // 5ms — short enough not to disrupt timing
  digitalWrite(LED_PIN, LOW);
}

// ─────────────────────────────────────────────────────────────
//  Message processing
// ─────────────────────────────────────────────────────────────

void processIncoming(const String& raw, const uint8_t* senderMAC) {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, raw);

  if (err) {
    Serial.printf("[WARN] Malformed packet from %02X:%02X:%02X — %s\n",
      senderMAC[3], senderMAC[4], senderMAC[5], err.c_str());
    return;
  }

  const char* fromNode = doc["node"]  | "?";
  const char* msgType  = doc["type"]  | "?";
  long        ts       = doc["ts"]    | 0L;

  Serial.printf("[RECV] from=%-2s type=%-10s ts=%lu\n", fromNode, msgType, ts);

  // Update peer health table
  int idx = peerIndex(String(fromNode));
  if (idx >= 0) {
    state.peerLastSeen[idx] = millis();
    state.peerStatus[idx]   = "ONLINE";
  }

  // Handle ALERT — relay immediately (mesh forwarding)
  if (strcmp(msgType, MSG_ALERT) == 0) {
    const char* alertType = doc["sensor"] | "UNKNOWN";
    float       value     = doc["value"]  | 0.0f;
    Serial.printf("[ALERT] Relaying %s alert from Node %s (value=%.2f)\n",
                  alertType, fromNode, value);
    // Re-broadcast so other nodes and the gateway receive it
    esp_now_send(BROADCAST_MAC, (const uint8_t*)raw.c_str(), raw.length());
    blinkLED(3, 100, 80);
  }

  // Handle ACK — log delivery confirmation
  if (strcmp(msgType, MSG_ACK) == 0) {
    Serial.printf("[ACK ] Delivery confirmed from Node %s\n", fromNode);
  }
}

// ─────────────────────────────────────────────────────────────
//  JSON send helper with retry
// ─────────────────────────────────────────────────────────────

void sendJson(const String& type, JsonDocument& payload) {
  payload["node"] = NODE_NAME;
  payload["type"] = type;
  payload["ts"]   = millis();
  payload["ver"]  = NODE_VERSION;

  char buf[250];
  size_t len = serializeJson(payload, buf);

  bool sent = false;
  for (int attempt = 1; attempt <= MAX_RETRY_COUNT && !sent; attempt++) {
    esp_err_t result = esp_now_send(BROADCAST_MAC, (const uint8_t*)buf, len);
    if (result == ESP_OK) {
      state.packetsSent++;
      sent = true;
      if (attempt > 1) {
        Serial.printf("[INFO] Sent on attempt %d\n", attempt);
      }
    } else {
      Serial.printf("[WARN] Send failed (attempt %d/%d) — err=0x%X\n",
                    attempt, MAX_RETRY_COUNT, result);
      delay(RETRY_DELAY_MS);
    }
  }

  if (!sent) {
    Serial.printf("[ERROR] Packet dropped after %d retries\n", MAX_RETRY_COUNT);
    state.sendFailures++;
  }
}

// ─────────────────────────────────────────────────────────────
//  Heartbeat — announces this node is alive + network status
// ─────────────────────────────────────────────────────────────

void sendHeartbeat() {
  StaticJsonDocument<256> doc;
  doc["uptime_ms"] = millis() - state.bootTime;
  doc["tx_ok"]     = state.packetsSent;
  doc["tx_fail"]   = state.sendFailures;
  doc["rx"]        = state.packetsReceived;

  // Embed peer status snapshot
  JsonObject peers = doc.createNestedObject("peers");
  peers["A"] = state.peerStatus[0];
  peers["B"] = state.peerStatus[1];
  peers["C"] = state.peerStatus[2];

  sendJson(MSG_HEARTBEAT, doc);
  Serial.printf("[BEAT] Heartbeat sent — uptime=%lums tx=%u rx=%u fail=%u\n",
                millis() - state.bootTime,
                state.packetsSent, state.packetsReceived, state.sendFailures);
}

// ─────────────────────────────────────────────────────────────
//  Peer health watchdog
// ─────────────────────────────────────────────────────────────

void checkPeerHealth() {
  const char* names[] = {"A","B","C"};
  for (int i = 0; i < 3; i++) {
    if (String(NODE_NAME) == names[i]) continue;  // Skip self
    if (state.peerLastSeen[i] == 0) continue;     // Never seen — still UNKNOWN

    unsigned long silence = millis() - state.peerLastSeen[i];
    if (silence > NODE_OFFLINE_TIMEOUT_MS && state.peerStatus[i] != "OFFLINE") {
      state.peerStatus[i] = "OFFLINE";
      Serial.printf("[WARN] Node %s went OFFLINE (silent for %lums)\n",
                    names[i], silence);
      // Send a network status alert so dashboard logs the event
      StaticJsonDocument<128> doc;
      doc["offline_node"] = names[i];
      doc["silent_ms"]    = silence;
      sendJson(MSG_STATUS, doc);
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  Utilities
// ─────────────────────────────────────────────────────────────

void blinkLED(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH); delay(onMs);
    digitalWrite(LED_PIN, LOW);  delay(offMs);
  }
}

int peerIndex(const String& name) {
  if (name == "A") return 0;
  if (name == "B") return 1;
  if (name == "C") return 2;
  return -1;
}

// ─────────────────────────────────────────────────────────────
//  Setup
// ─────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);

  Serial.println("\n========================================");
  Serial.printf("  Mesh Node %s  |  Firmware v%s\n", NODE_NAME, NODE_VERSION);
  Serial.println("  Adaptive Disaster Mesh Network");
  Serial.println("========================================");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Wi-Fi must be STA for ESP-NOW
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

  state.bootTime = millis();

  // Startup blink — 3 slow pulses signals ready
  blinkLED(3, 200, 200);
  Serial.printf("[INFO] Node %s ready on channel %d\n", NODE_NAME, WIFI_CHANNEL);
  Serial.println("========================================\n");
}

// ─────────────────────────────────────────────────────────────
//  Main loop
// ─────────────────────────────────────────────────────────────

void loop() {
  unsigned long now = millis();

  // Heartbeat
  if (now - state.lastHeartbeatSent >= HEARTBEAT_INTERVAL_MS) {
    sendHeartbeat();
    state.lastHeartbeatSent = now;
  }

  // Peer health watchdog
  checkPeerHealth();

  delay(10);  // Yield — prevents WDT reset
}
