/**

 * BU03-Kit (база) → ESP32-S3: AT+DISTANCE

 * TX1 → GPIO17, RX1 ← GPIO18, GND общий. Тег: Role:0.

 */



#include <Adafruit_GFX.h>

#include <Adafruit_ILI9341.h>

#include <SPI.h>



#define TFT_CS  10

#define TFT_DC  11

#define TFT_RST 12

#define TFT_SCK 13

#define TFT_MOSI 14

#define TFT_BL  21



#define BU03_RX 17

#define BU03_TX 18



#define CAL_OFFSET_M   (-0.20f)

#define POLL_MS          500

#define AT_TIMEOUT_MS   1500

#define STALE_MS        4000

#define NO_SIGNAL_MS    8000

#define BOOT_WAIT_MS    2500



Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);



float distM = 0.0f;

uint32_t lastOkMs = 0;

uint32_t lastPollMs = 0;

String rx;



static void flushRx() {

  while (Serial2.available()) (void)Serial2.read();

}



static float parseDist(const String &s) {

  int i = s.indexOf("distance:");

  if (i < 0) return -1.0f;

  return s.substring(i + 9).toFloat();

}



static bool waitRx(uint32_t timeoutMs, bool needDist) {

  rx = "";

  uint32_t t0 = millis();

  while (millis() - t0 < timeoutMs) {

    while (Serial2.available()) {

      char c = (char)Serial2.read();

      if (c == '\r') continue;

      if (rx.length() < 256) rx += c;

    }

    if (needDist) {

      if (parseDist(rx) >= 0.0f && rx.indexOf("OK") >= 0) return true;

    } else if (rx.indexOf("OK") >= 0) {

      return true;

    }

    delay(1);

  }

  return false;

}



static bool pollDistance() {

  for (int i = 0; i < 3; i++) {

    flushRx();

    Serial2.print("AT+DISTANCE\r\n");

    if (waitRx(AT_TIMEOUT_MS, true)) {

      distM = parseDist(rx) + CAL_OFFSET_M;

      lastOkMs = millis();

      return true;

    }

    delay(100);

  }

  return false;

}



static void logMiss() {

  if (rx.length() == 0) {

    Serial.println(F("miss: no bytes (TX1/RX1 wiring? tag on?)"));

    return;

  }

  Serial.printf("miss [%u]: ", rx.length());

  for (unsigned i = 0; i < rx.length() && i < 100; i++) {

    char c = rx[i];

    if (c >= 32 && c < 127) Serial.write(c);

    else Serial.printf("\\x%02X", (uint8_t)c);

  }

  Serial.println();

}



static void drawScreen() {

  uint32_t age = lastOkMs ? millis() - lastOkMs : NO_SIGNAL_MS + 1;

  int mode = (age >= NO_SIGNAL_MS) ? 0 : (age >= STALE_MS ? 2 : 1);



  static int lastMode = -1;

  static float lastDist = -1.0f;

  if (mode == lastMode && (mode == 0 || distM == lastDist)) return;

  lastMode = mode;

  lastDist = distM;



  tft.fillRect(0, 50, tft.width(), 140, 0x0000);

  if (mode == 0) {

    tft.setTextColor(0xFFE0);

    tft.setTextSize(3);

    tft.setCursor(20, 110);

    tft.print("NO SIGNAL");

    return;

  }

  char buf[12];

  snprintf(buf, sizeof(buf), "%.2f", distM);

  tft.setTextColor(mode == 2 ? 0xFFE0 : 0x07E0);

  tft.setTextSize(6);

  tft.setCursor(36, 88);

  tft.print(buf);

  tft.setTextSize(2);

  tft.setTextColor(0x3186);

  tft.setCursor(tft.width() - 36, 168);

  tft.print("m");

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

  tft.fillScreen(0x0000);

  tft.setTextSize(2);

  tft.setTextColor(0x07FF);

  tft.setCursor(8, 12);

  tft.print("BU03 distance");



  Serial2.setRxBufferSize(1024);

  Serial2.begin(115200, SERIAL_8N1, BU03_RX, BU03_TX);



  Serial.println(F("=== BU03 AT ==="));

  Serial.println(F("TX1->GPIO17, RX1<-GPIO18"));

  delay(BOOT_WAIT_MS);

  flushRx();



  Serial2.print("AT\r\n");

  if (waitRx(800, false)) {

    Serial.println(F("link OK"));

  } else {

    Serial.println(F("link FAIL — swap TX/RX or wire TX1/RX1 (not PA2/PA3)"));

    logMiss();

  }

}



void loop() {

  if (millis() - lastPollMs >= POLL_MS) {

    lastPollMs = millis();

    if (pollDistance())

      Serial.printf("%.3f m\n", distM);

    else

      logMiss();

  }

  drawScreen();

}


