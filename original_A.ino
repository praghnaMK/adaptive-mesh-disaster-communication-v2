#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// 🔹 CHANGE THIS FOR EACH BOARD BEFORE UPLOADING:
// For Node A: "A"
// For Node B: "B"
// For Node C: "C"
String nodeName = "A";   // <<--- EDIT THIS ONLY

// Broadcast MAC address (ESP-NOW)
uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

const int LED_PIN = 2;   // On-board LED pin (works on most ESP32 dev boards)

esp_now_peer_info_t peerInfo;

// Receive callback function
void OnDataRecv(const esp_now_recv_info * info, const uint8_t *data, int len) {
  Serial.print("📩 Node ");
  Serial.print(nodeName);
  Serial.print(" received: ");

  for (int i = 0; i < len; i++) {
    Serial.print((char)data[i]);
  }
  Serial.println();

  // Blink LED when a message is received
  digitalWrite(LED_PIN, HIGH);
  delay(80);
  digitalWrite(LED_PIN, LOW);
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Wi-Fi must be in STA mode for ESP-NOW
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);  // All nodes use channel 1

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  // Register receive callback
  esp_now_register_recv_cb(OnDataRecv);

  // Add broadcast peer (so we can send)
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, broadcastMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add broadcast peer");
    return;
  }

  Serial.print("✅ Node ");
  Serial.print(nodeName);
  Serial.println(" READY!");
}

void loop() {
  // Prepare message
  String msg = "Hello from Node ";
  msg += nodeName;

  // Send to broadcast: everyone in range will receive
  esp_err_t result = esp_now_send(broadcastMAC, (const uint8_t *)msg.c_str(), msg.length());

  if (result == ESP_OK) {
    Serial.println("📤 Broadcast sent from Node " + nodeName);
  } else {
    Serial.print("❌ Error sending from Node ");
    Serial.println(nodeName);
  }

  delay(3000);  // Send every 3 seconds
}
