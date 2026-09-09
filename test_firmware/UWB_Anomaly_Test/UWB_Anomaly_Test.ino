/**
 * S.T.A.L.K.E.R. — UWB_Anomaly_Test (тестовая прошивка, НЕ боевая)
 *
 * Роль: АНОМАЛИЯ (cat=0) — BU03 как ANCHOR (role=1), ESP-NOW урон.
 * Проверяет сценарии:
 *   1) один ПДА в радиусе двух разных аномалий
 *   2) несколько ПДА в радиусе одной аномалии
 *   3) несколько ПДА в радиусе нескольких аномалий
 *
 * Железо: ESP32-S3 + BU03-Kit (UART G1/G2, PWR G42) — как на плате.
 *   На макете без ключа: BU03 питается напрямую 3.3V → HAS_BU03_PWR 0.
 *
 * Логика (по MASTER_SPECIFICATION.md §14 «Аномалия — anchor на точке»):
 *   - ПДА шлёт ZONE_HELLO (ESP-NOW) → аномалия выдаёт ZONE_ASSIGN slot=N
 *   - ПДА: AT+SETCFG=N,0,1,1 (tag, слот N) → SLOT_READY
 *   - Аномалия опрашивает AT+DISTANCE; dist < cfgRadius → DAMAGE/RADIATION
 *     по MAC из таблицы слотов (unicast, не broadcast)
 *   - Выход из зоны → SLOT_RELEASE → сброс слота
 *
 * Протокол пакетов — совпадает с боевыми Field_ESP32.ino / PDA_ESP32.ino.
 * Serial Monitor: 115200.
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
//  КОНФИГ ТЕСТА (правьте под сценарий)
// ─────────────────────────────────────────────────────
#define ANOMALY_ID       1     // 1 или 2 — для логов (не UWB ID)
#define UWB_ANCHOR_ID    1     // фиксированный UWB ID аномалии (0–10)
#define CFG_RADIUS_M     3.0f  // радиус зоны, метры
#define CFG_DMG          10    // урон за тик
#define CFG_DMG_MASK     1     // битмаска типа урона (1 = ВЗРЫВ)
#define CFG_RAD_DMG      0     // 0 = без радиации; >0 = RAD за тик
#define DMG_INTERVAL_MS  2000  // период урона
#define RAD_INTERVAL_MS  3000  // период радиации
#define ZONE_TIMEOUT_MS  5000  // слот жив, пока есть пакеты от ПДА

// BU03
#define BU03_RX          1
#define BU03_TX          2
#define BU03_PWR         42
#define HAS_BU03_PWR     1     // 0 = BU03 питается напрямую 3.3V (макет)
#define BU03_CH          1     // канал UWB (CH:1 — весь полигон)
#define BU03_RATE        1     // 6.8M
#define AT_TIMEOUT_MS    1500
#define BOOT_WAIT_MS     2500
#define DIST_POLL_MS     1000

// ─────────────────────────────────────────────────────
//  СЛОТЫ (slot ↔ ESP-NOW MAC)
// ─────────────────────────────────────────────────────
#define MAX_SLOTS 8
struct SlotEntry {
  bool    used;
  uint8_t mac[6];
  uint32_t lastSeenMs;
};
SlotEntry slots[MAX_SLOTS];

uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ─────────────────────────────────────────────────────
//  BU03 (UART AT)
// ─────────────────────────────────────────────────────
HardwareSerial bu03Serial(1);
String bu03Rx;
float lastDistM = -1.0f;
uint32_t lastDistMs = 0;

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

bool bu03WaitResponse(uint32_t ms, bool needDistance) {
  bu03Rx = "";
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    while (bu03Serial.available()) {
      char c = (char)bu03Serial.read();
      if (c == '\r') continue;
      if (bu03Rx.length() < 320) bu03Rx += c;
    }
    if (needDistance) {
      if (bu03Rx.indexOf("distance:") >= 0 && bu03Rx.indexOf("OK") >= 0)
        return true;
    } else if (bu03Rx.indexOf("OK") >= 0 || bu03Rx.indexOf("ERR") >= 0) {
      return true;
    }
    delay(1);
  }
  return bu03Rx.length() > 0;
}

bool bu03SendCmd(const char *cmd, bool needDistance) {
  bu03Flush();
  bu03Serial.print(cmd);
  bu03Serial.print("\r\n");
  return bu03WaitResponse(AT_TIMEOUT_MS, needDistance);
}

float bu03ParseDistance(const String &s) {
  int i = s.indexOf("distance:");
  if (i < 0) return -1.0f;
  return s.substring(i + 9).toFloat();
}

bool initBu03() {
  bu03Power(true);
  bu03Serial.setRxBufferSize(1024);
  bu03Serial.begin(115200, SERIAL_8N1, BU03_RX, BU03_TX);
  delay(400);
  bu03Flush();

  if (!bu03SendCmd("AT", false) || bu03Rx.indexOf("OK") < 0) {
    Serial.println("[BU03] NO LINK — check G1/G2, G42 PWR");
    return false;
  }

  // Роль anchor (1), фиксированный ID точки, канал/скорость полигона
  char cmd[48];
  snprintf(cmd, sizeof(cmd), "AT+SETCFG=%d,1,%d,%d", UWB_ANCHOR_ID, BU03_CH, BU03_RATE);
  if (!bu03SendCmd(cmd, false) || bu03Rx.indexOf("ERR") >= 0) {
    Serial.println("[BU03] SETCFG anchor FAIL");
    return false;
  }
  bu03SendCmd("AT+SAVE", false);

  Serial.println("[BU03] anchor ready, ID=" + String(UWB_ANCHOR_ID));
  return true;
}

bool pollDistance() {
  if (!bu03SendCmd("AT+DISTANCE", true)) return false;
  if (bu03Rx.indexOf("ERR") >= 0) return false;
  float d = bu03ParseDistance(bu03Rx);
  if (d < 0.0f) return false;
  lastDistM = d;
  lastDistMs = millis();
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

int findSlotByMac(const uint8_t *mac) {
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (slots[i].used && memcmp(slots[i].mac, mac, 6) == 0) return i;
  }
  return -1;
}

int allocSlot(const uint8_t *mac) {
  int idx = findSlotByMac(mac);
  if (idx >= 0) return idx;
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (!slots[i].used) {
      slots[i].used = true;
      memcpy(slots[i].mac, mac, 6);
      slots[i].lastSeenMs = millis();
      return i;
    }
  }
  return -1; // все слоты заняты
}

void releaseSlot(int idx) {
  if (idx < 0 || idx >= MAX_SLOTS) return;
  slots[idx].used = false;
  memset(slots[idx].mac, 0, 6);
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(Packet)) return;
  Packet pkt;
  memcpy(&pkt, data, sizeof(Packet));

  if (pkt.msg_type == MSG_ZONE_HELLO) {
    int slot = allocSlot(info->src_addr);
    if (slot < 0) {
      Serial.println("ZONE_HELLO: no free slot");
      return;
    }
    slots[slot].lastSeenMs = millis();
    Serial.printf("ZONE_HELLO from %02X:%02X:%02X:%02X:%02X:%02X → slot %d\n",
                  info->src_addr[0], info->src_addr[1], info->src_addr[2],
                  info->src_addr[3], info->src_addr[4], info->src_addr[5], slot);
    sendPacketTo(info->src_addr, EMITTER_ANOMALY, MSG_ZONE_ASSIGN, slot, 0, 0);

  } else if (pkt.msg_type == MSG_SLOT_READY) {
    int slot = findSlotByMac(info->src_addr);
    if (slot >= 0) {
      slots[slot].lastSeenMs = millis();
      Serial.printf("SLOT_READY slot %d (MAC %02X:%02X:%02X:%02X:%02X:%02X)\n",
                    slot, info->src_addr[0], info->src_addr[1], info->src_addr[2],
                    info->src_addr[3], info->src_addr[4], info->src_addr[5]);
    }

  } else if (pkt.msg_type == MSG_SLOT_RELEASE) {
    int slot = findSlotByMac(info->src_addr);
    if (slot >= 0) {
      Serial.printf("SLOT_RELEASE slot %d\n", slot);
      releaseSlot(slot);
    }

  } else if (pkt.msg_type == MSG_ACK) {
    // ПДА подтвердил урон — можно логировать (опционально)
    int slot = findSlotByMac(info->src_addr);
    if (slot >= 0) slots[slot].lastSeenMs = millis();
  }
}

// ─────────────────────────────────────────────────────
//  УРОН ПО СЛОТАМ
// ─────────────────────────────────────────────────────
void tickDamage() {
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (!slots[i].used) continue;
    if (millis() - slots[i].lastSeenMs > ZONE_TIMEOUT_MS) {
      Serial.printf("slot %d timeout — release\n", i);
      releaseSlot(i);
      continue;
    }
    // Урон только если ПДА реально в радиусе (UWB)
    if (lastDistM >= 0.0f && lastDistM <= CFG_RADIUS_M) {
      sendPacketTo(slots[i].mac, EMITTER_ANOMALY, MSG_DAMAGE, CFG_DMG, 0, CFG_DMG_MASK);
      Serial.printf("DMG slot %d: %.2f m ≤ %.1f m\n", i, lastDistM, CFG_RADIUS_M);
    } else {
      Serial.printf("slot %d: %.2f m > %.1f m (вне зоны)\n", i, lastDistM, CFG_RADIUS_M);
    }
  }
}

void tickRadiation() {
  if (CFG_RAD_DMG <= 0) return;
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (!slots[i].used) continue;
    if (lastDistM >= 0.0f && lastDistM <= CFG_RADIUS_M) {
      sendPacketTo(slots[i].mac, EMITTER_ANOMALY, MSG_RADIATION, CFG_RAD_DMG, 0, 0);
      Serial.printf("RAD slot %d\n", i);
    }
  }
}

// ─────────────────────────────────────────────────────
//  SETUP / LOOP
// ─────────────────────────────────────────────────────
uint32_t lastDmgMs = 0;
uint32_t lastRadMs = 0;

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("=== UWB_Anomaly_Test ===");
  Serial.printf("Anomaly #%d, UWB anchor ID=%d, radius=%.1f m\n",
                ANOMALY_ID, UWB_ANCHOR_ID, CFG_RADIUS_M);

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

  Serial.println("READY — жду ZONE_HELLO от ПДА");
}

void loop() {
  uint32_t now = millis();

  // Опрос дистанции (аномалия меряет сама)
  if (now - lastDistMs >= DIST_POLL_MS) {
    lastDistMs = now;
    if (pollDistance()) {
      Serial.printf("DIST: %.2f m\n", lastDistM);
    } else {
      Serial.println("DIST: no data");
      lastDistM = -1.0f;
    }
  }

  if (now - lastDmgMs >= DMG_INTERVAL_MS) {
    lastDmgMs = now;
    tickDamage();
  }

  if (now - lastRadMs >= RAD_INTERVAL_MS) {
    lastRadMs = now;
    tickRadiation();
  }
}