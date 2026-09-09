/**
 * LoRa — тест ПЕРЕДАТЧИК (узел A)
 *
 * ⚠️ Открывайте ЭТУ ПАПКУ как скетч (не test_firmware целиком!).
 *
 * Библиотека: LoRa by Sandeep Mistry
 * Распайка Ra-01 → ESP32:
 *   3.3V→3.3V | GND→GND
 *   SCK→18 | MISO→19 | MOSI→23 | NSS→5 | RST→14 | DIO0→26
 *
 * Serial Monitor: 115200
 */

#include <SPI.h>
#include <LoRa.h>

#define LORA_SCK  18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS   5
#define LORA_RST  14
#define LORA_DIO0 26

#define LORA_FREQ_HZ   433E6
#define LORA_SYNC_WORD 0x12
#define LORA_SF        7
#define LORA_BW        125E3
#define LORA_CR        5
#define LORA_TX_POWER  17

#define MY_NODE_ID     1
#define DEST_NODE_ID   0xFF

uint16_t packetNum = 0;

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=== LoRa TEST SENDER (node 1) ===");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ_HZ)) {
    Serial.println("LoRa init FAILED!");
    while (true) { delay(1000); }
  }

  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setTxPower(LORA_TX_POWER);

  Serial.println("LoRa OK. TX каждые 2 сек...");
}

void loop() {
  char payload[32];
  snprintf(payload, sizeof(payload), "ping #%u", packetNum);

  LoRa.beginPacket();
  LoRa.write(DEST_NODE_ID);
  LoRa.write(MY_NODE_ID);
  LoRa.write(0x01);
  LoRa.print(payload);
  LoRa.endPacket();

  Serial.printf("[TX] %s\n", payload);
  packetNum++;
  delay(2000);
}
