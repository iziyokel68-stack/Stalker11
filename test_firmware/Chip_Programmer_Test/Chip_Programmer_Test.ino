/**
 * S.T.A.L.K.E.R. — тест программатора чипов на отдельном ESP32-S3
 *
 * Тот же Serial-протокол, что Chip_Programmer_ESP32 / programmer.py
 * (STALKER:CHIP_BOX:v1). Плата — отдельный ESP32-S3, EEPROM напрямую на I2C.
 *
 * Плата: ESP32-S3 (GPIO 39/40 нет на классическом ESP32-WROOM:
 *         GPIO 39 там input-only, GPIO 40 отсутствует).
 *
 * Arduino IDE:
 *   Board              ESP32S3 Dev Module
 *   USB CDC On Boot    Enabled
 *   Upload Speed       115200 (если 921600 срывается)
 *   Serial Monitor     115200
 *
 * Подключение EEPROM (24LC256 / AT24C32, адрес 0x50):
 *   EEPROM VCC  → 3.3V
 *   EEPROM GND  → GND
 *   EEPROM SDA  → GPIO 40
 *   EEPROM SCL  → GPIO 39
 *   EEPROM WP   → GND (запись разрешена)
 *   EEPROM A0,A1,A2 → GND
 *   Подтяжки 4.7 кОм SDA и SCL → 3.3V (обязательны)
 *
 * Протокол (115200):
 *   STALKER_WHO              → STALKER:CHIP_BOX:v1
 *   PING                     → PONG
 *   CONFIG_WRITE:type=...    → OK:WRITTEN:CRC=XXXX
 *   CONFIG_READ              → CONFIG:...
 *   VERIFY                   → OK:CRC=XXXX
 *   WIPE                     → OK:WIPED
 *   TXN_START:... / TXN_STATUS / TXN_RESET
 *
 * Диагностика (Serial Monitor, programmer.py их не шлёт):
 *   HELP / I2C_SCAN / EEPROM_PING / DUMP
 */

#include <Wire.h>
#include "eeprom_protocol.h"

#define I2C_SDA   40
#define I2C_SCL   39
#define FW_VER    "1.2-addr8"

#define EEPROM_ADDR   0x50
#define EEPROM_WR_DLY 10     // tWR у AT24Cxx до 10 мс
#define EEPROM_PAGE   8      // 24C02=8, 24C04=16, 24LC256=64 — 8 безопасно для всех

#define OFF_TYPE    0x00
#define OFF_SUB     0x01
#define OFF_USES    0x02
#define OFF_RSVD    0x03
#define OFF_PARAMS  0x04
#define OFF_CRC     0x24
#define DATA_SIZE   0x24

// 1 = AT24C02/04/08/16 (байт адреса), 2 = AT24C32 / 24LC256
uint8_t eepromAddrBytes = 1;

struct ChipData {
    uint8_t  chip_type;
    uint8_t  chip_sub;
    uint8_t  chip_uses;
    uint8_t  rsvd;
    int16_t  params[16];
};

uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

uint8_t eepromDev(uint16_t addr) {
    if (eepromAddrBytes == 2) return EEPROM_ADDR;
    return (uint8_t)(EEPROM_ADDR | ((addr >> 8) & 0x07));
}

void eepromSendAddr(uint16_t addr) {
    if (eepromAddrBytes == 2) {
        Wire.write((uint8_t)(addr >> 8));
    }
    Wire.write((uint8_t)(addr & 0xFF));
}

bool eepromPresent() {
    Wire.beginTransmission(EEPROM_ADDR);
    return Wire.endTransmission() == 0;
}

bool eepromWaitReady(uint16_t addr = 0) {
    uint8_t dev = eepromDev(addr);
    for (int i = 0; i < 40; i++) {
        Wire.beginTransmission(dev);
        if (Wire.endTransmission() == 0) return true;
        delay(1);
    }
    return false;
}

bool eepromWriteByte(uint16_t addr, uint8_t val) {
    if (!eepromWaitReady(addr)) return false;
    uint8_t dev = eepromDev(addr);
    Wire.beginTransmission(dev);
    eepromSendAddr(addr);
    Wire.write(val);
    if (Wire.endTransmission() != 0) return false;
    delay(EEPROM_WR_DLY);
    return eepromWaitReady(addr);
}

