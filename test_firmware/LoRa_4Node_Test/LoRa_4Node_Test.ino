/**
 * LoRa — тест 4 узлов (ESP32-WROOM-32 + Ra-01)
 *
 * ⚠️ Открывайте ЭТУ ПАПКУ как скетч (не test_firmware целиком!).
 *
 * Перед прошивкой задайте MY_NODE_ID (1..4) для каждого ESP32.
 *
 * Библиотека: LoRa by Sandeep Mistry (ZIP с GitHub)
 * Распайка Ra-01 → ESP32 DevKit:
 *   3.3V→3.3V | GND→GND
 *   SCK→18 | MISO→19 | MOSI→23 | NSS→5 | RST→14 | DIO0→26
 *
 * Serial Monitor: 115200
 */

#include <SPI.h>
#include <LoRa.h>

// ── ID этого узла: поменяйте перед прошивкой каждого ESP32 ──
#define MY_NODE_ID  1

// ── Пины LoRa (breadboard / ESP32 DevKit) ──
#define LORA_SCK   13
#define LORA_MISO  40
#define LORA_MOSI  14
#define LORA_CS    39
#define LORA_RST   12
#define LORA_DIO0  41

#define REG_VERSION 0x42

// ── Радио (одинаково на всех 4 узлах) ──
#define LORA_FREQ_HZ   433E6
#define LORA_SYNC_WORD 0x12
#define LORA_SF        7
#define LORA_BW        125E3
#define LORA_CR        5
#define LORA_TX_POWER  17

#define LORA_BROADCAST 0xFF
#define LORA_HEADER_SIZE 3
#define LORA_CRC_SIZE    2
#define LORA_MAX_PAYLOAD 120

// ── Типы сообщений ──
#define MSG_PING     0x01
#define MSG_TEXT     0x02
#define MSG_COMMAND  0x03
#define MSG_STATUS   0x04

static const char *MSG_TYPE_NAMES[] = {
  "UNKNOWN", "PING", "TEXT", "COMMAND", "STATUS"
};

static const char *NODE_NAMES[] = {
  "", "ESP-1", "ESP-2", "ESP-3", "ESP-4"
};

uint32_t rxCount = 0;
uint32_t txCount = 0;
uint32_t crcErrors = 0;
uint32_t lastHeartbeat = 0;
bool autoPing = false;
uint32_t lastAutoPing = 0;
uint8_t defaultMsgType = MSG_TEXT;

String serialLine;

// ── CRC16 (CRC-CCITT, как в eeprom_txn.h) ──
uint16_t loraCrc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

const char *msgTypeName(uint8_t type) {
  switch (type) {
    case MSG_PING:    return "PING";
    case MSG_TEXT:    return "TEXT";
    case MSG_COMMAND: return "COMMAND";
    case MSG_STATUS:  return "STATUS";
    default:          return "UNKNOWN";
  }
}

const char *nodeName(uint8_t id) {
  if (id >= 1 && id <= 4) return NODE_NAMES[id];
  if (id == LORA_BROADCAST) return "ALL";
  return "?";
}

const char *deliveryLabel(uint8_t destId) {
  if (destId == LORA_BROADCAST) return "BROADCAST (для всех)";
  if (destId == MY_NODE_ID) return "UNICAST (мне)";
  return "UNICAST (не мне)";
}

bool isForMe(uint8_t destId) {
  return destId == LORA_BROADCAST || destId == MY_NODE_ID;
}

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
    Serial.println("OK (SX1278 отвечает)");
  } else if (ver == 0x00) {
    Serial.println("ПЛОХО: MISO молчит (провод, питание 3.3V, модуль)");
  } else if (ver == 0xFF) {
    Serial.println("ПЛОХО: MISO=1 (CS, MOSI/MISO, мёртвый модуль)");
  } else {
    Serial.println("ПЛОХО: неожиданный ответ SPI");
  }
}

void printHexPayload(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    Serial.printf("%02X", data[i]);
    if (i + 1 < len) Serial.print(' ');
  }
}

// Печать payload как UTF-8 (русский и др. — не только ASCII)
void printPayloadUtf8(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    uint8_t b = data[i];
    if (b == '\r') continue;
    if (b == '\n') {
      Serial.print("\\n");
    } else if (b < 32 || b == 127) {
      Serial.printf("\\x%02X", b);
    } else {
      Serial.write(b);
    }
  }
}

