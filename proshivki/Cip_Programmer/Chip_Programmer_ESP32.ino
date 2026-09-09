/**
 * S.T.A.L.K.E.R. — Программатор чипов (Chip_Programmer_ESP32) v1.0
 *
 * Плата: ESP32-C3 SuperMini (или любой ESP32 с I2C)
 * EEPROM: AT24C04 / AT24C08 / AT24C32 (любой I2C EEPROM на адресе 0x50)
 *
 * Подключение EEPROM-чипа к ESP32-C3:
 *   EEPROM VCC  → 3.3V
 *   EEPROM GND  → GND
 *   EEPROM SDA  → GPIO 5
 *   EEPROM SCL  → GPIO 6
 *   EEPROM WP   → GND (разрешить запись)
 *   EEPROM A0,A1,A2 → GND (адрес 0x50)
 *
 * Протокол Serial (115200 бод):
 *   PC  → "STALKER_WHO"      → "STALKER:CHIP_BOX:v1"
 *   PC  → "CONFIG_WRITE:..."  → пишет в EEPROM, отвечает "OK:WRITTEN:CRC=XXXX"
 *   PC  → "CONFIG_READ"       → читает EEPROM, отвечает "CONFIG:..."
 *   PC  → "VERIFY"            → читает и считает CRC, отвечает "OK:CRC=XXXX" или "ERROR:CRC"
 *   PC  → "PING"              → "PONG"
 *   PC  → "LORA_TX:..."       → "ERROR:NO_LORA" (LoRa только на Мастер-Пульте)
 *
 * Формат CONFIG_WRITE:
 *   type=0,sub=0,uses=1,p0=50,...,name=Иван
 *   type: 0=Расходники, 1=Броня, 2=Артефакт, 3=Админка
 *   sub:  подтип (для расходников и админки; 7 = РЕГИСТРАЦИЯ)
 *   uses: кол-во использований (255 = бесконечно)
 *   p0..p15: параметры (знаковые int16)
 *   name=: UTF-8 в расширение EEPROM @0x26 (до 24 байт), чип регистрации
 *
 * Карта EEPROM (адреса байт):
 *   0x00      chip_type  (uint8)
 *   0x01      chip_sub   (uint8)
 *   0x02      chip_uses  (uint8,  255=бесконечно)
 *   0x03      резерв
 *   0x04..0x23 params[0..15] (16 x int16, little-endian)
 *   0x24..0x25 CRC16 (little-endian, покрывает 0x00..0x23)
 *   0x26..    имя / строки (eeprom_protocol.h EEPROM_CHIP_EXT_BASE)
 */

#include <Wire.h>
#include <string.h>
#include "../../common/eeprom_protocol.h"

// ─────────────────────────────────────────────────────
//  ПИНЫ
// ─────────────────────────────────────────────────────
#define I2C_SDA   5
#define I2C_SCL   6

// ─────────────────────────────────────────────────────
//  EEPROM ПАРАМЕТРЫ
// ─────────────────────────────────────────────────────
#define EEPROM_ADDR   0x50   // A0=A1=A2=GND
#define EEPROM_WR_DLY 5      // мс между записями страниц (AT24Cxx требует ≥5 мс)

// Адреса в EEPROM
#define OFF_TYPE    0x00
#define OFF_SUB     0x01
#define OFF_USES    0x02
#define OFF_RSVD    0x03
#define OFF_PARAMS  0x04     // 16 x int16 = 32 байта (0x04..0x23)
#define OFF_CRC     0x24     // 2 байта (0x24..0x25)
#define DATA_SIZE   0x24     // байт данных под CRC (0x00..0x23)
#define OFF_NAME    0x26     // UTF-8 имя, до NAME_MAX байт (с нулём)
#define NAME_MAX    24

// ─────────────────────────────────────────────────────
//  СТРУКТУРА ДАННЫХ ЧИПА
// ─────────────────────────────────────────────────────
struct ChipData {
    uint8_t  chip_type;
    uint8_t  chip_sub;
    uint8_t  chip_uses;
    uint8_t  rsvd;
    int16_t  params[16];
};

// ─────────────────────────────────────────────────────
//  CRC16 (CRC-CCITT / IBM)
// ─────────────────────────────────────────────────────
uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

// ─────────────────────────────────────────────────────
//  EEPROM: ЗАПИСЬ БАЙТА
// ─────────────────────────────────────────────────────
bool eepromWriteByte(uint16_t addr, uint8_t val) {
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    Wire.write(val);
    uint8_t err = Wire.endTransmission();
    delay(EEPROM_WR_DLY);
    return err == 0;
}

bool eepromReadByte(uint16_t addr, uint8_t& val) {
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    if (Wire.endTransmission() != 0) return false;
    Wire.requestFrom((uint8_t)EEPROM_ADDR, (uint8_t)1);
    if (!Wire.available()) return false;
    val = Wire.read();
    return true;
}

