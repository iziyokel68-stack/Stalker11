#ifndef STALKER_EEPROM_TXN_H
#define STALKER_EEPROM_TXN_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define TXN_EEPROM_BASE   0x0080
#define TXN_BLOCK_SIZE    36
#define TXN_DATA_SIZE     34
#define TXN_MAGIC_0       0x53
#define TXN_MAGIC_1       0x54
#define TXN_VERSION       1

#define TXN_STATE_IDLE        0
#define TXN_STATE_PENDING     1
#define TXN_STATE_PROCESSING  2
#define TXN_STATE_SUCCESS     3
#define TXN_STATE_FAILED      4

#define TXN_OP_PURCHASE       0
#define TXN_OP_BANK           1
#define TXN_OP_QUEST          2
#define TXN_OP_ADMIT          3
#define TXN_OP_ATM            4

#define TXN_FLAG_DISCOUNT       0x01
#define TXN_FLAG_BANK_DEPOSIT   0x02
#define TXN_FLAG_QUEST_COMPLETE 0x04

#define TXN_RESULT_OK                 0
#define TXN_RESULT_INSUFFICIENT_FUNDS 1
#define TXN_RESULT_LEVEL_TOO_LOW      2
#define TXN_RESULT_SYSTEM_LOCKED      3
#define TXN_RESULT_BAD_CRC            4
#define TXN_RESULT_BAD_MAGIC          5
#define TXN_RESULT_QUEST_NOT_FOUND    6
#define TXN_RESULT_QUEST_DUPLICATE    7
#define TXN_RESULT_NOT_IMPLEMENTED    8
#define TXN_RESULT_UNKNOWN            99

#define TXN_OFF_MAGIC       0
#define TXN_OFF_VERSION     2
#define TXN_OFF_STATE       3
#define TXN_OFF_TXN_ID      4
#define TXN_OFF_AMOUNT      8
#define TXN_OFF_ITEM_ID     12
#define TXN_OFF_OP_TYPE     14
#define TXN_OFF_FLAGS       15
#define TXN_OFF_BALANCE     16
#define TXN_OFF_RESULT      20
#define TXN_OFF_PAID        22
#define TXN_OFF_RESERVED    26
#define TXN_OFF_CRC         34

static inline uint16_t txn_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static inline int32_t txn_read_i32(const uint8_t *buf, int off) {
    return (int32_t)((uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) |
                     ((uint32_t)buf[off + 2] << 16) | ((uint32_t)buf[off + 3] << 24));
}

static inline uint32_t txn_read_u32(const uint8_t *buf, int off) {
    return (uint32_t)txn_read_i32(buf, off);
}

static inline uint16_t txn_read_u16(const uint8_t *buf, int off) {
    return (uint16_t)(buf[off] | (buf[off + 1] << 8));
}

static inline void txn_write_i32(uint8_t *buf, int off, int32_t v) {
    buf[off]     = (uint8_t)(v & 0xFF);
    buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    buf[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    buf[off + 3] = (uint8_t)((v >> 24) & 0xFF);
}

static inline void txn_write_u32(uint8_t *buf, int off, uint32_t v) {
    txn_write_i32(buf, off, (int32_t)v);
}

static inline void txn_write_u16(uint8_t *buf, int off, uint16_t v) {
    buf[off]     = (uint8_t)(v & 0xFF);
    buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline void txn_recalc_crc(uint8_t *block) {
    uint16_t crc = txn_crc16(block, TXN_DATA_SIZE);
    txn_write_u16(block, TXN_OFF_CRC, crc);
}

static inline bool txn_validate_block(const uint8_t *block) {
    if (block[TXN_OFF_MAGIC] != TXN_MAGIC_0 || block[TXN_OFF_MAGIC + 1] != TXN_MAGIC_1)
        return false;
    if (block[TXN_OFF_VERSION] != TXN_VERSION)
        return false;
    uint16_t stored = txn_read_u16(block, TXN_OFF_CRC);
    uint16_t calc = txn_crc16(block, TXN_DATA_SIZE);
    return stored == calc;
}

static inline void txn_init_idle(uint8_t *block) {
    memset(block, 0, TXN_BLOCK_SIZE);
    block[TXN_OFF_MAGIC] = TXN_MAGIC_0;
    block[TXN_OFF_MAGIC + 1] = TXN_MAGIC_1;
    block[TXN_OFF_VERSION] = TXN_VERSION;
    block[TXN_OFF_STATE] = TXN_STATE_IDLE;
    txn_recalc_crc(block);
}

static inline void txn_build_common(uint8_t *block, uint32_t txn_id, uint8_t op_type,
                                    int32_t amount_rub, uint16_t item_id, uint8_t flags) {
    memset(block, 0, TXN_BLOCK_SIZE);
    block[TXN_OFF_MAGIC] = TXN_MAGIC_0;
    block[TXN_OFF_MAGIC + 1] = TXN_MAGIC_1;
    block[TXN_OFF_VERSION] = TXN_VERSION;
    block[TXN_OFF_STATE] = TXN_STATE_IDLE;
    txn_write_u32(block, TXN_OFF_TXN_ID, txn_id);
    txn_write_i32(block, TXN_OFF_AMOUNT, amount_rub);
    txn_write_u16(block, TXN_OFF_ITEM_ID, item_id);
    block[TXN_OFF_OP_TYPE] = op_type;
    block[TXN_OFF_FLAGS] = flags;
    txn_recalc_crc(block);
}

static inline void txn_build_purchase(uint8_t *block, uint32_t txn_id, int32_t amount_rub,
                                      uint16_t item_id) {
    txn_build_common(block, txn_id, TXN_OP_PURCHASE, amount_rub, item_id, 0);
}

static inline void txn_set_state(uint8_t *block, uint8_t state) {
    block[TXN_OFF_STATE] = state;
    txn_recalc_crc(block);
}

static inline uint8_t txn_get_state(const uint8_t *block) {
    return block[TXN_OFF_STATE];
}

static inline bool txn_is_terminal_state(uint8_t state) {
    return state == TXN_STATE_SUCCESS || state == TXN_STATE_FAILED;
}

#endif