bool eepromReadBlock(uint16_t addr, uint8_t* buf, uint8_t len) {
    for (int attempt = 0; attempt < 4; attempt++) {
        if (!eepromWaitReady(addr)) {
            delay(5);
            continue;
        }
        uint8_t dev = eepromDev(addr);
        Wire.beginTransmission(dev);
        eepromSendAddr(addr);
        if (Wire.endTransmission(false) != 0) {
            delay(5);
            continue;
        }
        uint8_t n = Wire.requestFrom(dev, len);
        if (n != len) {
            delay(5);
            continue;
        }
        for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
        return true;
    }
    return false;
}

bool eepromReadByte(uint16_t addr, uint8_t& val) {
    return eepromReadBlock(addr, &val, 1);
}

bool eepromWriteBlock(uint16_t addr, const uint8_t* data, uint8_t len) {
    uint8_t i = 0;
    while (i < len) {
        uint16_t a = addr + i;
        if (!eepromWaitReady(a)) return false;
        uint8_t chunk = EEPROM_PAGE - (uint8_t)(a % EEPROM_PAGE);
        if (chunk > len - i) chunk = len - i;
        if (eepromAddrBytes == 1 && (uint8_t)a + chunk < (uint8_t)a) {
            chunk = (uint8_t)(256 - (a & 0xFF));
        }
        uint8_t dev = eepromDev(a);
        Wire.beginTransmission(dev);
        eepromSendAddr(a);
        for (uint8_t j = 0; j < chunk; j++) Wire.write(data[i + j]);
        if (Wire.endTransmission() != 0) return false;
        delay(EEPROM_WR_DLY);
        i += chunk;
    }
    return eepromWaitReady(addr);
}

bool eepromDetectAddrWidth() {
    const uint16_t probe = 0x00F0;
    uint8_t val = 0;

    eepromAddrBytes = 1;
    if (eepromWriteByte(probe, 0x5A) && eepromReadByte(probe, val) && val == 0x5A) {
        Serial.println("EEPROM:ADDR=8bit (24C02/04/08/16)");
        eepromWriteByte(probe, 0xFF);
        return true;
    }

    eepromAddrBytes = 2;
    if (eepromWriteByte(probe, 0x5A) && eepromReadByte(probe, val) && val == 0x5A) {
        Serial.println("EEPROM:ADDR=16bit (24C32/24LC256)");
        eepromWriteByte(probe, 0xFF);
        return true;
    }

    eepromAddrBytes = 1;
    Serial.println("EEPROM:ADDR=8bit (fallback)");
    return false;
}

void packChipBuf(const ChipData& d, uint8_t* buf) {
    memset(buf, 0, DATA_SIZE);
    buf[OFF_TYPE] = d.chip_type;
    buf[OFF_SUB]  = d.chip_sub;
    buf[OFF_USES] = d.chip_uses;
    buf[OFF_RSVD] = 0;
    for (int i = 0; i < 16; i++) {
        buf[OFF_PARAMS + i * 2]     = (uint8_t)(d.params[i] & 0xFF);
        buf[OFF_PARAMS + i * 2 + 1] = (uint8_t)((d.params[i] >> 8) & 0xFF);
    }
}

bool writeChip(const ChipData& d) {
    uint8_t buf[DATA_SIZE + 2];
    packChipBuf(d, buf);
    uint16_t crc = crc16(buf, DATA_SIZE);
    buf[OFF_CRC]     = (uint8_t)(crc & 0xFF);
    buf[OFF_CRC + 1] = (uint8_t)((crc >> 8) & 0xFF);
    return eepromWriteBlock(0, buf, DATA_SIZE + 2);
}

bool readChip(ChipData& d, uint16_t& storedCrc) {
    uint8_t buf[DATA_SIZE + 2];
    if (!eepromReadBlock(0, buf, DATA_SIZE + 2)) return false;
    storedCrc = (uint16_t)buf[OFF_CRC] | ((uint16_t)buf[OFF_CRC + 1] << 8);

    d.chip_type = buf[OFF_TYPE];
    d.chip_sub  = buf[OFF_SUB];
    d.chip_uses = buf[OFF_USES];
    d.rsvd      = 0;
    for (int i = 0; i < 16; i++) {
        d.params[i] = (int16_t)((uint16_t)buf[OFF_PARAMS + i * 2]
                              | ((uint16_t)buf[OFF_PARAMS + i * 2 + 1] << 8));
    }
    return true;
}