bool sendPacket(uint8_t destId, uint8_t msgType, const uint8_t *payload, size_t payloadLen) {
  if (payloadLen > LORA_MAX_PAYLOAD) {
    Serial.println("ERR: payload слишком большой");
    return false;
  }

  uint8_t frame[LORA_HEADER_SIZE + LORA_MAX_PAYLOAD + LORA_CRC_SIZE];
  size_t frameLen = LORA_HEADER_SIZE + payloadLen;

  frame[0] = destId;
  frame[1] = MY_NODE_ID;
  frame[2] = msgType;
  if (payloadLen > 0) memcpy(frame + LORA_HEADER_SIZE, payload, payloadLen);

  uint16_t crc = loraCrc16(frame, frameLen);
  frame[frameLen]     = (uint8_t)(crc & 0xFF);
  frame[frameLen + 1] = (uint8_t)((crc >> 8) & 0xFF);
  frameLen += LORA_CRC_SIZE;

  LoRa.beginPacket();
  LoRa.write(frame, frameLen);
  LoRa.endPacket();
  LoRa.receive();

  txCount++;
  Serial.println("─────────────────────────");
  Serial.printf("[TX #%lu] %s → %s (%s)\n",
                (unsigned long)txCount,
                nodeName(MY_NODE_ID),
                nodeName(destId),
                deliveryLabel(destId));
  Serial.printf("  type=0x%02X (%s)  len=%u\n", msgType, msgTypeName(msgType), (unsigned)payloadLen);
  if (payloadLen > 0) {
    Serial.print("  data(utf8): \"");
    printPayloadUtf8(payload, payloadLen);
    Serial.println("\"");
    Serial.print("  data(hex):   ");
    printHexPayload(payload, payloadLen);
    Serial.println();
  }
  return true;
}

bool sendText(uint8_t destId, uint8_t msgType, const String &text) {
  return sendPacket(destId, msgType, (const uint8_t *)text.c_str(), text.length());
}

void handleReceivedPacket(int packetSize) {
  if (packetSize < LORA_HEADER_SIZE + LORA_CRC_SIZE) {
    Serial.printf("[RX] слишком короткий пакет (%d байт)\n", packetSize);
    Serial.print("  raw: ");
    while (LoRa.available()) Serial.printf("%02X ", LoRa.read());
    Serial.println();
    return;
  }

  uint8_t frame[LORA_HEADER_SIZE + LORA_MAX_PAYLOAD + LORA_CRC_SIZE];
  int idx = 0;
  while (LoRa.available() && idx < (int)sizeof(frame)) {
    frame[idx++] = LoRa.read();
  }

  if (idx < LORA_HEADER_SIZE + LORA_CRC_SIZE) return;

  uint16_t storedCrc = (uint16_t)frame[idx - 2] | ((uint16_t)frame[idx - 1] << 8);
  uint16_t calcCrc = loraCrc16(frame, (size_t)(idx - LORA_CRC_SIZE));

  uint8_t destId  = frame[0];
  uint8_t srcId   = frame[1];
  uint8_t msgType = frame[2];
  size_t payloadLen = (size_t)(idx - LORA_HEADER_SIZE - LORA_CRC_SIZE);
  const uint8_t *payload = frame + LORA_HEADER_SIZE;

  rxCount++;
  Serial.println("═════════════════════════");
  Serial.printf("[RX #%lu] size=%d  RSSI=%d dBm  SNR=%.1f dB\n",
                (unsigned long)rxCount, packetSize,
                LoRa.packetRssi(), LoRa.packetSnr());

  if (calcCrc != storedCrc) {
    crcErrors++;
    Serial.printf("  CRC BAD (stored=%04X calc=%04X) — пакет отброшен\n",
                  storedCrc, calcCrc);
    return;
  }

  bool mine = isForMe(destId);
  Serial.printf("  ОТ КОГО:  %s (id=%d)\n", nodeName(srcId), srcId);
  Serial.printf("  КОМУ:     %s (dest=%d)\n", deliveryLabel(destId), destId);
  Serial.printf("  РЕЖИМ:    %s\n", mine ? ">>> ОБРАБАТЫВАЮ <<<" : ">>> НЕ ДЛЯ МЕНЯ (только лог) <<<");
  Serial.printf("  type=0x%02X (%s)  payload=%u байт\n",
                msgType, msgTypeName(msgType), (unsigned)payloadLen);

  if (payloadLen > 0) {
    Serial.print("  data(utf8): \"");
    printPayloadUtf8(payload, payloadLen);
    Serial.println("\"");
    Serial.print("  data(hex):   ");
    printHexPayload(payload, payloadLen);
    Serial.println();
  } else {
    Serial.println("  data: (пусто)");
  }

  if (msgType == MSG_PING && mine && srcId != MY_NODE_ID) {
    char reply[32];
    snprintf(reply, sizeof(reply), "pong from %d", MY_NODE_ID);
    sendText(srcId, MSG_TEXT, reply);
  }
}

void printHelp() {
  Serial.println();
  Serial.println("Команды Serial:");
  Serial.println("  HELP              — эта справка");
  Serial.println("  STATUS            — состояние узла");
  Serial.println("  BCAST <текст>     — broadcast всем (dest=0xFF)");
  Serial.println("  TO <1-4> <текст>  — unicast конкретному ESP");
  Serial.println("  PING              — broadcast ping");
  Serial.println("  AUTO ON | OFF     — авто-ping каждые 5 с");
  Serial.println("  TYPE <hex>        — тип для BCAST/TO (по умолч. 0x02 TEXT)");
  Serial.println();
  Serial.println("Формат пакета в эфире:");
  Serial.println("  [dest][src][type][payload...][CRC16 LE]");
  Serial.println("  dest=0xFF → broadcast, dest=1..4 → unicast");
  Serial.println();
}

