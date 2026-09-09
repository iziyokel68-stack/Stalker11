/**
 * S.T.A.L.K.E.R. — UWB_PDA_Test (тестовая прошивка, НЕ боевая)
 *
 * Роль: ПДА — BU03 как TAG (role=0), приём урона по ESP-NOW.
 * Проверяет сценарии:
 *   1) один ПДА в радиусе двух разных аномалий
 *   2) несколько ПДА в радиусе одной аномалии
 *   3) несколько ПДА в радиусе нескольких аномалий
 *
 * Железо: ESP32-S3 + BU03-Kit (UART G1/G2, PWR G42) — как на плате.
 *   На макете без ключа: BU03 питается напрямую 3.3V → HAS_BU03_PWR 0.
 *   Без экрана: вся диагностика в Serial Monitor (115200).
 *
 * Логика (по MASTER_SPECIFICATION.md §14 «Аномалия — anchor на точке»):
 *   - ПДА шлёт ZONE_HELLO (ESP-NOW) → аномалия выдаёт ZONE_ASSIGN slot=N
 *   - ПДА: AT+SETCFG=N,0,1,1 (tag, слот N) → SLOT_READY
 *   - Аномалия опрашивает AT+DISTANCE; dist < cfgRadius → DAMAGE/RADIATION
 *     по MAC из таблицы слотов (unicast)
 *   - Выход из зоны → SLOT_RELEASE → сброс слота
 *
 * Протокол пакетов — совпадает с боевыми Field_ESP32.ino / PDA_ESP32.ino.
 */

#include <WiFi.h>
#include <esp_now.h>

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
#define BU03_RX          1
#define BU03_TX          2
#define BU03_PWR         42
#define HAS_BU03_PWR     1     // 0 = BU03 питается напрямую 3.3V (макет)
#define BU03_CH          1     // канал UWB (CH:1 — весь полигон)
#define BU03_RATE        1     // 6.8M
#define AT_TIMEOUT_MS    1500
#define BOOT_WAIT_MS     2500

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

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(Packet)) return;
  Packet pkt;
  memcpy(&pkt, data, sizeof(Packet));

  if (pkt.msg_type == MSG_ZONE_ASSIGN) {
    int slot = (int)pkt.val1;
    Serial.printf("ZONE_ASSIGN slot %d from %02X:%02X:%02X:%02X:%02X:%02X\n",
                  slot, info->src_addr[0], info->src_addr[1], info->src_addr[2],
                  info->src_addr[3], info->src_addr[4], info->src_addr[5]);
    if (bu03SetSlot(slot)) {
      mySlot = slot;
      slotReady = true;
      lastSlotSeenMs = millis();
      sendPacketTo(info->src_addr, EMITTER_PLAYER, MSG_SLOT_READY, slot, 0, 0);
      Serial.printf("SLOT_READY sent (slot %d)\n", slot);
    } else {
      slotReady = false;
    }

  } else if (pkt.msg_type == MSG_DAMAGE && pkt.val1 > 0) {
    lastDmgAmount = pkt.val1;
    lastDmgMask = pkt.val3;
    dmgCount++;
    lastDmgMs = millis();
    lastSlotSeenMs = millis();
    Serial.printf("DMG +%d mask=0x%X (total %d) from %02X:%02X:%02X:%02X:%02X:%02X\n",
                  lastDmgAmount, lastDmgMask, dmgCount,
                  info->src_addr[0], info->src_addr[1], info->src_addr[2],
                  info->src_addr[3], info->src_addr[4], info->src_addr[5]);
    // ACK — аномалия обновляет lastSeenMs слота
    sendPacketTo(info->src_addr, EMITTER_PLAYER, MSG_ACK, 0, 0, 0);

  } else if (pkt.msg_type == MSG_RADIATION && pkt.val1 > 0) {
    lastRadAmount = pkt.val1;
    radCount++;
    lastRadMs = millis();
    lastSlotSeenMs = millis();
    Serial.printf("RAD +%d (total %d) from %02X:%02X:%02X:%02X:%02X:%02X\n",
                  lastRadAmount, radCount,
                  info->src_addr[0], info->src_addr[1], info->src_addr[2],
                  info->src_addr[3], info->src_addr[4], info->src_addr[5]);
  }
}

// ─────────────────────────────────────────────────────
//  SETUP / LOOP
// ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("=== UWB_PDA_Test ===");
  Serial.printf("PDA #%d, BU03 tag, жду ZONE_ASSIGN\n", PDA_ID);

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(1);
  WiFi.disconnect();
  delay(100);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init failed");
    return;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMac, 6);
  peer.channel = 1;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  esp_now_register_recv_cb(OnDataRecv);

  initBu03();

  Serial.println("READY — шлю ZONE_HELLO");
}

void loop() {
  uint32_t now = millis();

  // ZONE_HELLO — пока нет слота (или периодически для обновления)
  if (now - lastHelloMs >= ZONE_HELLO_MS) {
    lastHelloMs = now;
    sendBroadcast(EMITTER_PLAYER, MSG_ZONE_HELLO, PDA_ID, 0, 0);
    Serial.printf("ZONE_HELLO (PDA #%d)\n", PDA_ID);
  }

  // Авто-выход из зоны (тест) — SLOT_RELEASE
  if (SLOT_RELEASE_MS > 0 && mySlot >= 0 &&
      now - lastSlotSeenMs >= SLOT_RELEASE_MS) {
    Serial.printf("AUTO SLOT_RELEASE slot %d\n", mySlot);
    sendBroadcast(EMITTER_PLAYER, MSG_SLOT_RELEASE, mySlot, 0, 0);
    mySlot = -1;
    slotReady = false;
  }
}