uint16_t calcChipCrc(const ChipData& d) {
    uint8_t buf[DATA_SIZE];
    packChipBuf(d, buf);
    return crc16(buf, DATA_SIZE);
}

String chipToString(const ChipData& d) {
    String s = "";
    s += "type=" + String(d.chip_type) + ",";
    s += "sub="  + String(d.chip_sub)  + ",";
    s += "uses=" + String(d.chip_uses) + ",";
    for (int i = 0; i < 16; i++) {
        s += "p" + String(i) + "=" + String(d.params[i]);
        if (i < 15) s += ",";
    }
    return s;
}

ChipData parseChipConfig(const String& cfg) {
    ChipData d = {0, 0, 1, 0, {0}};
    int start = 0;
    while (start < (int)cfg.length()) {
        int comma = cfg.indexOf(',', start);
        if (comma == -1) comma = cfg.length();
        String pair = cfg.substring(start, comma);
        int eq = pair.indexOf('=');
        if (eq > 0) {
            String key = pair.substring(0, eq);
            String val = pair.substring(eq + 1);
            key.trim(); val.trim();
            if      (key == "type") d.chip_type = (uint8_t)val.toInt();
            else if (key == "sub")  d.chip_sub  = (uint8_t)val.toInt();
            else if (key == "uses") d.chip_uses = (uint8_t)constrain(val.toInt(), 0, 255);
            else if (key.startsWith("p")) {
                int idx = key.substring(1).toInt();
                if (idx >= 0 && idx <= 15) d.params[idx] = (int16_t)val.toInt();
            }
        }
        start = comma + 1;
    }
    return d;
}

void cmdHelp() {
    Serial.println("HELP: STALKER_WHO PING CONFIG_WRITE: CONFIG_READ VERIFY WIPE");
    Serial.println("HELP: TXN_START: TXN_STATUS TXN_READ TXN_RESET");
    Serial.println("HELP: I2C_SCAN EEPROM_PING DUMP ADDR8 ADDR16");
}

void cmdI2cScan() {
    Serial.println("I2C_SCAN:begin SDA=GPIO40 SCL=GPIO39");
    uint8_t found = 0;
    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  0x%02X", addr);
            if (addr == EEPROM_ADDR) Serial.print("  EEPROM");
            Serial.println();
            found++;
        }
        delay(2);
    }
    Serial.printf("I2C_SCAN:end count=%u\n", (unsigned)found);
    if (found == 0) {
        Serial.println("I2C_SCAN:hint check VCC GND SDA=40 SCL=39 WP=GND pullups 4.7k");
    }
}

void cmdEepromPing() {
    if (!eepromPresent()) {
        Serial.println("EEPROM:MISSING addr=0x50");
        return;
    }
    Serial.println("EEPROM:OK addr=0x50");

    ChipData d;
    uint16_t storedCrc;
    if (!readChip(d, storedCrc)) {
        Serial.println("EEPROM:HEADER_READ_FAIL");
        return;
    }
    uint16_t calc = calcChipCrc(d);
    char hexStored[8], hexCalc[8];
    snprintf(hexStored, sizeof(hexStored), "%04X", storedCrc);
    snprintf(hexCalc, sizeof(hexCalc), "%04X", calc);
    Serial.println("EEPROM:HEADER " + chipToString(d));
    Serial.printf("EEPROM:CRC stored=%s calc=%s %s\n",
                  hexStored, hexCalc, (calc == storedCrc) ? "OK" : "MISMATCH");

    uint8_t block[TXN_BLOCK_SIZE];
    if (!eepromReadBlock(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE)) {
        Serial.println("EEPROM:TXN_READ_FAIL");
        return;
    }
    if (!txn_validate_block(block)) {
        Serial.println("EEPROM:TXN_BAD_CRC_OR_EMPTY");
        return;
    }
    Serial.println("EEPROM:TXN_OK state=" + String(block[TXN_OFF_STATE]) +
        ",txn_id=" + String(txn_read_u32(block, TXN_OFF_TXN_ID)) +
        ",amount=" + String(txn_read_i32(block, TXN_OFF_AMOUNT)) +
        ",result=" + String(txn_read_u16(block, TXN_OFF_RESULT)));
}