bool eepromReadBlock(uint16_t addr, uint8_t* buf, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        if (!eepromReadByte(addr + i, buf[i])) return false;
    }
    return true;
}

bool eepromWriteBlock(uint16_t addr, const uint8_t* data, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        if (!eepromWriteByte(addr + i, data[i])) return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────
//  EEPROM: ЗАПИСЬ ВСЕГО ЧИПА
// ─────────────────────────────────────────────────────
bool writeChip(const ChipData& d) {
    uint8_t buf[DATA_SIZE] = {0};
    buf[OFF_TYPE] = d.chip_type;
    buf[OFF_SUB]  = d.chip_sub;
    buf[OFF_USES] = d.chip_uses;
    buf[OFF_RSVD] = 0;
    for (int i = 0; i < 16; i++) {
        buf[OFF_PARAMS + i*2]   = (uint8_t)(d.params[i] & 0xFF);
        buf[OFF_PARAMS + i*2+1] = (uint8_t)((d.params[i] >> 8) & 0xFF);
    }

    // Пишем данные байт за байтом
    for (int i = 0; i < (int)DATA_SIZE; i++) {
        if (!eepromWriteByte(i, buf[i])) return false;
    }

    // Считаем и пишем CRC
    uint16_t crc = crc16(buf, DATA_SIZE);
    if (!eepromWriteByte(OFF_CRC,     (uint8_t)(crc & 0xFF)))       return false;
    if (!eepromWriteByte(OFF_CRC + 1, (uint8_t)((crc >> 8) & 0xFF))) return false;

    return true;
}

// ─────────────────────────────────────────────────────
//  EEPROM: ЧТЕНИЕ ВСЕГО ЧИПА
// ─────────────────────────────────────────────────────
bool readChip(ChipData& d, uint16_t& storedCrc) {
    uint8_t buf[DATA_SIZE];
    for (int i = 0; i < (int)DATA_SIZE; i++) {
        if (!eepromReadByte(i, buf[i])) return false;
    }
    uint8_t crcL, crcH;
    if (!eepromReadByte(OFF_CRC,     crcL)) return false;
    if (!eepromReadByte(OFF_CRC + 1, crcH)) return false;
    storedCrc = (uint16_t)crcL | ((uint16_t)crcH << 8);

    d.chip_type = buf[OFF_TYPE];
    d.chip_sub  = buf[OFF_SUB];
    d.chip_uses = buf[OFF_USES];
    d.rsvd      = 0;
    for (int i = 0; i < 16; i++) {
        d.params[i] = (int16_t)( (uint16_t)buf[OFF_PARAMS + i*2]
                                | ((uint16_t)buf[OFF_PARAMS + i*2+1] << 8) );
    }
    return true;
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

String extractChipName(const String& cfg) {
    int idx = cfg.indexOf("name=");
    if (idx < 0)
        return "";
    int start = idx + 5;
    int end = cfg.indexOf(',', start);
    if (end < 0)
        end = cfg.length();
    String n = cfg.substring(start, end);
    n.trim();
    return n;
}

bool writeChipName(const String& name) {
    uint8_t buf[NAME_MAX];
    memset(buf, 0, NAME_MAX);
    size_t n = name.length();
    if (n > NAME_MAX - 1)
        n = NAME_MAX - 1;
    if (n)
        memcpy(buf, name.c_str(), n);
    return eepromWriteBlock(OFF_NAME, buf, NAME_MAX);
}

// ─────────────────────────────────────────────────────
//  РАЗБОР КОНФИГА (key=value через запятую)
// ─────────────────────────────────────────────────────
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
            if      (key == "type")  d.chip_type = (uint8_t)val.toInt();
            else if (key == "sub")   d.chip_sub  = (uint8_t)val.toInt();
            else if (key == "uses")  d.chip_uses = (uint8_t)constrain(val.toInt(), 0, 255);
            else if (key.startsWith("p")) {
                int idx = key.substring(1).toInt();
                if (idx >= 0 && idx <= 15) d.params[idx] = (int16_t)val.toInt();
            }
        }
        start = comma + 1;
    }
    return d;
}

// ─────────────────────────────────────────────────────
//  SERIAL
// ─────────────────────────────────────────────────────
String serialBuf = "";

