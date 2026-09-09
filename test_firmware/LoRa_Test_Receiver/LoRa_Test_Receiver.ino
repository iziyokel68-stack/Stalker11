/**
 * LoRa — тест ПРИЁМНИК (узел B)
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

#define REG_VERSION 0x42

#define LORA_FREQ_HZ   433E6
#define LORA_SYNC_WORD 0x12
#define LORA_SF        7
#define LORA_BW        125E3
#define LORA_CR        5

uint32_t lastHeartbeat = 0;
uint32_t rxCount = 0;

void loraHardwareReset() {
  pinMode(LORA_RST, OUTPUT);
  digitalWrite(LORA_RST, LOW);
  delay(20);
  digitalWrite(LORA_RST, HIGH);
  delay(20);
}

uint8_t readLoRaRegister(uint8_t reg) {
  digitalWrite(LORA_CS, LOW);
  SPI.transfer(reg & 0x7F);
  uint8_t value = SPI.transfer(0x00);
  digitalWrite(LORA_CS, HIGH);
  return value;
}

void printSpiDiagnostic() {
  pinMode(LORA_CS, OUTPUT);
  digitalWrite(LORA_CS, HIGH);
  loraHardwareReset();

  uint8_t ver = readLoRaRegister(REG_VERSION);
  Serial.printf("SPI diag: REG_VERSION = 0x%02X  ", ver);

  if (ver == 0x12) {
    Serial.println("OK (чип SX1278 отвечает)");
  } else if (ver == 0x00) {
    Serial.println("ПЛОХО: MISO молчит (провод MISO, питание 3.3V, модуль)");
  } else if (ver == 0xFF) {
    Serial.println("ПЛОХО: MISO всегда 1 (CS, MOSI/MISO, мёртвый модуль)");
  } else {
    Serial.println("ПЛОХО: неожиданный ответ SPI");
  }
}

void dumpRaw(int packetSize) {
  Serial.printf("[RAW] size=%d  hex:", packetSize);
  while (LoRa.available()) {
    Serial.printf(" %02X", LoRa.read());
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=== LoRa TEST RECEIVER (node 2) ===");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

  Serial.println("--- Диагностика SPI до LoRa.begin ---");
  printSpiDiagnostic();

  if (!LoRa.begin(LORA_FREQ_HZ)) {
    Serial.println();
    Serial.println("LoRa init FAILED!");
    Serial.println();
    Serial.println("ТЕСТ: поменяйте Ra-01 местами между ESP:");
    Serial.println("  - ошибка переехала на передатчик → мёртвый модуль Ra-01");
    Serial.println("  - ошибка осталась на этом ESP   → провода/плата приёмника");
    Serial.println();
    Serial.println("Проверьте в IDE: Тот же Board, что у работающего Sender.");
    while (true) {
      delay(5000);
      printSpiDiagnostic();
    }
  }

  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.receive();

  Serial.println("LoRa OK. Слушаю эфир...");
  lastHeartbeat = millis();
}

void loop() {
  int packetSize = LoRa.parsePacket();

  if (packetSize > 0) {
    rxCount++;
    Serial.println("─────────────────────────");
    Serial.printf("[RX #%lu] size=%d  RSSI=%d dBm  SNR=%.1f dB\n",
                  (unsigned long)rxCount, packetSize,
                  LoRa.packetRssi(), LoRa.packetSnr());

    if (packetSize >= 3) {
      uint8_t destId  = LoRa.read();
      uint8_t srcId   = LoRa.read();
      uint8_t msgType = LoRa.read();

      String payload = "";
      while (LoRa.available()) {
        payload += (char)LoRa.read();
      }

      Serial.printf("  dest=%d  src=%d  type=0x%02X  data=%s\n",
                    destId, srcId, msgType, payload.c_str());
    } else {
      dumpRaw(packetSize);
    }

    LoRa.receive();
  }

  if (millis() - lastHeartbeat >= 5000) {
    lastHeartbeat = millis();
    Serial.println("[...] слушаю...");
  }
}
