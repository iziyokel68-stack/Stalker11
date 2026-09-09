/**

 * S.T.A.L.K.E.R. — тест EEPROM через TCA9548A

 *

 * Назначение: проверка 2 (или больше) EEPROM на мультиплексоре TCA9548A

 * без дисплея и остальной периферии ПДА.

 *

 * Поддерживаемые платы (выбор в Arduino IDE → Tools → Board):

 *   • ESP32 Dev Module        — ESP32-WROOM32 DevKit (тестовый этап 1)

 *   • ESP32S3 Dev Module      — ESP32-S3-N16R8 (целевая плата ПДА, этап 2)

 *

 * Среда: Arduino IDE 2.x, пакет esp32 by Espressif

 * Serial Monitor: 115200 бод

 *   S3: USB CDC On Boot = Enabled

 *

 * ─── Пины I2C (зависят от платы, см. #ifdef ниже) ───

 *

 *   ESP32-WROOM32 DevKit:

 *     GPIO 21 → TCA SDA   (штатная I2C-шина DevKit)

 *     GPIO 22 → TCA SCL

 *     ⚠ НЕ использовать GPIO 6–11 — заняты внутренней SPI Flash!

 *

 *   ESP32-S3 DevKit (как на плате ПДА):

 *     GPIO 9  → TCA SDA

 *     GPIO 8  → TCA SCL

 *

 * ─── Подключение ESP32 → модуль TCA9548A ───

 *   ESP32 3V3  → TCA VCC

 *   ESP32 GND  → TCA GND

 *   GPIO SDA   → TCA SDA  (главная I2C-шина)

 *   GPIO SCL   → TCA SCL

 *

 * На модуле TCA: A0, A1, A2 → GND (адрес мультиплексора 0x70).

 * Подтяжки 4.7 кОм SDA/SCL → 3V3 — на модуле TCA или на ESP32.

 *

 * ─── Подключение EEPROM (24LC256 / AT24C32, DIP-8) ───

 *   Каждый чип на отдельном канале TCA (SDn / SCn):

 *

 *   Канал TCA │ Типичный слот ПДА │ SDA      │ SCL

 *   ──────────┼───────────────────┼──────────┼─────

 *   CH1       │ Универсальный     │ SD1      │ SC1

 *   CH2       │ Броня             │ SD2      │ SC2

 *

 *   EEPROM пин │ Сигнал

 *   ───────────┼──────────────────────────────

 *   8 (VCC)    │ 3.3 V

 *   4 (GND)    │ GND

 *   5 (SDA)    │ SDn канала TCA

 *   6 (SCL)    │ SCn канала TCA

 *   7 (WP)     │ GND  (разрешить запись)

 *   1–3 A0–A2  │ GND  (I2C-адрес 0x50)

 *

 * Команды Serial:

 *   TEST       — полный тест (скан TCA + R/W на CH1 и CH2)

 *   SCAN       — сканировать I2C на главной шине и на каждом канале

 *   CH0..CH7   — выбрать канал и сканировать подшину

 *   HELP       — справка

 *

 * При старте автоматически запускается TEST.

 *

 * ⚠️ БЕЗОПАСНОСТЬ: если провод GND греется — НЕМЕДЛЕННО отключите питание!

 *    См. README_EEPROM_TCA_Test.md — чеклист диагностики КЗ.

 */



#include <Wire.h>



// ─── Выбор пинов I2C по целевой плате ───

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARDUINO_ESP32S3_DEV)

  #define BOARD_NAME    "ESP32-S3"

  #define PIN_SDA       9

  #define PIN_SCL       8

#elif defined(CONFIG_IDF_TARGET_ESP32) || defined(ARDUINO_ESP32_DEV)

  // GPIO21/22 — стандартные I2C на DevKit; безопасны (не strapping, не flash).

  // GPIO6–11 на WROOM32 заняты внутренней SPI Flash — не трогать!

  #define BOARD_NAME    "ESP32-WROOM32"

  #define PIN_SDA       21

  #define PIN_SCL       22

#else

  #error "Выберите плату: ESP32 Dev Module или ESP32S3 Dev Module"

#endif



#define I2C_HZ        100000



#define TCA_ADDR      0x70   // A0=A1=A2=GND на модуле TCA

