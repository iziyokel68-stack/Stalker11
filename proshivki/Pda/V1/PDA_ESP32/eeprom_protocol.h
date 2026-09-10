/**
 * S.T.A.L.K.E.R. — Unified EEPROM protocol (24LC256 cartridge memory map)
 *
 * All player-device interactions (store, ATM, quests, admission terminal, armor,
 * artifacts, consumables) use the same physical EEPROM chip on TCA channels.
 * Combat / radiation / safe zones stay on ESP-NOW — not covered here.
 *
 * Two interaction patterns on CH0 (MUX_CH_UNIVERSAL):
 *   A) Chip handoff — terminal or programmer writes chip header @0x00; player
 *      inserts cartridge; PDA reads header on insert (applyChip).
 *   B) Cable / desk cassette — shared EEPROM; terminal writes TXN block @0x80;
 *      PDA polls every ~200 ms (see eeprom_txn.h state machine).
 *
 * CH2, CH3, CH5, CH6: universal equipment slots — chip header only (no TXN).
 * Type (armor / artifact / other) comes from the chip header, not the channel.
 * CH1, CH4: reserved (mux_channels.h).
 *
 * Memory map (24LC256, 32 KiB):
 *
 *   0x0000..0x0025  Chip header (38 B): type, sub, uses, params[16], CRC16
 *   0x0026..0x007F  Chip extension (reserved — quest title, strings)
 *   0x0080..0x00A3  Terminal TXN block (36 B): magic ST, state machine, op_type
 *   0x00A4..0x00FF  Post-TXN reserved
 *   0x0100..         Test / future regions (see EEPROM_TCA_Test)
 *
 * TXN block layout and helpers: eeprom_txn.h
 */
#ifndef STALKER_EEPROM_PROTOCOL_H
#define STALKER_EEPROM_PROTOCOL_H

#include "eeprom_txn.h"

#define EEPROM_CHIP_HEADER_BASE   0x0000
#define EEPROM_CHIP_HEADER_SIZE   0x0026   // 0x00..0x25 inclusive
#define EEPROM_CHIP_EXT_BASE      0x0026
#define EEPROM_CHIP_EXT_SIZE      0x005A   // 0x26..0x7F
#define EEPROM_TXN_REGION_BASE    TXN_EEPROM_BASE
#define EEPROM_TXN_REGION_SIZE    TXN_BLOCK_SIZE
#define EEPROM_POST_TXN_BASE      0x00A4
#define EEPROM_POST_TXN_SIZE      0x005C   // 0xA4..0xFF
#define EEPROM_TEST_AREA_BASE     0x0100

/** Human-readable op names for Serial logs (index = op_type). */
static inline const char *txn_op_name(uint8_t op) {
    switch (op) {
    case TXN_OP_PURCHASE: return "PURCHASE";
    case TXN_OP_BANK:     return "BANK";
    case TXN_OP_QUEST:    return "QUEST";
    case TXN_OP_ADMIT:    return "ADMIT";
    case TXN_OP_ATM:      return "ATM";
    case TXN_OP_REGISTER: return "REGISTER";
    default:              return "UNKNOWN";
    }
}

/** Human-readable result codes for Serial logs (result = TXN_RESULT_*). */
static inline const char *txn_result_name(uint16_t result) {
    switch (result) {
    case TXN_RESULT_OK:                 return "OK";
    case TXN_RESULT_INSUFFICIENT_FUNDS: return "INSUFFICIENT_FUNDS";
    case TXN_RESULT_LEVEL_TOO_LOW:      return "LEVEL_TOO_LOW";
    case TXN_RESULT_SYSTEM_LOCKED:      return "SYSTEM_LOCKED";
    case TXN_RESULT_BAD_CRC:            return "BAD_CRC";
    case TXN_RESULT_BAD_MAGIC:          return "BAD_MAGIC";
    case TXN_RESULT_QUEST_NOT_FOUND:    return "QUEST_NOT_FOUND";
    case TXN_RESULT_QUEST_DUPLICATE:    return "QUEST_DUPLICATE";
    case TXN_RESULT_NOT_IMPLEMENTED:    return "NOT_IMPLEMENTED";
    case TXN_RESULT_UNKNOWN:            return "UNKNOWN";
    default:                            return "UNKNOWN";
    }
}

#endif // STALKER_EEPROM_PROTOCOL_H
