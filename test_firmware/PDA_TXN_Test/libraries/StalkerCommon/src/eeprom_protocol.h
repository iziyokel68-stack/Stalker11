#ifndef STALKER_EEPROM_PROTOCOL_H
#define STALKER_EEPROM_PROTOCOL_H

#include "eeprom_txn.h"

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