void printStatus() {
  Serial.println();
  Serial.printf("Узел: %s (MY_NODE_ID=%d)\n", nodeName(MY_NODE_ID), MY_NODE_ID);
  Serial.printf("TX=%lu  RX=%lu  CRC errors=%lu\n",
                (unsigned long)txCount, (unsigned long)rxCount, (unsigned long)crcErrors);
  Serial.printf("Auto-ping: %s  default type=0x%02X (%s)\n",
                autoPing ? "ON" : "OFF", defaultMsgType, msgTypeName(defaultMsgType));
  Serial.println();
}

void handleSerialCommand(const String &line) {
  String cmd = line;
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd.equalsIgnoreCase("HELP") || cmd.equalsIgnoreCase("?")) {
    printHelp();
    return;
  }

  if (cmd.equalsIgnoreCase("STATUS")) {
    printStatus();
    return;
  }

  if (cmd.equalsIgnoreCase("PING")) {
    char buf[24];
    snprintf(buf, sizeof(buf), "ping from %d", MY_NODE_ID);
    sendText(LORA_BROADCAST, MSG_PING, buf);
    return;
  }

  if (cmd.equalsIgnoreCase("AUTO ON")) {
    autoPing = true;
    lastAutoPing = millis();
    Serial.println("Auto-ping: ON (каждые 5 с, broadcast)");
    return;
  }

  if (cmd.equalsIgnoreCase("AUTO OFF")) {
    autoPing = false;
    Serial.println("Auto-ping: OFF");
    return;
  }

  if (cmd.startsWith("TYPE ")) {
    String hex = cmd.substring(5);
    hex.trim();
    defaultMsgType = (uint8_t)strtol(hex.c_str(), NULL, 0);
    Serial.printf("Default type = 0x%02X (%s)\n", defaultMsgType, msgTypeName(defaultMsgType));
    return;
  }

  if (cmd.startsWith("BCAST ")) {
    String text = cmd.substring(6);
    text.trim();
    if (text.length() == 0) {
      Serial.println("ERR: BCAST <текст>");
      return;
    }
    sendText(LORA_BROADCAST, defaultMsgType, text);
    return;
  }

  if (cmd.startsWith("TO ")) {
    int spaceIdx = cmd.indexOf(' ', 3);
    if (spaceIdx < 0) {
      Serial.println("ERR: TO <1-4> <текст>");
      return;
    }
    int dest = cmd.substring(3, spaceIdx).toInt();
    String text = cmd.substring(spaceIdx + 1);
    text.trim();
    if (dest < 1 || dest > 4) {
      Serial.println("ERR: dest должен быть 1..4");
      return;
    }
    if (text.length() == 0) {
      Serial.println("ERR: TO <1-4> <текст>");
      return;
    }
    sendText((uint8_t)dest, defaultMsgType, text);
    return;
  }

  Serial.println("Неизвестная команда. Введите HELP");
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=== LoRa 4-NODE TEST ===");
  Serial.printf("Узел: %s (MY_NODE_ID=%d)\n", nodeName(MY_NODE_ID), MY_NODE_ID);

  if (MY_NODE_ID < 1 || MY_NODE_ID > 4) {
    Serial.println("ERR: MY_NODE_ID должен быть 1..4 — исправьте в коде и перепрошейте!");
    while (true) delay(1000);
  }

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

  Serial.println("--- Диагностика SPI ---");
  printSpiDiagnostic();

  if (!LoRa.begin(LORA_FREQ_HZ)) {
    Serial.println();
    Serial.println("LoRa init FAILED!");
    Serial.println("Swap-тест: переставьте Ra-01 на другой ESP —");
    Serial.println("  ошибка переехала → мёртвый модуль");
    Serial.println("  ошибка осталась  → провода/плата");
    while (true) {
      delay(5000);
      printSpiDiagnostic();
    }
  }

  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setTxPower(LORA_TX_POWER);
  LoRa.receive();

  Serial.println("LoRa OK. Слушаю эфир...");
  printHelp();
  lastHeartbeat = millis();
}

void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize > 0) {
    handleReceivedPacket(packetSize);
    LoRa.receive();
  }

  while (Serial.available()) {
    int raw = Serial.read();
    if (raw < 0) break;
    uint8_t c = (uint8_t)raw;
    if (c == '\n' || c == '\r') {
      if (serialLine.length() > 0) {
        handleSerialCommand(serialLine);
        serialLine = "";
      }
    } else {
      serialLine += (char)c;
      if (serialLine.length() > 160) serialLine = "";
    }
  }

  if (autoPing && millis() - lastAutoPing >= 5000) {
    lastAutoPing = millis();
    char buf[24];
    snprintf(buf, sizeof(buf), "auto #%lu", (unsigned long)(txCount + 1));
    sendText(LORA_BROADCAST, MSG_PING, buf);
  }

  if (millis() - lastHeartbeat >= 10000) {
    lastHeartbeat = millis();
    Serial.printf("[...] %s слушает (RX=%lu)\n", nodeName(MY_NODE_ID), (unsigned long)rxCount);
  }
}
