/**
 * S.T.A.L.K.E.R. — тест условной компиляции, профиль ESP32-C3
 *
 * Проверяет ветку #ifdef как в Terminal_ESP32 / Cashier_ESP32:
 *   TERMINAL_BOARD_ESP32C3 → I2C GPIO 5/6
 *
 * Открывайте: test_firmware/CondCompile_ESP32C3/CondCompile_ESP32C3.ino
 *
 * Arduino IDE:
 *   Board  — ESP32C3 Dev Module (SuperMini)
 *   Monitor — 115200 бод
 *
 * Ожидание при старте:
 *   STALKER:COND_COMPILE:v1,profile=ESP32-C3,i2c_sda=5,i2c_scl=6,tca_mux=1
 *   BRANCH:TCA_MUX_ENABLED
 *   OK:READY
 *
 * Сравните с CondCompile_WROOM32 — profile и пины должны отличаться.
 * См. test_firmware/CondCompile_Test/README_CondCompile_Test.md
 */

#include <Wire.h>

#define TERMINAL_BOARD_ESP32C3

#if defined(TERMINAL_BOARD_ESP32C3)
  #define BOARD_PROFILE "ESP32-C3"
  #define I2C_SDA    5
  #define I2C_SCL    6
  #define PIN_LED    8
#elif defined(TERMINAL_BOARD_WROOM32)
  #define BOARD_PROFILE "ESP32-WROOM32"
  #define I2C_SDA    21
  #define I2C_SCL    22
  #define PIN_LED    2
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_ESP32C3_DEV)
  #define BOARD_PROFILE "ESP32-C3"
  #define I2C_SDA    5
  #define I2C_SCL    6
  #define PIN_LED    8
#elif defined(CONFIG_IDF_TARGET_ESP32) || defined(ARDUINO_ESP32_DEV)
  #define BOARD_PROFILE "ESP32-WROOM32"
  #define I2C_SDA    21
  #define I2C_SCL    22
  #define PIN_LED    2
#else
  #error "Выберите плату: ESP32 Dev Module или ESP32C3 Dev Module (или задайте TERMINAL_BOARD_*)"
#endif

#define USE_TCA_MUX 1

static uint32_t lastBlinkMs = 0;
static bool ledOn = false;

void printBanner() {
  Serial.printf(
    "STALKER:COND_COMPILE:v1,profile=%s,i2c_sda=%d,i2c_scl=%d,tca_mux=%d\n",
    BOARD_PROFILE, I2C_SDA, I2C_SCL, USE_TCA_MUX);
#if USE_TCA_MUX
  Serial.println("BRANCH:TCA_MUX_ENABLED");
#else
  Serial.println("BRANCH:DIRECT_I2C");
#endif
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  printBanner();
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println("OK:READY");
}

void loop() {
  uint32_t now = millis();
  if (now - lastBlinkMs >= 500) {
    lastBlinkMs = now;
    ledOn = !ledOn;
    digitalWrite(PIN_LED, ledOn ? HIGH : LOW);
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "WHO" || cmd == "STALKER_WHO") {
      printBanner();
    } else if (cmd == "PING") {
      Serial.println("PONG");
    } else if (cmd == "HELP") {
      Serial.println("WHO / STALKER_WHO — баннер профиля");
      Serial.println("PING — PONG");
      Serial.println("HELP — эта справка");
    }
  }
}
