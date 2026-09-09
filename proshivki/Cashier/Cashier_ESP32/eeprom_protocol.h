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

#endif
