/**
 * S.T.A.L.K.E.R. — Универсальный терминал (Terminal_ESP32) v1.0
 *
 * ─── Выбор платы (Arduino IDE → Tools → Board) ───
 *
 *   • ESP32 Dev Module — ESP32-WROOM-32 DevKit (рекомендуется)
 *       I2C: GPIO 21 (SDA), GPIO 22 (SCL)
 *       Serial: USB-UART (CP2102 / CH340), 115200 бод
 *
 *   • ESP32C3 Dev Module — ESP32-C3 SuperMini
 *       I2C: GPIO 5 (SDA), GPIO 6 (SCL)
 *
 * Принудительный профиль (раскомментируйте ОДИН):
 *   // #define TERMINAL_BOARD_WROOM32
 *   // #define TERMINAL_BOARD_ESP32C3
 *
 * Роли терминала (NVS key "terminal_role", переживает перезагрузку):
 *   STORE  — касса (op=PURCHASE, 0)
 *   ATM    — банкомат (op=ATM, 4)
 *   QUEST  — квестовая доска (op=QUEST, 2)
 *   ADMIT  — допуск в игру (op=ADMIT, 3)
 *   BANK   — банк (op=BANK, 1)
 *
 * Протокол Serial (115200):
 *   При загрузке: TERMINAL_ROLE:... и STALKER:TERMINAL:v1,... (до READY)
 *   STALKER_WHO              → STALKER:TERMINAL:v1,role=STORE,op_default=0,name=Касса
 *   TERMINAL_ROLE            → TERMINAL_ROLE:STORE,op_default=0,name=Касса
 *   TERMINAL_ROLE:ATM        → OK:TERMINAL_ROLE:ATM
 *   PING                     → PONG
 *   I2C_SCAN / EEPROM_PING
 *   TXN_START:amount=500,item=1[,op=N]  — без op используется op роли
 *   TXN_STATUS / TXN_WAIT / TXN_RESET
 *
 * Тест: test_firmware/Terminal_Test/README_Terminal_Test.md
 */

#include <Wire.h>
#include <Preferences.h>
#include "eeprom_protocol.h"
#include "mux_channels.h"

// ─── Профиль платы ───
// #define TERMINAL_BOARD_WROOM32
// #define TERMINAL_BOARD_ESP32C3

#if defined(TERMINAL_BOARD_ESP32C3)
  #define BOARD_NAME "ESP32-C3"
  #define I2C_SDA    5
  #define I2C_SCL    6
#elif defined(TERMINAL_BOARD_WROOM32)
  #define BOARD_NAME "ESP32-WROOM32"
  #define I2C_SDA    21
  #define I2C_SCL    22
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_ESP32C3_DEV)
  #define BOARD_NAME "ESP32-C3"
  #define I2C_SDA    5
  #define I2C_SCL    6
#elif defined(CONFIG_IDF_TARGET_ESP32) || defined(ARDUINO_ESP32_DEV)
  #define BOARD_NAME "ESP32-WROOM32"
  #define I2C_SDA    21
  #define I2C_SCL    22
#else
  #error "Выберите плату: ESP32 Dev Module или ESP32C3 Dev Module (или задайте TERMINAL_BOARD_*)"
#endif

#define USE_TCA_MUX 1
#define TERMINAL_TEST_MODE 0

#define EEPROM_ADDR EEPROM_ADDR_DEFAULT
#define EEPROM_WR_DLY 5
#define NVS_NS "stalker"
#define NVS_KEY_ROLE "terminal_role"

enum TerminalRole : uint8_t {
    ROLE_STORE = 0,
    ROLE_ATM,
    ROLE_QUEST,
    ROLE_ADMIT,
    ROLE_BANK,
    ROLE_COUNT
};

static const char *ROLE_NAMES[] = {"STORE", "ATM", "QUEST", "ADMIT", "BANK"};
static const char *ROLE_DISPLAY[] = {
    "Касса", "Банкомат", "Квестовая доска", "Допуск", "Банк"
};
static const uint8_t ROLE_DEFAULT_OP[] = {
    TXN_OP_PURCHASE,
    TXN_OP_ATM,
    TXN_OP_QUEST,
    TXN_OP_ADMIT,
    TXN_OP_BANK
};

Preferences prefs;
TerminalRole currentRole = ROLE_STORE;

#if USE_TCA_MUX
#define CASSETTE_CHANNEL MUX_CH_UNIVERSAL

