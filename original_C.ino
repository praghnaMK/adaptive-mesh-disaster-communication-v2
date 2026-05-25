#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

String nodeName = "C";    // Node C = Sensor node

uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const int LED_PIN = 2;
const int VIB_PIN = 4;    // SW-420 DO -> GPIO 4

esp_now_peer_info_t peerInfo;

unsigned long lastAlert = 0;
const unsigned long cooldown = 3000; // 3 seconds gap between alerts

void OnDataRecv(const esp_now_recv_info * info, const uint8_t *data, int len) {
  // Just for debugging: show if Node C also receives something
  Serial.print("📩 Node ");
  Serial.print(nodeName);
  Serial.print(" received: ");
  for (int i = 0; i < len; i++) Serial.print((char)data[i]);
  Serial.println();
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(VIB_PIN, INPUT);   // if too insensitive: change to INPUT_PULLUP

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, broadcastMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.println("✅ Node C (Sensor) READY!");
}

void loop() {
  int vib = digitalRead(VIB_PIN);

  if (vib == HIGH && (millis() - lastAlert > cooldown)) {
    String alert = "ALERT 🚨: Vibration / Earthquake detected at NODE C";

    Serial.println("Vibration detected! Sending ALERT...");
    esp_now_send(broadcastMAC, (const uint8_t *)alert.c_str(), alert.length());
    lastAlert = millis();

    // Flash LED a few times to show alert locally
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(120);
      digitalWrite(LED_PIN, LOW);
      delay(120);
    }
  }

  delay(10);  // fast loop, so we don't miss short pulses
}