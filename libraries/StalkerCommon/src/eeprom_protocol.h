#ifndef STALKER_EEPROM_PROTOCOL_H
#define STALKER_EEPROM_PROTOCOL_H

#include "eeprom_txn.h"

#define EEPROM_CHIP_HEADER_BASE   0x0000
#define EEPROM_CHIP_HEADER_SIZE   0x0026
#define EEPROM_TXN_REGION_BASE    TXN_EEPROM_BASE
#define EEPROM_TXN_REGION_SIZE    TXN_BLOCK_SIZE

static inline const char *txn_op_name(uint8_t op) {
    switch (op) {
    case TXN_OP_PURCHASE: return "PURCHASE";
    case TXN_OP_BANK:     return "BANK";
    case TXN_OP_QUEST:    return "QUEST";
    case TXN_OP_ADMIT:    return "ADMIT";
    case TXN_OP_ATM:      return "ATM";
    default:              return "UNKNOWN";
    }
}

static inline const char *txn_state_name(uint8_t st) {
    switch (st) {
    case TXN_STATE_IDLE:        return "IDLE";
    case TXN_STATE_PENDING:     return "PENDING";
    case TXN_STATE_PROCESSING:  return "PROCESSING";
    case TXN_STATE_SUCCESS:     return "SUCCESS";
    case TXN_STATE_FAILED:      return "FAILED";
    default:                    return "?";
    }
}

#endif