#define EEPROM_BASE   0x50   // A0=A1=A2=GND на EEPROM



// Тестируемые каналы TCA (2 EEPROM → обычно CH1 universal и CH2 armor)

#define CH_FIRST      1

#define CH_LAST       2

#define EXPECT_CHIPS  2



// Адрес в EEPROM для теста (вне заголовка чипа 0x00..0x25)

#define TEST_ADDR     0x0100

#define TEST_LEN      8

static const uint8_t TEST_PATTERN[TEST_LEN] = {

  0x53, 0x54, 0x41, 0x4C, 0x4B, 0x45, 0x52, 0x21  // "STALKER!"

};



#define EEPROM_WR_MS  5      // задержка записи страницы (24LC256)



// ─── TCA ───

bool tcaSelect(uint8_t channel) {

  if (channel > 7) return false;

  Wire.beginTransmission(TCA_ADDR);

  Wire.write((uint8_t)(1u << channel));

  return Wire.endTransmission() == 0;

}



bool tcaDisableAll() {

  Wire.beginTransmission(TCA_ADDR);

  Wire.write((uint8_t)0);

  return Wire.endTransmission() == 0;

}



bool tcaPresent() {

  Wire.beginTransmission(TCA_ADDR);

  return Wire.endTransmission() == 0;

}



// ─── EEPROM (16-бит адрес, 24LC256 / AT24C32) ───

