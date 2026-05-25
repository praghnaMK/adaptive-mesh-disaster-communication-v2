#include <SPI.h>
#include <LoRa.h>

// ----- LoRa pin configuration (BOTH NODES) -----
// LoRa VCC  -> 3.3V
// LoRa GND  -> GND
// LoRa SCK  -> GPIO 18
// LoRa MISO -> GPIO 19
// LoRa MOSI -> GPIO 23
// LoRa NSS  -> GPIO 5
// LoRa RST  -> GPIO 14
// LoRa DIO0 -> GPIO 2

#define LORA_SCK    18
#define LORA_MISO   19
#define LORA_MOSI   23
#define LORA_SS     5
#define LORA_RST    14
#define LORA_DIO0   2

// 🔴 SET THIS TO MATCH YOUR MODULE BAND
#define LORA_FREQ   433E6   // OR 868E6 / 915E6

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("========== NODE D (RECEIVER) BOOT ==========");
  Serial.println("Configuring SPI & LoRa...");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  Serial.println("SPI.begin() done");

  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  Serial.println("LoRa.setPins() done");

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("❌ LoRa init FAILED on Node D!");
    Serial.println("Check wiring & frequency.");
    while (true) {
      delay(1000);
    }
  }

  // Match exactly with Node C:
  LoRa.setSpreadingFactor(12);         // super slow but robust
  LoRa.setSignalBandwidth(125E3);      // 125 kHz
  LoRa.setCodingRate4(5);              // 4/5
  LoRa.disableCrc();                   // no CRC to avoid mismatch
  LoRa.setSyncWord(0x34);              // same on both nodes

  Serial.println("✅ Node D: LoRa init OK, waiting for packets...");
  Serial.println("============================================");
}

void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    Serial.print("\n📩 Packet received! Length = ");
    Serial.println(packetSize);

    String incoming = "";
    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }

    Serial.print("Data: ");
    Serial.println(incoming);

    long rssi = LoRa.packetRssi();
    float snr = LoRa.packetSnr();
    Serial.print("RSSI: ");
    Serial.print(rssi);
    Serial.print("  |  SNR: ");
    Serial.println(snr);
  }
}