bool tcaSelect(uint8_t channel) {
    if (channel > 7) return false;
    Wire.beginTransmission(TCA_ADDR_DEFAULT);
    Wire.write((uint8_t)(1 << channel));
    return Wire.endTransmission() == 0;
}

void muxSelectCassette() {
    tcaSelect(CASSETTE_CHANNEL);
}
#else
void muxSelectCassette() {}
#endif

String serialBuf = "";
uint32_t nextTxnId = 1;

int roleFromName(const String &name) {
    String u = name;
    u.toUpperCase();
    u.trim();
    for (uint8_t i = 0; i < ROLE_COUNT; i++) {
        if (u == ROLE_NAMES[i]) return (int)i;
    }
    return -1;
}

void loadTerminalRole() {
    prefs.begin(NVS_NS, true);
    uint8_t stored = prefs.getUChar(NVS_KEY_ROLE, ROLE_STORE);
    prefs.end();
    if (stored >= ROLE_COUNT) stored = ROLE_STORE;
    currentRole = (TerminalRole)stored;
}

void saveTerminalRole(TerminalRole role) {
    currentRole = role;
    prefs.begin(NVS_NS, false);
    prefs.putUChar(NVS_KEY_ROLE, (uint8_t)role);
    prefs.end();
}

String terminalRoleLine() {
    uint8_t r = (uint8_t)currentRole;
    String s = "TERMINAL_ROLE:" + String(ROLE_NAMES[r]);
    s += ",op_default=" + String(ROLE_DEFAULT_OP[r]);
    s += ",name=" + String(ROLE_DISPLAY[r]);
    return s;
}

String stalkerWhoLine() {
    uint8_t r = (uint8_t)currentRole;
    String s = "STALKER:TERMINAL:v1,role=" + String(ROLE_NAMES[r]);
    s += ",op_default=" + String(ROLE_DEFAULT_OP[r]);
    s += ",name=" + String(ROLE_DISPLAY[r]);
    return s;
}

bool eepromWriteByte(uint16_t addr, uint8_t val) {
    muxSelectCassette();
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    Wire.write(val);
    uint8_t err = Wire.endTransmission();
    delay(EEPROM_WR_DLY);
    return err == 0;
}

bool eepromReadByte(uint16_t addr, uint8_t &val) {
    muxSelectCassette();
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    if (Wire.endTransmission() != 0) return false;
    Wire.requestFrom((uint8_t)EEPROM_ADDR, (uint8_t)1);
    if (!Wire.available()) return false;
    val = Wire.read();
    return true;
}

bool eepromWriteBlock(uint16_t addr, const uint8_t *data, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        if (!eepromWriteByte(addr + i, data[i])) return false;
    }
    return true;
}

bool eepromReadBlock(uint16_t addr, uint8_t *buf, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        if (!eepromReadByte(addr + i, buf[i])) return false;
    }
    return true;
}

bool eepromPresent() {
    muxSelectCassette();
    Wire.beginTransmission(EEPROM_ADDR);
    return Wire.endTransmission() == 0;
}

bool txnRead(uint8_t *block) {
    return eepromReadBlock(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE);
}

bool txnWrite(const uint8_t *block) {
    return eepromWriteBlock(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE);
}

String txnStatusLine(const uint8_t *block) {
    uint16_t result = txn_read_u16(block, TXN_OFF_RESULT);
    uint8_t op = block[TXN_OFF_OP_TYPE];
    String s = "TXN:state=" + String(block[TXN_OFF_STATE]);
    s += ",txn_id=" + String(txn_read_u32(block, TXN_OFF_TXN_ID));
    s += ",amount=" + String(txn_read_i32(block, TXN_OFF_AMOUNT));
    s += ",item=" + String(txn_read_u16(block, TXN_OFF_ITEM_ID));
    s += ",paid=" + String(txn_read_i32(block, TXN_OFF_PAID));
    s += ",balance=" + String(txn_read_i32(block, TXN_OFF_BALANCE));
    s += ",result=" + String(result);
    s += ",result_name=" + String(txn_result_name(result));
    s += ",op=" + String(op);
    s += ",op_name=" + String(txn_op_name(op));
    s += ",flags=" + String(block[TXN_OFF_FLAGS]);
    return s;
}