void processCommand(const String& cmd) {
    if (cmd == "STALKER_WHO") {
        Serial.println("STALKER:CHIP_BOX:v1");
    }
    else if (cmd == "PING") {
        Serial.println("PONG");
    }
    else if (cmd.startsWith("LORA_TX:")) {
        Serial.println("ERROR:NO_LORA");
    }
    else if (cmd.startsWith("CONFIG_WRITE:")) {
        String cfg = cmd.substring(13);
        ChipData d = parseChipConfig(cfg);
        Serial.println("INFO:WRITING...");
        if (writeChip(d)) {
            // Верификация
            ChipData d2; uint16_t crc;
            if (readChip(d2, crc)) {
                uint8_t buf[DATA_SIZE] = {0};
                buf[0]=d2.chip_type; buf[1]=d2.chip_sub; buf[2]=d2.chip_uses;
                for(int i=0;i<16;i++){
                    buf[OFF_PARAMS+i*2]=(uint8_t)(d2.params[i]&0xFF);
                    buf[OFF_PARAMS+i*2+1]=(uint8_t)((d2.params[i]>>8)&0xFF);
                }
                uint16_t calc = crc16(buf, DATA_SIZE);
                if (calc == crc) {
                    char hex[8]; snprintf(hex, sizeof(hex), "%04X", crc);
                    String nm = extractChipName(cfg);
                    if (nm.length() || (d.chip_type == 3 && d.chip_sub == 7)) {
                        writeChipName(nm);
                    }
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
        ChipData d; uint16_t crc;
        if (readChip(d, crc)) {
            uint8_t buf[DATA_SIZE] = {0};
            buf[0]=d.chip_type; buf[1]=d.chip_sub; buf[2]=d.chip_uses;
            for(int i=0;i<16;i++){
                buf[OFF_PARAMS+i*2]=(uint8_t)(d.params[i]&0xFF);
                buf[OFF_PARAMS+i*2+1]=(uint8_t)((d.params[i]>>8)&0xFF);
            }
            uint16_t calc = crc16(buf, DATA_SIZE);
            char hex[8]; snprintf(hex,sizeof(hex),"%04X",crc);
            String status = (calc == crc) ? "OK" : "CRC_WARN";
            Serial.println("CONFIG:" + chipToString(d) + ",crc_status=" + status);
        } else {
            Serial.println("ERROR:READ_FAILED:I2C_ERR");
        }
    }
    else if (cmd == "VERIFY") {
        ChipData d; uint16_t storedCrc;
        if (readChip(d, storedCrc)) {
            uint8_t buf[DATA_SIZE] = {0};
            buf[0]=d.chip_type; buf[1]=d.chip_sub; buf[2]=d.chip_uses;
            for(int i=0;i<16;i++){
                buf[OFF_PARAMS+i*2]=(uint8_t)(d.params[i]&0xFF);
                buf[OFF_PARAMS+i*2+1]=(uint8_t)((d.params[i]>>8)&0xFF);
            }
            uint16_t calc = crc16(buf, DATA_SIZE);
            char hex[8]; snprintf(hex,sizeof(hex),"%04X",calc);
            if (calc == storedCrc) Serial.println("OK:CRC=" + String(hex));
            else                   Serial.println("ERROR:CRC_MISMATCH:CALC=" + String(hex));
        } else {
            Serial.println("ERROR:READ_FAILED:I2C_ERR");
        }
    }
    else if (cmd == "WIPE") {
        // Стереть чип (записать нули)
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
        case TXN_OP_REGISTER:
            txn_build_register(block, (uint32_t)txnId, (uint16_t)item,
                               questId.length() ? questId.c_str() : nullptr);
            break;
        default:
            Serial.println("ERROR:BAD_OP"); return;
        }
        txn_set_state(block, TXN_STATE_PENDING);
        if (!eepromWriteBlock(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE)) {
            Serial.println("ERROR:WRITE_FAILED"); return;
        }
        Serial.println("OK:TXN_PENDING:op=" + String(op) + ",txn_id=" + String(txnId));
    }
    else if (cmd == "TXN_STATUS" || cmd == "TXN_READ") {
        uint8_t block[TXN_BLOCK_SIZE];
        if (!eepromReadBlock(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE)) {
            Serial.println("ERROR:READ_FAILED"); return;
        }
        if (!txn_validate_block(block)) {
            Serial.println("ERROR:BAD_CRC"); return;
        }
        Serial.println("TXN:state=" + String(block[TXN_OFF_STATE]) +
            ",txn_id=" + String(txn_read_u32(block, TXN_OFF_TXN_ID)) +
            ",amount=" + String(txn_read_i32(block, TXN_OFF_AMOUNT)) +
            ",result=" + String(txn_read_u16(block, TXN_OFF_RESULT)));
    }
    else if (cmd == "TXN_RESET") {
        uint8_t block[TXN_BLOCK_SIZE];
        txn_init_idle(block);
        Serial.println(eepromWriteBlock(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE) ? "OK:TXN_IDLE" : "ERROR:RESET_FAILED");
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

// ─────────────────────────────────────────────────────
//  SETUP / LOOP
// ─────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);  // 100 кГц — надёжно для AT24Cxx

    // Проверка наличия EEPROM
    Wire.beginTransmission(EEPROM_ADDR);
    bool eepromFound = (Wire.endTransmission() == 0);

    Serial.println("=== STALKER Chip Programmer v1.0 ===");
    if (eepromFound) {
        Serial.println("EEPROM: FOUND at 0x50");
    } else {
        Serial.println("EEPROM: NOT FOUND! Проверь подключение SDA/SCL/VCC/GND");
    }
    Serial.println("READY");
}

void loop() {
    handleSerial();
}