bool eepromWrite(uint8_t dev, uint16_t addr, const uint8_t *data, uint8_t len) {

  Wire.beginTransmission(dev);

  Wire.write((uint8_t)(addr >> 8));

  Wire.write((uint8_t)(addr & 0xFF));

  for (uint8_t i = 0; i < len; i++) Wire.write(data[i]);

  if (Wire.endTransmission() != 0) return false;

  delay(EEPROM_WR_MS);

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



bool eepromPing(uint8_t dev) {

  Wire.beginTransmission(dev);

  return Wire.endTransmission() == 0;

}



int findEepromOnBus(uint8_t *outAddr) {

  for (uint8_t a = 0; a < 8; a++) {

    uint8_t dev = EEPROM_BASE | a;

    if (eepromPing(dev)) {

      if (outAddr) *outAddr = dev;

      return (int)dev;

    }

  }

  return -1;

}



void scanBus(const char *label) {

  Serial.printf("  [%s] ", label);

  bool any = false;

  for (uint8_t addr = 0x08; addr < 0x78; addr++) {

    Wire.beginTransmission(addr);

    if (Wire.endTransmission() == 0) {

      Serial.printf("0x%02X ", addr);

      any = true;

    }

  }

  Serial.println(any ? "" : "(пусто)");

}



bool testEepromRw(uint8_t dev, uint8_t channel) {

  Serial.printf("  CH%d EEPROM 0x%02X: запись %d байт @0x%04X ... ",

                channel, dev, TEST_LEN, TEST_ADDR);

  if (!eepromWrite(dev, TEST_ADDR, TEST_PATTERN, TEST_LEN)) {

    Serial.println("FAIL (write)");

    return false;

  }



  uint8_t buf[TEST_LEN] = {0};

  uint8_t n = eepromRead(dev, TEST_ADDR, buf, TEST_LEN);

  if (n != TEST_LEN) {

    Serial.printf("FAIL (read %u/%u bytes)\n", n, TEST_LEN);

    return false;

  }



  for (uint8_t i = 0; i < TEST_LEN; i++) {

    if (buf[i] != TEST_PATTERN[i]) {

      Serial.printf("FAIL (verify byte %u: got 0x%02X)\n", i, buf[i]);

      return false;

    }

  }

  Serial.println("OK");

  return true;

}



void cmdScan() {

  Serial.println("\n=== I2C SCAN ===");

  tcaDisableAll();

  delay(2);

  Serial.println("Главная шина (ESP32 ↔ TCA):");

  scanBus("main");



  for (uint8_t ch = 0; ch < 8; ch++) {

    if (!tcaSelect(ch)) {

      Serial.printf("CH%d: не удалось выбрать канал\n", ch);

      continue;

    }

    delay(2);

    Serial.printf("CH%d подшина:\n", ch);

    scanBus("sub");

  }

  tcaSelect(0);

}



bool runFullTest() {

  Serial.println("\n========================================");

  Serial.printf("  EEPROM + TCA9548A TEST (%s)\n", BOARD_NAME);

  Serial.printf("  SDA=GPIO%d SCL=GPIO%d  TCA=0x%02X\n",

                PIN_SDA, PIN_SCL, TCA_ADDR);

  Serial.println("========================================");



  if (!tcaPresent()) {

    Serial.println("\nFAIL: TCA9548A не найден на 0x70");

    Serial.printf("  Проверьте: GND, 3V3, SDA→G%d, SCL→G%d, адрес A0-A2=GND\n",

                  PIN_SDA, PIN_SCL);

    return false;

  }

  Serial.println("TCA9548A 0x70 — OK\n");



  int found = 0;

  int passed = 0;



  for (uint8_t ch = CH_FIRST; ch <= CH_LAST; ch++) {

    if (!tcaSelect(ch)) {

      Serial.printf("CH%d: select FAIL\n", ch);

      continue;

    }

    delay(2);



    uint8_t dev = 0;

    int eeprom = findEepromOnBus(&dev);

    if (eeprom < 0) {

      Serial.printf("CH%d: EEPROM не найден\n", ch);

      continue;

    }



    found++;

    Serial.printf("CH%d: найден 0x%02X\n", ch, dev);

    if (testEepromRw(dev, ch)) passed++;

  }



  tcaSelect(0);



  Serial.println("\n--- ИТОГ ---");

  Serial.printf("  Чипов найдено:  %d\n", found);

  Serial.printf("  R/W тест OK:    %d\n", passed);

  Serial.printf("  Ожидалось:      %d\n", EXPECT_CHIPS);



  bool ok = (found == EXPECT_CHIPS && passed == EXPECT_CHIPS);

  Serial.printf("\n%s\n", ok ? ">>> PASS <<<" : ">>> FAIL <<<");

  if (!ok && found < EXPECT_CHIPS) {

    Serial.println("Подключите 2 EEPROM на разные каналы TCA (обычно CH1 и CH2).");

    Serial.println("WP и A0-A2 каждого чипа должны быть на GND.");

  }

  return ok;

}



void printHelp() {

  Serial.println("\nКоманды:");

  Serial.println("  TEST   — полный тест TCA + EEPROM (CH1, CH2)");

  Serial.println("  SCAN   — I2C-сканер главной шины и каждого канала");

  Serial.println("  CH0..7 — выбрать канал TCA и сканировать подшину");

  Serial.println("  HELP   — эта справка");

}



void setup() {

  Serial.begin(115200);

  delay(800);



  Wire.begin(PIN_SDA, PIN_SCL);

  Wire.setClock(I2C_HZ);



  Serial.println();

  Serial.printf("EEPROM_TCA_Test v1.1 — STALKER PDA [%s]\n", BOARD_NAME);

  Serial.printf("I2C: SDA=GPIO%d, SCL=GPIO%d\n", PIN_SDA, PIN_SCL);

  printHelp();



  runFullTest();

  Serial.println("\nГотов. Введите TEST или SCAN.");

}



void loop() {

  if (!Serial.available()) return;



  String cmd = Serial.readStringUntil('\n');

  cmd.trim();

  cmd.toUpperCase();



  if (cmd.length() == 0) return;



  if (cmd == "TEST") {

    runFullTest();

  } else if (cmd == "SCAN") {

    cmdScan();

  } else if (cmd == "HELP" || cmd == "?") {

    printHelp();

  } else if (cmd.length() == 3 && cmd.charAt(0) == 'C' && cmd.charAt(1) == 'H') {

    int ch = cmd.charAt(2) - '0';

    if (ch >= 0 && ch <= 7) {

      if (tcaSelect((uint8_t)ch)) {

        Serial.printf("Канал %d выбран. Подшина:\n", ch);

        scanBus("sub");

      } else {

        Serial.printf("Не удалось выбрать CH%d\n", ch);

      }

    } else {

      Serial.println("Используйте CH0 .. CH7");

    }

  } else {

    Serial.printf("Неизвестно: %s (HELP)\n", cmd.c_str());

  }

}