void cmdDump() {
    uint8_t hdr[EEPROM_CHIP_HEADER_SIZE];
    if (!eepromReadBlock(0x0000, hdr, EEPROM_CHIP_HEADER_SIZE)) {
        Serial.println("ERROR:DUMP_FAILED:I2C_ERR");
        return;
    }
    Serial.println("DUMP:header 0x00..0x25");
    for (uint8_t i = 0; i < EEPROM_CHIP_HEADER_SIZE; i += 8) {
        Serial.printf("  %02X:", i);
        uint8_t n = EEPROM_CHIP_HEADER_SIZE - i;
        if (n > 8) n = 8;
        for (uint8_t j = 0; j < n; j++) Serial.printf(" %02X", hdr[i + j]);
        Serial.println();
    }

    uint8_t txn[TXN_BLOCK_SIZE];
    if (!eepromReadBlock(TXN_EEPROM_BASE, txn, TXN_BLOCK_SIZE)) {
        Serial.println("ERROR:DUMP_TXN_FAILED:I2C_ERR");
        return;
    }
    Serial.println("DUMP:txn 0x80..0xA3");
    for (uint8_t i = 0; i < TXN_BLOCK_SIZE; i += 8) {
        Serial.printf("  %02X:", (unsigned)(TXN_EEPROM_BASE + i));
        uint8_t n = TXN_BLOCK_SIZE - i;
        if (n > 8) n = 8;
        for (uint8_t j = 0; j < n; j++) Serial.printf(" %02X", txn[i + j]);
        Serial.println();
    }
}

