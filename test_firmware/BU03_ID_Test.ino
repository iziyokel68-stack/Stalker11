/**
 * BU03 ID test — ESP меняет ID через AT (база на аномалии)
 * TX1 → GPIO17, RX1 ← GPIO18, GND
 *
 * Цикл: прочитать ID → 0, restart, 1, restart … → UWB OFF → UWB ON → вернуть исходный ID
 * Serial 115200. После теста прошейте BU03_Distance_AT.ino
 *
 * #define BU03_ROLE 1 — база (ваша схема). 0 — тег на ПДА.
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>

#define TFT_CS   10
#define TFT_DC   11
#define TFT_RST  12
#define TFT_SCK  13
#define TFT_MOSI 14
#define TFT_BL   21

#define BU03_RX  17
#define BU03_TX  18

#define BU03_ROLE    1   // 1=anchor (база), 0=tag (ПДА)
#define BU03_CH      1   // ch5
#define BU03_RATE    1   // 6.8M
#define TEST_ID_MAX  5   // тест ID 0..5 (поставьте 10 для полного диапазона)
#define STEP_MS    1000
#define AT_TIMEOUT  1500
#define BOOT_MS     2800
#define UWB_OFF_MS  2000

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

String rx;
int origId = -1;
int testId = 0;
int phase = 0;          // 0=set, 1=verify live, 2=save, 3=restart, 4=wait boot, 5=verify reboot
bool cycling = false;
bool uwbPollEnabled = true;
uint32_t stepAt = 0;
uint32_t bootAt = 0;
char lineBuf[48];

static void flushRx() {
  while (Serial2.available()) (void)Serial2.read();
}

static bool waitOk(uint32_t timeoutMs) {
  rx = "";
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (Serial2.available()) {
      char c = (char)Serial2.read();
      if (c == '\r') continue;
      if (rx.length() < 320) rx += c;
    }
    if (rx.indexOf("OK") >= 0 || rx.indexOf("ERR") >= 0) return true;
    delay(1);
  }
  return rx.length() > 0;
}

static int parseIdFromCfg(const String &s) {
  int i = s.indexOf("ID:");
  if (i < 0) return -1;
  return s.substring(i + 3).toInt();
}

static bool sendCmd(const char *cmd, const char *label) {
  flushRx();
  Serial.printf(">> %s\n", cmd);
  Serial2.print(cmd);
  Serial2.print("\r\n");
  if (!waitOk(AT_TIMEOUT)) {
    Serial.printf("!! timeout (%s)\n", label);
    if (rx.length()) Serial.println(rx);
    return false;
  }
  Serial.println(rx);
  return rx.indexOf("ERR") < 0;
}

static bool getCfg(int *outId) {
  if (!sendCmd("AT+GETCFG", "GETCFG")) return false;
  if (outId) *outId = parseIdFromCfg(rx);
  return true;
}

static bool setCfg(int id) {
  snprintf(lineBuf, sizeof(lineBuf), "AT+SETCFG=%d,%d,%d,%d", id, BU03_ROLE, BU03_CH, BU03_RATE);
  return sendCmd(lineBuf, "SETCFG");
}

static bool saveCfg() {
  return sendCmd("AT+SAVE", "SAVE");
}

static bool restartBu03() {
  return sendCmd("AT+RESTART", "RESTART");
}

static void drawTft(const char *title, int id, bool pollOn) {
  tft.fillScreen(0x0000);
  tft.setTextColor(0x07FF);
  tft.setTextSize(2);
  tft.setCursor(4, 8);
  tft.print(title);
  tft.setTextColor(0xFFE0);
  tft.setCursor(4, 40);
  tft.printf("ID: %d", id);
  tft.setTextColor(pollOn ? 0x07E0 : 0xF800);
  tft.setCursor(4, 68);
  tft.print(pollOn ? "UWB: ON" : "UWB: OFF");
  if (origId >= 0) {
    tft.setTextColor(0x3186);
    tft.setCursor(4, 96);
    tft.printf("orig: %d", origId);
  }
}

static void stepSetId(int id) {
  Serial.printf("\n=== SET ID=%d ===\n", id);
  drawTft("SET ID", id, uwbPollEnabled);
  setCfg(id);
}

static void stepVerify(const char *tag) {
  int id = -1;
  Serial.printf("--- verify %s ---\n", tag);
  if (getCfg(&id)) {
    Serial.printf("parsed ID=%d (expect %d)\n", id, testId);
    drawTft(tag, id, uwbPollEnabled);
  }
}

static void stepRestoreOrig() {
  if (origId < 0) return;
  Serial.printf("\n=== RESTORE orig ID=%d ===\n", origId);
  drawTft("RESTORE", origId, uwbPollEnabled);
  setCfg(origId);
  saveCfg();
  restartBu03();
  delay(BOOT_MS);
  flushRx();
  getCfg(nullptr);
}

void setup() {
  Serial.begin(115200);
#if TFT_BL >= 0
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif
  SPI.begin(TFT_SCK, -1, TFT_MOSI, -1);
  tft.begin();
  tft.setRotation(1);
  drawTft("BOOT", -1, true);

  Serial2.setRxBufferSize(1024);
  Serial2.begin(115200, SERIAL_8N1, BU03_RX, BU03_TX);

  Serial.println(F("\n=== BU03 ID TEST ==="));
  Serial.printf("role=%s CH=%d\n", BU03_ROLE ? "anchor" : "tag", BU03_CH);
  delay(BOOT_MS);
  flushRx();

  if (!sendCmd("AT", "AT")) {
    Serial.println(F("link FAIL — TX1/RX1?"));
    drawTft("NO LINK", -1, false);
    return;
  }

  if (getCfg(&origId)) {
    Serial.printf("original ID=%d\n", origId);
    testId = 0;
    cycling = true;
    phase = 0;
    stepAt = millis();
    stepSetId(testId);
  }
}

void loop() {
  uint32_t now = millis();

  if (cycling && (now - stepAt >= STEP_MS)) {
    stepAt = now;

    if (phase == 0) {
      phase = 1;
      stepVerify("after SET (live)");
    } else if (phase == 1) {
      phase = 2;
      Serial.println("--- SAVE ---");
      saveCfg();
    } else if (phase == 2) {
      phase = 3;
      Serial.println("--- RESTART ---");
      restartBu03();
      bootAt = now;
      phase = 4;
      stepAt = now;
    } else if (phase == 4) {
      if (now - bootAt < BOOT_MS) return;
      flushRx();
      phase = 5;
      stepVerify("after RESTART");
    } else if (phase == 5) {
      testId++;
      if (testId <= TEST_ID_MAX) {
        phase = 0;
        stepSetId(testId);
      } else {
        cycling = false;
        Serial.println(F("\n=== UWB OFF (ESP stops AT) ==="));
        uwbPollEnabled = false;
        drawTft("UWB OFF", parseIdFromCfg(rx), false);
        stepAt = now;
        phase = 10;
      }
    } else if (phase == 10) {
      if (now - stepAt < UWB_OFF_MS) return;
      Serial.println(F("=== UWB ON (AT+DISTANCE) ==="));
      uwbPollEnabled = true;
      drawTft("UWB ON", -1, true);
      if (BU03_ROLE == 1)
        sendCmd("AT+DISTANCE", "DISTANCE");
      phase = 11;
      stepAt = now;
    } else if (phase == 11) {
      stepRestoreOrig();
      Serial.println(F("\n=== TEST DONE — check Serial log ==="));
      Serial.println(F("ID should have cycled 0..N with GETCFG after each step."));
      drawTft("DONE", origId, true);
      phase = 99;
    }
  }

  if (uwbPollEnabled && BU03_ROLE == 1 && !cycling && phase == 99) {
    static uint32_t lastDist = 0;
    if (now - lastDist >= 2000) {
      lastDist = now;
      sendCmd("AT+DISTANCE", "DISTANCE");
    }
  }
}
