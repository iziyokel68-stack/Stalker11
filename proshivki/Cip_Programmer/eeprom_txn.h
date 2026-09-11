/**
 * S.T.A.L.K.E.R. — Shared EEPROM terminal↔PDA transaction protocol v1
 *
 * Region: 0x0080..0x00A3 (36 bytes), separate from game chip header 0x00..0x25.
 * Used by cashier, ATM, quest board, admission desk — any CH0 cable terminal.
 * Full memory map: eeprom_protocol.h
 *
 * Flow:
 *   1. Terminal writes payload + CRC, then sets state = TXN_PENDING (last).
 *   2. PDA sees PENDING, sets TXN_PROCESSING, applies op_type handler, writes result.
 *   3. Terminal polls until terminal state; shows result; resets to TXN_IDLE.
 *
 * Cable mode (no chip move): same EEPROM at desk; 4-wire to PDA TCA CH0.
 * Terminal ESP32 uses direct I2C to the local cassette; PDA selects
 * MUX_CH_UNIVERSAL (CH0) on its TCA.
 * Electrical: one master at a time; protocol state machine avoids collisions.
 *
 * Field semantics (common 36-byte block):
 *   txn_id     — unique request id (dedup on PDA)
 *   amount     — RUB: price / transfer / quest reward (signed int32)
 *   item_id    — catalog id: shop item, quest catalog, bank account code
 *   op_type    — TXN_OP_* (PURCHASE, BANK, QUEST, ADMIT, ATM)
 *   flags      — TXN_FLAG_* (bank direction, quest complete, discount marker)
 *   balance    — filled by PDA: player RUB after operation
 *   result     — TXN_RESULT_* 
 *   paid       — actual debit/credit applied (after discount)
 *   reserved   — quest_id prefix (8 bytes ASCII) for TXN_OP_QUEST;
 *                assigned PDA UID prefix for TXN_OP_REGISTER (YYYYMMDD)
 */
#ifndef STALKER_EEPROM_TXN_H
#define STALKER_EEPROM_TXN_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define TXN_EEPROM_BASE   0x0080
#define TXN_BLOCK_SIZE    36
#define TXN_DATA_SIZE     34   // bytes covered by CRC (before CRC field)
#define TXN_MAGIC_0       0x53 // 'S'
#define TXN_MAGIC_1       0x54 // 'T'
#define TXN_VERSION       1

#define TXN_STATE_IDLE        0
#define TXN_STATE_PENDING     1
#define TXN_STATE_PROCESSING  2
#define TXN_STATE_SUCCESS     3
#define TXN_STATE_FAILED      4

#define TXN_OP_PURCHASE       0
#define TXN_OP_BANK           1   // deposit / withdraw (flags)
#define TXN_OP_QUEST          2   // accept or turn-in (flags)
#define TXN_OP_ADMIT          3   // session admit (cashier / master terminal via CH0 TXN)
#define TXN_OP_ATM            4   // alias semantics for BANK + transfer code
#define TXN_OP_REGISTER       5   // master desk: assign player ID + UID via cable

#define TXN_FLAG_DISCOUNT       0x01  // set by PDA when rank discount applied
#define TXN_FLAG_BANK_DEPOSIT   0x02  // BANK: deposit (else withdraw)
#define TXN_FLAG_QUEST_COMPLETE 0x04  // QUEST: turn-in (else accept new task)
#define TXN_FLAG_QUEST_HIDDEN   0x08  // QUEST: hidden (needs LVL_HIDDEN_QUEST)

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

// Offsets inside TXN block (relative to TXN_EEPROM_BASE)
#define TXN_OFF_MAGIC       0
#define TXN_OFF_VERSION     2
#define TXN_OFF_STATE       3
#define TXN_OFF_TXN_ID      4   // uint32 LE
#define TXN_OFF_AMOUNT      8   // int32 LE — base price (RUB)
#define TXN_OFF_ITEM_ID     12  // uint16 LE
#define TXN_OFF_OP_TYPE     14
#define TXN_OFF_FLAGS       15
#define TXN_OFF_BALANCE     16  // int32 LE — filled by PDA
#define TXN_OFF_RESULT      20  // uint16 LE — filled by PDA
#define TXN_OFF_PAID        22  // int32 LE — actual debit after discount
#define TXN_OFF_RESERVED    26  // 8 bytes
#define TXN_OFF_CRC         34  // uint16 LE

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

/** Terminal: purchase request (state IDLE; caller sets PENDING last). */
static inline void txn_build_purchase(uint8_t *block, uint32_t txn_id, int32_t amount_rub,
                                      uint16_t item_id) {
    txn_build_common(block, txn_id, TXN_OP_PURCHASE, amount_rub, item_id, 0);
}

/** Terminal: bank deposit or withdraw. amount_rub must be > 0. */
static inline void txn_build_bank(uint8_t *block, uint32_t txn_id, int32_t amount_rub,
                                  uint16_t account_code, bool deposit) {
    uint8_t flags = deposit ? TXN_FLAG_BANK_DEPOSIT : 0;
    txn_build_common(block, txn_id, TXN_OP_BANK, amount_rub, account_code, flags);
}

/** Terminal: quest accept or turn-in. quest_id up to 8 chars in reserved. */
static inline void txn_build_quest(uint8_t *block, uint32_t txn_id, uint16_t quest_cat_id,
                                   int32_t rub_reward, const char *quest_id_prefix,
                                   bool complete, bool hidden) {
    uint8_t flags = 0;
    if (complete)
        flags |= TXN_FLAG_QUEST_COMPLETE;
    if (hidden)
        flags |= TXN_FLAG_QUEST_HIDDEN;
    txn_build_common(block, txn_id, TXN_OP_QUEST, rub_reward, quest_cat_id, flags);
    if (quest_id_prefix) {
        size_t n = strlen(quest_id_prefix);
        if (n > 8) n = 8;
        memcpy(block + TXN_OFF_RESERVED, quest_id_prefix, n);
    }
    txn_recalc_crc(block);
}

/** Terminal: session admit request (master desk). */
static inline void txn_build_admit(uint8_t *block, uint32_t txn_id) {
    txn_build_common(block, txn_id, TXN_OP_ADMIT, 0, 0, 0);
}

/** Master CHIP_BOX: register PDA over shared EEPROM + 4-wire. item_id = player ID. */
static inline void txn_build_register(uint8_t *block, uint32_t txn_id, uint16_t player_id,
                                      const char *uid_prefix) {
    txn_build_common(block, txn_id, TXN_OP_REGISTER, 0, player_id, 0);
    if (uid_prefix) {
        size_t n = strlen(uid_prefix);
        if (n > 8) n = 8;
        memcpy(block + TXN_OFF_RESERVED, uid_prefix, n);
    }
    txn_recalc_crc(block);
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

#endif // STALKER_EEPROM_TXN_H
