/**
 * S.T.A.L.K.E.R. PDA — guided hardware test (on-screen flow)
 * Board: ESP32-S3-N16R8 only! (Arduino: ESP32S3 Dev Module)
 * Pins: MASTER_SPECIFICATION.md ch.3 — G21 = vibro, NOT TFT_BL
 * TFT: tft_panel.h after begin() (MADCTL 0x88, full 320×240)
 *
 * Libraries (Arduino Library Manager):
 *   Adafruit GFX Library, Adafruit ILI9341, DFRobot DFPlayer Mini,
 *   U8g2_for_Adafruit_GFX
 *
 * SD card (DFPlayer): mp3/0001.mp3 … mp3/0004.mp3
 * Serial Monitor 115200 — logs each step; no commands required.
 *
 * Flow: buttons → motor → sound×4 → P-MOS → UWB → LED → I2C → LoRa → summary → distance
 * Each step waits for OK before continuing (time to verify hardware).
 * PWR/SND/VIB/BL — физические ключи питания (не GPIO, не тестируются прошивкой).
 *
 * BU03 UART (переключатель перед прошивкой):
 *   BU03_PINS_PLATE 1 — плата: RX=G1, TX=G2 (MASTER_SPEC §3)
 *   BU03_PINS_PLATE 0 — макет: RX=G18, TX=G17 (старый breadboard, конфликт с DFPlayer!)
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <DFRobotDFPlayerMini.h>
#include <Preferences.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_now.h>
#include <esp_system.h>

// ─── TFT (SPI) ───
#define TFT_CS   10
#define TFT_DC   11
#define TFT_RST  12
#define TFT_SCK  13
#define TFT_MOSI 14
#define TFT_MISO 40
#include "tft_panel.h"

#define LORA_CS   39
#define LORA_RST  TFT_RST   // G12 — shared with TFT (no pulse after boot)
#define LORA_DIO0 41
#define REG_VERSION 0x42

// ─── Buttons (INPUT_PULLUP) ───
#define BTN_DN   4
#define BTN_RT   5
#define BTN_OK   6
#define BTN_ESC  7

// ─── LEDs ───
#define LED_RED    15
#define LED_GREEN  16

// ─── I2C → TCA9548A ───
#define PIN_SDA   9
#define PIN_SCL   8
#define TCA_ADDR  0x70
#define EEPROM_BASE 0x50

// ─── DFPlayer UART ───
#define DF_TX  17
#define DF_RX  18

// ─── Vibro motor (NPN on G21) ───
#define PIN_VIBRO 21

// ─── BU03 UWB (UART) + P-MOS G42 ───
// 1 = плата (G1/G2). 0 = макет на G17/G18 — только если провода ещё там!
#define BU03_PINS_PLATE  1

#if BU03_PINS_PLATE
#define BU03_RX   1   // ESP Serial1 RX ← BU03 TX (PA2)
#define BU03_TX   2   // ESP Serial1 TX → BU03 RX (PA3)
#else
#define BU03_RX  18   // breadboard (конфликтует с DFPlayer на плате!)
#define BU03_TX  17
#endif

#define BU03_PWR       42
#define HAS_BU03_PWR   1   // 0 = BU03 всегда на 3.3V, без ключа G42
#define BU03_BOOT_MS   5000 // пауза после G42 HIGH — STM на BU03 просыпается

// ─── Colors RGB565 ───
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_GREEN  0x07E0
#define C_YELLOW 0xFFE0
#if TFT_BGR_SWAP
#define C_RED    0x001F
#define C_BLUE   0xF800
#else
#define C_RED    0xF800
#define C_BLUE   0x001F
#endif
#define C_ORANGE 0xFD20
#define C_CYAN   0x07FF
#define C_DGRAY  0x3186
#define C_LGRAY  0xC618
#define C_BORDER 0x7BCF

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;
HardwareSerial dfSerial(2);
HardwareSerial bu03Serial(1);
DFRobotDFPlayerMini dfPlayer;
Preferences prefs;

bool displayOk = false;
bool audioOk   = false;
bool espNowOk  = false;

struct Results {
  bool display  = false;
  bool buttons  = false;
  bool vibro    = false;
  bool dfplayer = false;
  bool uwb      = false;
  bool uwb_pmos = false;
  bool led      = false;
  bool i2c      = false;
  bool lora     = false;
  bool nvs      = false;
  bool espnow   = false;
} results;

// ─── Guided test state machine ───
enum Phase : uint8_t {
  PH_SPLASH,
  PH_DISPLAY,
  PH_BTN_DN, PH_BTN_RT, PH_BTN_OK, PH_BTN_ESC,
  PH_MOTOR_RUN, PH_MOTOR_OK,
  PH_SOUND_1, PH_SOUND_2, PH_SOUND_3, PH_SOUND_4,
  PH_UWB_PMOS, PH_UWB_PMOS_OK,
  PH_UWB_TEST, PH_UWB_OK,
  PH_LED_RED, PH_LED_RED_OK, PH_LED_GREEN, PH_LED_GREEN_OK,
  PH_I2C_TEST,
  PH_LORA_TEST,
  PH_SUMMARY,
  PH_DISTANCE
};

Phase phase = PH_SPLASH;
Phase lastDrawnPhase = PH_DISTANCE;
uint32_t phaseEnteredMs = 0;
int soundTrack = 0;

#define DFPLAYER_CMD_TIMEOUT_MS 400
#define VIBRO_PULSE_MS          60
#define VIBRO_GAP_MS           600
#define VIBRO_PREP_MS          800
#define VIBRO_PULSE_COUNT        2
uint8_t displayColorIdx = 0;
bool btnDone[4] = {false, false, false, false};
bool btnAwaitOk = false;
bool displayAwaitOk = false;
bool motorPulsing = false;
bool motorPrepDone = false;
uint8_t motorPulseIdx = 0;
uint32_t motorPulseMs = 0;
String bu03Rx;

#define BU03_POLL_MS       750
#define BU03_AT_TIMEOUT_MS 1500
#define BU03_RAW_MAX       96

float distM = -1.0f;
uint32_t distLastOkMs = 0;
uint32_t distLastPollMs = 0;
char distLastRaw[BU03_RAW_MAX] = "";
bool distScreenDirty = true;

struct Bu03Cfg {
  int id = -1;
  int role = -1; // 0=tag, 1=anchor
  int ch = -1;
  int rate = -1;
  bool valid = false;
} bu03Cfg;

bool bu03BenchAnchor = false; // временно перевели ПДА в anchor для теста на столе
bool bu03UartSwapped = false;

static const int BTN_PINS[4] = {BTN_DN, BTN_RT, BTN_OK, BTN_ESC};
static const char *BTN_LABELS[4] = {"DOWN", "RIGHT", "OK", "ESC"};

bool btnLast[4];
uint32_t btnDebounce[4];

#define EEPROM_PAGE 64
#define EEPROM_TEST_LEN 8
static const uint8_t EEPROM_PATTERN[EEPROM_TEST_LEN] = {
    0x53, 0x54, 0x41, 0x4C, 0x4B, 0x45, 0x52, 0x21};
// Слоты ПДА: CH1 universal, CH2 armor, CH4–CH6 artifacts (mux_channels.h)
static const uint8_t MUX_CARTRIDGE_CHS[] = {1, 2, 4, 5, 6};
#define MUX_CARTRIDGE_CH_COUNT (sizeof(MUX_CARTRIDGE_CHS) / sizeof(MUX_CARTRIDGE_CHS[0]))

// ─── UI helpers ───
int scrW() { return tft.width(); }
int scrH() { return tft.height(); }

void dispClear() {
  if (!displayOk) return;
  tft.fillScreen(C_BLACK);
}

void dispLine(int x, int y, const char *text, uint16_t color = C_WHITE) {
  if (!displayOk) return;
  u8g2.setForegroundColor(color);
  u8g2.setCursor(x, y + 10);
  u8g2.print(text);
}

void dispHeader(const char *title) {
  dispClear();
  dispLine(4, 4, title, C_CYAN);
  tft.drawFastHLine(0, 22, scrW(), C_BORDER);
}

void dispBody(int row, const char *text, uint16_t color = C_WHITE) {
  dispLine(8, 28 + row * 20, text, color);
}

void dispSuccess(const char *msg) {
  dispBody(4, msg, C_GREEN);
  tft.drawRect(4, scrH() - 36, scrW() - 8, 28, C_GREEN);
  dispLine(12, scrH() - 32, "SUCCESSFUL", C_GREEN);
}

void dispFail(const char *msg) {
  dispBody(4, msg, C_RED);
  dispLine(12, scrH() - 32, "FAILED", C_RED);
}

void dispProgress(uint8_t step, uint8_t total) {
  char buf[20];
  snprintf(buf, sizeof(buf), "Step %u/%u", step, total);
  dispLine(scrW() - 72, 4, buf, C_DGRAY);
}

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
  u8g2.setFont(u8g2_font_6x12_tf);
  return true;
}

void enterPhase(Phase p) {
  phase = p;
  phaseEnteredMs = millis();
  lastDrawnPhase = PH_DISTANCE;
  if (p == PH_DISPLAY) displayColorIdx = 0;
  if (p < PH_SOUND_1 || p > PH_SOUND_4) soundTrack = 0;
  Serial.printf("[PHASE] %d\n", (int)p);
}

bool phaseJustEntered() {
  return lastDrawnPhase != phase;
}

void markPhaseDrawn() {
  lastDrawnPhase = phase;
}

void vibroEnsureOff() {
  digitalWrite(PIN_VIBRO, LOW);
}

void reduceLoadBeforeVibro() {
  bu03PwrGpio(false);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
  if (espNowOk) {
    esp_now_deinit();
    espNowOk = false;
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  idleSpiChipSelects();
  Serial.println("[VIBRO] Load reduced (BU03 off, WiFi off)");
}

void ensureRadioInit() {
  if (espNowOk)
    return;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(80);
  espNowOk = (esp_now_init() == ESP_OK);
}

// ─── Input ───
bool isPressed(int pin) { return digitalRead(pin) == LOW; }

bool inLedTestPhase() {
  return phase >= PH_LED_RED && phase <= PH_LED_GREEN_OK;
}

void pollButtons() {
  for (int i = 0; i < 4; i++) {
    bool r = digitalRead(BTN_PINS[i]);
    if (r != btnLast[i]) btnDebounce[i] = millis();
    if ((millis() - btnDebounce[i]) > 40) {
      if (r == LOW && btnLast[i] != LOW) {
        Serial.printf("[BTN] %s\n", BTN_LABELS[i]);
        if (!inLedTestPhase()) digitalWrite(LED_GREEN, HIGH);
      }
      if (r == HIGH && btnLast[i] == LOW && !inLedTestPhase())
        digitalWrite(LED_GREEN, LOW);
      btnLast[i] = r;
    }
  }
}

bool waitButtonEdge(int btnIndex) {
  return isPressed(BTN_PINS[btnIndex]);
}

bool okPressedOnce() {
  static bool wasPressed = false;
  bool now = isPressed(BTN_OK);
  bool edge = now && !wasPressed;
  wasPressed = now;
  return edge;
}

// ─── BU03 UWB + P-MOS (G42 → 2N2222 → AO3401) ───
void bu03PwrGpio(bool on) {
#if HAS_BU03_PWR
  pinMode(BU03_PWR, OUTPUT);
  digitalWrite(BU03_PWR, on ? HIGH : LOW);
#else
  (void)on;
#endif
}

void bu03Power(bool on) {
  bu03PwrGpio(on);
#if HAS_BU03_PWR
  if (on)
    delay(BU03_BOOT_MS);
#endif
}

void bu03LogConfig(const char *tag) {
  Serial.printf("[%s] UART RX=G%d TX=G%d (PLATE=%d)\n", tag, BU03_RX, BU03_TX,
                BU03_PINS_PLATE);
#if HAS_BU03_PWR
  Serial.printf("[%s] G42=%s boot=%ums after HIGH\n", tag,
                digitalRead(BU03_PWR) ? "HIGH" : "LOW", BU03_BOOT_MS);
#else
  Serial.printf("[%s] HAS_BU03_PWR=0 (always on)\n", tag);
#endif
}

void bu03BeginUart() {
  bu03Serial.setRxBufferSize(1024);
  bu03Serial.end();
  delay(50);
  if (bu03UartSwapped)
    bu03Serial.begin(115200, SERIAL_8N1, BU03_TX, BU03_RX);
  else
    bu03Serial.begin(115200, SERIAL_8N1, BU03_RX, BU03_TX);
  delay(400);
}

void bu03SwapUart() {
  bu03UartSwapped = !bu03UartSwapped;
  Serial.printf("[UWB] Swap UART -> RX=G%d TX=G%d\n",
                bu03UartSwapped ? BU03_TX : BU03_RX,
                bu03UartSwapped ? BU03_RX : BU03_TX);
  bu03BeginUart();
}

bool bu03RxLooksLikeEcho() {
  if (bu03Rx.indexOf("OK") >= 0 || bu03Rx.indexOf("ERR") >= 0)
    return false;
  String s = bu03Rx;
  s.trim();
  return s == "AT" || s == "at";
}

void bu03PrintUartFailHints() {
  Serial.println("[UWB] ===== UART / POWER DIAG =====");
  if (bu03RxLooksLikeEcho())
    Serial.println("[UWB] ECHO: ESP hears its own AT — swap TX/RX or module dead");
  Serial.println("[UWB] G42 HIGH / LED on != AT OK. Need UART + 3.3V on BU03 VCC.");
  Serial.printf("[UWB] Сейчас: RX=G%d TX=G%d (PLATE=%d)\n", BU03_RX, BU03_TX,
                BU03_PINS_PLATE);
  Serial.println("[UWB] 1) Мультиметр: VCC BU03 после P-MOS ~3.3V при G42 HIGH");
  Serial.println("[UWB] 2) GND ESP = GND BU03 (0 Ом)");
  Serial.printf("[UWB] 3) G%d <- TX модуля, G%d -> RX модуля (или swap)\n", BU03_RX,
                BU03_TX);
  if (BU03_PINS_PLATE)
    Serial.println("[UWB] 4) Провода на G17/G18? Поставь BU03_PINS_PLATE 0");
  else
    Serial.println("[UWB] 4) На плате нужны G1/G2 — BU03_PINS_PLATE 1");
  Serial.println("[UWB] 5) BU03 на USB-UART адаптер: AT -> OK?");
  Serial.printf("[UWB] last rx len=%u\n", bu03Rx.length());
}

void bu03ShowUartFailScreen(const char *title) {
  dispHeader(title);
  if (bu03RxLooksLikeEcho()) {
    dispFail("UART echo — no OK");
    dispBody(2, "ESP hears own AT", C_ORANGE);
    dispBody(3, "Swap TX/RX on module", C_YELLOW);
  } else {
    dispFail("Power != UART run");
    dispBody(3, "0 bytes = no UART link", C_RED);
  }
  char pins[32];
  snprintf(pins, sizeof(pins), "RX=G%d TX=G%d%s", bu03UartSwapped ? BU03_TX : BU03_RX,
           bu03UartSwapped ? BU03_RX : BU03_TX, bu03UartSwapped ? " swap" : "");
  dispBody(4, pins, C_CYAN);
  dispBody(5, "VCC BU03 ~3.3V?", C_YELLOW);
  dispBody(6, "GND common, swap TX/RX", C_DGRAY);
#if BU03_PINS_PLATE
  dispBody(7, "Wires on G17/G18?", C_ORANGE);
  dispBody(8, "Set BU03_PINS_PLATE 0", C_ORANGE);
#else
  dispBody(7, "PCB needs G1/G2", C_ORANGE);
#endif
  if (bu03Rx.length() > 0) {
    char hint[36];
    snprintf(hint, sizeof(hint), "rx: %s", bu03Rx.substring(0, 28).c_str());
    dispBody(10, hint, C_LGRAY);
  }
}

void bu03Flush() {
  while (bu03Serial.available()) (void)bu03Serial.read();
}

void bu03ListenBoot(uint32_t ms) {
  bu03Flush();
  bu03Rx = "";
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    while (bu03Serial.available()) {
      char c = (char)bu03Serial.read();
      if (c == '\r')
        continue;
      if (bu03Rx.length() < 320)
        bu03Rx += c;
    }
    delay(1);
  }
  if (bu03Rx.length() > 0)
    Serial.printf("[UWB] boot uart (%u B): %s\n", bu03Rx.length(), bu03Rx.c_str());
  else
    Serial.println("[UWB] boot uart: silent (module TX not reaching ESP G1)");
}

bool bu03WaitOk(uint32_t ms) {
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

bool bu03Send(const char *cmd) {
  bu03Flush();
  Serial.printf("[UWB] >> %s\n", cmd);
  bu03Serial.print(cmd);
  bu03Serial.print("\r\n");
  return bu03WaitOk(2000);
}

bool bu03AtProbe() {
  for (int attempt = 0; attempt < 3; attempt++) {
    if (bu03Send("AT") && bu03Rx.indexOf("OK") >= 0)
      return true;
    Serial.printf("[UWB] AT retry %d, rx='%s'%s\n", attempt + 1, bu03Rx.c_str(),
                  bu03RxLooksLikeEcho() ? " (echo)" : "");
    delay(500);
  }
  return false;
}

bool bu03AtProbeTrySwap() {
  bu03UartSwapped = false;
  bu03BeginUart();
  if (bu03AtProbe())
    return true;

  if (bu03RxLooksLikeEcho())
    Serial.println("[UWB] Echo on normal pins — swap TX/RX");
  else
    Serial.println("[UWB] No OK on normal pins — try swap");

  bu03SwapUart();
  if (bu03AtProbe()) {
    Serial.println("[UWB] OK with SWAPPED TX/RX — fix wiring on PCB!");
    return true;
  }

  bu03UartSwapped = false;
  bu03BeginUart();
  return false;
}

// P-MOS: G42 LOW → BU03 off; G42 HIGH → BU03 on. R8 10k на затвор обязателен.
bool testPmosSwitch() {
#if !HAS_BU03_PWR
  Serial.println("[PMOS] SKIP — HAS_BU03_PWR=0");
  return true;
#else
  bu03LogConfig("PMOS");
  bu03BeginUart();

  bu03PwrGpio(true);
  Serial.println("[PMOS] G42 HIGH, waiting boot...");
  delay(BU03_BOOT_MS);
  bu03LogConfig("PMOS");
  bu03ListenBoot(1500);
  if (!bu03AtProbeTrySwap()) {
    Serial.println("[PMOS] FAIL — no AT with G42 HIGH");
    bu03PrintUartFailHints();
    return false;
  }

  bu03PwrGpio(false);
  Serial.println("[PMOS] G42 LOW");
  delay(800);
  bu03Flush();
  bool silentWhenOff = !bu03AtProbe();
  Serial.printf("[PMOS] G42 LOW → AT silent: %s\n", silentWhenOff ? "YES" : "NO");

  bu03PwrGpio(true);
  Serial.println("[PMOS] G42 HIGH again");
  delay(BU03_BOOT_MS);
  bool okWhenOn = bu03AtProbe();
  Serial.printf("[PMOS] G42 HIGH → AT OK: %s\n", okWhenOn ? "YES" : "NO");

  if (!silentWhenOff || !okWhenOn)
    bu03PrintUartFailHints();
  return silentWhenOff && okWhenOn;
#endif
}

bool testUwbLink() {
  bu03LogConfig("UWB");
  bu03Power(true);
  bu03BeginUart();
  bu03LogConfig("UWB");
  bu03ListenBoot(1500);

  if (bu03AtProbeTrySwap()) {
    Serial.println(bu03Rx);
    return true;
  }

  bu03PrintUartFailHints();
  return false;
}

float bu03ParseDistance(const String &s) {
  int i = s.indexOf("distance:");
  if (i < 0)
    return -1.0f;
  return s.substring(i + 9).toFloat();
}

void bu03StoreRaw(const String &s) {
  size_t n = s.length();
  if (n >= BU03_RAW_MAX)
    n = BU03_RAW_MAX - 1;
  s.substring(0, n).toCharArray(distLastRaw, BU03_RAW_MAX);
}

int bu03ParseField(const String &s, const char *key) {
  int i = s.indexOf(key);
  if (i < 0)
    return -1;
  return s.substring(i + strlen(key)).toInt();
}

bool bu03ReadCfg(Bu03Cfg *out) {
  if (!bu03EnsureLink())
    return false;
  if (!bu03Send("AT+GETCFG")) {
    bu03StoreRaw(bu03Rx);
    return false;
  }
  bu03StoreRaw(bu03Rx);
  Bu03Cfg c;
  c.id = bu03ParseField(bu03Rx, "ID:");
  c.role = bu03ParseField(bu03Rx, "Role:");
  if (c.role < 0)
    c.role = bu03ParseField(bu03Rx, "ROLE:");
  c.ch = bu03ParseField(bu03Rx, "CH:");
  c.rate = bu03ParseField(bu03Rx, "Rate:");
  if (c.rate < 0)
    c.rate = bu03ParseField(bu03Rx, "RATE:");
  // fallback: "ID:0,1,1,1" style
  if (c.role < 0) {
    int i = bu03Rx.indexOf("ID:");
    if (i >= 0) {
      String tail = bu03Rx.substring(i + 3);
      int c1 = tail.indexOf(',');
      if (c1 > 0) {
        c.id = tail.substring(0, c1).toInt();
        int c2 = tail.indexOf(',', c1 + 1);
        if (c2 > c1)
          c.role = tail.substring(c1 + 1, c2).toInt();
        int c3 = tail.indexOf(',', c2 + 1);
        if (c3 > c2)
          c.ch = tail.substring(c2 + 1, c3).toInt();
        int c4 = tail.indexOf(',', c3 + 1);
        if (c4 > c3)
          c.rate = tail.substring(c3 + 1, c4).toInt();
      }
    }
  }
  c.valid = (c.id >= 0 && c.role >= 0);
  if (out)
    *out = c;
  bu03Cfg = c;
  Serial.printf("[UWB] CFG id=%d role=%s ch=%d rate=%d\n", c.id,
                c.role ? "anchor" : "tag", c.ch, c.rate);
  return c.valid;
}

bool bu03IsAnchorRole() {
  return bu03Cfg.valid && bu03Cfg.role == 1;
}

bool bu03SetBenchAnchor() {
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+SETCFG=0,1,1,1");
  Serial.println("[UWB] Bench test: PDA -> anchor (id=0)");
  if (!bu03Send(cmd))
    return false;
  if (!bu03Send("AT+SAVE"))
    return false;
  if (!bu03Send("AT+RESTART"))
    return false;
  delay(2800);
  bu03Flush();
  bu03BenchAnchor = true;
  return bu03ReadCfg(nullptr) && bu03IsAnchorRole();
}

bool bu03EnsureLink() {
  if (!results.uwb) {
    bu03Power(true);
    bu03Serial.setRxBufferSize(1024);
    bu03Serial.begin(115200, SERIAL_8N1, BU03_RX, BU03_TX);
    delay(400);
    results.uwb = bu03Send("AT") && bu03Rx.indexOf("OK") >= 0;
  }
  return results.uwb;
}

bool bu03PollDistance() {
  if (!bu03EnsureLink())
    return false;
  if (!bu03IsAnchorRole()) {
    Serial.println("[UWB] SKIP AT+DISTANCE — role is TAG (only anchor polls)");
    return false;
  }
  if (!bu03Send("AT+DISTANCE")) {
    Serial.println("[UWB] DISTANCE — no reply");
    bu03StoreRaw(bu03Rx);
    return false;
  }
  bu03StoreRaw(bu03Rx);
  float d = bu03ParseDistance(bu03Rx);
  if (d < 0.0f) {
    Serial.printf("[UWB] DISTANCE parse fail: %s\n", distLastRaw);
    return false;
  }
  distM = d;
  distLastOkMs = millis();
  Serial.printf("[UWB] distance=%.2f m\n", distM);
  return true;
}

void showDistanceScreen() {
  dispHeader("UWB DISTANCE");

  if (!results.uwb) {
    dispBody(0, "NO LINK", C_RED);
    dispBody(2, "Check G1/G2, G42", C_YELLOW);
    return;
  }

  char cfgLine[40];
  if (bu03Cfg.valid) {
    snprintf(cfgLine, sizeof(cfgLine), "ID:%d %s CH:%d", bu03Cfg.id,
             bu03Cfg.role ? "ANCHOR" : "TAG", bu03Cfg.ch);
    dispBody(0, cfgLine, bu03Cfg.role ? C_CYAN : C_YELLOW);
  }

  if (!bu03IsAnchorRole()) {
    dispBody(2, "TAG: no AT+DISTANCE", C_ORANGE);
    dispBody(4, "2nd BU03=anchor polls", C_DGRAY);
    dispBody(5, "See distance THERE", C_DGRAY);
    dispBody(7, "OK = PDA bench anchor", C_GREEN);
    dispBody(8, "Set 2nd BU03 as TAG", C_DGRAY);
    dispBody(9, "id=1 role=0 CH:1", C_DGRAY);
    dispBody(10, "ESC = summary", C_DGRAY);
    return;
  }

  uint32_t age = distLastOkMs ? (millis() - distLastOkMs) / 1000UL : 999;
  bool fresh = distLastOkMs && (millis() - distLastOkMs < 4000);

  char line[40];
  if (fresh) {
    snprintf(line, sizeof(line), "%.2f m", distM);
    u8g2.setFont(u8g2_font_10x20_tf);
    u8g2.setForegroundColor(C_GREEN);
    u8g2.setCursor(24, 82);
    u8g2.print(line);
    u8g2.setFont(u8g2_font_6x12_tf);
    snprintf(line, sizeof(line), "OK  (%lus ago)", age);
    dispBody(5, line, C_GREEN);
  } else if (distLastOkMs) {
    snprintf(line, sizeof(line), "%.2f m", distM);
    u8g2.setFont(u8g2_font_10x20_tf);
    u8g2.setForegroundColor(C_YELLOW);
    u8g2.setCursor(24, 82);
    u8g2.print(line);
    u8g2.setFont(u8g2_font_6x12_tf);
    snprintf(line, sizeof(line), "STALE (%lus)", age);
    dispBody(5, line, C_YELLOW);
  } else {
    u8g2.setFont(u8g2_font_10x20_tf);
    u8g2.setForegroundColor(C_ORANGE);
    u8g2.setCursor(48, 82);
    u8g2.print("---");
    u8g2.setFont(u8g2_font_6x12_tf);
    dispBody(5, "NO DATA — need TAG", C_ORANGE);
  }

  if (bu03BenchAnchor)
    dispBody(7, "bench anchor mode", C_CYAN);
  dispBody(8, distLastRaw, C_LGRAY);
  dispBody(10, "OK=poll ESC=summary", C_DGRAY);
}

// ─── LoRa probe (SPI register read) ───
uint8_t loraReadReg(uint8_t reg) {
  idleSpiChipSelects();
  digitalWrite(LORA_CS, LOW);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(reg & 0x7F);
  uint8_t v = SPI.transfer(0x00);
  SPI.endTransaction();
  digitalWrite(LORA_CS, HIGH);
  return v;
}

const char *loraDiagHint(uint8_t ver) {
  if (ver == 0x12) return "Chip SX1278 OK";
  if (ver == 0x00) return "MISO silent / 3V3?";
  if (ver == 0xFF) return "MISO stuck / CS / module";
  return "Unexpected SPI reply";
}

uint8_t loraProbeVersion() {
  idleSpiChipSelects();
  pinMode(LORA_DIO0, INPUT);
  delay(2);

  uint8_t last = 0x00;
  for (int i = 0; i < 5; i++) {
    uint8_t v = loraReadReg(REG_VERSION);
    Serial.printf("[LoRa] REG_VERSION try %d = 0x%02X\n", i + 1, v);
    if (v == 0x12) return 0x12;
    last = v;
    delay(3);
  }
  return last;
}

// ─── I2C / TCA / EEPROM ───
bool tcaSelect(uint8_t channel) {
  if (channel > 7) return false;
  Wire.beginTransmission(TCA_ADDR);
  Wire.write((uint8_t)(1 << channel));
  return Wire.endTransmission() == 0;
}

bool tcaFound() {
  Wire.beginTransmission(TCA_ADDR);
  return Wire.endTransmission() == 0;
}

bool eepromWrite(uint8_t dev, uint16_t addr, const uint8_t *data, uint8_t len) {
  Wire.beginTransmission(dev);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  for (uint8_t i = 0; i < len; i++) Wire.write(data[i]);
  if (Wire.endTransmission() != 0) return false;
  delay(5);
  return true;
}

uint8_t eepromRead(uint8_t dev, uint16_t addr, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(dev);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  if (Wire.endTransmission(false) != 0) return 0;
  uint8_t n = Wire.requestFrom(dev, len);
  for (uint8_t i = 0; i < n && Wire.available(); i++) buf[i] = Wire.read();
  return n;
}

bool testEepromOnBus(uint8_t dev, int channel) {
  uint16_t testAddr = 0x0100;
  if (!eepromWrite(dev, testAddr, EEPROM_PATTERN, EEPROM_TEST_LEN)) return false;
  uint8_t buf[EEPROM_TEST_LEN] = {0};
  if (eepromRead(dev, testAddr, buf, EEPROM_TEST_LEN) != EEPROM_TEST_LEN) return false;
  for (uint8_t i = 0; i < EEPROM_TEST_LEN; i++) {
    if (buf[i] != EEPROM_PATTERN[i]) return false;
  }
  Serial.printf("[I2C] CH%d EEPROM 0x%02X OK\n", channel, dev);
  return true;
}

bool runI2cTest() {
  if (!tcaFound()) {
    Serial.println("[I2C] FAIL — TCA9548A @0x70 not found (SDA=G8 SCL=G9)");
    return false;
  }
  Serial.println("[I2C] TCA9548A @0x70 OK");

  int okCh = 0;
  for (uint8_t i = 0; i < MUX_CARTRIDGE_CH_COUNT; i++) {
    uint8_t ch = MUX_CARTRIDGE_CHS[i];
    if (!tcaSelect(ch)) {
      Serial.printf("[I2C] CH%d select FAIL\n", ch);
      continue;
    }
    delay(2);
    Wire.beginTransmission(EEPROM_BASE);
    uint8_t ack = Wire.endTransmission();
    if (ack != 0) {
      Serial.printf("[I2C] CH%d no EEPROM @0x50 (err=%u)\n", ch, ack);
      continue;
    }
    Serial.printf("[I2C] CH%d ACK @0x50, testing R/W...\n", ch);
    if (testEepromOnBus(EEPROM_BASE, ch))
      okCh++;
    else
      Serial.printf("[I2C] CH%d R/W FAIL (WP->GND? bad cartridge?)\n", ch);
  }
  tcaSelect(0);
  Serial.printf("[I2C] %d cartridge slot(s) R/W OK (slots CH1 CH2 CH4-CH6)\n", okCh);
  return okCh > 0;
}

bool runNvsTest() {
  prefs.begin("hw_test", false);
  prefs.putInt("magic", 0xDEAD);
  prefs.putString("tag", "STALKER_S3");
  prefs.end();
  prefs.begin("hw_test", true);
  int magic = prefs.getInt("magic", 0);
  String tag = prefs.getString("tag", "");
  prefs.end();
  prefs.begin("hw_test", false);
  prefs.clear();
  prefs.end();
  return magic == 0xDEAD && tag == "STALKER_S3";
}

// ─── Phase screens ───
void showButtonPrompt(int idx) {
  char title[16];
  snprintf(title, sizeof(title), "BUTTON %d/4", idx + 1);
  dispHeader(title);
  dispProgress((uint8_t)(idx + 1), 4);
  char line[32];
  snprintf(line, sizeof(line), "Press %s button", BTN_LABELS[idx]);
  dispBody(0, line, C_YELLOW);
  dispBody(2, "(hold until green LED)", C_DGRAY);
}

void showSoundPrompt(int track) {
  char title[16];
  snprintf(title, sizeof(title), "SOUND %d/4", track);
  dispHeader(title);
  dispProgress((uint8_t)track, 4);
  char line[28];
  snprintf(line, sizeof(line), "Playing file %d...", track);
  dispBody(0, line, C_YELLOW);
  dispBody(2, "Listen to speaker", C_DGRAY);
  dispBody(4, "Press OK when heard", C_YELLOW);
}

void showSoundSkipped(const char *reason) {
  dispHeader("SOUND TEST");
  dispBody(0, "Sound skipped", C_YELLOW);
  dispBody(2, reason, C_ORANGE);
}

void skipAllSounds(const char *reason) {
  results.dfplayer = false;
  showSoundSkipped(reason);
  dispBody(4, "Press OK to continue", C_YELLOW);
  Serial.printf("[AUDIO] SKIP ALL — %s\n", reason);
  soundTrack = 0;
}

bool initDfPlayer() {
  while (dfSerial.available()) (void)dfSerial.read();
  dfPlayer.setTimeOut(DFPLAYER_CMD_TIMEOUT_MS);
  // doReset=true actually probes the module (begin(..., false) always reports OK)
  if (!dfPlayer.begin(dfSerial, true, true)) return false;
  if (dfPlayer.readState() < 0) return false;
  dfPlayer.volume(15);
  return true;
}

void showSummary() {
  dispHeader("ALL TESTS DONE");
  auto row = [&](int r, const char *lbl, bool ok) {
    dispLine(8, 28 + r * 16, lbl, C_LGRAY);
    dispLine(scrW() - 52, 28 + r * 16, ok ? "OK" : "FAIL", ok ? C_GREEN : C_RED);
  };
  row(0, "Display", results.display);
  row(1, "Buttons", results.buttons);
  row(2, "Motor", results.vibro);
  row(3, "Sound", results.dfplayer);
  row(4, "P-MOS G42", results.uwb_pmos);
  row(5, "UWB", results.uwb);
  row(6, "LED", results.led);
  row(7, "I2C/EEPROM", results.i2c);
  row(8, "LoRa", results.lora);
  dispBody(10, "OK = distance screen", C_DGRAY);
}

// ─── Guided test runner (called from loop) ───
void runGuidedTest() {
  pollButtons();
  if (!motorPulsing)
    vibroEnsureOff();

  switch (phase) {
    case PH_SPLASH:
      if (phaseJustEntered()) {
        dispHeader("PDA HW TEST");
        dispBody(0, "S.T.A.L.K.E.R.", C_GREEN);
        dispBody(2, "Guided test starting...", C_WHITE);
        dispBody(4, "Watch the display", C_DGRAY);
        dispBody(6, "Press OK to start", C_YELLOW);
        markPhaseDrawn();
      }
      if (okPressedOnce())
        enterPhase(PH_DISPLAY);
      break;

    case PH_DISPLAY: {
      static const uint16_t cols[] = {C_RED, C_GREEN, C_BLUE, C_WHITE};
      if (phaseJustEntered()) {
        displayAwaitOk = false;
        displayColorIdx = 0;
        markPhaseDrawn();
      }
      if (!displayAwaitOk) {
        if (displayColorIdx < 4 && millis() - phaseEnteredMs >= displayColorIdx * 280UL) {
          tft.fillScreen(cols[displayColorIdx]);
          displayColorIdx++;
        }
        if (displayColorIdx >= 4 && millis() - phaseEnteredMs > 1400) {
          results.display = displayOk;
          dispHeader("DISPLAY");
          dispSuccess("Display test OK");
          dispBody(2, "Red / green / blue / white", C_DGRAY);
          dispBody(4, "Press OK to continue", C_YELLOW);
          displayAwaitOk = true;
        }
      } else if (okPressedOnce()) {
        enterPhase(PH_BTN_DN);
      }
      break;
    }

    case PH_BTN_DN:
    case PH_BTN_RT:
    case PH_BTN_OK:
    case PH_BTN_ESC: {
      int idx = (int)phase - (int)PH_BTN_DN;
      if (phaseJustEntered()) {
        btnAwaitOk = false;
        showButtonPrompt(idx);
        markPhaseDrawn();
      }
      if (!btnAwaitOk && waitButtonEdge(idx)) {
        btnDone[idx] = true;
        dispSuccess("Button OK");
        Serial.printf("[BTN] %s OK\n", BTN_LABELS[idx]);
        if (idx == 2) {
          enterPhase((Phase)((int)phase + 1));
        } else {
          dispBody(6, "Press OK to continue", C_YELLOW);
          btnAwaitOk = true;
        }
      }
      if (btnAwaitOk && okPressedOnce())
        enterPhase((Phase)((int)phase + 1));
      break;
    }

    case PH_MOTOR_RUN:
      if (phaseJustEntered()) {
        motorPrepDone = false;
        motorPulsing = false;
        motorPulseIdx = 0;
        reduceLoadBeforeVibro();
        dispHeader("MOTOR TEST");
        dispBody(0, "Switch VIB = ON", C_YELLOW);
        dispBody(2, "Lowering load...", C_DGRAY);
        dispBody(4, "Short pulses (anti-brownout)", C_DGRAY);
        markPhaseDrawn();
      }
      if (!motorPrepDone) {
        if (millis() - phaseEnteredMs >= VIBRO_PREP_MS) {
          motorPrepDone = true;
          motorPulsing = true;
          motorPulseMs = millis();
          digitalWrite(PIN_VIBRO, HIGH);
          Serial.println("[VIBRO] pulse 1");
        }
        break;
      }
      if (motorPulsing) {
        uint32_t elapsed = millis() - motorPulseMs;
        if (elapsed < VIBRO_PULSE_MS) {
          digitalWrite(PIN_VIBRO, HIGH);
        } else if (elapsed < VIBRO_PULSE_MS + VIBRO_GAP_MS) {
          vibroEnsureOff();
        } else {
          motorPulseIdx++;
          if (motorPulseIdx < VIBRO_PULSE_COUNT) {
            motorPulseMs = millis();
            digitalWrite(PIN_VIBRO, HIGH);
            Serial.printf("[VIBRO] pulse %u\n", motorPulseIdx + 1);
          } else {
            vibroEnsureOff();
            motorPulsing = false;
            results.vibro = true;
            enterPhase(PH_MOTOR_OK);
          }
        }
      }
      break;

    case PH_MOTOR_OK:
      if (phaseJustEntered()) {
        dispHeader("MOTOR TEST");
        dispSuccess("Test of the motor");
        dispBody(2, "is successful", C_GREEN);
        dispBody(4, "Press OK to continue", C_YELLOW);
        markPhaseDrawn();
      }
      if (okPressedOnce())
        enterPhase(PH_SOUND_1);
      break;

    case PH_SOUND_1:
    case PH_SOUND_2:
    case PH_SOUND_3:
    case PH_SOUND_4: {
      int track = (int)phase - (int)PH_SOUND_1 + 1;

      if (!audioOk) {
        if (phaseJustEntered()) {
          skipAllSounds("No 5V / DFPlayer");
          markPhaseDrawn();
        }
        if (okPressedOnce())
          enterPhase(PH_UWB_PMOS);
        break;
      }

      if (soundTrack != track) {
        soundTrack = track;
        showSoundPrompt(track);
        dfPlayer.setTimeOut(DFPLAYER_CMD_TIMEOUT_MS);
        dfPlayer.volume(22);
        dfPlayer.playMp3Folder(track);
        Serial.printf("[AUDIO] play mp3/000%d.mp3\n", track);
        while (dfPlayer.available()) {
          if (dfPlayer.readType() == TimeOut) {
            audioOk = false;
            skipAllSounds("No 5V / DFPlayer");
            markPhaseDrawn();
            break;
          }
          (void)dfPlayer.read();
        }
        if (!audioOk) break;
      }

      if (okPressedOnce()) {
        char msg[36];
        snprintf(msg, sizeof(msg), "Sound file %d", track);
        dispHeader("SOUND TEST");
        dispSuccess(msg);
        dispBody(2, "is successful", C_GREEN);
        if (track == 4) results.dfplayer = true;
        soundTrack = 0;
        enterPhase((Phase)((int)phase + 1));
      }
      break;
    }

    case PH_UWB_PMOS:
      if (phaseJustEntered()) {
        dispHeader("P-MOS G42 TEST");
        dispBody(0, "Cut BU03 3.3V...", C_YELLOW);
        dispBody(2, "G42 -> 2N2222 -> P-MOS", C_DGRAY);
        markPhaseDrawn();
        results.uwb_pmos = testPmosSwitch();
        enterPhase(PH_UWB_PMOS_OK);
      }
      break;

    case PH_UWB_PMOS_OK:
      if (phaseJustEntered()) {
        dispHeader("P-MOS G42 TEST");
        if (results.uwb_pmos) {
          dispSuccess("P-MOS switch OK");
          dispBody(2, "OFF=no AT, ON=AT OK", C_GREEN);
        } else {
          bu03ShowUartFailScreen("P-MOS G42 TEST");
          dispBody(11, "Also: AO3401, R8, 220R", C_DGRAY);
        }
        dispBody(13, "Press OK to continue", C_YELLOW);
        markPhaseDrawn();
      }
      if (okPressedOnce())
        enterPhase(PH_UWB_TEST);
      break;

    case PH_UWB_TEST:
      if (phaseJustEntered()) {
        dispHeader("UWB TEST");
        dispBody(0, "Checking BU03...", C_YELLOW);
        dispBody(2, "UART AT command", C_DGRAY);
        markPhaseDrawn();
        results.uwb = testUwbLink();
        enterPhase(PH_UWB_OK);
      }
      break;

    case PH_UWB_OK:
      if (phaseJustEntered()) {
        dispHeader("UWB TEST");
        if (results.uwb) {
          dispSuccess("Test of the UWB");
          dispBody(2, "is successful", C_GREEN);
        } else {
          bu03ShowUartFailScreen("UWB TEST");
        }
        dispBody(13, "Press OK to continue", C_YELLOW);
        markPhaseDrawn();
      }
      if (okPressedOnce())
        enterPhase(PH_LED_RED);
      break;

    case PH_LED_RED:
      if (phaseJustEntered()) {
        dispHeader("LED TEST 1/2");
        dispBody(0, "RED LED should be ON", C_RED);
        dispBody(2, "Press OK if you see it", C_YELLOW);
        digitalWrite(LED_RED, HIGH);
        digitalWrite(LED_GREEN, LOW);
        markPhaseDrawn();
      }
      if (okPressedOnce()) {
        dispHeader("LED TEST 1/2");
        dispSuccess("Red LED confirmed");
        Serial.println("[LED] RED OK");
        enterPhase(PH_LED_RED_OK);
      }
      break;

    case PH_LED_RED_OK:
      if (phaseJustEntered()) {
        digitalWrite(LED_RED, LOW);
        markPhaseDrawn();
        enterPhase(PH_LED_GREEN);
      }
      break;

    case PH_LED_GREEN:
      if (phaseJustEntered()) {
        dispHeader("LED TEST 2/2");
        dispBody(0, "GREEN LED should be ON", C_GREEN);
        dispBody(2, "Press OK if you see it", C_YELLOW);
        dispBody(4, "(not button flash)", C_DGRAY);
        digitalWrite(LED_RED, LOW);
        digitalWrite(LED_GREEN, HIGH);
        markPhaseDrawn();
      }
      if (okPressedOnce()) {
        dispHeader("LED TEST 2/2");
        dispSuccess("Green LED confirmed");
        Serial.println("[LED] GREEN OK");
        results.led = true;
        enterPhase(PH_LED_GREEN_OK);
      }
      break;

    case PH_LED_GREEN_OK:
      if (phaseJustEntered()) {
        digitalWrite(LED_GREEN, LOW);
        markPhaseDrawn();
        enterPhase(PH_I2C_TEST);
      }
      break;

    case PH_I2C_TEST:
      if (phaseJustEntered()) {
        dispHeader("I2C / EEPROM");
        dispBody(0, "Testing cartridges...", C_YELLOW);
        results.i2c = runI2cTest();
        if (results.i2c)
          dispSuccess("I2C/EEPROM OK");
        else {
          dispFail("No EEPROM found");
          dispBody(2, "Slots CH1 CH2 CH4-CH6", C_YELLOW);
          dispBody(3, "WP+ A0-A2 -> GND", C_ORANGE);
        }
        dispBody(13, "Press OK to continue", C_YELLOW);
        markPhaseDrawn();
      }
      if (okPressedOnce())
        enterPhase(PH_LORA_TEST);
      break;

    case PH_LORA_TEST:
      if (phaseJustEntered()) {
        dispHeader("LoRa TEST");
        dispBody(0, "SPI probe...", C_YELLOW);
        bu03Power(false);
        uint8_t ver = loraProbeVersion();
        results.lora = (ver == 0x12);
        char buf[28];
        snprintf(buf, sizeof(buf), "REG=0x%02X", ver);
        dispBody(2, buf, C_WHITE);
        dispBody(3, loraDiagHint(ver), results.lora ? C_GREEN : C_ORANGE);
        if (results.lora) {
          dispSuccess("LoRa chip OK");
        } else {
          dispFail("LoRa not detected");
          dispBody(5, "Check G39/G40/3V3", C_DGRAY);
        }
        dispBody(13, "Press OK to continue", C_YELLOW);
        Serial.printf("[LoRa] %s (0x%02X)\n", loraDiagHint(ver), ver);
        markPhaseDrawn();
      }
      if (okPressedOnce())
        enterPhase(PH_SUMMARY);
      break;

    case PH_SUMMARY:
      if (phaseJustEntered()) {
        ensureRadioInit();
        results.buttons = btnDone[0] && btnDone[1] && btnDone[2] && btnDone[3];
        results.nvs = runNvsTest();
        results.espnow = espNowOk;
        showSummary();
        Serial.println("\n===== GUIDED TEST SUMMARY =====");
        Serial.printf("  Display:   %s\n", results.display ? "OK" : "FAIL");
        Serial.printf("  Buttons:   %s\n", results.buttons ? "OK" : "FAIL");
        Serial.printf("  Motor:     %s\n", results.vibro ? "OK" : "FAIL");
        Serial.printf("  Sound:     %s\n", results.dfplayer ? "OK" : "FAIL");
        Serial.printf("  P-MOS G42: %s\n", results.uwb_pmos ? "OK" : "FAIL");
        Serial.printf("  UWB:       %s\n", results.uwb ? "OK" : "FAIL");
        Serial.printf("  LED:       %s\n", results.led ? "OK" : "FAIL");
        Serial.printf("  I2C:       %s\n", results.i2c ? "OK" : "FAIL");
        Serial.printf("  LoRa:      %s\n", results.lora ? "OK" : "FAIL");
        Serial.printf("  NVS:       %s\n", results.nvs ? "OK" : "FAIL");
        Serial.printf("  ESP-NOW:   %s\n", results.espnow ? "OK" : "FAIL");
        Serial.println("===============================\n");
        markPhaseDrawn();
      }
      if (okPressedOnce())
        enterPhase(PH_DISTANCE);
      break;

    case PH_DISTANCE:
      if (phaseJustEntered()) {
        bu03Power(true);
        bu03Serial.setRxBufferSize(1024);
        bu03Serial.begin(115200, SERIAL_8N1, BU03_RX, BU03_TX);
        delay(400);
        distM = -1.0f;
        distLastOkMs = 0;
        distLastPollMs = 0;
        bu03BenchAnchor = false;
        bu03ReadCfg(nullptr);
        distScreenDirty = true;
        Serial.println("[UWB] Live distance screen");
        markPhaseDrawn();
      }

      if (bu03IsAnchorRole() && millis() - distLastPollMs >= BU03_POLL_MS) {
        distLastPollMs = millis();
        bu03PollDistance();
        distScreenDirty = true;
      }

      if (waitButtonEdge(2)) {
        if (!bu03IsAnchorRole()) {
          if (bu03SetBenchAnchor()) {
            distLastOkMs = 0;
            bu03PollDistance();
          }
        } else {
          bu03PollDistance();
        }
        distScreenDirty = true;
      }
      if (waitButtonEdge(3)) { // ESC — back to summary
        enterPhase(PH_SUMMARY);
        break;
      }

      if (distScreenDirty) {
        showDistanceScreen();
        distScreenDirty = false;
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("=== STALKER PDA GUIDED HW TEST ===");
  esp_reset_reason_t rr = esp_reset_reason();
  if (rr == ESP_RST_BROWNOUT)
    Serial.println("[BOOT] !!! BROWNOUT reset — check battery / 5V / VIB key");
  Serial.printf("[BOOT] reset reason=%d\n", (int)rr);
  Serial.println("Pins: MASTER_SPECIFICATION.md ch.3");
  Serial.printf("BU03: RX=G%d TX=G%d PLATE=%d boot=%ums\n", BU03_RX, BU03_TX,
                BU03_PINS_PLATE, BU03_BOOT_MS);

  for (int i = 0; i < 4; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
    btnLast[i] = HIGH;
    btnDebounce[i] = 0;
  }

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(PIN_VIBRO, OUTPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(PIN_VIBRO, LOW);

#if HAS_BU03_PWR
  pinMode(BU03_PWR, OUTPUT);
  digitalWrite(BU03_PWR, LOW);
#endif

  Serial.print("Display... ");
  displayOk = initDisplay();
  Serial.println(displayOk ? "OK" : "FAIL");

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(100000);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(80);
  espNowOk = (esp_now_init() == ESP_OK);
  Serial.printf("ESP-NOW: %s\n", espNowOk ? "OK" : "FAIL");

  Serial.print("DFPlayer... ");
  dfSerial.begin(9600, SERIAL_8N1, DF_RX, DF_TX);
  audioOk = initDfPlayer();
  Serial.println(audioOk ? "OK" : "FAIL (no 5V / no response)");

  enterPhase(PH_SPLASH);
  Serial.println("Guided test running on display...");
}

void loop() {
  runGuidedTest();
  delay(10);
}
