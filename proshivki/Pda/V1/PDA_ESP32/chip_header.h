/**
 * S.T.A.L.K.E.R. — EEPROM chip header @0x00 (handoff cartridges)
 *
 * Written by Chip_Programmer / master app; read by PDA applyChip().
 * TXN block @0x80 is separate (eeprom_txn.h) — cable terminals on CH0.
 *
 *   0x00      type   0=consumable 1=armor 2=artifact 3=admin
 *   0x01      sub    subtype (consumable / admin)
 *   0x02      uses   255 = infinite
 *   0x03      reserved
 *   0x04..23  params[16] int16 LE
 *   0x24..25  CRC16-CCITT (poly 0x1021, init 0xFFFF) over 0x00..0x23
 *   0x26..    UTF-8 name (registration), up to 24 bytes
 */
#ifndef STALKER_CHIP_HEADER_H
#define STALKER_CHIP_HEADER_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#define CHIP_OFF_TYPE    0x00
#define CHIP_OFF_SUB     0x01
#define CHIP_OFF_USES    0x02
#define CHIP_OFF_RSVD    0x03
#define CHIP_OFF_PARAMS  0x04
#define CHIP_OFF_CRC     0x24
#define CHIP_DATA_SIZE   0x24
#define CHIP_HEADER_SIZE 0x26
#define CHIP_OFF_NAME    0x26
#define CHIP_NAME_MAX    24
#define CHIP_USES_INFINITE 255

#define CHIP_TYPE_CONSUMABLE 0
#define CHIP_TYPE_ARMOR      1
#define CHIP_TYPE_ARTIFACT   2
#define CHIP_TYPE_ADMIN      3
#define CHIP_TYPE_QUEST      4
#define CHIP_TYPE_SHOP       5

#define CHIP_ADM_SAVE        8

#define CHIP_SUB_HEAL     0
#define CHIP_SUB_ANTIRAD  1
#define CHIP_SUB_REGEN    2
#define CHIP_SUB_STIM     3
#define CHIP_SUB_RESTORE  4
#define CHIP_SUB_UPGRADE  5

#define CHIP_ADM_REVIVE      0
#define CHIP_ADM_MONEY       1
#define CHIP_ADM_LEVEL       2
#define CHIP_ADM_IMMUNITY    3
#define CHIP_ADM_RESET       4
#define CHIP_ADM_NEUTRALIZE  5
#define CHIP_ADM_ADMIT       6
#define CHIP_ADM_REGISTER    7

#define LVL_CONSUMABLE   1
#define LVL_STORE        2
#define LVL_ARTIFACT1    2
#define LVL_ATM          3
#define LVL_ARMOR        5
#define LVL_ARTIFACT2    7
#define LVL_ARTIFACT3    10
#define LVL_ARENA        12
#define LVL_HIDDEN_QUEST 20
#define LVL_DETECTOR     25
#define LVL_GLOBAL_MSG   50

#define CHIP_MAX_ARMOR     1
#define CHIP_MAX_ARTIFACT  3

typedef struct {
    uint8_t type;
    uint8_t sub;
    uint8_t uses;
    int16_t params[16];
} ChipHeader;

static inline uint16_t chip_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

static inline void chip_pack(const ChipHeader *d, uint8_t *buf) {
    memset(buf, 0, CHIP_DATA_SIZE);
    buf[CHIP_OFF_TYPE] = d->type;
    buf[CHIP_OFF_SUB] = d->sub;
    buf[CHIP_OFF_USES] = d->uses;
    for (int i = 0; i < 16; i++) {
        buf[CHIP_OFF_PARAMS + i * 2] = (uint8_t)(d->params[i] & 0xFF);
        buf[CHIP_OFF_PARAMS + i * 2 + 1] = (uint8_t)((d->params[i] >> 8) & 0xFF);
    }
}

static inline bool chip_parse(const uint8_t *hdr, ChipHeader *d) {
    uint16_t stored = (uint16_t)hdr[CHIP_OFF_CRC] | ((uint16_t)hdr[CHIP_OFF_CRC + 1] << 8);
    if (chip_crc16(hdr, CHIP_DATA_SIZE) != stored)
        return false;
    d->type = hdr[CHIP_OFF_TYPE];
    d->sub = hdr[CHIP_OFF_SUB];
    d->uses = hdr[CHIP_OFF_USES];
    for (int i = 0; i < 16; i++) {
        d->params[i] = (int16_t)((uint16_t)hdr[CHIP_OFF_PARAMS + i * 2] |
                                 ((uint16_t)hdr[CHIP_OFF_PARAMS + i * 2 + 1] << 8));
    }
    return true;
}

/** Level required to equip the next artifact (alreadyEquipped = 0..2). 255 = no more. */
static inline int chip_artifact_level_required(int alreadyEquipped) {
    if (alreadyEquipped <= 0)
        return LVL_ARTIFACT1;
    if (alreadyEquipped == 1)
        return LVL_ARTIFACT2;
    if (alreadyEquipped == 2)
        return LVL_ARTIFACT3;
    return 255;
}

#endif /* STALKER_CHIP_HEADER_H */