void processCommand(const String& cmd) {
    if (cmd == "STALKER_WHO") {
        Serial.println("STALKER:CHIP_BOX:v1");
    }
    else if (cmd == "PING") {
        Serial.println("PONG");
    }
    else if (cmd == "HELP" || cmd == "?") {
        cmdHelp();
    }
    else if (cmd == "I2C_SCAN") {
        cmdI2cScan();
    }
    else if (cmd == "EEPROM_PING") {
        cmdEepromPing();
    }
    else if (cmd == "DUMP") {
        cmdDump();
    }
    else if (cmd == "ADDR8") {
        eepromAddrBytes = 1;
        Serial.println("EEPROM:ADDR=8bit");
    }
    else if (cmd == "ADDR16") {
        eepromAddrBytes = 2;
        Serial.println("EEPROM:ADDR=16bit");
    }
    else if (cmd.startsWith("CONFIG_WRITE:")) {
        String cfg = cmd.substring(13);
        ChipData d = parseChipConfig(cfg);
        Serial.println("INFO:WRITING...");
        if (writeChip(d)) {
            ChipData d2;
            uint16_t crc;
            if (readChip(d2, crc)) {
                uint16_t calc = calcChipCrc(d2);
                if (calc == crc) {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "%04X", crc);
                    Serial.println("OK:WRITTEN:CRC=" + String(hex));
                } else {
                    Serial.println("ERROR:VERIFY_FAILED:CRC_MISMATCH");
                }
            } else {
                Serial.println("ERROR:VERIFY_FAILED:READ_ERR");
            }
        } else {
            Serial.println("ERROR:WRITE_FAILED:I2C_ERR");
        }
    }
    else if (cmd == "CONFIG_READ") {
        ChipData d;
        uint16_t crc;
        if (readChip(d, crc)) {
            uint16_t calc = calcChipCrc(d);
            char hex[8];
            snprintf(hex, sizeof(hex), "%04X", crc);
            String status = (calc == crc) ? "OK" : "CRC_WARN";
            Serial.println("CONFIG:" + chipToString(d) + ",crc_status=" + status);
        } else {
            Serial.println("ERROR:READ_FAILED:I2C_ERR");
        }
    }
    else if (cmd == "VERIFY") {
        ChipData d;
        uint16_t storedCrc;
        if (readChip(d, storedCrc)) {
            uint16_t calc = calcChipCrc(d);
            char hex[8];
            snprintf(hex, sizeof(hex), "%04X", calc);
            if (calc == storedCrc) Serial.println("OK:CRC=" + String(hex));
            else                  Serial.println("ERROR:CRC_MISMATCH:CALC=" + String(hex));
        } else {
            Serial.println("ERROR:READ_FAILED:I2C_ERR");
        }
    }
    else if (cmd == "WIPE") {
        bool ok = true;
        for (int i = 0; i <= (int)(OFF_CRC + 1); i++) {
            if (!eepromWriteByte(i, 0)) { ok = false; break; }
        }
        Serial.println(ok ? "OK:WIPED" : "ERROR:WIPE_FAILED");
    }
    else if (cmd.startsWith("TXN_START:")) {
        String args = cmd.substring(10);
        int amount = 0, item = 0, txnId = 1, op = TXN_OP_PURCHASE, flags = 0;
        String questId;
        int start = 0;
        while (start < (int)args.length()) {
            int comma = args.indexOf(',', start);
            if (comma == -1) comma = args.length();
            String pair = args.substring(start, comma);
            int eq = pair.indexOf('=');
            if (eq > 0) {
                String key = pair.substring(0, eq);
                String val = pair.substring(eq + 1);
                key.trim(); val.trim();
                if (key == "amount") amount = val.toInt();
                else if (key == "item") item = val.toInt();
                else if (key == "txn_id") txnId = val.toInt();
                else if (key == "op") op = val.toInt();
                else if (key == "flags") flags = val.toInt();
                else if (key == "quest_id") questId = val;
            }
            start = comma + 1;
        }
        uint8_t block[TXN_BLOCK_SIZE];
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
        txn_set_state(block, TXN_STATE_PENDING);
        if (!eepromWriteBlock(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE)) {
            Serial.println("ERROR:WRITE_FAILED");
            return;
        }
        Serial.println("OK:TXN_PENDING:op=" + String(op) + ",txn_id=" + String(txnId));
    }
    else if (cmd == "TXN_STATUS" || cmd == "TXN_READ") {
        uint8_t block[TXN_BLOCK_SIZE];
        if (!eepromReadBlock(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE)) {
            Serial.println("ERROR:READ_FAILED");
            return;
        }
        if (!txn_validate_block(block)) {
            Serial.println("ERROR:BAD_CRC");
            return;
        }
        Serial.println("TXN:state=" + String(block[TXN_OFF_STATE]) +
            ",txn_id=" + String(txn_read_u32(block, TXN_OFF_TXN_ID)) +
            ",amount=" + String(txn_read_i32(block, TXN_OFF_AMOUNT)) +
            ",result=" + String(txn_read_u16(block, TXN_OFF_RESULT)));
    }
    else if (cmd == "TXN_RESET") {
        uint8_t block[TXN_BLOCK_SIZE];
        txn_init_idle(block);
        Serial.println(eepromWriteBlock(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE)
                           ? "OK:TXN_IDLE" : "ERROR:RESET_FAILED");
    }
}

String serialBuf = "";

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

void printBanner() {
    Serial.println("=== STALKER Chip Programmer TEST v" FW_VER " ===");
    Serial.printf("Board: ESP32-S3 | I2C SDA=GPIO%d SCL=GPIO%d @100kHz\n",
                  I2C_SDA, I2C_SCL);
    if (eepromPresent()) {
        Serial.printf("EEPROM: FOUND at 0x50  ADDR=%ubit\n",
                      (unsigned)(eepromAddrBytes * 8));
    } else {
        Serial.println("EEPROM: NOT FOUND! Проверь SDA=40 SCL=39 VCC GND WP и подтяжки 4.7k");
    }
    Serial.println("Type HELP for I2C_SCAN EEPROM_PING DUMP");
    Serial.println("READY");
}

void setup() {
    Serial.begin(115200);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0);
#endif
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 5000) {
        delay(10);
    }
    delay(200);

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);
    Wire.setTimeOut(50);

    if (eepromPresent()) {
        eepromDetectAddrWidth();
    }

    printBanner();
}

void loop() {
    handleSerial();
}
