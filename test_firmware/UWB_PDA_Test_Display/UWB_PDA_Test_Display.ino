/**
 * S.T.A.L.K.E.R. — UWB_PDA_Test_Display (тестовая прошивка, НЕ боевая)
 *
 * Роль: ПДА — BU03 как TAG (role=0), приём урона по ESP-NOW.
 * ВАРИАНТ ДЛЯ СОБРАННОГО ПДА С ЭКРАНОМ: вся диагностика на TFT (320×240),
 * Serial остаётся только для ошибок инициализации.
 *
 * Зачем: при тесте 3 сценариев можно одновременно считывать только 2 Serial.
 * Собранный ПДА с экраном становится ТРЕТЬИМ выводом информации — статус
 * виден прямо на устройстве, без COM-порта.
 *
 * Логика — та же, что в UWB_PDA_Test.ino (см. MASTER_SPECIFICATION.md §14):
 *   - ПДА шлёт ZONE_HELLO (ESP-NOW) → аномалия выдаёт ZONE_ASSIGN slot=N
 *   - ПДА: AT+SETCFG=N,0,1,1 (tag, слот N) → SLOT_READY
 *   - Аномалия опрашивает AT+DISTANCE; dist < cfgRadius → DAMAGE/RADIATION
 *     по MAC из таблицы слотов (unicast)
 *   - Выход из зоны → SLOT_RELEASE → сброс слота
 *
 * Экран: M024 320×240 (ILI9342-подобный) — tft_panel.h из боевого ПДА.
 * Протокол пакетов — совпадает с боевыми Field_ESP32.ino / PDA_ESP32.ino.
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WiFi.h>
#include <esp_now.h>

// ─────────────────────────────────────────────────────
//  РАСПИНОВКА (как на собранном ПДА — MASTER_SPEC §3)
// ─────────────────────────────────────────────────────
#define TFT_CS 10
#define TFT_DC 11
#define TFT_RST 12
#define TFT_SCK 13
#define TFT_MOSI 14
#define TFT_MISO 40
#include "tft_panel.h"

// BU03 UWB (Serial1)
#define BU03_RX 1
#define BU03_TX 2
#define BU03_PWR 42
#define HAS_BU03_PWR 1 // 0 = BU03 питается напрямую 3.3V (макет)

// ─────────────────────────────────────────────────────
//  ПРОТОКОЛ (совпадает с боевыми прошивками)
// ─────────────────────────────────────────────────────
#pragma pack(push, 1)
struct Packet {
  uint8_t  emitter;
  uint8_t  msg_type;
  int16_t  val1;
  int16_t  val2;
  int16_t  val3;
};
#pragma pack(pop)

#define EMITTER_ANOMALY 2
#define EMITTER_PLAYER  1

// Msg (protocol.py v4.2)
#define MSG_DAMAGE      1
#define MSG_RADIATION   3
#define MSG_ACK         6
#define MSG_SAFE_ZONE   7

// Зонные пакеты (черновик спеки §14) — расширяем msg_type
#define MSG_ZONE_HELLO   20
#define MSG_ZONE_ASSIGN  21
#define MSG_SLOT_READY   22
#define MSG_SLOT_RELEASE 23

// ─────────────────────────────────────────────────────
//  КОНФИГ ТЕСТА
// ─────────────────────────────────────────────────────
#define PDA_ID           1     // 1, 2, 3… — для логов
#define ZONE_HELLO_MS    1000  // период ZONE_HELLO
#define ZONE_TIMEOUT_MS  5000  // слот жив, пока есть пакеты от ПДА
#define SLOT_RELEASE_MS  8000  // авто-выход из зоны (тест) — 0 = не выходить

// BU03
#define BU03_CH          1     // канал UWB (CH:1 — весь полигон)
#define BU03_RATE        1     // 6.8M
#define AT_TIMEOUT_MS    1500
#define BOOT_WAIT_MS     2500

// ─────────────────────────────────────────────────────
//  ЦВЕТА (как в боевом ПДА)
// ─────────────────────────────────────────────────────
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_GREEN 0x07E0
#define C_YELLOW 0xFFE0
#if TFT_BGR_SWAP
#define C_RED 0x001F
#else
#define C_RED 0xF800
#endif
#define C_ORANGE 0xFD20
#define C_CYAN 0x07FF
#define C_DGRAY 0x3186
#define C_LGRAY 0xC618
#define C_BORDER 0x7BCF

// ─────────────────────────────────────────────────────
//  ЭКРАН
// ─────────────────────────────────────────────────────
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

#define SCR_W TFT_SCR_W
#define SCR_H TFT_SCR_H

void idleSpiChipSelects() {
  pinMode(TFT_CS, OUTPUT);
  pinMode(LORA_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(LORA_CS, HIGH);
}

bool initDisplay() {
  idleSpiChipSelects();
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, -1);
  tft.begin();
  tftApplyPanel(tft);
  tft.fillScreen(C_BLACK);
  u8g2.begin(tft);
  u8g2.setFontMode(1);
  u8g2.setFontDirection(0);
  u8g2.setFont(u8g2_font_8x13_t_cyrillic);
  return true;
}

void printRus(int x, int y, const char *text, uint16_t color) {
  u8g2.setForegroundColor(color);
  u8g2.setCursor(x, y + 13);
  u8g2.print(text);
}

void printRusStr(int x, int y, const String &text, uint16_t color) {
  u8g2.setForegroundColor(color);
  u8g2.setCursor(x, y + 13);
  u8g2.print(text);
}

// ─────────────────────────────────────────────────────
//  СОСТОЯНИЕ
// ─────────────────────────────────────────────────────
int  mySlot = -1;          // выданный аномалией слот (-1 = нет)
bool slotReady = false;    // BU03 принял слот
uint32_t lastHelloMs = 0;
uint32_t lastSlotSeenMs = 0;
uint32_t lastDmgMs = 0;
uint32_t lastRadMs = 0;
int  dmgCount = 0;
int  radCount = 0;
int  lastDmgAmount = 0;
int  lastDmgMask = 0;
int  lastRadAmount = 0;
char lastDmgMac[18] = "-";
char lastRadMac[18] = "-";
char lastAssignMac[18] = "-";

uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ─────────────────────────────────────────────────────
//  BU03 (UART AT)
// ─────────────────────────────────────────────────────
HardwareSerial bu03Serial(1);
String bu03Rx;

void bu03Power(bool on) {
#if HAS_BU03_PWR
  pinMode(BU03_PWR, OUTPUT);
  digitalWrite(BU03_PWR, on ? HIGH : LOW);
  if (on) delay(BOOT_WAIT_MS);
#else
  (void)on;
#endif
}

void bu03Flush() {
  while (bu03Serial.available()) (void)bu03Serial.read();
}

bool bu03WaitResponse(uint32_t ms) {
  bu03Rx = "";
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    while (bu03Serial.available()) {
      char c = (char)bu03Serial.read();
      if (c == '\r') continue;
      if (bu03Rx.length() < 320) bu03Rx += c;
    }
    if (bu03Rx.indexOf("OK") >= 0 || bu03Rx.indexOf("ERR") >= 0) return true;
    delay(1);
  }
  return bu03Rx.length() > 0;
}

bool bu03SendCmd(const char *cmd) {
  bu03Flush();
  bu03Serial.print(cmd);
  bu03Serial.print("\r\n");
  return bu03WaitResponse(AT_TIMEOUT_MS);
}

bool initBu03() {
  bu03Power(true);
  bu03Serial.setRxBufferSize(1024);
  bu03Serial.begin(115200, SERIAL_8N1, BU03_RX, BU03_TX);
  delay(400);
  bu03Flush();

  if (!bu03SendCmd("AT") || bu03Rx.indexOf("OK") < 0) {
    Serial.println("[BU03] NO LINK — check G1/G2, G42 PWR");
    return false;
  }
  Serial.println("[BU03] AT OK");
  return true;
}

// Роль tag (0), слот N, канал/скорость полигона
bool bu03SetSlot(int slot) {
  char cmd[48];
  snprintf(cmd, sizeof(cmd), "AT+SETCFG=%d,0,%d,%d", slot, BU03_CH, BU03_RATE);
  if (!bu03SendCmd(cmd) || bu03Rx.indexOf("ERR") >= 0) {
    Serial.printf("[BU03] SETCFG slot %d FAIL\n", slot);
    return false;
  }
  bu03SendCmd("AT+SAVE");
  Serial.printf("[BU03] tag slot %d set\n", slot);
  return true;
}

// ─────────────────────────────────────────────────────
//  ESP-NOW
// ─────────────────────────────────────────────────────
void sendPacketTo(const uint8_t *mac, uint8_t emitter, uint8_t msg,
                  int16_t v1, int16_t v2, int16_t v3) {
  Packet pkt;
  pkt.emitter  = emitter;
  pkt.msg_type = msg;
  pkt.val1     = v1;
  pkt.val2     = v2;
  pkt.val3     = v3;
  esp_now_send(mac, (uint8_t *)&pkt, sizeof(Packet));
}

void sendBroadcast(uint8_t emitter, uint8_t msg, int16_t v1, int16_t v2, int16_t v3) {
  sendPacketTo(broadcastMac, emitter, msg, v1, v2, v3);
}

void macToStr(const uint8_t *mac, char *out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(Packet)) return;
  Packet pkt;
  memcpy(&pkt, data, sizeof(Packet));

  if (pkt.msg_type == MSG_ZONE_ASSIGN) {
    int slot = (int)pkt.val1;
    macToStr(info->src_addr, lastAssignMac);
    if (bu03SetSlot(slot)) {
      mySlot = slot;
      slotReady = true;
      lastSlotSeenMs = millis();
      sendPacketTo(info->src_addr, EMITTER_PLAYER, MSG_SLOT_READY, slot, 0, 0);
    } else {
      slotReady = false;
    }

  } else if (pkt.msg_type == MSG_DAMAGE && pkt.val1 > 0) {
    lastDmgAmount = pkt.val1;
    lastDmgMask = pkt.val3;
    dmgCount++;
    lastDmgMs = millis();
    lastSlotSeenMs = millis();
    macToStr(info->src_addr, lastDmgMac);
    sendPacketTo(info->src_addr, EMITTER_PLAYER, MSG_ACK, 0, 0, 0);

  } else if (pkt.msg_type == MSG_RADIATION && pkt.val1 > 0) {
    lastRadAmount = pkt.val1;
    radCount++;
    lastRadMs = millis();
    lastSlotSeenMs = millis();
    macToStr(info->src_addr, lastRadMac);
  }
}

// ─────────────────────────────────────────────────────
//  ОТРИСОВКА (третий «Serial» — статус на экране)
// ─────────────────────────────────────────────────────
uint32_t lastDrawMs = 0;
#define DRAW_MS 200

void drawStatus() {
  tft.fillScreen(C_BLACK);

  printRus(8, 6, "PDA #" + String(PDA_ID) + " — UWB TEST", C_CYAN);
  tft.drawFastHLine(8, 24, SCR_W - 16, C_BORDER);

  // Слот
  printRus(12, 40, "СЛОТ:", C_WHITE);
  if (mySlot >= 0 && slotReady) {
    printRusStr(88, 40, String(mySlot), C_GREEN);
  } else {
    printRusStr(88, 40, "-", C_DGRAY);
  }

  // BU03
  printRus(12, 66, "BU03:", C_WHITE);
  printRusStr(88, 66, bu03Rx.length() ? "OK" : "NO LINK", C_GREEN);

  // Урон
  printRus(12, 92, "УРОН:", C_WHITE);
  char dmgBuf[24];
  snprintf(dmgBuf, sizeof(dmgBuf), "+%d x%d", lastDmgAmount, dmgCount);
  printRusStr(88, 92, String(dmgBuf), C_RED);

  // Радиация
  printRus(12, 118, "RAD:", C_WHITE);
  char radBuf[24];
  snprintf(radBuf, sizeof(radBuf), "+%d x%d", lastRadAmount, radCount);
  printRusStr(88, 118, String(radBuf), C_ORANGE);

  // Последний урон — от кого
  printRus(12, 144, "DMG MAC:", C_DGRAY);
  printRusStr(12, 164, String(lastDmgMac), C_LGRAY);

  // Последний слот — от кого
  printRus(12, 190, "SLOT MAC:", C_DGRAY);
  printRusStr(12, 210, String(lastAssignMac), C_LGRAY);
}

// ─────────────────────────────────────────────────────
//  SETUP / LOOP
// ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);

  initDisplay();
  printRus(72, 72, "S.T.A.L.K.E.R.", C_GREEN);
  printRus(108, 100, "UWB PDA TEST", C_LGRAY);
  printRus(108, 128, "Wi-Fi...", C_CYAN);

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(1);
  WiFi.disconnect();
  delay(100);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init failed");
    printRus(108, 156, "ESP-NOW FAIL", C_RED);
    return;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMac, 6);
  peer.channel = 1;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  esp_now_register_recv_cb(OnDataRecv);

  initBu03();

  printRus(108, 184, "READY", C_GREEN);
  delay(500);
}

void loop() {
  uint32_t now = millis();

  // ZONE_HELLO — пока нет слота (или периодически для обновления)
  if (now - lastHelloMs >= ZONE_HELLO_MS) {
    lastHelloMs = now;
    sendBroadcast(EMITTER_PLAYER, MSG_ZONE_HELLO, PDA_ID, 0, 0);
  }

  // Авто-выход из зоны (тест) — SLOT_RELEASE
  if (SLOT_RELEASE_MS > 0 && mySlot >= 0 &&
      now - lastSlotSeenMs >= SLOT_RELEASE_MS) {
    sendBroadcast(EMITTER_PLAYER, MSG_SLOT_RELEASE, mySlot, 0, 0);
    mySlot = -1;
    slotReady = false;
  }

  // Экран — третий вывод информации
  if (now - lastDrawMs >= DRAW_MS) {
    lastDrawMs = now;
    drawStatus();
  }
}