bool parseKeyVal(const String &cfg, const char *key, int &out) {
    String needle = String(key) + "=";
    int pos = cfg.indexOf(needle);
    if (pos < 0) return false;
    int start = pos + needle.length();
    int end = cfg.indexOf(',', start);
    if (end < 0) end = cfg.length();
    out = cfg.substring(start, end).toInt();
    return true;
}

bool parseKeyStr(const String &cfg, const char *key, String &out) {
    String needle = String(key) + "=";
    int pos = cfg.indexOf(needle);
    if (pos < 0) return false;
    int start = pos + needle.length();
    int end = cfg.indexOf(',', start);
    if (end < 0) end = cfg.length();
    out = cfg.substring(start, end);
    out.trim();
    return true;
}

void cmdI2cScan() {
    Serial.println("I2C_SCAN:begin");
    uint8_t found = 0;
    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
        muxSelectCassette();
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  0x%02X\n", addr);
            found++;
        }
        delay(2);
    }
#if USE_TCA_MUX
    Serial.printf("I2C_SCAN:end count=%u (TCA CH%u selected)\n",
                  (unsigned)found, (unsigned)CASSETTE_CHANNEL);
#else
    Serial.printf("I2C_SCAN:end count=%u (direct bus)\n", (unsigned)found);
#endif
}

void cmdEepromPing() {
    if (!eepromPresent()) {
        Serial.println("EEPROM:MISSING");
        return;
    }
    Serial.println("EEPROM:OK addr=0x50");
    uint8_t block[TXN_BLOCK_SIZE];
    if (!txnRead(block)) {
        Serial.println("EEPROM:READ_FAIL");
        return;
    }
    if (!txn_validate_block(block)) {
        Serial.println("EEPROM:TXN_BAD_CRC");
        return;
    }
    Serial.println("EEPROM:TXN_OK " + txnStatusLine(block));
}

void cmdTerminalRole(const String &args) {
    if (args.length() == 0) {
        Serial.println(terminalRoleLine());
        return;
    }
    int idx = roleFromName(args);
    if (idx < 0) {
        Serial.println("ERROR:BAD_ROLE");
        return;
    }
    saveTerminalRole((TerminalRole)idx);
    Serial.println("OK:" + terminalRoleLine());
}

void cmdTxnStart(const String &args) {
    if (!eepromPresent()) {
        Serial.println("ERROR:EEPROM_NOT_FOUND");
        return;
    }
    int amount = 0, item = 0, txnId = 0, op = ROLE_DEFAULT_OP[(uint8_t)currentRole], flags = 0;
    bool opSpecified = false;
    String questId;
    parseKeyVal(args, "amount", amount);
    parseKeyVal(args, "item", item);
    if (parseKeyVal(args, "op", op)) opSpecified = true;
    if (!opSpecified) op = ROLE_DEFAULT_OP[(uint8_t)currentRole];
    parseKeyVal(args, "flags", flags);
    parseKeyStr(args, "quest_id", questId);
    if (!parseKeyVal(args, "txn_id", txnId) || txnId <= 0)
        txnId = (int)nextTxnId++;

    uint8_t block[TXN_BLOCK_SIZE];
    if (!txnRead(block)) {
        Serial.println("ERROR:READ_FAILED");
        return;
    }
    if (txn_validate_block(block) && txn_get_state(block) != TXN_STATE_IDLE) {
        Serial.println("ERROR:TXN_BUSY:" + txnStatusLine(block));
        return;
    }

    switch (op) {
    case TXN_OP_PURCHASE:
        if (amount <= 0) { Serial.println("ERROR:BAD_AMOUNT"); return; }
        txn_build_purchase(block, (uint32_t)txnId, amount, (uint16_t)item);
        break;
    case TXN_OP_BANK:
    case TXN_OP_ATM:
        if (amount <= 0) { Serial.println("ERROR:BAD_AMOUNT"); return; }
        txn_build_bank(block, (uint32_t)txnId, amount, (uint16_t)item,
                       (flags & TXN_FLAG_BANK_DEPOSIT) != 0);
        break;
    case TXN_OP_QUEST:
        txn_build_quest(block, (uint32_t)txnId, (uint16_t)item, amount,
                        questId.length() ? questId.c_str() : nullptr,
                        (flags & TXN_FLAG_QUEST_COMPLETE) != 0);
        break;
    case TXN_OP_ADMIT:
        txn_build_admit(block, (uint32_t)txnId);
        break;
    default:
        Serial.println("ERROR:BAD_OP");
        return;
    }
    if (flags && op != TXN_OP_BANK && op != TXN_OP_QUEST)
        block[TXN_OFF_FLAGS] = (uint8_t)flags;

    txn_set_state(block, TXN_STATE_PENDING);
    if (!txnWrite(block)) {
        Serial.println("ERROR:WRITE_FAILED");
        return;
    }
    Serial.println("OK:TXN_PENDING:" + txnStatusLine(block));
}

void cmdTxnStatus() {
    if (!eepromPresent()) {
        Serial.println("ERROR:EEPROM_NOT_FOUND");
        return;
    }
    uint8_t block[TXN_BLOCK_SIZE];
    if (!txnRead(block)) {
        Serial.println("ERROR:READ_FAILED");
        return;
    }
    if (!txn_validate_block(block)) {
        Serial.println("ERROR:BAD_CRC");
        return;
    }
    Serial.println(txnStatusLine(block));
}

void cmdTxnWait(const String &args) {
    int timeoutMs = 30000;
    parseKeyVal(args, "timeout_ms", timeoutMs);
    uint32_t start = millis();
    while ((int32_t)(millis() - start) < timeoutMs) {
        uint8_t block[TXN_BLOCK_SIZE];
        if (txnRead(block) && txn_validate_block(block)) {
            uint8_t st = txn_get_state(block);
            if (txn_is_terminal_state(st)) {
                if (st == TXN_STATE_SUCCESS)
                    Serial.println("OK:TXN_DONE:" + txnStatusLine(block));
                else
                    Serial.println("ERROR:TXN_FAILED:" + txnStatusLine(block));
                return;
            }
        }
        delay(100);
        yield();
    }
    Serial.println("ERROR:TIMEOUT");
}

void cmdTxnReset() {
    uint8_t block[TXN_BLOCK_SIZE];
    txn_init_idle(block);
    if (txnWrite(block))
        Serial.println("OK:TXN_IDLE");
    else
        Serial.println("ERROR:RESET_FAILED");
}

void processCommand(const String &cmd) {
    if (cmd == "STALKER_WHO") {
        Serial.println(stalkerWhoLine());
    } else if (cmd == "PING") {
        Serial.println("PONG");
    } else if (cmd == "I2C_SCAN") {
        cmdI2cScan();
    } else if (cmd == "EEPROM_PING") {
        cmdEepromPing();
    } else if (cmd == "TERMINAL_ROLE" || cmd.startsWith("TERMINAL_ROLE:")) {
        String args = (cmd.length() > 14) ? cmd.substring(14) : "";
        cmdTerminalRole(args);
    } else if (cmd.startsWith("TXN_START:")) {
        cmdTxnStart(cmd.substring(10));
    } else if (cmd == "TXN_STATUS") {
        cmdTxnStatus();
    } else if (cmd.startsWith("TXN_WAIT:")) {
        cmdTxnWait(cmd.substring(9));
    } else if (cmd == "TXN_WAIT") {
        cmdTxnWait("");
    } else if (cmd == "TXN_RESET") {
        cmdTxnReset();
    } else {
        Serial.println("ERROR:UNKNOWN_CMD");
    }
}

void handleSerial() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            serialBuf.trim();
            if (serialBuf.length() > 0) {
                processCommand(serialBuf);
                serialBuf = "";
            }
        } else {
            serialBuf += c;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    loadTerminalRole();
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);

    Serial.println("=== STALKER Terminal v1.0 ===");
    Serial.println(terminalRoleLine());
    Serial.println(stalkerWhoLine());  // handshake для программатора PC (до READY)
    Serial.printf("Board: %s | I2C SDA=GPIO%d SCL=GPIO%d @100kHz\n",
                  BOARD_NAME, I2C_SDA, I2C_SCL);
#if USE_TCA_MUX
    Wire.beginTransmission(TCA_ADDR_DEFAULT);
    if (Wire.endTransmission() == 0)
        Serial.printf("TCA9548A @0x70 OK, cassette on CH%u\n", (unsigned)CASSETTE_CHANNEL);
    else
        Serial.println("TCA9548A: NOT FOUND");
#endif
    if (eepromPresent())
        Serial.println("EEPROM: FOUND at 0x50");
    else
        Serial.println("EEPROM: NOT FOUND — check wiring");
#if TERMINAL_TEST_MODE
    Serial.println("TEST_MODE: ON — use I2C_SCAN / EEPROM_PING");
#endif
    Serial.println("READY");
}

void loop() {
    handleSerial();
}
